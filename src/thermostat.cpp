#include "global.h"

static unsigned long lastThermostatTick = 0;
static const unsigned long THERMOSTAT_INTERVAL_MS = 500;

// Внутренние флаги потребности в нагреве с учетом гистерезиса 0.5°C
static bool z1FloorDemand = true;
static bool z2FloorDemand = true;

// Защита от частого переключения реле (минимум 3 секунды между коммутациями)
static unsigned long z1LastSwitchTime = 0;
static unsigned long z2LastSwitchTime = 0;
static const unsigned long MIN_SWITCH_INTERVAL_MS = 3000;

void thermostat_init() {
    Serial.printf("[THERMO] Инициализация реле: SSR_Z1=GPIO%d, SSR_Z2=GPIO%d\n",
                  PIN_SSR_ZONE1, PIN_SSR_ZONE2);
    pinMode(PIN_SSR_ZONE1, OUTPUT);
    digitalWrite(PIN_SSR_ZONE1, LOW);

    pinMode(PIN_SSR_ZONE2, OUTPUT);
    digitalWrite(PIN_SSR_ZONE2, LOW);

    zone1.relayState = false;
    zone2.relayState = false;
    airHeatingDemand = true;
    z1FloorDemand = true;
    z2FloorDemand = true;
}

void thermostat_tick() {
    unsigned long now = millis();
    if (now - lastThermostatTick < THERMOSTAT_INTERVAL_MS) {
        return;
    }
    lastThermostatTick = now;

    // =========================================================================
    // 1. ТЕРМОСТАТ ВОЗДУХА В ПОМЕЩЕНИИ (гистерезис 0.5°C)
    //    Если воздух нагрелся до уставки -> гасим ОБА пола!
    // =========================================================================
    if (!air.sensorError) {
        if (air.currentTemp >= targetAirTemp) {
            if (airHeatingDemand) {
                airHeatingDemand = false;
                Serial.printf("[THERMO] Воздух нагрелся (%.1f°C >= уставки %.1f°C) -> Отключение ОБОИХ полов!\n",
                              air.currentTemp, targetAirTemp);
            }
        } else if (air.currentTemp <= (targetAirTemp - HYSTERESIS_TEMP)) {
            if (!airHeatingDemand) {
                airHeatingDemand = true;
                Serial.printf("[THERMO] Воздух остыл (%.1f°C <= %.1f°C) -> Запрос на нагрев комнатного воздуха.\n",
                              air.currentTemp, targetAirTemp - HYSTERESIS_TEMP);
            }
        }
    } else {
        // При ошибке комнатного датчика отключаем нагрев ради безопасности
        airHeatingDemand = false;
    }

    // =========================================================================
    // 2. ТЕРМОСТАТ ПОЛА ЗОНЫ 1 (гистерезис 0.5°C от targetFloorTemp)
    // =========================================================================
    if (!zone1.sensorError) {
        if (zone1.currentTemp >= targetFloorTemp) {
            if (z1FloorDemand) {
                z1FloorDemand = false;
                Serial.printf("[THERMO] Пол Зоны 1 нагрелся (%.1f°C >= уставки %.1f°C) -> Отключение Зоны 1.\n",
                              zone1.currentTemp, targetFloorTemp);
            }
        } else if (zone1.currentTemp <= (targetFloorTemp - HYSTERESIS_TEMP)) {
            z1FloorDemand = true;
        }
    } else {
        z1FloorDemand = false;
    }

    // =========================================================================
    // 3. ТЕРМОСТАТ ПОЛА ЗОНЫ 2 (гистерезис 0.5°C от targetFloorTemp)
    // =========================================================================
    if (!zone2.sensorError) {
        if (zone2.currentTemp >= targetFloorTemp) {
            if (z2FloorDemand) {
                z2FloorDemand = false;
                Serial.printf("[THERMO] Пол Зоны 2 нагрелся (%.1f°C >= уставки %.1f°C) -> Отключение Зоны 2.\n",
                              zone2.currentTemp, targetFloorTemp);
            }
        } else if (zone2.currentTemp <= (targetFloorTemp - HYSTERESIS_TEMP)) {
            z2FloorDemand = true;
        }
    } else {
        z2FloorDemand = false;
    }

    // =========================================================================
    // 4. ПРАВИЛО "ЧТО ПЕРВОЕ СРАБОТАЕТ, ТО И ОТКЛЮЧАЕТ":
    //    Реле зоны активно только если:
    //    - Главное питание отопления ВКЛЮЧЕНО (heatingEnabled == true)
    //    - Нет аварии перегрева стяжки (currentTemp < max_floor_temp)
    //    - Воздух еще ТРЕБУЕТ нагрева (airHeatingDemand == true)
    //    - И пол данной зоны еще ТРЕБУЕТ нагрева (floorDemand == true)
    // =========================================================================
    bool desiredZ1 = heatingEnabled && 
                     !zone1.sensorError && 
                     (zone1.currentTemp < sysCfg.max_floor_temp) && 
                     airHeatingDemand && 
                     z1FloorDemand;

    bool desiredZ2 = heatingEnabled && 
                     !zone2.sensorError && 
                     (zone2.currentTemp < sysCfg.max_floor_temp) && 
                     airHeatingDemand && 
                     z2FloorDemand;

    // Применение к реле Зоны 1
    if (desiredZ1 != zone1.relayState) {
        if (!desiredZ1 || (now - z1LastSwitchTime >= MIN_SWITCH_INTERVAL_MS)) {
            zone1.relayState = desiredZ1;
            digitalWrite(PIN_SSR_ZONE1, zone1.relayState ? HIGH : LOW);
            z1LastSwitchTime = now;
            Serial.printf("[THERMO] Зона 1 SSR -> %s (Пол: %.1f/%.1f, Возд: %.1f/%.1f)\n",
                          zone1.relayState ? "ВКЛ (Греет)" : "ВЫКЛ (Пауза)",
                          zone1.currentTemp, targetFloorTemp, air.currentTemp, targetAirTemp);
            net_mqtt_publish_state();
        }
    } else {
        digitalWrite(PIN_SSR_ZONE1, zone1.relayState ? HIGH : LOW);
    }

    // Применение к реле Зоны 2
    if (desiredZ2 != zone2.relayState) {
        if (!desiredZ2 || (now - z2LastSwitchTime >= MIN_SWITCH_INTERVAL_MS)) {
            zone2.relayState = desiredZ2;
            digitalWrite(PIN_SSR_ZONE2, zone2.relayState ? HIGH : LOW);
            z2LastSwitchTime = now;
            Serial.printf("[THERMO] Зона 2 SSR -> %s (Пол: %.1f/%.1f, Возд: %.1f/%.1f)\n",
                          zone2.relayState ? "ВКЛ (Греет)" : "ВЫКЛ (Пауза)",
                          zone2.currentTemp, targetFloorTemp, air.currentTemp, targetAirTemp);
            net_mqtt_publish_state();
        }
    } else {
        digitalWrite(PIN_SSR_ZONE2, zone2.relayState ? HIGH : LOW);
    }
}
