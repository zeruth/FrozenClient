#include "console/CVarHandlers.hpp"
#include "console/CVar.hpp"
#include <storm/String.hpp>

int32_t CVarDefaultCommandHandler(const char* command, const char* arguments) {
    // TODO

    return 0;
}

int32_t CVarListCommandHandler(const char* command, const char* arguments) {
    // TODO

    return 0;
}

// 0x7681F0 in the original
int32_t CVarSetCommandHandler(const char* command, const char* arguments) {
    char name[64];
    char value[2048];

    SStrTokenize(&arguments, name, sizeof(name), " ,;\t\"\r\n", nullptr);
    SStrTokenize(&arguments, value, sizeof(value), " ,;\t\"\r\n", nullptr);

    CVar* var = CVar::Lookup(name);

    if (!var) {
        CVar::Register(name, "", 0x0, value, nullptr, DEFAULT, true, nullptr, false);
        return 1;
    }

    var->Set(value, true, false, false, true);

    return 1;
}

int32_t CVarResetCommandHandler(const char* command, const char* arguments) {
    // TODO

    return 0;
}
