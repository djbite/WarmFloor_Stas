#include "global.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Физическое определение глобальных переменных датчиков и зон
ZoneData zone1;
ZoneData zone2;
AirData air;
bool sensorsSimulationActive = false; // Имитация ПОЛНОСТЬЮ ОТКЛЮЧЕНА

// Аппаратные объекты 1-Wire
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dtSensors(&oneWire);

// Тайминги опроса
static unsigned long lastPollTime = 0;
static const unsigned long POLL_INTERVAL_MS = 2000; // Опрос ровно каждые 2 секунды

// Счетчики ошибок датчиков (алгоритм 5 ошибок подряд)
static const uint8_t MAX_SENSOR_ERRORS = 5;
static uint8_t z1ErrorCount = 0;
static uint8_t z2ErrorCount = 0;
static uint8_t airErrorCount = 0;

static uint8_t physicalSensorsFound = 0;

// Проверка: заполнен ли адрес в массиве SENSOR_ADDRESSES
static bool isAddressConfigured(const uint8_t addr[8]) {
    for (int i = 1; i < 8; i++) {
        if (addr[i] != 0x00) return true; // Есть хотя бы один ненулевой байт кроме 0x28
    }
    return false;
}

// Форматированный вывод адреса в стиле C-массива для легкого копирования в global.h
static void printAddressFormatted(const uint8_t addr[8]) {
    Serial.print("{ ");
    for (uint8_t i = 0; i < 8; i++) {
        Serial.printf("0x%02X", addr[i]);
        if (i < 7) Serial.print(", ");
    }
    Serial.print(" }");
}

// ==============================================================================
// СКАНИРОВАНИЕ ШИНЫ 1-WIRE
// ==============================================================================
void sensors_scan_bus() {
    Serial.println();
    Serial.println("==================================================================");
    Serial.println("  [1-WIRE] СКАНИРОВАНИЕ ШИНЫ DS18B20 НА GPIO " + String(PIN_ONEWIRE) + "...");
    Serial.println("==================================================================");

    oneWire.reset_search();
    physicalSensorsFound = 0;
    uint8_t addr[8];

    while (oneWire.search(addr)) {
        if (OneWire::crc8(addr, 7) != addr[7]) {
            Serial.println("  [1-WIRE] ⚠️ Ошибка контрольной суммы CRC датчика!");
            continue;
        }

        if (addr[0] != 0x28 && addr[0] != 0x10 && addr[0] != 0x22) {
            Serial.printf("  [1-WIRE] Обнаружено неизвестное 1-Wire устройство (тип 0x%02X)\n", addr[0]);
            continue;
        }

        physicalSensorsFound++;
        Serial.printf("  [1-WIRE] Датчик #%d: ", physicalSensorsFound);
        printAddressFormatted(addr);

        // Пробный замер температуры
        dtSensors.requestTemperaturesByAddress(addr);
        float t = dtSensors.getTempC(addr);
        if (t != DEVICE_DISCONNECTED_C && t > -55.0f && t < 125.0f && t != 85.0f) {
            Serial.printf(" -> T = %.2f °C\n", t);
        } else {
            Serial.printf(" -> T = (чтение...)\n");
        }
    }

    Serial.println("------------------------------------------------------------------");
    Serial.printf("  [1-WIRE] ИТОГО ОБНАРУЖЕНО НА ШИНЕ: %d шт.\n", physicalSensorsFound);
    Serial.println("==================================================================");

    if (physicalSensorsFound == 0) {
        Serial.println("  [1-WIRE] Датчики пока не обнаружены (проверьте подключение шины и подтяжку 4.7кОм).");
    } else {
        Serial.println("💡 Скопируйте нужные адреса в массив SENSOR_ADDRESSES в global.h");
    }
    Serial.println("==================================================================");
}

uint8_t sensors_get_count() {
    return physicalSensorsFound;
}

void sensors_init() {
    sensorsSimulationActive = false; // Имитация полностью исключена

    dtSensors.begin();
    dtSensors.setWaitForConversion(false); // Неблокирующий опрос (не тормозит Wi-Fi)

    // При старте начинаем с 0 ошибок, чтобы алгоритм наглядно отсчитал 1..5 ошибок
    zone1.currentTemp = SENSOR_ERR_TEMP;
    zone1.relayState = false;
    zone1.sensorError = false;

    zone2.currentTemp = SENSOR_ERR_TEMP;
    zone2.relayState = false;
    zone2.sensorError = false;

    air.currentTemp = SENSOR_ERR_TEMP;
    air.sensorError = false;

    z1ErrorCount = 0;
    z2ErrorCount = 0;
    airErrorCount = 0;

    Serial.println("[SENSORS] Инициализация чисто физического опроса DS18B20 (каждые 2 сек)...");
    sensors_scan_bus();

    // Запускаем первое измерение
    dtSensors.requestTemperatures();
}

// Обработка результата считывания одного датчика по алгоритму 5 ошибок
static void processSensorReading(float rawTemp, float& outTemp, bool& outError, uint8_t& errorCounter, const char* name) {
    bool ok = (rawTemp != DEVICE_DISCONNECTED_C && rawTemp > -55.0f && rawTemp < 125.0f && rawTemp != 85.0f);

    if (ok) {
        // УСПЕШНОЕ ЧТЕНИЕ: моментальный сброс счетчика ошибок в 0!
        if (errorCounter > 0 || outError) {
            Serial.printf("[SENSORS] ✔ Датчик %s восстановил связь (T=%.2f °C). Ошибки сброшены в 0.\n", name, rawTemp);
        }
        errorCounter = 0;
        outTemp = rawTemp;
        outError = false;
    } else {
        // ОШИБКА ЧТЕНИЯ: увеличиваем счетчик сбоев
        if (errorCounter < MAX_SENSOR_ERRORS) {
            errorCounter++;
            Serial.printf("[SENSORS] ⚠️ Сбой чтения %s (%d/%d)\n", name, errorCounter, MAX_SENSOR_ERRORS);

            // АВАРИЯ ФИКСИРУЕТСЯ СТРОГО НА 5-Й ОШИБКЕ ПОДРЯД:
            if (errorCounter == MAX_SENSOR_ERRORS) {
                outError = true;
                outTemp = SENSOR_ERR_TEMP;
                Serial.printf("[SENSORS] ❌ АВАРИЯ: %s — 5 ОШИБОК ПОДРЯД! Нагрев зоны отключен ради безопасности.\n", name);
            }
        }
    }
}

void sensors_tick() {
    // Возможность запустить сканирование вручную из монитора порта: отправить 'scan'
    if (Serial.available()) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();
        if (cmd.equalsIgnoreCase("scan") || cmd.equalsIgnoreCase("1wire") || cmd.equalsIgnoreCase("поиск")) {
            sensors_scan_bus();
            dtSensors.requestTemperatures();
            return;
        }
    }

    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) {
        return; // Опрос ровно каждые 2 секунды
    }
    lastPollTime = now;

    // Чтение Зоны 1 (Пол 1)
    if (isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE1])) {
        float t1 = dtSensors.getTempC(SENSOR_ADDRESSES[SENSOR_IDX_ZONE1]);
        processSensorReading(t1, zone1.currentTemp, zone1.sensorError, z1ErrorCount, "Зона 1 (Пол)");
    } else {
        processSensorReading(DEVICE_DISCONNECTED_C, zone1.currentTemp, zone1.sensorError, z1ErrorCount, "Зона 1 (Пол)");
    }

    // Чтение Зоны 2 (Пол 2)
    if (isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE2])) {
        float t2 = dtSensors.getTempC(SENSOR_ADDRESSES[SENSOR_IDX_ZONE2]);
        processSensorReading(t2, zone2.currentTemp, zone2.sensorError, z2ErrorCount, "Зона 2 (Пол)");
    } else {
        processSensorReading(DEVICE_DISCONNECTED_C, zone2.currentTemp, zone2.sensorError, z2ErrorCount, "Зона 2 (Пол)");
    }

    // Чтение Воздуха
    if (isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_AIR])) {
        float tAir = dtSensors.getTempC(SENSOR_ADDRESSES[SENSOR_IDX_AIR]);
        processSensorReading(tAir, air.currentTemp, air.sensorError, airErrorCount, "Воздух (Комната)");
    } else {
        processSensorReading(DEVICE_DISCONNECTED_C, air.currentTemp, air.sensorError, airErrorCount, "Воздух (Комната)");
    }

    // Запуск следующего неблокирующего измерения
    dtSensors.requestTemperatures();
}
