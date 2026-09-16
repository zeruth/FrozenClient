#include "ui/game/ActionBarScript.hpp"
#include "ui/FrameScript.hpp"
#include "db/Db.hpp"
#include "ui/game/CGActionBar.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

// Lua numbers action slots from 1; CGActionBar stores them from 0.
int32_t ActionSlot(lua_State* L, int32_t index) {
    return lua_isnumber(L, index) ? static_cast<int32_t>(lua_tonumber(L, index)) - 1 : -1;
}

// The icon an action draws: spell id -> Spell.dbc iconID -> SpellIcon.dbc texture path.
const char* ActionTexture(int32_t slot) {
    uint32_t type = CGActionBar::GetActionType(slot);
    uint32_t id = CGActionBar::GetActionID(slot);

    if (!id) {
        return nullptr;
    }

    // Items and macros carry their art elsewhere (item cache, macro icon list); only spells can be
    // resolved today, and answering nil for the rest is what a missing icon looks like anyway.
    if (type != CGActionBar::ACTION_BUTTON_SPELL && type != CGActionBar::ACTION_BUTTON_C) {
        return nullptr;
    }

    auto spell = g_spellDB.GetRecord(static_cast<int32_t>(id));

    if (!spell || !spell->m_spellIconID) {
        return nullptr;
    }

    auto icon = g_spellIconDB.GetRecord(spell->m_spellIconID);

    return icon && icon->m_textureFilename && *icon->m_textureFilename
        ? icon->m_textureFilename
        : nullptr;
}

int32_t Script_GetActionInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetActionTexture(lua_State* L) {
    const char* texture = ActionTexture(ActionSlot(L, 1));

    if (texture) {
        lua_pushstring(L, texture);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_GetActionCount(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetActionCooldown(lua_State* L) {
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 3;
}

int32_t Script_GetActionAutocast(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetActionText(lua_State* L) {
    // Only macros carry text -- the label drawn across the button. A spell has none, and FrameXML
    // relies on that to decide whether to draw the name at all.
    int32_t slot = ActionSlot(L, 1);
    uint32_t type = CGActionBar::GetActionType(slot);

    if (type != CGActionBar::ACTION_BUTTON_MACRO && type != CGActionBar::ACTION_BUTTON_CMACRO) {
        lua_pushnil(L);

        return 1;
    }

    // Macros are stored client-side and are not implemented yet, so the slot is known to hold one
    // but its name cannot be produced.
    lua_pushnil(L);

    return 1;
}

int32_t Script_HasAction(lua_State* L) {
    int32_t slot = ActionSlot(L, 1);

    if (CGActionBar::GetAction(slot)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_UseAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PickupAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PlaceAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsAttackAction(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsCurrentAction(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsAutoRepeatAction(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsUsableAction(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsConsumableAction(lua_State* L) {
    // TODO consumable actions; nothing is until item actions are ported
    return 0;
}

int32_t Script_IsStackableAction(lua_State* L) {
    // TODO stackable actions
    return 0;
}

int32_t Script_IsEquippedAction(lua_State* L) {
    // True only for an item action whose item is currently equipped. Item actions are not resolved
    // yet, so the honest answer is nil rather than a false that claims the item was checked.
    lua_pushnil(L);

    return 1;
}

int32_t Script_ActionHasRange(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsActionInRange(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetBonusBarOffset(lua_State* L) {
    lua_pushnumber(L, CGActionBar::GetBonusBarOffset());

    return 1;
}

int32_t Script_GetMultiCastBarOffset(lua_State* L) {
    // TODO the totem bar page; 0 until the multi cast bar is ported
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_ChangeActionBarPage(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetActionBarPage(lua_State* L) {
    if (CGActionBar::s_tempPageActiveFlags) {
        lua_pushinteger(L, 1);
    } else {
        lua_pushinteger(L, CGActionBar::s_currentPage + 1);
    }

    return 1;
}

int32_t Script_GetActionBarToggles(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetActionBarToggles(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsPossessBarVisible(lua_State* L) {
    // Possession is not implemented, so its bar can never be up.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_GetMultiCastTotemSpells(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetMultiCastSpell(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetActionInfo",              &Script_GetActionInfo },
    { "GetActionTexture",           &Script_GetActionTexture },
    { "GetActionCount",             &Script_GetActionCount },
    { "GetActionCooldown",          &Script_GetActionCooldown },
    { "GetActionAutocast",          &Script_GetActionAutocast },
    { "GetActionText",              &Script_GetActionText },
    { "HasAction",                  &Script_HasAction },
    { "UseAction",                  &Script_UseAction },
    { "PickupAction",               &Script_PickupAction },
    { "PlaceAction",                &Script_PlaceAction },
    { "IsAttackAction",             &Script_IsAttackAction },
    { "IsCurrentAction",            &Script_IsCurrentAction },
    { "IsAutoRepeatAction",         &Script_IsAutoRepeatAction },
    { "IsUsableAction",             &Script_IsUsableAction },
    { "IsConsumableAction",         &Script_IsConsumableAction },
    { "IsStackableAction",          &Script_IsStackableAction },
    { "IsEquippedAction",           &Script_IsEquippedAction },
    { "ActionHasRange",             &Script_ActionHasRange },
    { "IsActionInRange",            &Script_IsActionInRange },
    { "GetBonusBarOffset",          &Script_GetBonusBarOffset },
    { "GetMultiCastBarOffset",      &Script_GetMultiCastBarOffset },
    { "ChangeActionBarPage",        &Script_ChangeActionBarPage },
    { "GetActionBarPage",           &Script_GetActionBarPage },
    { "GetActionBarToggles",        &Script_GetActionBarToggles },
    { "SetActionBarToggles",        &Script_SetActionBarToggles },
    { "IsPossessBarVisible",        &Script_IsPossessBarVisible },
    { "GetMultiCastTotemSpells",    &Script_GetMultiCastTotemSpells },
    { "SetMultiCastSpell",          &Script_SetMultiCastSpell },
};

void ActionBarRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
