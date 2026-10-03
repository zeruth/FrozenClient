#ifndef COMMON_OS_HPP
#define COMMON_OS_HPP

const char* OsGetLastErrorStr();

void OsFreeLastErrorStr(const char* str);

// C linkage: the Lua core (C) calls this before every indirect call it makes.
extern "C" void OsValidateFunctionPointer(const void* function);

#endif
