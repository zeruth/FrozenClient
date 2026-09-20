#include "util/Lua.hpp"
#include "ui/game/UIBindingsScript.hpp"
#include "ui/game/CGUIBindings.hpp"
#include "ui/game/UIBindings.hpp"
#include "ui/FrameScript.hpp"
#include "util/Unimplemented.hpp"

namespace {

// ref: FUN_0055dc00
int32_t Script_GetNumBindings(lua_State* L) {
    lua_pushnumber(L, UIBindingsGetCount());

    return 1;
}

// ref: FUN_0055e8d0
//
// GetBinding(index[, mode]) -> command, key1, key2, ...
//
// The index is 1-based. The reference pushes the command name and then every key bound to it,
// returning 1 + however many keys there were, so a command with nothing bound returns just the
// name -- which is every command here until a bindings cache exists.
//
// The optional mode selects one of five binding sets. Only the effective set exists here, so the
// argument is accepted and ignored rather than rejected.
int32_t Script_GetBinding(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return luaL_error(L, "Usage: GetBinding(index[, mode])");
    }

    auto command = UIBindingsGetByIndex(static_cast<int32_t>(lua_tonumber(L, 1)) - 1);

    // The reference pushes its empty-string global when the index names nothing, rather than
    // failing, so the pane can walk past a gap.
    lua_pushstring(L, command ? command->name.c_str() : "");

    int32_t keys = 0;

    while (command) {
        auto key = UIBindingsGetKey(command->name.c_str(), keys);

        if (!key) {
            break;
        }

        lua_pushstring(L, key);
        keys++;
    }

    return keys + 1;
}

int32_t Script_SetBinding(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetBindingSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetBindingItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetBindingMacro(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetBindingClick(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOverrideBinding(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOverrideBindingSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOverrideBindingItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOverrideBindingMacro(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetOverrideBindingClick(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearOverrideBindings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0055e9b0
//
// GetBindingKey("COMMAND"[, mode]) -> key1, key2, ...
//
// Returns nothing at all when the command has no keys, which is what FrameXML expects: the action
// button asks for a key and shows no hotkey text when it gets none.
int32_t Script_GetBindingKey(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Usage: GetBindingKey(\"COMMAND\"[, mode])");
    }

    auto command = lua_tostring(L, 1);
    int32_t keys = 0;

    while (true) {
        auto key = UIBindingsGetKey(command, keys);

        if (!key) {
            break;
        }

        lua_pushstring(L, key);
        keys++;
    }

    return keys;
}

// ref: FUN_00562550
//
// GetBindingAction("KEY"[, checkOverride][, mode]) -> command, or the empty string.
//
// The empty string rather than nil is the reference's answer for an unbound key, and FrameXML
// tests it with a string compare.
int32_t Script_GetBindingAction(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        return luaL_error(L, "Usage: GetBindingAction(\"KEY\"[, checkOverride][, mode])");
    }

    auto command = UIBindingsGetCommandForKey(lua_tostring(L, 1));

    lua_pushstring(L, command ? command : "");

    return 1;
}

int32_t Script_GetBindingByKey(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RunBinding(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetCurrentBindingSet(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_LoadBindings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SaveBindings(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0055dc60
int32_t Script_GetNumModifiedClickActions(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGUIBindings::s_modifiedClicks.Count()));

    return 1;
}

// ref: FUN_0055ea70
// 1-based, and out of range answers nil rather than erroring.
//
// The order diverges: the reference walks a hash off the bindings object, this walks the list in
// the order Bindings.xml declared them. Nothing reads the order for meaning -- the options panel
// lists whatever comes back -- but the sequence is not the reference's.
int32_t Script_GetModifiedClickAction(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetModifiedClickAction(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))) - 1;

    if (index >= CGUIBindings::s_modifiedClicks.Count()) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushstring(L, CGUIBindings::s_modifiedClicks[index].action.GetString());

    return 1;
}

// ref: FUN_0055fb90
int32_t Script_SetModifiedClick(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: SetModifiedClick(\"action\", \"binding\")");

        return 0;
    }

    auto action = lua_tostring(L, 1);
    auto binding = lua_tostring(L, 2);
    auto click = action ? CGUIBindings::GetModifiedClick(action) : nullptr;

    uint32_t flags = 0;
    uint32_t button = 0;

    if (click && binding && CGUIBindings::ParseBinding(binding, &flags, &button)) {
        click->modifier.Copy(binding);

        return 0;
    }

    // One message for both failures, as the reference has it -- the caller is not told which.
    luaL_error(L, "SetModifiedClick(): Unknown action (%s) or binding (%s)", action, binding);

    return 0;
}

// ref: FUN_0055fc20
// The reference rebuilds this text from the flags and button it stored rather than keeping the
// string. Every value Bindings.xml ships round-trips through that rebuild unchanged, so handing
// back what was stored is the same answer for the shipped data.
int32_t Script_GetModifiedClick(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: GetModifiedClick(\"action\")");

        return 0;
    }

    auto action = lua_tostring(L, 1);
    auto click = action ? CGUIBindings::GetModifiedClick(action) : nullptr;

    if (!click) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushstring(L, click->modifier.GetString());

    return 1;
}

// ref: FUN_0055fcc0
// No argument check on purpose: the reference takes whatever lua_tostring gives it, and a missing
// argument asks the broader question "is any modifier held at all".
int32_t Script_IsModifiedClick(lua_State* L) {
    auto action = lua_tostring(L, 1);

    auto held = action
        ? CGUIBindings::IsModifiedClick(action)
        : CGUIBindings::AnyModifierHeld();

    // 1 or nil, not true or false -- FrameXML tests these with plain truthiness either way, but
    // a few places compare against 1.
    if (held) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_GetClickFrame(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetNumBindings",             &Script_GetNumBindings },
    { "GetBinding",                 &Script_GetBinding },
    { "SetBinding",                 &Script_SetBinding },
    { "SetBindingSpell",            &Script_SetBindingSpell },
    { "SetBindingItem",             &Script_SetBindingItem },
    { "SetBindingMacro",            &Script_SetBindingMacro },
    { "SetBindingClick",            &Script_SetBindingClick },
    { "SetOverrideBinding",         &Script_SetOverrideBinding },
    { "SetOverrideBindingSpell",    &Script_SetOverrideBindingSpell },
    { "SetOverrideBindingItem",     &Script_SetOverrideBindingItem },
    { "SetOverrideBindingMacro",    &Script_SetOverrideBindingMacro },
    { "SetOverrideBindingClick",    &Script_SetOverrideBindingClick },
    { "ClearOverrideBindings",      &Script_ClearOverrideBindings },
    { "GetBindingKey",              &Script_GetBindingKey },
    { "GetBindingAction",           &Script_GetBindingAction },
    { "GetBindingByKey",            &Script_GetBindingByKey },
    { "RunBinding",                 &Script_RunBinding },
    { "GetCurrentBindingSet",       &Script_GetCurrentBindingSet },
    { "LoadBindings",               &Script_LoadBindings },
    { "SaveBindings",               &Script_SaveBindings },
    { "GetNumModifiedClickActions", &Script_GetNumModifiedClickActions },
    { "GetModifiedClickAction",     &Script_GetModifiedClickAction },
    { "SetModifiedClick",           &Script_SetModifiedClick },
    { "GetModifiedClick",           &Script_GetModifiedClick },
    { "IsModifiedClick",            &Script_IsModifiedClick },
    { "GetClickFrame",              &Script_GetClickFrame },
};

void UIBindingsRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
