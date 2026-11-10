#include "global.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

bool mqttConnected = false;

static WiFiClient espClient;
static PubSubClient client(espClient);

static unsigned long lastMqttConnectAttempt = 0;
static const unsigned long MQTT_RECONNECT_INTERVAL_MS = 6000;

static unsigned long lastTelemetryPublish = 0;
static const unsigned long TELEMETRY_INTERVAL_MS = 5000; // Отправка единого пакета раз в 5 сек

// Отправка единого краткого JSON пакета в MQTT без спама
void net_mqtt_publish_state() {
    if (!client.connected()) return;

    String prefix = String(sysCfg.mqtt_topic_prefix);
    String stateTopic = prefix + "/state";

    JsonDocument doc;
    // Уставки
    doc["fl_set"] = roundf(targetFloorTemp * 10.0f) / 10.0f;
    doc["ar_set"] = roundf(targetAirTemp * 10.0f) / 10.0f;

    // Зона 1
    doc["z1_t"] = roundf(zone1.currentTemp * 10.0f) / 10.0f;
    doc["z1_h"] = zone1.relayState ? 1 : 0;

    // Зона 2
    doc["z2_t"] = roundf(zone2.currentTemp * 10.0f) / 10.0f;
    doc["z2_h"] = zone2.relayState ? 1 : 0;

    // Воздух
    doc["air"] = roundf(air.currentTemp * 10.0f) / 10.0f;
    doc["ar_dem"] = airHeatingDemand ? 1 : 0;
    doc["heat"] = heatingEnabled ? 1 : 0;

    char buffer[160];
    size_t len = serializeJson(doc, buffer, sizeof(buffer));
    client.publish(stateTopic.c_str(), (const uint8_t*)buffer, len, true);
}

static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    char message[128];
    if (length >= sizeof(message)) length = sizeof(message) - 1;
    memcpy(message, payload, length);
    message[length] = '\0';

    Serial.printf("[MQTT] Команда [%s]: %s\n", topic, message);

    String t = String(topic);
    String prefix = String(sysCfg.mqtt_topic_prefix);

    // 1. Прием единого JSON пакета команд на prefix/set
    if (t == (prefix + "/set")) {
        JsonDocument inDoc;
        DeserializationError err = deserializeJson(inDoc, message);
        if (!err) {
            bool changed = false;
            if (inDoc["floor_set"].is<float>()) {
                targetFloorTemp = inDoc["floor_set"];
                changed = true;
            }
            if (inDoc["air_set"].is<float>()) {
                targetAirTemp = inDoc["air_set"];
                changed = true;
            }
            if (inDoc["heat"].is<int>()) {
                heatingEnabled = (inDoc["heat"] == 1);
                zone1.enabled = heatingEnabled;
                zone2.enabled = heatingEnabled;
                changed = true;
            }
            if (changed) {
                storage_request_save();
                net_mqtt_publish_state();
            }
        }
        return;
    }

    // 2. Удобные стандартные топики прямого управления
    if (t == (prefix + "/floor/set_target")) {
        float val = atof(message);
        if (val >= 10.0f && val <= sysCfg.max_floor_temp) {
            targetFloorTemp = val;
            storage_request_save();
            net_mqtt_publish_state();
        }
    } else if (t == (prefix + "/air/set_target")) {
        float val = atof(message);
        if (val >= 10.0f && val <= 35.0f) {
            targetAirTemp = val;
            storage_request_save();
            net_mqtt_publish_state();
        }
    } else if (t == (prefix + "/heating/set_power") || t == (prefix + "/power")) {
        heatingEnabled = (strcasecmp(message, "ON") == 0 || strcmp(message, "1") == 0);
        zone1.enabled = heatingEnabled;
        zone2.enabled = heatingEnabled;
        storage_request_save();
        net_mqtt_publish_state();
    }
}

void net_mqtt_init() {
    if (!sysCfg.mqtt_enabled || strlen(sysCfg.mqtt_server) == 0) {
        Serial.println("[MQTT] Клиент отключен в настройках.");
        return;
    }

    Serial.printf("[MQTT] Настройка подключения к %s:%d...\n", sysCfg.mqtt_server, sysCfg.mqtt_port);
    client.setServer(sysCfg.mqtt_server, sysCfg.mqtt_port);
    client.setCallback(mqttCallback);
}

void net_mqtt_tick() {
    if (!sysCfg.mqtt_enabled || !wifiConnected) {
        mqttConnected = false;
        return;
    }

    if (client.connected()) {
        mqttConnected = true;
        client.loop(); // Неблокирующий цикл

        unsigned long now = millis();
        if (now - lastTelemetryPublish >= TELEMETRY_INTERVAL_MS) {
            lastTelemetryPublish = now;
            net_mqtt_publish_state();
        }
        return;
    }

    mqttConnected = false;
    unsigned long now = millis();
    if (now - lastMqttConnectAttempt >= MQTT_RECONNECT_INTERVAL_MS) {
        lastMqttConnectAttempt = now;
        Serial.printf("[MQTT] Попытка подключения к брокеру %s:%d...\n", sysCfg.mqtt_server, sysCfg.mqtt_port);

        String clientId = "WarmFloor-" + String((uint32_t)ESP.getEfuseMac(), HEX);
        String prefix = String(sysCfg.mqtt_topic_prefix);
        String willTopic = prefix + "/status";

        bool ok = false;
        if (strlen(sysCfg.mqtt_user) > 0) {
            ok = client.connect(clientId.c_str(), sysCfg.mqtt_user, sysCfg.mqtt_pass,
                                willTopic.c_str(), 1, true, "offline");
        } else {
            ok = client.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline");
        }

        if (ok) {
            Serial.println("[MQTT] ✔ Подключение к брокеру успешно установлено!");
            mqttConnected = true;
            client.publish(willTopic.c_str(), "online", true);

            // Подписка на топики
            client.subscribe((prefix + "/set").c_str());
            client.subscribe((prefix + "/floor/set_target").c_str());
            client.subscribe((prefix + "/air/set_target").c_str());
            client.subscribe((prefix + "/heating/set_power").c_str());
            client.subscribe((prefix + "/power").c_str());

            net_mqtt_publish_state();
        } else {
            Serial.printf("[MQTT] Ошибка подключения, rc=%d\n", client.state());
        }
    }
}

void net_mqtt_disconnect() {
    if (client.connected()) {
        String prefix = String(sysCfg.mqtt_topic_prefix);
        String willTopic = prefix + "/status";
        // При корректном выключении/рестарте публикуем offline (retained)
        client.publish(willTopic.c_str(), "offline", true);
        client.disconnect();
    }
}
