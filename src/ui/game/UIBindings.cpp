#include "ui/game/UIBindings.hpp"
#include "util/SFile.hpp"
#include <cstring>
#include "ui/FrameXML.hpp"
#include "util/Log.hpp"
#include "util/CStatus.hpp"
#include "ui/Util.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"

#include <common/MD5.hpp>
#include <common/xml/XMLNode.hpp>
#include <common/xml/XMLTree.hpp>
#include <storm/String.hpp>
#include <vector>
#include "ui/simple/CSimpleTop.hpp"

namespace {

std::vector<UIBindingCommand> s_commands;
bool s_loaded = false;

// Lookup during the load itself, before EnsureLoaded has finished -- the public UIBindingsFind
// would recurse into it. The reference guards every registration with the same test.
const UIBindingCommand* UIBindingsFindLoaded(const char* name) {
    for (auto& command : s_commands) {
        if (!SStrCmpI(command.name.c_str(), name, 0x7FFFFFFF)) {
            return &command;
        }
    }

    return nullptr;
}

// DIVERGENCE: the reference parses Bindings.xml during UI initialisation, alongside the rest of
// FrameXML. This loads on the first query instead. For a table that is read-only and has no
// dependency on anything else being up, the two are indistinguishable from the outside, and it
// keeps the port from having to take a position on startup ordering it cannot yet verify.
void EnsureLoaded() {
    if (s_loaded) {
        return;
    }

    s_loaded = true;

    MD5_CTX md5;
    MD5Init(&md5);

    CStatus status;
    XMLTree* tree = FrameXML_LoadXML("Interface\\FrameXML\\Bindings.xml", &md5, &status);

    if (!tree) {
        return;
    }

    auto root = XMLTree_GetRoot(tree);

    // The header attribute appears once, on the first command of a group, and every command after
    // it belongs to that group until the next one says otherwise.
    std::string header;

    for (auto node = root ? root->GetChild() : nullptr; node; node = node->GetSibling()) {
        auto name = node->GetName();

        // Bindings.xml also carries ModifiedClick rows, which are not key bindings.
        if (!name || SStrCmpI(name, "Binding", 0x7FFFFFFF)) {
            continue;
        }

        auto commandName = node->GetAttributeByName("name");

        if (!commandName || !*commandName) {
            continue;
        }

        // Rows the reference drops on the floor before registering anything. A debug binding is
        // only for a debug build, and a row that names a platform is only for that platform --
        // the reference compares against "windows" literally.
        auto debug = node->GetAttributeByName("debug");

        if (debug && StringToBOOL(debug)) {
            continue;
        }

        auto platform = node->GetAttributeByName("platform");

        if (platform && *platform && SStrCmpI(platform, "windows", 0x7FFFFFFF)) {
            continue;
        }

        if (UIBindingsFindLoaded(commandName)) {
            continue;
        }

        auto groupName = node->GetAttributeByName("header");

        if (groupName && *groupName) {
            header = groupName;

            // A header is registered as a command in its own right, named HEADER_<name>, and it
            // takes an index in the list like any other. That is not a quirk to work around: it
            // is how the key binding pane draws its section titles, by walking the same indices
            // and finding a HEADER_ row where a heading belongs. Dropping them would renumber
            // every command after the first group.
            UIBindingCommand headerRow;
            headerRow.name = "HEADER_" + header;
            headerRow.header = header;
            headerRow.isHeader = true;

            if (!UIBindingsFindLoaded(headerRow.name.c_str())) {
                s_commands.push_back(headerRow);
            }
        }

        UIBindingCommand command;
        command.name = commandName;
        command.header = header;

        auto body = node->GetBody();
        command.script = body ? body : "";

        // Compiled once, here, exactly as the reference does it -- same wrapper, same four
        // parameter names. A command with no body keeps -1 and runs nothing.
        if (!command.script.empty()) {
            command.function = FrameScript_CompileFunction(
                command.name.c_str(),
                "return function(keystate, pressure, angle, precision) %s end",
                command.script.c_str(),
                &status
            );
        }

        auto runOnUp = node->GetAttributeByName("runOnUp");
        command.runOnUp = runOnUp && StringToBOOL(runOnUp);

        // A hidden row, or a joystick row on a machine with no joystick, is still registered --
        // SetBinding can still name it -- but the reference gives it a negative index so it never
        // appears in the numbered walk the pane does.
        auto hidden = node->GetAttributeByName("hidden");
        command.hidden = hidden && StringToBOOL(hidden);

        // The default key, which is the ONLY place a default binding can come from: the parser
        // reads this attribute and writes it straight into binding set 0. See
        // docs/ref/parity-bindings.md -- the shipped 3.3.5a file uses it on no Binding row at all,
        // so this is implemented and inert against that data rather than left out.
        auto defaultKey = node->GetAttributeByName("default");

        if (defaultKey && *defaultKey) {
            command.keys[0] = defaultKey;
        }

        s_commands.push_back(command);
    }

    XMLTree_Free(tree);

    SysMsgPrintf(SYSMSG_INFO, "Bindings: %d commands from Bindings.xml",
                 static_cast<int32_t>(s_commands.size()));

    // The reference loads the default keys straight after the UI's XML (FUN_005643b0 at
    // 0x0052adec); here that is straight after the commands they name.
    UIBindingsLoadDefaults();
}

} // namespace

int32_t UIBindingsGetCount() {
    EnsureLoaded();

    return static_cast<int32_t>(s_commands.size());
}

const UIBindingCommand* UIBindingsGetByIndex(int32_t index) {
    EnsureLoaded();

    if (index < 0 || index >= static_cast<int32_t>(s_commands.size())) {
        return nullptr;
    }

    return &s_commands[index];
}

const UIBindingCommand* UIBindingsFind(const char* name) {
    EnsureLoaded();

    if (!name || !*name) {
        return nullptr;
    }

    for (auto& command : s_commands) {
        if (!SStrCmpI(command.name.c_str(), name, 0x7FFFFFFF)) {
            return &command;
        }
    }

    return nullptr;
}

const char* UIBindingsGetKey(const char* command, int32_t n) {
    auto found = UIBindingsFind(command);

    if (!found || n < 0 || n >= 2) {
        return nullptr;
    }

    return found->keys[n].empty() ? nullptr : found->keys[n].c_str();
}

const char* UIBindingsGetCommandForKey(const char* key) {
    EnsureLoaded();

    if (!key || !*key) {
        return nullptr;
    }

    for (auto& command : s_commands) {
        for (auto& bound : command.keys) {
            if (!bound.empty() && !SStrCmpI(bound.c_str(), key, 0x7FFFFFFF)) {
                return command.name.c_str();
            }
        }
    }

    return nullptr;
}

bool UIBindingsSetKey(const char* key, const char* command) {
    EnsureLoaded();

    if (!key || !*key) {
        return false;
    }

    // A key maps to at most one command, so binding it somewhere new takes it off wherever it was.
    // That happens even when the new command turns out not to exist, which is what makes
    // SetBinding("KEY") with no command an unbind.
    for (auto& existing : s_commands) {
        for (auto& bound : existing.keys) {
            if (!bound.empty() && !SStrCmpI(bound.c_str(), key, 0x7FFFFFFF)) {
                bound.clear();
            }
        }
    }

    if (!command || !*command) {
        return true;
    }

    for (auto& target : s_commands) {
        if (SStrCmpI(target.name.c_str(), command, 0x7FFFFFFF)) {
            continue;
        }

        // Two keys may name one command. A third displaces the first, which is the reference's
        // behaviour and the reason the pane only ever offers two slots.
        if (target.keys[0].empty()) {
            target.keys[0] = key;
        } else if (target.keys[1].empty()) {
            target.keys[1] = key;
        } else {
            target.keys[0] = key;
        }

        return true;
    }

    return false;
}

// ref: FUN_0055f860
//
// The reference passes four arguments: the key state as a string, then pressure, angle and
// precision. Only the first means anything without joystick input, and the other three go in as
// zero, which is what the reference passes for a keyboard event too.
bool UIBindingsRunCommand(const char* command, bool keyDown) {
    auto found = UIBindingsFind(command);

    if (!found || found->function == -1) {
        return false;
    }

    // A release is swallowed unless the command asked for it. This is the whole reason runOnUp
    // exists: without the test, every movement binding would fire twice per press.
    if (!keyDown && !found->runOnUp) {
        return false;
    }

    auto L = FrameScript_GetContext();

    if (!L) {
        return false;
    }

    lua_pushstring(L, keyDown ? "down" : "up");
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    FrameScript_Execute(found->function, nullptr, 4, nullptr, nullptr);

    return true;
}

// ref: FUN_0055dd10
//
// The binding name for a key code. The codes are the reference's own KEY enum and frozen's matches
// it value for value, so this is a direct transcription rather than a mapping.
//
// Printable characters name themselves; the reference builds a one-character string for them.
const char* UIBindingsKeyName(int32_t key, char* buffer, size_t bufferBytes) {
    // 0x21..0xFF is the printable range, which names itself.
    if (static_cast<uint32_t>(key) - 0x21 < 0xDF) {
        SStrPrintf(buffer, bufferBytes, "%c", static_cast<char>(key));

        return buffer;
    }

    if (key >= KEY_NUMPAD0 && key <= KEY_NUMPAD0 + 9) {
        SStrPrintf(buffer, bufferBytes, "NUMPAD%d", key - KEY_NUMPAD0);

        return buffer;
    }

    // Two F-key runs, both numbered off 0x2FF: F1..F12 at 0x300 and F14..F19 at 0x30D. F13 is the
    // odd one out at 0x212, where the reference calls it PRINTSCREEN.
    if ((key >= 0x300 && key <= 0x30B) || (key > 0x30C && key < 0x313)) {
        SStrPrintf(buffer, bufferBytes, "F%d", key - 0x2FF);

        return buffer;
    }

    switch (key) {
    case KEY_LSHIFT:        return "LSHIFT";
    case KEY_RSHIFT:        return "RSHIFT";
    case 0x2:               return "LCTRL";
    case 0x3:               return "RCTRL";
    case 0x4:               return "LALT";
    case 0x5:               return "RALT";
    case -1:                return "NONE";
    case KEY_SPACE:         return "SPACE";
    case 0x10A:             return "NUMPADPLUS";
    case 0x10B:             return "NUMPADMINUS";
    case 0x10C:             return "NUMPADMULTIPLY";
    case 0x10D:             return "NUMPADDIVIDE";
    case 0x10E:             return "NUMPADDECIMAL";
    case KEY_ESCAPE:        return "ESCAPE";
    case KEY_ENTER:         return "ENTER";
    case 0x202:             return "BACKSPACE";
    case KEY_TAB:           return "TAB";
    case 0x204:             return "LEFT";
    case 0x205:             return "UP";
    case 0x206:             return "RIGHT";
    case 0x207:             return "DOWN";
    case 0x208:             return "INSERT";
    case 0x209:             return "DELETE";
    case 0x20A:             return "HOME";
    case 0x20B:             return "END";
    case 0x20C:             return "PAGEUP";
    case 0x20D:             return "PAGEDOWN";
    case 0x20E:             return "CAPSLOCK";
    case KEY_NUMLOCK:       return "NUMLOCK";
    case KEY_PRINTSCREEN:   return "PRINTSCREEN";
    case 0x30C:             return "NUMPADEQUALS";
    default:                return "UNKNOWN";
    }
}

// ref: FUN_0055d990
//
// The modifier prefixes, in the reference's order: alt, then control, then shift.
//
// Each group has a sided and an unsided form, and the UNSIDED name is used only when BOTH sides of
// that modifier are set in the mask -- one side alone gives "LALT-" or "RALT-". So a caller that
// does not care which side was pressed sets both bits.
void UIBindingsModifierPrefix(uint32_t modifiers, char* buffer, size_t bufferBytes) {
    struct Group {
        uint32_t left;
        uint32_t right;
        const char* both;
        const char* leftOnly;
        const char* rightOnly;
    };

    static const Group GROUPS[] = {
        { 0x10, 0x20, "ALT-",   "LALT-",   "RALT-"   },
        { 0x04, 0x08, "CTRL-",  "LCTRL-",  "RCTRL-"  },
        { 0x01, 0x02, "SHIFT-", "LSHIFT-", "RSHIFT-" },
    };

    buffer[0] = '\0';

    for (auto& group : GROUPS) {
        auto mask = modifiers & (group.left | group.right);

        const char* prefix = nullptr;

        if (mask == (group.left | group.right)) {
            prefix = group.both;
        } else if (mask == group.left) {
            prefix = group.leftOnly;
        } else if (mask == group.right) {
            prefix = group.rightOnly;
        }

        if (prefix) {
            SStrPack(buffer, prefix, bufferBytes);
        }
    }
}

// ref: FUN_005641c0
// A bindings text: "BINDINGMODE n" picks the set, "bind KEY COMMAND" binds, "modifiedclick ACTION
// BINDING" sets a modified click. Frozen keeps one binding set, so every mode binds into it.
void UIBindingsLoadText(int32_t set, const char* text) {
    (void)set;

    while (text && *text) {
        char line[1024];
        SStrTokenize(&text, line, sizeof(line), "\r\n", nullptr);

        const char* cursor = line;

        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }

        if (!SStrCmpI(cursor, "BINDINGMODE ", 12)) {
            continue;
        }

        if (!SStrCmpI(cursor, "bind ", 5)) {
            const char* rest = cursor + 5;
            char key[32];
            SStrTokenize(&rest, key, sizeof(key), " ", nullptr);

            if (rest && *key) {
                UIBindingsSetKey(key, rest);
            }

            continue;
        }

        // "modifiedclick": CGUIBindings owns the modified clicks, which come from Bindings.xml's
        // own rows; a defaults file that overrides one is not handled yet.
    }
}

// ref: FUN_005643b0
// PARTIAL: a joystick's own default bindings (FUN_005f9890's XML, "DefaultBindings") and the
// account-data handlers for the saved sets (FUN_006b9050, types 2 and 3) are the joystick and
// account-data ports'.
void UIBindingsLoadDefaults() {
    void* data = nullptr;

    if (!SFile::Load(nullptr, "WTF\\DefaultBindings.wtf", &data, nullptr, 1, 0x1, nullptr) || !data) {
        return;
    }

    UIBindingsLoadText(0, static_cast<const char*>(data));
    SFile::Unload(data);
}

// ref: FUN_005622e0
// The command bound to a key with modifiers, trying the most specific prefix combination first:
// with "ALT-CTRL-X" bound only as "X", the X binding still answers.
const char* UIBindingsFindForKey(const char* key) {
    char prefixes[3][32];
    uint32_t count = 0;

    while (count < 3) {
        auto dash = SStrChr(key, '-');

        if (!dash || dash == key) {
            break;
        }

        size_t length = static_cast<size_t>(dash - key);

        if (length > 31) {
            length = 31;
        }

        memcpy(prefixes[count], key, length);
        prefixes[count][length] = '\0';
        key = dash + 1;
        count++;
    }

    // Per prefix count, the number of tries and the prefix mask of each, most specific first
    // (the table the reference builds on its stack).
    static const uint32_t s_tries[4] = { 1, 2, 4, 8 };
    static const uint32_t s_masks[4][8] = {
        { 0 },
        { 1, 0 },
        { 3, 2, 1, 0 },
        { 7, 6, 5, 3, 4, 2, 1, 0 },
    };

    for (uint32_t t = 0; t < s_tries[count]; t++) {
        char name[128] = "";

        for (uint32_t p = 0; p < count; p++) {
            if (s_masks[count][t] & (1u << p)) {
                SStrPack(name, prefixes[p], sizeof(name));
                SStrPack(name, "-", sizeof(name));
            }
        }

        SStrPack(name, key, sizeof(name));

        if (auto command = UIBindingsGetCommandForKey(name)) {
            return command;
        }
    }

    return nullptr;
}

// ref: FUN_00563150
// PARTIAL: the SPELL, ITEM, MACRO and CLICK override forms (SetBindingSpell and friends) are the
// action-bar, item, macro and secure-frame ports'; a plain command runs.
int32_t UIBindingsDispatchKey(const char* key, int32_t down) {
    auto command = UIBindingsFindForKey(key);

    if (!command) {
        return 0;
    }

    // a key binding is a hardware event for as long as it runs
    auto top = CSimpleTop::s_instance;
    top->m_hardwareEvent = 1;

    int32_t handled;

    if (!SStrCmpI(command, "SPELL ", 6) || !SStrCmpI(command, "ITEM ", 5)
        || !SStrCmpI(command, "MACRO ", 6) || !SStrCmpI(command, "CLICK ", 6)) {
        handled = 1;
    } else {
        handled = UIBindingsRunCommand(command, down != 0) ? 1 : 0;
    }

    top->m_hardwareEvent = 0;

    return handled;
}
