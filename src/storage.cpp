#include "global.h"
#include <Preferences.h>

static Preferences prefs;
static const char* PREFS_NAMESPACE = "warmfloor";

// Физическое определение глобальных переменных уставок и настроек
SystemConfig sysCfg;
float targetFloorTemp = DEFAULT_FLOOR_SET;
float targetAirTemp = DEFAULT_AIR_SET;
bool airHeatingDemand = true;
bool heatingEnabled = true;

static bool setpointsDirty = false;
static unsigned long dirtyTimestamp = 0;
static const unsigned long PREFS_SAVE_DELAY_MS = 2500; // Отложенное сохранение для сбережения flash-памяти

void storage_init() {
    Serial.println("[STORAGE] Инициализация NVS Preferences...");
    prefs.begin(PREFS_NAMESPACE, false);

    // 1. Загрузка уставок (две уставки: пол общая и воздух)
    targetFloorTemp = prefs.getFloat("t_fl", DEFAULT_FLOOR_SET);
    targetAirTemp = prefs.getFloat("t_ar", DEFAULT_AIR_SET);

    // 2. Единый флаг питания отопления (термостата)
    heatingEnabled = prefs.getBool("heat_en", true);
    zone1.enabled = heatingEnabled;
    zone2.enabled = heatingEnabled;

    zone1.currentTemp = SENSOR_ERR_TEMP;
    zone1.relayState = false;
    zone1.sensorError = false;

    zone2.currentTemp = SENSOR_ERR_TEMP;
    zone2.relayState = false;
    zone2.sensorError = false;

    air.currentTemp = SENSOR_ERR_TEMP;
    air.sensorError = false;

    // 3. Системные настройки (значения по умолчанию берутся из global.h)
    sysCfg.max_floor_temp = prefs.getFloat("max_t", DEFAULT_MAX_FLOOR_T);

    String sSsid = prefs.getString("ssid", DEFAULT_WIFI_SSID);
    String sPass = prefs.getString("pass", DEFAULT_WIFI_PASS);
    strncpy(sysCfg.wifi_ssid, sSsid.c_str(), sizeof(sysCfg.wifi_ssid) - 1);
    sysCfg.wifi_ssid[sizeof(sysCfg.wifi_ssid) - 1] = '\0';
    strncpy(sysCfg.wifi_pass, sPass.c_str(), sizeof(sysCfg.wifi_pass) - 1);
    sysCfg.wifi_pass[sizeof(sysCfg.wifi_pass) - 1] = '\0';

    strncpy(sysCfg.ap_ssid, DEFAULT_AP_SSID, sizeof(sysCfg.ap_ssid) - 1);
    sysCfg.ap_ssid[sizeof(sysCfg.ap_ssid) - 1] = '\0';
    strncpy(sysCfg.ap_pass, DEFAULT_AP_PASS, sizeof(sysCfg.ap_pass) - 1);
    sysCfg.ap_pass[sizeof(sysCfg.ap_pass) - 1] = '\0';

    // 4. MQTT настройки (значения по умолчанию берутся из global.h)
    sysCfg.mqtt_enabled = prefs.getBool("mq_en", DEFAULT_MQTT_ENABLED);
    String sMqSrv = prefs.getString("mq_srv", DEFAULT_MQTT_SERVER);
    strncpy(sysCfg.mqtt_server, sMqSrv.c_str(), sizeof(sysCfg.mqtt_server) - 1);
    sysCfg.mqtt_server[sizeof(sysCfg.mqtt_server) - 1] = '\0';

    sysCfg.mqtt_port = prefs.getUShort("mq_port", DEFAULT_MQTT_PORT);

    String sMqUsr = prefs.getString("mq_usr", DEFAULT_MQTT_USER);
    strncpy(sysCfg.mqtt_user, sMqUsr.c_str(), sizeof(sysCfg.mqtt_user) - 1);
    sysCfg.mqtt_user[sizeof(sysCfg.mqtt_user) - 1] = '\0';

    String sMqPwd = prefs.getString("mq_pwd", DEFAULT_MQTT_PASS);
    strncpy(sysCfg.mqtt_pass, sMqPwd.c_str(), sizeof(sysCfg.mqtt_pass) - 1);
    sysCfg.mqtt_pass[sizeof(sysCfg.mqtt_pass) - 1] = '\0';

    String sMqPre = prefs.getString("mq_pre", DEFAULT_MQTT_PREFIX);
    strncpy(sysCfg.mqtt_topic_prefix, sMqPre.c_str(), sizeof(sysCfg.mqtt_topic_prefix) - 1);
    sysCfg.mqtt_topic_prefix[sizeof(sysCfg.mqtt_topic_prefix) - 1] = '\0';

    prefs.end();

    Serial.printf("[STORAGE] NVS: Пол=%.1f°C, Воздух=%.1f°C, Отопление=%s\n",
                  targetFloorTemp, targetAirTemp, heatingEnabled ? "ВКЛ" : "ВЫКЛ");
    Serial.printf("[STORAGE] Сеть: Wi-Fi SSID='%s', MQTT='%s:%d' (вкл=%d)\n",
                  sysCfg.wifi_ssid, sysCfg.mqtt_server, sysCfg.mqtt_port, sysCfg.mqtt_enabled);
}

void storage_save_setpoints() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putFloat("t_fl", targetFloorTemp);
    prefs.putFloat("t_ar", targetAirTemp);
    prefs.putBool("heat_en", heatingEnabled);
    prefs.end();

    setpointsDirty = false;
    Serial.printf("[STORAGE] Сохранено: Пол=%.1f°C, Воздух=%.1f°C, Отопление=%s\n",
                  targetFloorTemp, targetAirTemp, heatingEnabled ? "ВКЛ" : "ВЫКЛ");
}

void storage_request_save() {
    setpointsDirty = true;
    dirtyTimestamp = millis();
}

void storage_save_config() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putFloat("max_t", sysCfg.max_floor_temp);
    prefs.putString("ssid", sysCfg.wifi_ssid);
    prefs.putString("pass", sysCfg.wifi_pass);
    prefs.putBool("mq_en", sysCfg.mqtt_enabled);
    prefs.putString("mq_srv", sysCfg.mqtt_server);
    prefs.putUShort("mq_port", sysCfg.mqtt_port);
    prefs.putString("mq_usr", sysCfg.mqtt_user);
    prefs.putString("mq_pwd", sysCfg.mqtt_pass);
    prefs.putString("mq_pre", sysCfg.mqtt_topic_prefix);
    prefs.end();

    Serial.println("[STORAGE] Системная конфигурация (Wi-Fi/MQTT/Защита) сохранена в NVS.");
}

void storage_tick() {
    if (setpointsDirty && (millis() - dirtyTimestamp >= PREFS_SAVE_DELAY_MS)) {
        storage_save_setpoints();
    }
}
