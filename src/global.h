#pragma once

#include <Arduino.h>
#include "version.h"

// ==============================================================================
// 1. АППАРАТНАЯ КОНФИГУРАЦИЯ ПИНОВ (ESP32-C3 SuperMini)
//    (СТРОГО БЕЗ МАГИЧЕСКИХ ЧИСЕЛ В КОДЕ СОГЛАСНО PROJECT_STANDARDS.md)
// ==============================================================================
#define PIN_TM_CLK              0   // Дисплей TM1637: Тактирование CLK
#define PIN_TM_DIO              1   // Дисплей TM1637: Линия данных DIO
#define PIN_BTN_MODE            2   // Кнопка ВЫБОР/РЕЖИМ (SET/MODE) - к GND (INPUT_PULLUP)
#define PIN_BTN_DOWN            3   // Кнопка МИНУС/ВНИЗ (DOWN/-)    - к GND (INPUT_PULLUP)
#define PIN_ONEWIRE             4   // Шина 1-Wire: 3x DS18B20 (подтяжка 4.7кОм к 3.3V)
#define PIN_SSR_ZONE1           5   // Реле SSR Зона 1 (Теплый пол 1) - активный HIGH
#define PIN_SSR_ZONE2           6   // Реле SSR Зона 2 (Теплый пол 2) - активный HIGH
#define PIN_BTN_UP              7   // Кнопка ПЛЮС/ВВЕРХ (UP/+)      - к GND (INPUT_PULLUP)
#define PIN_STATUS_LED          8   // Встроенный синий светодиод платы SuperMini (активный LOW)

// ==============================================================================
// 2. БАЗОВЫЕ КОНСТАНТЫ И ПАРАМЕТРЫ РАБОТЫ ТЕРМОСТАТА
// ==============================================================================
#define SERIAL_BAUD             115200
#define BOOT_DELAY_MS           5000  // Обязательная пауза в setup() для монитора порта (PROJECT_STANDARDS)

#define SENSOR_ERR_TEMP         -127.0f
#define DEFAULT_MAX_FLOOR_T     36.0f // Аварийная защита от перегрева стяжки/покрытия (°C)
#define HYSTERESIS_TEMP         0.5f  // Фиксированный гистерезис термостата (°C)
#define DEFAULT_FLOOR_SET       26.0f // Общая уставка пола по умолчанию (°C)
#define DEFAULT_AIR_SET         22.0f // Уставка комнатного воздуха по умолчанию (°C)

// ==============================================================================
// 3. НАСТРОЙКИ СЕТИ WI-FI И MQTT ПО УМОЛЧАНИЮ
// ==============================================================================
#define DEFAULT_WIFI_SSID       "RT-GPON-246D"      // Имя домашней сети Wi-Fi
#define DEFAULT_WIFI_PASS       "6yuwU9kHye"        // Пароль домашней сети Wi-Fi
#define WIFI_SSID               DEFAULT_WIFI_SSID
#define WIFI_PASS               DEFAULT_WIFI_PASS
#define DEFAULT_AP_SSID         "WarmFloor-Stas"    // Имя точки доступа SoftAP
#define DEFAULT_AP_PASS         "12345678"          // Пароль точки доступа (минимум 8 символов)

#define DEFAULT_MQTT_ENABLED    true               // Включение MQTT клиента по умолчанию
#define DEFAULT_MQTT_SERVER     "m2.wqtt.ru"     // IP адрес или имя MQTT брокера
#define DEFAULT_MQTT_PORT       13611                // Порт MQTT брокера
#define DEFAULT_MQTT_USER       "u_VKCUCZ"                  // Логин авторизации MQTT (если требуется)
#define DEFAULT_MQTT_PASS       "ZF0sZf1T"                  // Пароль авторизации MQTT (если требуется)
#define DEFAULT_MQTT_PREFIX     "warmfloor"         // Корневой префикс топиков

// ==============================================================================
// 4. СТРУКТУРЫ ДАННЫХ
// ==============================================================================
struct ZoneData {
    bool enabled;           // Флаг работы зоны (true = активна)
    float currentTemp;      // Текущая измеренная температура пола (°C)
    bool relayState;        // Физическое состояние реле SSR (true = ON, false = OFF)
    bool sensorError;       // Флаг ошибки/обрыва датчика
};

struct AirData {
    float currentTemp;      // Температура воздуха в помещении (°C)
    bool sensorError;       // Флаг ошибки датчика воздуха
};

struct SystemConfig {
    char wifi_ssid[33];
    char wifi_pass[65];
    char ap_ssid[33];
    char ap_pass[33];
    char mqtt_server[65];
    uint16_t mqtt_port;
    char mqtt_user[33];
    char mqtt_pass[33];
    char mqtt_topic_prefix[33];
    bool mqtt_enabled;
    float max_floor_temp;
};

// ==============================================================================
// 5. ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ (ТОЛЬКО EXTERN, ФИЗИЧЕСКИ ОПРЕДЕЛЕНЫ В МОДУЛЯХ .CPP)
// ==============================================================================
extern ZoneData zone1;
extern ZoneData zone2;
extern AirData air;
extern SystemConfig sysCfg;

extern float targetFloorTemp;     // Общая уставка пола для Зоны 1 и Зоны 2 (°C)
extern float targetAirTemp;       // Уставка температуры воздуха в помещении (°C)
extern bool airHeatingDemand;     // Флаг: требует ли воздух тепла
extern bool heatingEnabled;       // Единый главный флаг питания отопления (ВКЛ / ВЫКЛ)

extern bool wifiConnected;
extern bool apModeActive;
extern bool mqttConnected;
extern bool sensorsSimulationActive;
extern String localIpAddress;

// ==============================================================================
// 6. ВСЕ ПРОТОТИПЫ ИНИЦИАЛИЗАЦИИ И НЕБЛОКИРУЮЩИХ ТИКОВ ИСПОЛНЯЕМЫХ МОДУЛЕЙ
// ==============================================================================
// 1. Хранилище NVS Preferences (storage.cpp)
void storage_init();
void storage_save_setpoints();
void storage_request_save();
void storage_save_config();
void storage_tick();

// 2. Датчики температуры / Имитатор (sensors.cpp)
void sensors_init();
void sensors_tick();
uint8_t sensors_get_count();

// 3. Алгоритм термостата и управление реле SSR (thermostat.cpp)
void thermostat_init();
void thermostat_tick();

// 4. Дисплей TM1637 и 3 кнопки (local_ui.cpp)
void local_ui_show_boot();
void local_ui_init();
void local_ui_tick();
void local_ui_show_msg(const char* msg, uint16_t durationMs = 1500);

// 5. Сеть Wi-Fi (net_wifi.cpp)
void net_wifi_init();
void net_wifi_tick();

// 6. Веб-сервер (net_webserver.cpp)
void net_webserver_init();
void net_webserver_tick();

// 7. MQTT клиент (net_mqtt.cpp)
void net_mqtt_init();
void net_mqtt_tick();
void net_mqtt_publish_state();
void net_mqtt_disconnect();

// 8. Системный светодиод активности (status_led.cpp)
void status_led_init();
void status_led_tick();
