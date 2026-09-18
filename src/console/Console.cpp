#include "console/Console.hpp"
#include <cstdarg>
#include <storm/String.hpp>

static int32_t s_active;
static int32_t s_consoleAccessEnabled;
static KEY s_consoleKey = KEY_TILDE;
static CONSOLERESIZESTATE s_consoleResizeState = CS_NONE;

int32_t ConsoleAccessGetEnabled() {
    return s_consoleAccessEnabled;
}

void ConsoleAccessSetEnabled(int32_t enable) {
    s_consoleAccessEnabled = enable;
}

int32_t ConsoleGetActive() {
    return s_active;
}

KEY ConsoleGetHotKey() {
    return s_consoleKey;
}

CONSOLERESIZESTATE ConsoleGetResizeState() {
    return s_consoleResizeState;
}

void ConsoleSetActive(int32_t active) {
    s_active = active;
}

void ConsoleSetHotKey(KEY hotkey) {
    s_consoleKey = hotkey;
}

void ConsoleSetResizeState(CONSOLERESIZESTATE state) {
    s_consoleResizeState = state;
}

void ConsoleWrite(const char* text, COLOR_T color) {
    // TODO
}

// ref: FUN_00765360
void ConsolePrintf(const char* format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);
    SStrVPrintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    ConsoleWrite(buffer, DEFAULT_COLOR);
}
