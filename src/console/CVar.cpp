#include "console/CVar.hpp"
#include "console/Command.hpp"
#include "console/CVarHandlers.hpp"
#include "util/Filesystem.hpp"
#include "util/SFile.hpp"
#include <cstdio>
#include <cstring>
#include <storm/Memory.hpp>
#include <storm/String.hpp>

// Files up to this size are parsed from a stack buffer
#define CVAR_LOAD_STACK_BUFFER_SIZE 0x2000

bool CVar::m_initialized;
bool CVar::m_needsSave;
const char* CVar::s_filename = nullptr;
TSHashTable<CVar, HASHKEY_STRI> CVar::s_registeredCVars;

// Creates every directory along path (0x766320 in the original)
static int32_t s_CreatePathDirectories(const char* path) {
    if (!path || !*path) {
        return 0;
    }

    for (const char* separator = SStrChr(path + 1, '\\'); separator; separator = SStrChr(separator + 1, '\\')) {
        size_t length = separator - path;

        if (length >= STORM_MAX_PATH) {
            return 0;
        }

        char directory[STORM_MAX_PATH];
        memcpy(directory, path, length);
        directory[length] = '\0';

        if (!OsDirectoryExists(directory)) {
            OsCreateDirectory(directory, 0);

            if (!OsDirectoryExists(directory)) {
                return 0;
            }
        }
    }

    return 1;
}

// Feeds every "SET" line of an open config file through the console (0x766400 in the original)
static int32_t s_LoadFromFile(FILE* file) {
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (fileSize <= 0) {
        return 1;
    }

    auto size = static_cast<size_t>(fileSize);

    char stackBuffer[CVAR_LOAD_STACK_BUFFER_SIZE];
    char* buffer = stackBuffer;
    bool heapBuffer = size >= CVAR_LOAD_STACK_BUFFER_SIZE;

    if (heapBuffer) {
        buffer = static_cast<char*>(SMemAlloc(size + 1, __FILE__, __LINE__, 0));
    }

    size_t bytesRead = fread(buffer, 1, size, file);
    int32_t result = 0;

    if (bytesRead) {
        buffer[bytesRead] = '\0';

        const char* cursor = buffer;

        // Skip a UTF-8 byte order mark
        if (bytesRead > 2 && static_cast<uint8_t>(buffer[0]) == 0xEF && static_cast<uint8_t>(buffer[1]) == 0xBB && static_cast<uint8_t>(buffer[2]) == 0xBF) {
            cursor = buffer + 3;
        }

        char line[2048];

        do {
            SStrTokenize(&cursor, line, sizeof(line), "\r\n", nullptr);

            if (SStrCmpI(line, "SET ", 4) == 0) {
                ConsoleCommandExecute(line, 0);
            }
        } while (cursor && *cursor);

        result = 1;
    }

    if (heapBuffer) {
        SMemFree(buffer, __FILE__, __LINE__, 0);
    }

    return result;
}

void CVar::Initialize() {
    CVar::m_initialized = true;

    char basePath[STORM_MAX_PATH];
    SFile::GetBasePath(basePath, sizeof(basePath));
    SStrPrintf(basePath, sizeof(basePath), "%s%s\\", basePath, "WTF");
    s_CreatePathDirectories(basePath);

    ConsoleCommandRegister("set", CVarSetCommandHandler, DEFAULT, "Set the value of a CVar");
    ConsoleCommandRegister("cvar_reset", CVarResetCommandHandler, DEFAULT, "Set the value of a CVar to it's startup value");
    ConsoleCommandRegister("cvar_default", CVarDefaultCommandHandler, DEFAULT, "Set the value of a CVar to it's coded default value");
    ConsoleCommandRegister("cvarlist", CVarListCommandHandler, DEFAULT, "List cvars");
}

// Loads filename as given, then from the WTF directory (0x766530 in the original)
int32_t CVar::Load(const char* filename) {
    char path[STORM_MAX_PATH];
    SStrCopy(path, filename, sizeof(path));

    for (char* c = path; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    FILE* file = fopen(path, "rb");

    if (!file) {
        SStrPrintf(path, sizeof(path), "WTF/%s", filename);

        for (char* c = path; *c; ++c) {
            if (*c == '\\') {
                *c = '/';
            }
        }

        file = fopen(path, "rb");

        if (!file) {
            return 0;
        }
    }

    int32_t result = s_LoadFromFile(file);

    fclose(file);

    return result;
}

// Writes every saveable cvar whose category bits match as a SET line (0x767030 with the
// 0x766640 writer in the original). Cvars with the 0x80 flag, cvars still at their default value,
// and cvars matching excludeFlags are skipped.
static int32_t s_WriteCVars(uint32_t categoryFlags, uint32_t excludeFlags, FILE* file) {
    for (auto var = CVar::s_registeredCVars.Head(); var; var = CVar::s_registeredCVars.Next(var)) {
        if (!(var->m_flags & 0x1) || (var->m_flags & 0x80)) {
            continue;
        }

        if ((var->m_flags & 0x30) != categoryFlags || (var->m_flags & excludeFlags)) {
            continue;
        }

        const char* value = var->m_latchedValue.GetString();

        if (!value) {
            value = var->m_stringValue.GetString();
        }

        if (!value) {
            value = var->m_defaultValue.GetString();
        }

        if (!value) {
            continue;
        }

        const char* defaultValue = var->m_defaultValue.GetString();

        if (defaultValue && !SStrCmpI(value, defaultValue, STORM_MAX_STR)) {
            continue;
        }

        if (fprintf(file, "SET %s \"%s\"\n", var->m_key.GetString(), value) < 0) {
            return 0;
        }
    }

    return 1;
}

// Deletes filename, looking in the WTF directory when it isn't found as given (0x7665D0 in the
// original)
int32_t CVar::RemoveFile(const char* filename) {
    char path[STORM_MAX_PATH];
    SStrCopy(path, filename, sizeof(path));

    if (!OsFileExists(path)) {
        SStrPrintf(path, sizeof(path), "WTF\\%s", filename);

        if (!OsFileExists(path)) {
            return 0;
        }
    }

    for (char* c = path; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    remove(path);

    return 1;
}

// Writes the config file when a saved cvar changed (0x767100 in the original)
int32_t CVar::Save() {
    if (!CVar::m_needsSave) {
        return 1;
    }

    CVar::m_needsSave = 0;

    if (!CVar::s_filename) {
        return 0;
    }

    char path[STORM_MAX_PATH];
    SStrPrintf(path, sizeof(path), "WTF/%s", CVar::s_filename);

    for (char* c = path; *c; ++c) {
        if (*c == '\\') {
            *c = '/';
        }
    }

    FILE* file = fopen(path, "wb");

    if (!file) {
        return 0;
    }

    int32_t result = s_WriteCVars(0, 0, file);

    fclose(file);

    return result;
}

CVar* CVar::Lookup(const char* name) {
    return name
        ? CVar::s_registeredCVars.Ptr(name)
        : nullptr;
}

CVar* CVar::LookupRegistered(const char* name) {
    auto var = CVar::Lookup(name);
    if (!var) {
        return nullptr;
    }

    if (!(var->m_flags & 0x80000000) && !(var->m_flags & 0x80)) {
        return nullptr;
    }

    return var;
}

CVar* CVar::Register(const char* name, const char* help, uint32_t flags, const char* value, bool (*fcn)(CVar*, const char*, const char*, void*), uint32_t category, bool a7, void* arg, bool a9) {
    CVar* var = CVar::s_registeredCVars.Ptr(name);

    if (var) {
        bool setReset = var->m_resetValue.GetString() == nullptr;
        bool setDefault = var->m_defaultValue.GetString() == nullptr;

        // A cvar that Config.wtf created ahead of its registration keeps its flags (the archive
        // bit above all) apart from the category bits, which the registration supplies
        var->m_flags = flags | (var->m_flags & 0xFFFFFFCF);

        var->m_callback = fcn;
        var->m_arg = arg;

        bool setValue = false;
        if (fcn && !fcn(var, var->GetString(), var->GetString(), arg)) {
            setValue = true;
        }

        var->Set(value, setValue, setReset, setDefault, false);

        if (!a7) {
            var->m_flags |= 0x80000000;
        }

        if (a9 && !(var->m_flags & 0x80000000)) {
            var->m_flags |= 0x80;
        }
    } else {
        var = CVar::s_registeredCVars.New(name, 0, 0);

        var->m_stringValue.Copy(nullptr);
        var->m_floatValue = 0.0f;
        var->m_intValue = 0;
        var->m_modified = 0;
        var->m_category = category;
        var->m_defaultValue.Copy(nullptr);
        var->m_resetValue.Copy(nullptr);
        var->m_latchedValue.Copy(nullptr);
        var->m_callback = fcn;
        var->m_flags = 0;
        var->m_arg = arg;
        var->m_help.Copy(help);

        if (a7) {
            var->Set(value, true, true, false, false);
        } else {
            var->Set(value, true, false, true, false);
        }

        var->m_flags = flags;

        var->m_flags |= 0x1;

        if (!a7) {
            var->m_flags |= 0x80000000;
        }

        if (a9 && !(var->m_flags & 0x80000000)) {
            var->m_flags |= 0x80;
        }

        // TODO
        // ConsoleCommandRegister(var->m_key.GetString(), &CvarCommandHandler, category, help);
    }

    return var;
}

CVar::CVar() : TSHashObject<CVar, HASHKEY_STRI>() {
    // TODO
}

const char* CVar::GetDefaultValue() {
    return this->m_defaultValue.GetString();
}

float CVar::GetFloat() {
    return this->m_floatValue;
}

int32_t CVar::GetInt() {
    return this->m_intValue;
}

const char* CVar::GetString() {
    return this->m_stringValue.GetString();
}

void CVar::InternalSet(const char* value, bool setValue, bool setReset, bool setDefault, bool a6) {
    if (this->m_flags & 0x4 || !value) {
        return;
    }

    bool modified = false;

    const char* existingValue = this->m_stringValue.GetString();

    if (setValue && (!existingValue || SStrCmpI(value, existingValue, 0x7FFFFFFF))) {
        modified = true;

        this->m_stringValue.Copy(value);
        this->m_intValue = SStrToInt(value);
        this->m_floatValue = SStrToFloat(value);
    }

    if (setReset && !this->m_resetValue.GetString()) {
        modified = true;

        this->m_resetValue.Copy(value);
    }

    if (setDefault && !this->m_defaultValue.GetString()) {
        this->m_defaultValue.Copy(value);
    } else if (!modified) {
        return;
    }

    if (a6) {
        CVar::m_needsSave = 1;
    }
}

bool CVar::Set(const char* value, bool setValue, bool setReset, bool setDefault, bool a6) {
    if (setValue) {
        if (this->m_callback) {
            // TODO
            // sub_86B5A0(this->m_callback);

            if (!this->m_callback(this, this->m_stringValue.GetString(), value, this->m_arg)) {
                return true;
            }
        }

        this->m_modified++;

        if (this->m_flags & 0x2) {
            this->m_latchedValue.Copy(value);
            CVar::m_needsSave = 1;

            return true;
        }
    }

    this->InternalSet(value, setValue, setReset, setDefault, a6);

    return true;
}

int32_t CVar::Update() {
    if (!(this->m_flags & 0x2)) {
        return 0;
    }

    if (!this->m_latchedValue.GetString()) {
        return 0;
    }

    this->InternalSet(this->m_latchedValue.GetString(), true, false, false, true);
    this->m_latchedValue.Copy(nullptr);

    return 1;
}
