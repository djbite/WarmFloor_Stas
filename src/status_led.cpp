#include "global.h"

// На плате ESP32-C3 SuperMini синий светодиод на GPIO 8 управляется низким уровнем (Active LOW)
#define LED_ON  LOW
#define LED_OFF HIGH

static unsigned long lastLedTick = 0;
static uint16_t stepIndex = 0;

void status_led_init() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LED_OFF);
}

void status_led_tick() {
    unsigned long now = millis();
    if (now - lastLedTick < 50) {
        return; // Квант времени 50 мс
    }
    lastLedTick = now;
    stepIndex++;

    // 1. Аварийный режим: ошибка датчика при включенной зоне -> быстрый строб
    if ((zone1.enabled && zone1.sensorError) || (zone2.enabled && zone2.sensorError)) {
        digitalWrite(PIN_STATUS_LED, (stepIndex % 4 < 2) ? LED_ON : LED_OFF);
        return;
    }

    // 2. Режим точки доступа / подключение к сети -> мигание 500мс / 500мс
    if (apModeActive || !wifiConnected) {
        digitalWrite(PIN_STATUS_LED, (stepIndex % 20 < 10) ? LED_ON : LED_OFF);
        return;
    }

    // 3. Штатный режим (Wi-Fi подключен) -> двойная вспышка ("heartbeat") каждые 2.5 сек (50 шагов)
    uint16_t sub = stepIndex % 50;
    if (sub == 0 || sub == 1 || sub == 4 || sub == 5) {
        digitalWrite(PIN_STATUS_LED, LED_ON);
    } else {
        digitalWrite(PIN_STATUS_LED, LED_OFF);
    }
}
