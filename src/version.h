#pragma once

// Название проекта (без пробелов, английскими буквами)
#define PROJECT_NAME     "WarmFloor_Stas"

// Текущая версия прошивки
#define FIRMWARE_VERSION "1.0.24"

// Дата и время компиляции (подтягиваются компилятором автоматически)
#define COMPILE_DATE     __DATE__
#define COMPILE_TIME     __TIME__

// Макрос для удобного вывода полной строки версии
#define GET_VERSION_STRING() (String(PROJECT_NAME) + " v" + FIRMWARE_VERSION + " (" + COMPILE_DATE + " " + COMPILE_TIME + ")")
