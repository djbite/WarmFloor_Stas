#include "global.h"
#include <WiFi.h>
#include <ESPmDNS.h>

bool wifiConnected = false;
bool apModeActive = false;
String localIpAddress = "0.0.0.0";

static unsigned long lastWifiCheck = 0;
static const unsigned long WIFI_CHECK_INTERVAL_MS = 5000;
static unsigned long staConnectStartTime = 0;
static const unsigned long STA_CONNECT_TIMEOUT_MS = 15000;
static bool connectingSta = false;

static void startApMode() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(sysCfg.ap_ssid, sysCfg.ap_pass);
    apModeActive = true;
    wifiConnected = false;
    localIpAddress = WiFi.softAPIP().toString();
    Serial.printf("[WIFI] Запущена точка доступа: SSID '%s', IP: %s\n",
                  sysCfg.ap_ssid, localIpAddress.c_str());
}

void net_wifi_init() {
    Serial.println("[WIFI] Инициализация Wi-Fi...");
    WiFi.persistent(false);
    WiFi.disconnect(true);

    if (strlen(sysCfg.wifi_ssid) > 0) {
        Serial.printf("[WIFI] Попытка подключения к роутеру '%s'...\n", sysCfg.wifi_ssid);
        WiFi.mode(WIFI_STA);
        WiFi.begin(sysCfg.wifi_ssid, sysCfg.wifi_pass);
        connectingSta = true;
        staConnectStartTime = millis();
    } else {
        Serial.println("[WIFI] SSID домашней сети не задан. Запуск режима точки доступа.");
        startApMode();
    }
}

void net_wifi_tick() {
    unsigned long now = millis();

    // 1. Ожидание первого подключения к STA
    if (connectingSta) {
        if (WiFi.status() == WL_CONNECTED) {
            connectingSta = false;
            wifiConnected = true;
            apModeActive = false;
            localIpAddress = WiFi.localIP().toString();
            Serial.printf("[WIFI] ✔ Успешно подключено к Wi-Fi! IP: %s\n", localIpAddress.c_str());

            if (MDNS.begin("warmfloor")) {
                MDNS.addService("http", "tcp", 80);
                Serial.println("[WIFI] mDNS доступен по адресу: http://warmfloor.local");
            }
        } else if (now - staConnectStartTime >= STA_CONNECT_TIMEOUT_MS) {
            connectingSta = false;
            Serial.println("[WIFI] ❌ Таймаут подключения к роутеру. Переключение в режим SoftAP.");
            startApMode();
        }
        return;
    }

    // 2. Периодический контроль связи
    if (now - lastWifiCheck >= WIFI_CHECK_INTERVAL_MS) {
        lastWifiCheck = now;

        if (wifiConnected && WiFi.status() != WL_CONNECTED) {
            wifiConnected = false;
            Serial.println("[WIFI] Связь с роутером потеряна!");
            if (!apModeActive) {
                startApMode();
            }
        }
    }
}
