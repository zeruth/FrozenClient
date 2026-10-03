#ifndef CONSOLE_DEVICE_HPP
#define CONSOLE_DEVICE_HPP

#include "gx/CGxFormat.hpp"

#include "console/Detect.hpp"

const DefaultSettings* ConsoleDeviceGetDefaults();

int32_t ConsoleDeviceHardwareChanged();

void ConsoleDeviceSetDefaults(int32_t type);

void AddConsoleDeviceDefaultCallback(void (*callback)(int32_t type));

void RemoveConsoleDeviceDefaultCallback(void (*callback)(int32_t type));

void ConsoleDeviceInitialize(const char* title);

int32_t ConsoleDeviceExists();

#endif
