#include "global.h"
#include <OneWire.h>
#include <DallasTemperature.h>

// Физическое определение глобальных переменных датчиков и зон
ZoneData zone1;
ZoneData zone2;
AirData air;
bool sensorsSimulationActive = true;

// Аппаратные объекты 1-Wire
static OneWire oneWire(PIN_ONEWIRE);
static DallasTemperature dtSensors(&oneWire);

// Тайминги опроса
static unsigned long lastPollTime = 0;
static const unsigned long POLL_INTERVAL_MS = 2000; // Ровно каждые 2 секунды

// Счетчик ошибок для каждой зоны (алгоритм 5 ошибок подряд)
static const uint8_t MAX_SENSOR_ERRORS = 5;
static uint8_t z1ErrorCount = 0;
static uint8_t z2ErrorCount = 0;
static uint8_t airErrorCount = 0;

// Количество физически обнаруженных устройств на шине
static uint8_t physicalSensorsFound = 0;
static uint8_t scannedAddresses[SENSORS_COUNT][8];

// Проверка: прописан ли адрес вручную пользователем в global.h
static bool isAddressConfigured(const uint8_t addr[8]) {
    for (int i = 1; i < 8; i++) {
        if (addr[i] != 0x00) return true; // Если есть хоть один ненулевой байт кроме 0x28
    }
    return false;
}

// Форматированный вывод адреса в стиле C-массива для удобной вставки в global.h
static void printAddressFormatted(const uint8_t addr[8]) {
    Serial.print("{ ");
    for (uint8_t i = 0; i < 8; i++) {
        Serial.printf("0x%02X", addr[i]);
        if (i < 7) Serial.print(", ");
    }
    Serial.print(" }");
}

// ==============================================================================
// СКАНИРОВАНИЕ ШИНЫ 1-WIRE (ПОИСК ВСЕХ ФИЗИЧЕСКИХ ДАТЧИКОВ)
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
            Serial.println("  [1-WIRE] Ошибка CRC датчика, адрес поврежден!");
            continue;
        }

        // Проверяем тип микросхемы (семейство DS18B20 = 0x28)
        if (addr[0] != 0x28 && addr[0] != 0x10 && addr[0] != 0x22) {
            Serial.printf("  [1-WIRE] Неизвестное устройство 1-Wire (семейство 0x%02X)\n", addr[0]);
            continue;
        }

        if (physicalSensorsFound < SENSORS_COUNT) {
            memcpy(scannedAddresses[physicalSensorsFound], addr, 8);
        }
        physicalSensorsFound++;

        Serial.printf("  [1-WIRE] Датчик #%d: ", physicalSensorsFound);
        printAddressFormatted(addr);

        // Мгновенный пробный замер температуры найденного датчика
        dtSensors.requestTemperaturesByAddress(addr);
        float t = dtSensors.getTempC(addr);
        if (t > -55.0f && t < 125.0f && t != 85.0f) {
            Serial.printf(" -> T = %.2f °C\n", t);
        } else {
            Serial.printf(" -> T = (чтение...)\n");
        }
    }

    Serial.println("------------------------------------------------------------------");
    Serial.printf("  [1-WIRE] ИТОГО ОБНАРУЖЕНО ДАТЧИКОВ НА ШИНЕ: %d шт.\n", physicalSensorsFound);
    Serial.println("==================================================================");

    if (physicalSensorsFound > 0) {
        Serial.println("💡 ПОДСКАЗКА ДЛЯ РАСПОЗНАВАНИЯ ДАТЧИКОВ:");
        Serial.println("   Зажмите один датчик пальцами (нагрейте его) и введите в монитор");
        Serial.println("   команду 'scan' — датчик с выросшей температурой сразу определится!");
        Serial.println("   Затем скопируйте нужный адрес в массив SENSOR_ADDRESSES в global.h.");
        Serial.println("==================================================================");
    }
}

uint8_t sensors_get_count() {
    return physicalSensorsFound;
}

void sensors_init() {
    dtSensors.begin();
    dtSensors.setWaitForConversion(false); // Неблокирующее чтение температуры

    // Начальные безопасные значения
    zone1.currentTemp = 24.0f;
    zone1.relayState = false;
    zone1.sensorError = false;

    zone2.currentTemp = 24.0f;
    zone2.relayState = false;
    zone2.sensorError = false;

    air.currentTemp = 22.0f;
    air.sensorError = false;

    // Первичное сканирование шины при запуске
    sensors_scan_bus();

    // Проверяем: прописал ли пользователь реальные адреса в global.h
    bool userConfigured = isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE1]) ||
                          isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE2]) ||
                          isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_AIR]);

    if (userConfigured) {
        sensorsSimulationActive = false;
        Serial.println("[SENSORS] Используются фиксированные адреса датчиков из global.h.");
    } else if (physicalSensorsFound > 0) {
        sensorsSimulationActive = false;
        Serial.println("[SENSORS] Внимание: адреса в global.h пока не заданы вручную.");
        Serial.println("[SENSORS] Временно используется автоматическая привязка по порядку обнаружения.");
    } else {
        sensorsSimulationActive = true;
        Serial.println("[SENSORS] Физические датчики на шине не найдены!");
        Serial.println("[SENSORS] Активирован режим автоматической имитации (шаг 2 сек).");
    }

    // Запускаем первое асинхронное измерение
    dtSensors.requestTemperatures();
}

// Вспомогательная функция валидации и обработки температуры с алгоритмом 5 ошибок
static void processSensorReading(float rawTemp, float& outTemp, bool& outError, uint8_t& errorCounter, const char* sensorName) {
    // DS18B20: валидный диапазон от -55 до +125 °C, при ошибке возвращает -127.0f (DEVICE_DISCONNECTED_C)
    // 85.0 °C — значение сброса датчика при просадке питания
    bool isValid = (rawTemp > -55.0f && rawTemp < 125.0f && rawTemp != 85.0f);

    if (isValid) {
        // УСПЕШНОЕ ЧТЕНИЕ: сразу сбрасываем счетчик ошибок в 0!
        errorCounter = 0;
        outTemp = rawTemp;
        outError = false;
    } else {
        // НЕУДАЧНОЕ ЧТЕНИЕ: инкремент счетчика сбоев
        errorCounter++;
        Serial.printf("[SENSORS] ⚠️ Сбой чтения %s (%d/%d): T=%.1f\n",
                      sensorName, errorCounter, MAX_SENSOR_ERRORS, rawTemp);

        // АВАРИЯ ФИКСИРУЕТСЯ СТРОГО ПОСЛЕ 5 ОШИБОК ПОДРЯД:
        if (errorCounter >= MAX_SENSOR_ERRORS) {
            outError = true;
            outTemp = SENSOR_ERR_TEMP;
            Serial.printf("[SENSORS] ❌ АВАРИЯ: Датчик %s отключен (5 ошибок подряд)! Отопление зоны заблокировано.\n", sensorName);
        }
        // Если ошибок меньше 5, сохраняем предыдущую известную температуру
    }
}

void sensors_tick() {
    // Проверка ввода команды 'scan' в Serial мониторе для повторного сканирования
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

    // =========================================================================
    // 1. РЕЖИМ РЕАЛЬНЫХ ФИЗИЧЕСКИХ ДАТЧИКОВ DS18B20
    // =========================================================================
    if (!sensorsSimulationActive) {
        // Чтение температур по заданным 64-битным адресам
        const uint8_t* addrZ1 = isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE1]) 
                              ? SENSOR_ADDRESSES[SENSOR_IDX_ZONE1] 
                              : (physicalSensorsFound > 0 ? scannedAddresses[0] : nullptr);

        const uint8_t* addrZ2 = isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_ZONE2]) 
                              ? SENSOR_ADDRESSES[SENSOR_IDX_ZONE2] 
                              : (physicalSensorsFound > 1 ? scannedAddresses[1] : nullptr);

        const uint8_t* addrAir = isAddressConfigured(SENSOR_ADDRESSES[SENSOR_IDX_AIR]) 
                               ? SENSOR_ADDRESSES[SENSOR_IDX_AIR] 
                               : (physicalSensorsFound > 2 ? scannedAddresses[2] : nullptr);

        // Обработка Зоны 1 (пол 1)
        if (addrZ1 != nullptr) {
            float t1 = dtSensors.getTempC(addrZ1);
            processSensorReading(t1, zone1.currentTemp, zone1.sensorError, z1ErrorCount, "Зона 1 (Пол)");
        } else {
            zone1.sensorError = true;
            zone1.currentTemp = SENSOR_ERR_TEMP;
        }

        // Обработка Зоны 2 (пол 2)
        if (addrZ2 != nullptr) {
            float t2 = dtSensors.getTempC(addrZ2);
            processSensorReading(t2, zone2.currentTemp, zone2.sensorError, z2ErrorCount, "Зона 2 (Пол)");
        } else {
            zone2.sensorError = true;
            zone2.currentTemp = SENSOR_ERR_TEMP;
        }

        // Обработка Воздуха (комната)
        if (addrAir != nullptr) {
            float tAir = dtSensors.getTempC(addrAir);
            processSensorReading(tAir, air.currentTemp, air.sensorError, airErrorCount, "Воздух комнаты");
        } else {
            air.sensorError = true;
            air.currentTemp = SENSOR_ERR_TEMP;
        }

        // Запуск следующего неблокирующего измерения на 2 секунды
        dtSensors.requestTemperatures();
        return;
    }

    // =========================================================================
    // 2. РЕЖИМ ИМИТАЦИИ (активен, если физические датчики не припаяны)
    // =========================================================================
    // Периодическая проверка: вдруг датчики только что подключили на горячую
    static unsigned long lastAutoSearchTime = 0;
    if (now - lastAutoSearchTime >= 8000) {
        lastAutoSearchTime = now;
        uint8_t testAddr[8];
        oneWire.reset_search();
        if (oneWire.search(testAddr)) {
            Serial.println("[SENSORS] Обнаружено подключение датчиков! Перезапуск сканера...");
            sensors_init();
            return;
        }
    }

    // Имитация нагрева / остывания
    if (zone1.enabled && zone1.relayState) {
        zone1.currentTemp += 0.2f;
        if (zone1.currentTemp > 38.0f) zone1.currentTemp = 38.0f;
    } else {
        if (zone1.currentTemp > 21.2f) zone1.currentTemp -= 0.1f;
    }
    zone1.sensorError = false;

    if (zone2.enabled && zone2.relayState) {
        zone2.currentTemp += 0.2f;
        if (zone2.currentTemp > 38.0f) zone2.currentTemp = 38.0f;
    } else {
        if (zone2.currentTemp > 21.2f) zone2.currentTemp -= 0.1f;
    }
    zone2.sensorError = false;

    // Воздух плавно реагирует на нагрев полов
    if (zone1.relayState || zone2.relayState) {
        if (air.currentTemp < 23.5f) air.currentTemp += 0.05f;
    } else {
        if (air.currentTemp > 21.0f) air.currentTemp -= 0.05f;
    }
    air.sensorError = false;
}
