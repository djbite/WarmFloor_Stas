#include "global.h"

void setup() {
    Serial.begin(SERIAL_BAUD);

    // ОБЯЗАТЕЛЬНОЕ ИСКЛЮЧЕНИЕ ПО СТАНДАРТУ (PROJECT_STANDARDS.md):
    // 1. Инициализация дисплея и вывод "boot" ДО задержки 5000 мс, чтобы пользователь
    //    сразу видел визуальную индикацию старта платы
    local_ui_show_boot();

    // 2. Задержка 5000 мс сразу после Serial.begin(), чтобы встроенный Serial Monitor
    //    в VS Code успел подключиться через встроенный USB CDC и не пропустил лог старта
    delay(BOOT_DELAY_MS);

    Serial.println();
    Serial.println("==================================================");
    Serial.println("  " + GET_VERSION_STRING());
    Serial.println("  Система управления теплым полом (Стас)");
    Serial.println("  Плата: ESP32-C3 SuperMini");
    Serial.println("==================================================");
    Serial.printf("[INIT] Пины: TM1637(CLK=%d, DIO=%d), 1-Wire(DS18B20)=%d\n",
                  PIN_TM_CLK, PIN_TM_DIO, PIN_ONEWIRE);
    Serial.printf("[INIT] Пины: SSR_Z1=%d, SSR_Z2=%d, LED=%d\n",
                  PIN_SSR_ZONE1, PIN_SSR_ZONE2, PIN_STATUS_LED);
    Serial.printf("[INIT] Пины кнопок: MODE=%d, UP=%d, DOWN=%d\n",
                  PIN_BTN_MODE, PIN_BTN_UP, PIN_BTN_DOWN);

    // Инициализация подсистем строго по порядку
    storage_init();
    status_led_init();
    local_ui_init();
    sensors_init();
    thermostat_init();
    net_wifi_init();
    net_webserver_init();
    net_mqtt_init();

    Serial.println("[INIT] Все модули успешно инициализированы.");
}

void loop() {
    // В loop() только вызовы неблокирующих тиков модулей (никаких delay!)
    storage_tick();
    sensors_tick();
    thermostat_tick();
    local_ui_tick();
    net_wifi_tick();
    net_webserver_tick();
    net_mqtt_tick();
    status_led_tick();
}
