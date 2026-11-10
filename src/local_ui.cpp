#include "global.h"
#include <GyverTM1637.h>

static GyverTM1637 tm(PIN_TM_CLK, PIN_TM_DIO);

static const uint8_t DIGIT_HEX[] = {
    _0, _1, _2, _3, _4, _5, _6, _7, _8, _9
};

static inline uint8_t getDigitByte(uint8_t d) {
    if (d > 9) return _empty;
    return DIGIT_HEX[d];
}

// ----------------------------------------------------------------------
// Кнопки с неблокирующим антидребезгом
// ----------------------------------------------------------------------
struct ButtonHandler {
    uint8_t pin;
    bool lastReading;
    bool debouncedState;
    unsigned long lastDebounceTime;
    unsigned long pressStartTime;
    bool longReported;
    bool clicked;
    bool longPressed;
};

static ButtonHandler btnMode = { PIN_BTN_MODE, HIGH, HIGH, 0, 0, false, false, false };
static ButtonHandler btnUp   = { PIN_BTN_UP,   HIGH, HIGH, 0, 0, false, false, false };
static ButtonHandler btnDown = { PIN_BTN_DOWN, HIGH, HIGH, 0, 0, false, false, false };

static const unsigned long DEBOUNCE_DELAY_MS = 40;
static const unsigned long LONG_PRESS_MS = 800;

static void updateButton(ButtonHandler& btn) {
    bool raw = digitalRead(btn.pin);
    unsigned long now = millis();

    if (raw != btn.lastReading) {
        btn.lastDebounceTime = now;
    }
    btn.lastReading = raw;

    if ((now - btn.lastDebounceTime) > DEBOUNCE_DELAY_MS) {
        if (raw != btn.debouncedState) {
            btn.debouncedState = raw;
            if (btn.debouncedState == LOW) {
                btn.pressStartTime = now;
                btn.longReported = false;
            } else {
                if (!btn.longReported && (now - btn.pressStartTime < LONG_PRESS_MS)) {
                    btn.clicked = true;
                }
            }
        } else if (btn.debouncedState == LOW) {
            if (!btn.longReported && (now - btn.pressStartTime >= LONG_PRESS_MS)) {
                btn.longPressed = true;
                btn.longReported = true;
            }
        }
    }
}

// ----------------------------------------------------------------------
// Меню: 2 шага (Пол и Воздух). Вкл/выкл по удержанию UP/DOWN.
// ----------------------------------------------------------------------
enum UiMode {
    UI_ROTATION,       // Авторотация экранов (Зона 1, Зона 2, Воздух)
    UI_SET_FLOOR_T,    // Шаг 1: Настройка общей уставки пола ("FL" -> "26:0°")
    UI_SET_AIR_T       // Шаг 2: Настройка уставки воздуха ("Air" -> "22:0°")
};

static UiMode currentUiMode = UI_ROTATION;
static uint8_t rotationScreen = 0; // 0=T1, 1=T2, 2=Air
static unsigned long screenStartTime = 0;
static unsigned long menuStepStartTime = 0;
static bool menuValueInteracted = false;
static unsigned long lastInteractionTime = 0;

static const unsigned long HEADER_DURATION_MS = 750;  // Длительность показа заголовка ("1", "2", "Air")
static const unsigned long TOTAL_SCREEN_MS = 2800;     // Общая длительность одного экрана
static const unsigned long MENU_TIMEOUT_MS = 7000;     // Таймаут бездействия в меню

static bool msgActive = false;
static unsigned long msgUntil = 0;
static uint8_t msgBytes[4] = { _empty, _empty, _empty, _empty };

void local_ui_show_msg(const char* msg, uint16_t durationMs) {
    msgActive = true;
    msgUntil = millis() + durationMs;
    for (int i = 0; i < 4; i++) msgBytes[i] = _empty;

    if (strcasecmp(msg, "boot") == 0) {
        msgBytes[0] = _b; msgBytes[1] = _o; msgBytes[2] = _o; msgBytes[3] = _t;
    } else if (strcasecmp(msg, "SAVE") == 0) {
        msgBytes[0] = _S; msgBytes[1] = _A; msgBytes[2] = _empty; msgBytes[3] = _E;
    } else if (strcasecmp(msg, "On") == 0 || strcasecmp(msg, " On") == 0) {
        msgBytes[0] = _empty; msgBytes[1] = _empty; msgBytes[2] = _O; msgBytes[3] = _n;
    } else if (strcasecmp(msg, "OFF") == 0 || strcasecmp(msg, " OFF") == 0) {
        msgBytes[0] = _empty; msgBytes[1] = _O; msgBytes[2] = _F; msgBytes[3] = _F;
    } else if (strcasecmp(msg, "WIFI") == 0) {
        msgBytes[0] = _empty; msgBytes[1] = _F; msgBytes[2] = _i; msgBytes[3] = _empty;
    } else if (strcasecmp(msg, "AP") == 0) {
        msgBytes[0] = _A; msgBytes[1] = _P; msgBytes[2] = _empty; msgBytes[3] = _empty;
    } else {
        for (int i = 0; i < 4 && msg[i] != '\0'; i++) {
            char c = msg[i];
            if (c >= '0' && c <= '9') msgBytes[i] = getDigitByte(c - '0');
            else if (c == '-') msgBytes[i] = _dash;
            else msgBytes[i] = _empty;
        }
    }
}

// ==============================================================================
// ВЫВОД ТЕМПЕРАТУРЫ НА ЧАСОВОЙ ДИСПЛЕЙ С ДВОЕТОЧИЕМ: [ 2 ][ 5 ] : [ 5 ][ ° ]
// ==============================================================================
static void renderTempColonFormat(float temp, bool showColon = true) {
    int totalTenths = (int)roundf(temp * 10.0f);
    if (totalTenths < 0 || totalTenths > 999) {
        tm.point(POINT_OFF);
        tm.displayByte(_dash, _dash, _dash, _dash);
        return;
    }

    int whole = totalTenths / 10;
    int tenths = totalTenths % 10;

    uint8_t d0 = (whole >= 10) ? getDigitByte(whole / 10) : _empty;
    uint8_t d1 = getDigitByte(whole % 10);
    uint8_t d2 = getDigitByte(tenths);
    uint8_t d3 = _degree; // Значок градуса (верхний кружочек 0x63)

    tm.point(showColon ? POINT_ON : POINT_OFF);
    tm.displayByte(d0, d1, d2, d3);
}

static void renderUi() {
    if (msgActive) {
        if (millis() < msgUntil) {
            tm.point(POINT_OFF);
            tm.displayByte(msgBytes[0], msgBytes[1], msgBytes[2], msgBytes[3]);
            return;
        } else {
            msgActive = false;
        }
    }

    unsigned long now = millis();

    switch (currentUiMode) {
        case UI_ROTATION: {
            unsigned long elapsed = now - screenStartTime;

            // 1. Первые 750 мс каждого экрана — показываем имя зоны
            if (elapsed < HEADER_DURATION_MS) {
                tm.point(POINT_OFF);
                if (rotationScreen == 0) {
                    tm.displayByte(_empty, _1, _empty, _empty);      // " 1  "
                } else if (rotationScreen == 1) {
                    tm.displayByte(_empty, _2, _empty, _empty);      // " 2  "
                } else {
                    tm.displayByte(_A, _i, _r, _empty);              // "Air "
                }
                return;
            }

            // 2. Оставшиеся 2000 мс — значение температуры с двоеточием [Ц1][Ц2]:[Ц3][°]
            if (rotationScreen == 0) {
                if (!heatingEnabled) {
                    tm.point(POINT_OFF);
                    tm.displayByte(_1, _O, _F, _F);                  // "1OFF"
                } else {
                    renderTempColonFormat(zone1.currentTemp, true);  // "25:5°"
                }
            } else if (rotationScreen == 1) {
                if (!heatingEnabled) {
                    tm.point(POINT_OFF);
                    tm.displayByte(_2, _O, _F, _F);                  // "2OFF"
                } else {
                    renderTempColonFormat(zone2.currentTemp, true);  // "24:0°"
                }
            } else {
                renderTempColonFormat(air.currentTemp, true);        // "21:5°"
            }
            break;
        }

        case UI_SET_FLOOR_T: {
            unsigned long elapsed = now - menuStepStartTime;
            if (!menuValueInteracted && elapsed < 700) {
                // Короткая подсказка " FL " при входе
                tm.point(POINT_OFF);
                tm.displayByte(_empty, _F, _L, _empty);
            } else {
                // Значение уставки пола: "26:0°"
                renderTempColonFormat(targetFloorTemp, true);
            }
            break;
        }

        case UI_SET_AIR_T: {
            unsigned long elapsed = now - menuStepStartTime;
            if (!menuValueInteracted && elapsed < 700) {
                // Короткая подсказка "Air " при входе
                tm.point(POINT_OFF);
                tm.displayByte(_A, _i, _r, _empty);
            } else {
                // Значение уставки воздуха: "22:0°"
                renderTempColonFormat(targetAirTemp, true);
            }
            break;
        }
    }
}

void local_ui_show_boot() {
    tm.brightness(3);
    tm.point(POINT_OFF);
    tm.displayByte(_b, _o, _o, _t);
}

void local_ui_init() {
    Serial.printf("[UI] TM1637 (CLK=%d, DIO=%d) в режиме [Ц1][Ц2]:[Ц3][°] (двоеточие = разделитель)\n",
                  PIN_TM_CLK, PIN_TM_DIO);

    pinMode(PIN_BTN_MODE, INPUT_PULLUP);
    pinMode(PIN_BTN_UP, INPUT_PULLUP);
    pinMode(PIN_BTN_DOWN, INPUT_PULLUP);

    btnMode.lastReading = digitalRead(PIN_BTN_MODE);
    btnMode.debouncedState = btnMode.lastReading;

    btnUp.lastReading = digitalRead(PIN_BTN_UP);
    btnUp.debouncedState = btnUp.lastReading;

    btnDown.lastReading = digitalRead(PIN_BTN_DOWN);
    btnDown.debouncedState = btnDown.lastReading;

    screenStartTime = millis();
}

void local_ui_tick() {
    updateButton(btnMode);
    updateButton(btnUp);
    updateButton(btnDown);

    unsigned long now = millis();

    // 1. Кнопка MODE — переключение между 2 уставками и выходом
    if (btnMode.clicked) {
        btnMode.clicked = false;
        lastInteractionTime = now;
        menuStepStartTime = now;
        menuValueInteracted = false;

        if (currentUiMode == UI_ROTATION) {
            currentUiMode = UI_SET_FLOOR_T; // Шаг 1: Уставка пола
        } else if (currentUiMode == UI_SET_FLOOR_T) {
            currentUiMode = UI_SET_AIR_T;   // Шаг 2: Уставка воздуха
        } else if (currentUiMode == UI_SET_AIR_T) {
            currentUiMode = UI_ROTATION;    // Выход и сохранение
            storage_request_save();
            local_ui_show_msg("SAVE", 1000);
            screenStartTime = now;
        }
    }

    if (btnMode.longPressed) {
        btnMode.longPressed = false;
        if (currentUiMode != UI_ROTATION) {
            currentUiMode = UI_ROTATION;
            storage_request_save();
            local_ui_show_msg("SAVE", 1000);
            screenStartTime = now;
        }
    }

    // 2. Кнопки UP / DOWN (регулировка уставок или перелистывание экранов)
    if (btnUp.clicked) {
        btnUp.clicked = false;
        lastInteractionTime = now;
        menuValueInteracted = true;

        if (currentUiMode == UI_SET_FLOOR_T) {
            targetFloorTemp += 0.5f;
            if (targetFloorTemp > sysCfg.max_floor_temp) targetFloorTemp = sysCfg.max_floor_temp;
            storage_request_save();
        } else if (currentUiMode == UI_SET_AIR_T) {
            targetAirTemp += 0.5f;
            if (targetAirTemp > 35.0f) targetAirTemp = 35.0f;
            storage_request_save();
        } else {
            // В авторотации ручное листание вперед
            rotationScreen = (rotationScreen + 1) % 3;
            screenStartTime = now;
        }
    }

    if (btnDown.clicked) {
        btnDown.clicked = false;
        lastInteractionTime = now;
        menuValueInteracted = true;

        if (currentUiMode == UI_SET_FLOOR_T) {
            targetFloorTemp -= 0.5f;
            if (targetFloorTemp < 10.0f) targetFloorTemp = 10.0f;
            storage_request_save();
        } else if (currentUiMode == UI_SET_AIR_T) {
            targetAirTemp -= 0.5f;
            if (targetAirTemp < 10.0f) targetAirTemp = 10.0f;
            storage_request_save();
        } else {
            // В авторотации ручное листание назад
            rotationScreen = (rotationScreen + 2) % 3;
            screenStartTime = now;
        }
    }

    // =========================================================================
    // 3. ПРАВИЛО КНОПОК ПИТАНИЯ ОТОПЛЕНИЯ:
    //    Длительное удержание UP   (> 0.8 сек) -> ВКЛЮЧИТЬ отопление
    //    Длительное удержание DOWN (> 0.8 сек) -> ВЫКЛЮЧИТЬ отопление
    // =========================================================================
    if (btnUp.longPressed) {
        btnUp.longPressed = false;
        heatingEnabled = true;
        zone1.enabled = true;
        zone2.enabled = true;
        storage_request_save();
        net_mqtt_publish_state();
        local_ui_show_msg(" On", 1200);
        Serial.println("[UI] >>> Отопление ВКЛЮЧЕНО (удержание UP) <<<");
    }

    if (btnDown.longPressed) {
        btnDown.longPressed = false;
        heatingEnabled = false;
        zone1.enabled = false;
        zone2.enabled = false;
        storage_request_save();
        net_mqtt_publish_state();
        local_ui_show_msg(" OFF", 1200);
        Serial.println("[UI] >>> Отопление ВЫКЛЮЧЕНО (удержание DOWN) <<<");
    }

    // 4. Таймаут бездействия в меню настроек (автосохранение)
    if (currentUiMode != UI_ROTATION && (now - lastInteractionTime >= MENU_TIMEOUT_MS)) {
        currentUiMode = UI_ROTATION;
        storage_request_save();
        local_ui_show_msg("SAVE", 1000);
        screenStartTime = now;
    }

    // 5. Авторотация экранов каждые 2.8 сек
    if (currentUiMode == UI_ROTATION && !msgActive) {
        if (now - screenStartTime >= TOTAL_SCREEN_MS) {
            screenStartTime = now;
            rotationScreen = (rotationScreen + 1) % 3;
        }
    }

    // 6. Отрисовка дисплея
    renderUi();
}
