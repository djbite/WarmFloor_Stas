#include "global.h"
#include <math.h>

// Физическое определение переменных состояния датчиков и зон
ZoneData zone1;
ZoneData zone2;
AirData air;
bool sensorsSimulationActive = true;

static unsigned long lastPollTime = 0;
static const unsigned long POLL_INTERVAL_MS = 2000; // Опрос ровно каждые 2 секунды

void sensors_init() {
    sensorsSimulationActive = true;
    Serial.println("==========================================================");
    Serial.println("  [SENSORS] АКТИВИРОВАН РЕЖИМ ЧИСТОЙ ИМИТАЦИИ (2 сек)");
    Serial.println("  [SENSORS] Физическое чтение отключено до распайки.");
    Serial.println("  [SENSORS] Динамика: нагрев при включенном SSR, остывание при ВЫКЛ.");
    Serial.println("==========================================================");

    // Начальные комфортные значения
    zone1.currentTemp = 23.4f;
    zone1.sensorError = false;

    zone2.currentTemp = 22.8f;
    zone2.sensorError = false;

    air.currentTemp = 21.6f;
    air.sensorError = false;
}

uint8_t sensors_get_count() {
    return 0; // В режиме чистой имитации
}

void sensors_tick() {
    unsigned long now = millis();
    if (now - lastPollTime < POLL_INTERVAL_MS) {
        return; // Ровно каждые 2 секунды
    }
    lastPollTime = now;

    // 1. Имитация Зоны 1 (шаг каждые 2 сек)
    if (zone1.enabled && zone1.relayState) {
        // Нагрев при замкнутом реле SSR (+0.2°C)
        zone1.currentTemp += 0.2f;
        if (zone1.currentTemp > 38.0f) zone1.currentTemp = 38.0f;
    } else {
        // Остывание к комнатной температуре (-0.1°C)
        if (zone1.currentTemp > 21.2f) {
            zone1.currentTemp -= 0.1f;
        }
    }
    zone1.sensorError = false;

    // 2. Имитация Зоны 2 (шаг каждые 2 сек)
    if (zone2.enabled && zone2.relayState) {
        // Нагрев при замкнутом реле SSR (+0.2°C)
        zone2.currentTemp += 0.2f;
        if (zone2.currentTemp > 38.0f) zone2.currentTemp = 38.0f;
    } else {
        // Остывание к комнатной температуре (-0.1°C)
        if (zone2.currentTemp > 20.8f) {
            zone2.currentTemp -= 0.1f;
        }
    }
    zone2.sensorError = false;

    // 3. Имитация комнатного датчика воздуха (плавный дрейф)
    air.currentTemp = 21.5f + 0.3f * sinf(now / 30000.0f);
    air.sensorError = false;
}
