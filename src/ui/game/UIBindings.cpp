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
    // it belongs to that group until the next one says otherwise. Resolving it here means nothing
    // downstream has to remember the previous row.
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

        auto groupName = node->GetAttributeByName("header");

        if (groupName && *groupName) {
            header = groupName;
        }

        UIBindingCommand command;
        command.name = commandName;
        command.header = header;

        auto body = node->GetBody();
        command.script = body ? body : "";

        auto runOnUp = node->GetAttributeByName("runOnUp");
        command.runOnUp = runOnUp && StringToBOOL(runOnUp);

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
