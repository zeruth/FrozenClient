#include "console/Command.hpp"
#include "console/CommandHandlers.hpp"
#include "console/Console.hpp"
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <storm/Unicode.hpp>

// Name of the command most recently parsed by ConsoleCommandExecute
static char s_commandName[MAX_CMD_LENGTH];

// Splits command into the command name (first word, at most MAX_CMD_LENGTH - 1 bytes, honoring
// UTF-8 sequences) and the remaining arguments with surrounding spaces trimmed. Returns the
// registered command for the name, if any. (0x768900 in the original)
static CONSOLECOMMAND* ConsoleCommandParse(const char* command, const char** name, char* arguments, size_t argumentsSize) {
    char* out = s_commandName;
    int32_t remaining = MAX_CMD_LENGTH - 1;
    const char* cursor = command;

    while (remaining > 0) {
        int32_t length = 0;
        uint32_t code = SUniSGetUTF8(reinterpret_cast<const uint8_t*>(cursor), &length);

        if (code == 0xFFFFFFFF || code == ' ' || length > remaining) {
            break;
        }

        remaining -= length;

        while (length-- > 0) {
            *out++ = *cursor++;
        }
    }

    *out = '\0';

    if (name) {
        *name = s_commandName;
    }

    if (arguments) {
        int32_t length = 0;
        uint32_t code = SUniSGetUTF8(reinterpret_cast<const uint8_t*>(cursor), &length);

        while (code != 0xFFFFFFFF && code == ' ') {
            cursor += length;
            code = SUniSGetUTF8(reinterpret_cast<const uint8_t*>(cursor), &length);
        }

        SStrCopy(arguments, cursor, argumentsSize);

        size_t argumentsLength = SStrLen(arguments);

        while (argumentsLength > 0 && arguments[argumentsLength - 1] == ' ') {
            arguments[--argumentsLength] = '\0';
        }
    }

    return g_consoleCommandHash.Ptr(s_commandName);
}

// Records a command line in the history ring (0x76B3B0 in the original)
static void ConsoleCommandHistoryAdd(const char* command) {
    SStrCopy(g_commandHistory[g_commandHistoryIndex], command, CMD_BUFFER_SIZE);
    g_commandHistoryIndex = (g_commandHistoryIndex + 1) & (HISTORY_DEPTH - 1);
}

int32_t ValidateFileName(const char* filename) {
    if (SStrStr(filename, "..") || SStrStr(filename, "\\")) {
        // TODO
        // ConsoleWrite("File Name cannot contain '\\' or '..'", ERROR_COLOR);
        return 0;
    }

    const char* extension = SStrChrR(filename, '.');

    if (extension && SStrCmpI(extension, ".wtf", -1)) {
        // TODO
        // ConsoleWrite("Only .wtf extensions are allowed", ERROR_COLOR);
        return 0;
    }

    return 1;
}

TSHashTable<CONSOLECOMMAND, HASHKEY_STRI> g_consoleCommandHash;
char g_commandHistory[HISTORY_DEPTH][CMD_BUFFER_SIZE];
uint32_t g_commandHistoryIndex;

void ConsoleCommandDestroy() {
    g_consoleCommandHash.Clear();
}

// 0x7658A0 in the original
void ConsoleCommandExecute(const char* command, int32_t addToHistory) {
    // TODO the original first offers the line to the console's file capture mode ("run" command
    // recording), which can consume it

    while (*command == ' ') {
        ++command;
    }

    if (addToHistory) {
        const char* last = ConsoleCommandHistory(0);

        if (!*last || SStrCmp(command, last, STORM_MAX_STR) != 0) {
            ConsoleCommandHistoryAdd(command);
        }
    }

    auto arguments = static_cast<char*>(SMemAlloc(CMD_BUFFER_SIZE, __FILE__, __LINE__, 0));

    const char* name = nullptr;
    CONSOLECOMMAND* commandPtr = ConsoleCommandParse(command, &name, arguments, CMD_BUFFER_SIZE);

    if (!commandPtr) {
        // TODO the original consults an optional default handler installed by the script system
        // before falling back to "run"

        commandPtr = g_consoleCommandHash.Ptr("run");

        if (commandPtr) {
            // Unknown commands are handed to "run" as a script line
            name = "";
            SStrCopy(arguments, command, CMD_BUFFER_SIZE);
        } else {
            ConsoleWrite("Unknown command", DEFAULT_COLOR);
        }
    }

    if (commandPtr) {
        commandPtr->handler(name, arguments);
    }

    SMemFree(arguments, __FILE__, __LINE__, 0);
}

char* ConsoleCommandHistory(uint32_t index) {
    // Return a pointer to the buffer at the specified index
    return g_commandHistory[((g_commandHistoryIndex + (HISTORY_DEPTH - 1) - index) & (HISTORY_DEPTH - 1))];
}

uint32_t ConsoleCommandHistoryDepth() {
    return HISTORY_DEPTH;
}

void ConsoleCommandInitialize() {
    ConsoleCommandRegister("help", ConsoleCommand_Help, CONSOLE, "Provides help information about a command.");
}

int32_t ConsoleCommandRegister(const char* command, int32_t (*handler)(const char*, const char*), CATEGORY category, const char* helpText) {
    STORM_ASSERT(command);
    STORM_ASSERT(handler);

    if (SStrLen(command) > (MAX_CMD_LENGTH - 1) || g_consoleCommandHash.Ptr(command)) {
        // The command name exceeds MAX_CMD_LENGTH, minus the null terminator
        // or it has already been registered
        return 0;
    }

    // Register the new command
    auto commandPtr = g_consoleCommandHash.New(command, 0, 0);
    commandPtr->command = command;
    commandPtr->handler = handler;
    commandPtr->helpText = helpText;
    commandPtr->category = category;

    return 1;
}

void ConsoleCommandUnregister(const char* command) {
    if (command) {
        auto commandPtr = g_consoleCommandHash.Ptr(command);
        if (commandPtr) {
            g_consoleCommandHash.Delete(commandPtr);
        }
    }
}
