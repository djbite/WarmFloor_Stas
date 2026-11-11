#include "global.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static bool rebootRequested = false;
static unsigned long rebootTime = 0;

static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="ru">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Теплый пол Стаса</title>
<style>
:root {
  --bg: #121820;
  --card: #1c2430;
  --accent: #ff6b35;
  --accent-blue: #00b4d8;
  --text: #e0e6ed;
  --subtext: #8892b0;
  --success: #2ec4b6;
  --warning: #ff9f1c;
  --danger: #e63946;
  --border: #2a374a;
}
* { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
body { background: var(--bg); color: var(--text); padding: 16px; display: flex; justify-content: center; }
.container { max-width: 600px; width: 100%; display: flex; flex-direction: column; gap: 16px; }
header { text-align: center; padding: 12px 0; border-bottom: 1px solid var(--border); }
header h1 { font-size: 1.4rem; color: var(--accent); }
header p { font-size: 0.8rem; color: var(--subtext); margin-top: 4px; }
.sim-alert {
  background: var(--warning); color: #121820; font-weight: bold; font-size: 0.85rem;
  text-align: center; padding: 10px; border-radius: 8px; box-shadow: 0 2px 8px rgba(0,0,0,0.3);
}
.card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 16px; }
.card-master { display: flex; justify-content: space-between; align-items: center; border: 1px solid #3b4d66; background: #19222e; }
.card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px; }
.card-title { font-size: 1.1rem; font-weight: 600; }
.badge { font-size: 0.75rem; padding: 3px 8px; border-radius: 12px; font-weight: 600; text-transform: uppercase; }
.badge-on { background: rgba(46,196,182,0.2); color: var(--success); }
.badge-off { background: rgba(136,146,176,0.2); color: var(--subtext); }
.badge-heat { background: rgba(255,107,53,0.2); color: var(--accent); animation: pulse 1.5s infinite; }
.badge-air-off { background: rgba(0,180,216,0.2); color: var(--accent-blue); }
.badge-err { background: rgba(230,57,70,0.2); color: var(--danger); }
@keyframes pulse { 0% { opacity: 0.6; } 50% { opacity: 1; } 100% { opacity: 0.6; } }
.temp-row { display: flex; align-items: baseline; justify-content: space-between; margin: 8px 0 4px 0; }
.temp-val { font-size: 2.4rem; font-weight: bold; }
.temp-sub { font-size: 0.9rem; color: var(--subtext); }
.setpoints-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
.input-group { display: flex; flex-direction: column; gap: 4px; }
.input-group label { font-size: 0.75rem; color: var(--subtext); }
input[type="number"], input[type="text"], input[type="password"] {
  background: #141b24; border: 1px solid var(--border); color: #fff; padding: 10px 12px;
  border-radius: 6px; font-size: 1.05rem; outline: none; width: 100%;
}
.btn {
  background: var(--accent); border: none; color: #fff; font-weight: 600;
  padding: 10px; border-radius: 6px; cursor: pointer; transition: 0.2s; font-size: 0.9rem;
}
.btn:hover { opacity: 0.9; }
.btn-toggle { background: #2a374a; }
.btn-toggle.active { background: var(--success); }
.btn-master { padding: 12px 22px; font-size: 1.05rem; }
.btn-block { width: 100%; margin-top: 12px; }
.sys-info { font-size: 0.8rem; color: var(--subtext); line-height: 1.5; }
details { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 14px; }
summary { font-weight: 600; cursor: pointer; color: var(--accent-blue); outline: none; }
.form-grid { display: flex; flex-direction: column; gap: 10px; margin-top: 14px; }
</style>
</head>
<body>
<div class="container">
  <header>
    <h1>Теплый пол Стаса</h1>
    <p id="ver-info">ESP32-C3 SuperMini • Загрузка...</p>
  </header>

  <div id="sim-banner" class="sim-alert" style="display:none;">
    ⚠️ РЕЖИМ ИМИТАЦИИ: опрос каждые 2 сек (физика нагрева и остывания активна)
  </div>

  <!-- ГЛАВНЫЙ ВЫКЛЮЧАТЕЛЬ ОТОПЛЕНИЯ -->
  <div class="card card-master">
    <div>
      <div class="card-title">Главный выключатель</div>
      <div style="font-size:0.8rem; color:var(--subtext);">Единое управление отоплением для помещения</div>
    </div>
    <button id="master-btn" class="btn btn-toggle btn-master" onclick="toggleHeating()">Отопление ВЫКЛ</button>
  </div>

  <!-- БЛОК ДВУХ УСТАВОК (ПОЛ И ВОЗДУХ) -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">🎯 Две уставки (гистерезис 0.5°C)</span>
      <span style="font-size:0.8rem; color:var(--accent-blue);">Что первое нагрелось — то и гасит!</span>
    </div>
    <div class="setpoints-grid">
      <div class="input-group">
        <label>🔥 Уставка ПОЛА (общая, °C)</label>
        <input type="number" id="set-floor" step="0.5" min="10" max="36" onchange="applySetpoints()">
      </div>
      <div class="input-group">
        <label>💨 Уставка ВОЗДУХА (°C)</label>
        <input type="number" id="set-air" step="0.5" min="10" max="35" onchange="applySetpoints()">
      </div>
    </div>
  </div>

  <!-- ЗОНА 1 -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">Зона 1: Теплый пол</span>
      <span id="z1-status" class="badge badge-off">Выкл</span>
    </div>
    <div class="temp-row">
      <div>
        <span id="z1-cur" class="temp-val">--.-</span> <span style="font-size:1.4rem; color:var(--subtext)">°C</span>
      </div>
      <div class="temp-sub">Уставка пола: <b class="fl-tar-label">--.-</b> °C</div>
    </div>
  </div>

  <!-- ЗОНА 2 -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">Зона 2: Теплый пол</span>
      <span id="z2-status" class="badge badge-off">Выкл</span>
    </div>
    <div class="temp-row">
      <div>
        <span id="z2-cur" class="temp-val">--.-</span> <span style="font-size:1.4rem; color:var(--subtext)">°C</span>
      </div>
      <div class="temp-sub">Уставка пола: <b class="fl-tar-label">--.-</b> °C</div>
    </div>
  </div>

  <!-- ВОЗДУХ В ПОМЕЩЕНИИ -->
  <div class="card">
    <div class="card-header">
      <span class="card-title">Воздух в помещении</span>
      <span id="air-status" class="badge badge-on">Требует тепла</span>
    </div>
    <div class="temp-row">
      <div>
        <span id="air-cur" class="temp-val" style="color:var(--accent-blue);">--.-</span> <span style="font-size:1.4rem; color:var(--subtext)">°C</span>
      </div>
      <div class="temp-sub">Уставка воздуха: <b id="air-tar-label">--.-</b> °C</div>
    </div>
  </div>

  <!-- СИСТЕМНЫЕ НАСТРОЙКИ СЕТИ -->
  <details>
    <summary>⚙️ Сетевые настройки (Wi-Fi и MQTT)</summary>
    <div style="font-size:0.8rem; color:var(--subtext); margin:6px 0 10px 0;">
      ℹ️ Примечание: Уставки температуры пола и воздуха выше применяются <b>сразу на лету</b> без перезагрузки. 
      Перезагрузка платы выполняется только при смене параметров Wi-Fi/MQTT сети.
    </div>
    <form class="form-grid" onsubmit="saveSettings(event)">
      <div class="input-group">
        <label>Wi-Fi SSID</label>
        <input type="text" id="cfg-ssid" placeholder="Имя вашей сети">
      </div>
      <div class="input-group">
        <label>Wi-Fi Пароль</label>
        <input type="password" id="cfg-pass" placeholder="Пароль от Wi-Fi">
      </div>
      <div class="input-group">
        <label>Аварийный предел температуры пола (°C)</label>
        <input type="number" id="cfg-max" step="0.5" min="25" max="45">
      </div>
      <hr style="border:0; border-top:1px solid var(--border); margin:4px 0;">
      <div class="input-group">
        <label style="display:flex; align-items:center; gap:8px;">
          <input type="checkbox" id="cfg-mq-en"> Включить MQTT клиент
        </label>
      </div>
      <div class="input-group">
        <label>MQTT Брокер (IP или хост)</label>
        <input type="text" id="cfg-mq-srv" placeholder="192.168.1.100">
      </div>
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px;">
        <div class="input-group">
          <label>Порт</label>
          <input type="number" id="cfg-mq-port" value="1883">
        </div>
        <div class="input-group">
          <label>Префикс топика</label>
          <input type="text" id="cfg-mq-pre" value="warmfloor">
        </div>
      </div>
      <div style="display:grid; grid-template-columns:1fr 1fr; gap:10px;">
        <div class="input-group">
          <label>Логин</label>
          <input type="text" id="cfg-mq-usr">
        </div>
        <div class="input-group">
          <label>Пароль</label>
          <input type="password" id="cfg-mq-pwd">
        </div>
      </div>
      <button type="submit" class="btn btn-block">Сохранить и перезагрузить</button>
    </form>
  </details>

  <div class="card sys-info">
    <div><b>IP адрес:</b> <span id="sys-ip">--</span></div>
    <div><b>MQTT:</b> <span id="sys-mqtt">--</span> (единый пакет state)</div>
    <div><b>Аптайм:</b> <span id="sys-uptime">--</span></div>
  </div>
</div>

<script>
let state = {};

async function fetchStatus() {
  try {
    const res = await fetch('/api/status');
    state = await res.json();
    updateUI();
  } catch(e) {
    console.error(e);
  }
}

function updateUI() {
  if (!state) return;
  document.getElementById('ver-info').textContent = state.ver || 'Теплый пол Стаса';
  document.getElementById('sys-ip').textContent = state.ip;
  document.getElementById('sys-mqtt').textContent = state.mqtt ? 'Подключен' : 'Отключен';
  document.getElementById('sys-uptime').textContent = Math.floor(state.uptime / 60) + ' мин.';

  const simBanner = document.getElementById('sim-banner');
  simBanner.style.display = state.sim ? 'block' : 'none';

  // Уставки
  if (document.activeElement !== document.getElementById('set-floor')) {
    document.getElementById('set-floor').value = state.floor_set;
  }
  if (document.activeElement !== document.getElementById('set-air')) {
    document.getElementById('set-air').value = state.air_set;
  }
  document.querySelectorAll('.fl-tar-label').forEach(el => el.textContent = state.floor_set.toFixed(1));
  document.getElementById('air-tar-label').textContent = state.air_set.toFixed(1);

  // Главный выключатель
  const masterBtn = document.getElementById('master-btn');
  if (state.heat_en) {
    masterBtn.className = 'btn btn-toggle active btn-master';
    masterBtn.textContent = 'Отопление ВКЛ';
  } else {
    masterBtn.className = 'btn btn-toggle btn-master';
    masterBtn.textContent = 'Отопление ВЫКЛ';
  }

  // Воздух
  const airSt = document.getElementById('air-status');
  if (state.air_err || state.air_t <= -100) {
    document.getElementById('air-cur').textContent = '--.-';
    airSt.className = 'badge badge-err';
    airSt.textContent = 'Ошибка датчика';
  } else {
    document.getElementById('air-cur').textContent = state.air_t.toFixed(1);
    if (!state.heat_en) {
      airSt.className = 'badge badge-off';
      airSt.textContent = 'Отопление выключено';
    } else if (state.air_demand) {
      airSt.className = 'badge badge-on';
      airSt.textContent = 'Требует тепла';
    } else {
      airSt.className = 'badge badge-air-off';
      airSt.textContent = 'Воздух прогрет (гасит оба пола)';
    }
  }

  // Зона 1
  const z1St = document.getElementById('z1-status');
  if (state.z1_err || state.z1_t <= -100) {
    document.getElementById('z1-cur').textContent = '--.-';
    z1St.className = 'badge badge-err';
    z1St.textContent = 'Ошибка датчика';
  } else {
    document.getElementById('z1-cur').textContent = state.z1_t.toFixed(1);
    if (!state.heat_en) {
      z1St.className = 'badge badge-off';
      z1St.textContent = 'Выключен';
    } else if (state.z1_relay) {
      z1St.className = 'badge badge-heat';
      z1St.textContent = 'Нагрев (SSR ВКЛ)';
    } else if (!state.air_demand) {
      z1St.className = 'badge badge-air-off';
      z1St.textContent = 'Остановлен воздухом';
    } else {
      z1St.className = 'badge badge-on';
      z1St.textContent = 'Пол прогрет (ожидание)';
    }
  }

  // Зона 2
  const z2St = document.getElementById('z2-status');
  if (state.z2_err || state.z2_t <= -100) {
    document.getElementById('z2-cur').textContent = '--.-';
    z2St.className = 'badge badge-err';
    z2St.textContent = 'Ошибка датчика';
  } else {
    document.getElementById('z2-cur').textContent = state.z2_t.toFixed(1);
    if (!state.heat_en) {
      z2St.className = 'badge badge-off';
      z2St.textContent = 'Выключен';
    } else if (state.z2_relay) {
      z2St.className = 'badge badge-heat';
      z2St.textContent = 'Нагрев (SSR ВКЛ)';
    } else if (!state.air_demand) {
      z2St.className = 'badge badge-air-off';
      z2St.textContent = 'Остановлен воздухом';
    } else {
      z2St.className = 'badge badge-on';
      z2St.textContent = 'Пол прогрет (ожидание)';
    }
  }
}

async function applySetpoints() {
  const fl = parseFloat(document.getElementById('set-floor').value);
  const ar = parseFloat(document.getElementById('set-air').value);
  if (!isNaN(fl) && !isNaN(ar)) {
    await fetch(`/api/setpoint?floor=${fl}&air=${ar}`);
    fetchStatus();
  }
}

async function toggleHeating() {
  const newEn = !state.heat_en ? 1 : 0;
  await fetch(`/api/power?en=${newEn}`);
  fetchStatus();
}

async function saveSettings(e) {
  e.preventDefault();
  const payload = {
    ssid: document.getElementById('cfg-ssid').value,
    pass: document.getElementById('cfg-pass').value,
    max_t: parseFloat(document.getElementById('cfg-max').value),
    mq_en: document.getElementById('cfg-mq-en').checked,
    mq_srv: document.getElementById('cfg-mq-srv').value,
    mq_port: parseInt(document.getElementById('cfg-mq-port').value),
    mq_usr: document.getElementById('cfg-mq-usr').value,
    mq_pwd: document.getElementById('cfg-mq-pwd').value,
    mq_pre: document.getElementById('cfg-mq-pre').value
  };
  await fetch('/api/settings', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  alert('Настройки сохранены! Плата перезагружается...');
}

async function loadConfig() {
  try {
    const res = await fetch('/api/config');
    const c = await res.json();
    document.getElementById('cfg-ssid').value = c.ssid || '';
    document.getElementById('cfg-max').value = c.max_t || 36;
    document.getElementById('cfg-mq-en').checked = c.mq_en || false;
    document.getElementById('cfg-mq-srv').value = c.mq_srv || '';
    document.getElementById('cfg-mq-port').value = c.mq_port || 1883;
    document.getElementById('cfg-mq-usr').value = c.mq_usr || '';
    document.getElementById('cfg-mq-pre').value = c.mq_pre || 'warmfloor';
  } catch(e) {}
}

fetchStatus();
loadConfig();
setInterval(fetchStatus, 2000);
</script>
</body>
</html>
)rawliteral";

void net_webserver_init() {
    Serial.println("[WEBSERVER] Запуск HTTP сервера на порту 80...");

    // Главная страница
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });

    // API состояния
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["ver"] = GET_VERSION_STRING();
        doc["ip"] = localIpAddress;
        doc["uptime"] = millis() / 1000;
        doc["mqtt"] = mqttConnected;
        doc["sim"] = sensorsSimulationActive;

        doc["heat_en"] = heatingEnabled;
        doc["floor_set"] = roundf(targetFloorTemp * 10.0f) / 10.0f;
        doc["air_set"] = roundf(targetAirTemp * 10.0f) / 10.0f;
        doc["air_demand"] = airHeatingDemand;

        doc["z1_t"] = roundf(zone1.currentTemp * 10.0f) / 10.0f;
        doc["z1_relay"] = zone1.relayState;
        doc["z1_err"] = zone1.sensorError;

        doc["z2_t"] = roundf(zone2.currentTemp * 10.0f) / 10.0f;
        doc["z2_relay"] = zone2.relayState;
        doc["z2_err"] = zone2.sensorError;

        doc["air_t"] = roundf(air.currentTemp * 10.0f) / 10.0f;
        doc["air_err"] = air.sensorError;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json; charset=utf-8", response);
    });

    // API изменения уставок: /api/setpoint?floor=26.0&air=22.0
    server.on("/api/setpoint", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("floor")) {
            float f = request->getParam("floor")->value().toFloat();
            if (f >= 10.0f && f <= sysCfg.max_floor_temp) {
                targetFloorTemp = f;
            }
        }
        if (request->hasParam("air")) {
            float a = request->getParam("air")->value().toFloat();
            if (a >= 10.0f && a <= 35.0f) {
                targetAirTemp = a;
            }
        }
        storage_request_save();
        net_mqtt_publish_state();
        request->send(200, "text/plain", "OK");
    });

    // API управления питанием отопления: /api/power?en=1
    server.on("/api/power", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (request->hasParam("en")) {
            bool en = (request->getParam("en")->value().toInt() == 1);
            heatingEnabled = en;
            zone1.enabled = en;
            zone2.enabled = en;
            storage_request_save();
            net_mqtt_publish_state();
            Serial.printf("[WEB] Питание отопления: %s\n", en ? "ВКЛ" : "ВЫКЛ");
        }
        request->send(200, "text/plain", "OK");
    });

    // API чтения настроек
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request) {
        JsonDocument doc;
        doc["ssid"] = sysCfg.wifi_ssid;
        doc["max_t"] = sysCfg.max_floor_temp;
        doc["mq_en"] = sysCfg.mqtt_enabled;
        doc["mq_srv"] = sysCfg.mqtt_server;
        doc["mq_port"] = sysCfg.mqtt_port;
        doc["mq_usr"] = sysCfg.mqtt_user;
        doc["mq_pre"] = sysCfg.mqtt_topic_prefix;

        String response;
        serializeJson(doc, response);
        request->send(200, "application/json; charset=utf-8", response);
    });

    // API сохранения настроек (JSON POST)
    server.on("/api/settings", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL,
        [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (!err) {
                if (doc["ssid"].is<const char*>()) {
                    strncpy(sysCfg.wifi_ssid, doc["ssid"], sizeof(sysCfg.wifi_ssid) - 1);
                }
                if (doc["pass"].is<const char*>()) {
                    const char* p = doc["pass"];
                    if (strlen(p) > 0) {
                        strncpy(sysCfg.wifi_pass, p, sizeof(sysCfg.wifi_pass) - 1);
                    }
                }
                if (doc["max_t"].is<float>()) {
                    sysCfg.max_floor_temp = doc["max_t"];
                }
                if (doc["mq_en"].is<bool>()) {
                    sysCfg.mqtt_enabled = doc["mq_en"];
                }
                if (doc["mq_srv"].is<const char*>()) {
                    strncpy(sysCfg.mqtt_server, doc["mq_srv"], sizeof(sysCfg.mqtt_server) - 1);
                }
                if (doc["mq_port"].is<uint16_t>()) {
                    sysCfg.mqtt_port = doc["mq_port"];
                }
                if (doc["mq_usr"].is<const char*>()) {
                    strncpy(sysCfg.mqtt_user, doc["mq_usr"], sizeof(sysCfg.mqtt_user) - 1);
                }
                if (doc["mq_pwd"].is<const char*>()) {
                    const char* mp = doc["mq_pwd"];
                    if (strlen(mp) > 0) {
                        strncpy(sysCfg.mqtt_pass, mp, sizeof(sysCfg.mqtt_pass) - 1);
                    }
                }
                if (doc["mq_pre"].is<const char*>()) {
                    strncpy(sysCfg.mqtt_topic_prefix, doc["mq_pre"], sizeof(sysCfg.mqtt_topic_prefix) - 1);
                }

                storage_save_config();
                rebootRequested = true;
                rebootTime = millis() + 1500;
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(400, "application/json", "{\"status\":\"error\"}");
            }
        }
    );

    server.begin();
    Serial.println("[WEBSERVER] HTTP сервер успешно запущен.");
}

void net_webserver_tick() {
    if (rebootRequested && millis() >= rebootTime) {
        Serial.println("[SYSTEM] Перезагрузка по запросу веб-интерфейса...");
        net_mqtt_disconnect();
        ESP.restart();
    }
}
