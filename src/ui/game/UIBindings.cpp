#include "ui/game/UIBindings.hpp"
#include "ui/FrameXML.hpp"
#include "util/Log.hpp"
#include "util/CStatus.hpp"
#include "ui/Util.hpp"

#include <common/MD5.hpp>
#include <common/xml/XMLNode.hpp>
#include <common/xml/XMLTree.hpp>
#include <storm/String.hpp>
#include <vector>

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
