#include "console/CVarHandlers.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include <storm/String.hpp>

// ref: FUN_00767680
// Every registered CVar is also a console command: "farclip" prints its value (and the default,
// reset and pending values where they differ), "farclip 500" sets it -- through the callback,
// honouring read-only (0x100), latched (0x2, takes effect after a restart) and locked (0x4) flags.
int32_t CVarCommandHandler(const char* command, const char* arguments) {
    CVar* var = command ? CVar::Lookup(command) : nullptr;
    char buf[1024];

    while (*arguments == ' ') {
        arguments++;
    }

    if (!*arguments) {
        const char* value = var->m_stringValue.GetString();

        if (!value) {
            return 1;
        }

        SStrPrintf(buf, sizeof(buf), "CVar \"%s\" is \"%s\"", command, value);
        ConsoleWrite(buf, DEFAULT_COLOR);

        const char* defaultValue = var->m_defaultValue.GetString();

        if (defaultValue && SStrCmp(value, defaultValue, 0x7FFFFFFF)) {
            SStrPrintf(buf, sizeof(buf), "  default value \"%s\"", defaultValue);
            ConsoleWrite(buf, DEFAULT_COLOR);
        }

        const char* resetValue = var->m_resetValue.GetString();

        if (resetValue && SStrCmp(value, resetValue, 0x7FFFFFFF)) {
            SStrPrintf(buf, sizeof(buf), "  reset value \"%s\"", resetValue);
            ConsoleWrite(buf, DEFAULT_COLOR);
        }

        const char* latchedValue = var->m_latchedValue.GetString();

        if (latchedValue && SStrCmp(value, latchedValue, 0x7FFFFFFF)) {
            SStrPrintf(buf, sizeof(buf), "  pending value \"%s\"", latchedValue);
            ConsoleWrite(buf, DEFAULT_COLOR);
        }

        return 1;
    }

    if (var->m_flags & 0x100) {
        SStrPrintf(buf, sizeof(buf), "%s is read only.", var->m_key.GetString());
        ConsoleWrite(buf, DEFAULT_COLOR);

        return 1;
    }

    // The reference guards the pointer (FUN_0086b5a0, "Invalid function pointer") before calling
    if (var->m_callback && !var->m_callback(var, var->m_stringValue.GetString(), arguments, var->m_arg)) {
        return 1;
    }

    var->m_modified++;

    if (var->m_flags & 0x2) {
        var->m_latchedValue.Copy(arguments);
        CVar::m_needsSave = 1;

        return 1;
    }

    if (var->m_flags & 0x4) {
        return 1;
    }

    const char* current = var->m_stringValue.GetString();
    bool changed = !current || SStrCmpI(arguments, current, 0x7FFFFFFF) != 0;

    if (changed) {
        var->m_stringValue.Copy(arguments);
        var->m_intValue = SStrToInt(arguments);
        var->m_floatValue = SStrToFloat(arguments);
    }

    if (var->m_resetValue.GetString()) {
        if (changed) {
            CVar::m_needsSave = 1;
        }

        return 1;
    }

    // No reset value yet: the first console set becomes it
    var->m_resetValue.Copy(arguments);
    CVar::m_needsSave = 1;

    return 1;
}

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
