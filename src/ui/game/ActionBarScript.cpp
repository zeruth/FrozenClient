#include "ui/game/ActionBarScript.hpp"
#include <storm/String.hpp>
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "ui/FrameScript.hpp"
#include "db/Db.hpp"
#include "object/client/SpellBook.hpp"
#include "object/client/ObjMgr.hpp"
#include "net/Types.hpp"
#include <common/DataStore.hpp>
#include "object/Types.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ClntObjMgr.hpp"
#include "client/ClientServices.hpp"
#include "ui/game/Types.hpp"
#include "ui/game/CGActionBar.hpp"

// The reference names six in its own range error.
static const int32_t NUM_ACTIONBAR_PAGES = 6;
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

// The action types the server packs into the high byte, as AzerothCore's ActionButtonType names
// them. Lua wants the string form.
const char* ActionTypeName(uint32_t type) {
    switch (type) {
        case CGActionBar::ACTION_BUTTON_SPELL:
        case CGActionBar::ACTION_BUTTON_C:
            return "spell";

        case CGActionBar::ACTION_BUTTON_EQSET:
            return "equipmentset";

        case CGActionBar::ACTION_BUTTON_MACRO:
        case CGActionBar::ACTION_BUTTON_CMACRO:
            return "macro";

        case CGActionBar::ACTION_BUTTON_ITEM:
            return "item";

        default:
            return nullptr;
    }
}

// ref: FUN_005a8f10
// The return COUNT varies by action type, which is easy to miss and is why this used to answer
// three for everything:
//
//   empty or unrecognised slot   nothing at all
//   spell or companion           4 -- type, index, subType, spellID
//   item, macro, equipment set   2 -- type, index
//
// Three nils and nothing are the same thing to a Lua destructure, so the empty case never showed;
// the spell case was one short, and VehicleMenuBar.lua reads exactly that fourth value.
//
// DIVERGENCE in the SECOND value, and it is not new -- writing the fourth one just makes it
// visible. For a spell the reference returns the spellbook INDEX there, not the spell id: it runs
// the id through FUN_0053b4e0 with the pet flag and adds one. Frozen has no spellbook, so it
// returns the id in that position instead. The fourth value, which genuinely is the spell id, is
// now correct either way.
int32_t Script_GetActionInfo(lua_State* L) {
    int32_t slot = ActionSlot(L, 1);
    uint32_t packed = CGActionBar::GetAction(slot);

    const char* name = packed ? ActionTypeName(CGActionBar::GetActionType(slot)) : nullptr;

    if (!name) {
        return 0;
    }

    uint32_t id = CGActionBar::GetActionID(slot);

    lua_pushstring(L, name);
    lua_pushnumber(L, id);

    // Only a spell carries the last two. An item, a macro or an equipment set stops here, as the
    // reference does rather than padding with nils.
    if (SStrCmpI(name, "spell", STORM_MAX_STR)) {
        return 2;
    }

    // subType separates a player spell from a pet one, and for a companion it is CRITTER or MOUNT.
    // Frozen resolves neither the pet book nor companions, so every spell action reports as a
    // player spell -- which is what an action bar with no pet on it holds.
    lua_pushstring(L, "spell");
    lua_pushnumber(L, id);

    return 4;
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

// ref: FUN_005a7d10
int32_t Script_GetActionCount(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetActionCount(slot)");

        return 0;
    }

    auto slot = static_cast<int32_t>(lua_tonumber(L, 1) + 0.5) - 1;
    int32_t count = 0;

    if (slot >= 0 && slot < CGActionBar::NUM_ACTION_BUTTONS) {
        // TODO the reference reads the stack count for this slot out of a parallel table at
        // 00c1e118, filled as item counts arrive. Frozen keeps no such table yet, so every slot
        // reports 0 -- which is what a spell or macro slot reports in the reference anyway.
        count = 0;
    }

    lua_pushnumber(L, count);

    return 1;
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
    // CMSG_CAST_SPELL: uint8 castCount, uint32 spellId, uint8 castFlags, then SpellCastTargets,
    // which begins with a uint32 mask. Mask 0 is a self / no-target cast; TARGET_FLAG_UNIT (0x2)
    // is followed by the target's packed GUID.
    int32_t slot = ActionSlot(L, 1);
    uint32_t type = CGActionBar::GetActionType(slot);
    uint32_t id = CGActionBar::GetActionID(slot);

    if (!id || (type != CGActionBar::ACTION_BUTTON_SPELL && type != CGActionBar::ACTION_BUTTON_C)) {
        return 0;
    }

    // onSelf is UseAction's third argument; it forces the cast onto the player regardless of target.
    bool onSelf = lua_toboolean(L, 3) != 0;
    WOWGUID target = 0;

    if (!onSelf) {
        auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_UNIT, __FILE__, __LINE__);

        if (player) {
            auto unit = static_cast<CGUnit_C*>(player)->Unit();

            if (unit) {
                target = unit->target;
            }
        }
    }

    SpellBookCast(id, target);

    return 0;
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
    // Returns isUsable, notEnoughMana. ActionButton_UpdateUsable darkens the icon whenever the
    // first is false, which is why every button came up greyed: a stub returned nothing at all.
    //
    // Power costs and spell requirements are not evaluated yet, so a slot that holds a spell the
    // player actually knows reads as usable. That is the same answer the reference gives for a
    // spell with no cost and no unmet requirement, and it is much closer than "never usable".
    int32_t slot = ActionSlot(L, 1);
    uint32_t type = CGActionBar::GetActionType(slot);
    uint32_t id = CGActionBar::GetActionID(slot);

    bool usable = id != 0
        && (type == CGActionBar::ACTION_BUTTON_SPELL || type == CGActionBar::ACTION_BUTTON_C)
        && g_spellDB.GetRecord(static_cast<int32_t>(id)) != nullptr;

    lua_pushboolean(L, usable);
    lua_pushboolean(L, 0);

    return 2;
}

int32_t Script_IsConsumableAction(lua_State* L) {
    // TODO consumable actions; nothing is until item actions are ported
    return 0;
}

int32_t Script_IsStackableAction(lua_State* L) {
    // TODO stackable actions
    return 0;
}

// ref: FUN_005a8bc0 with its worker FUN_005a88b0
// True for an item action whose item is worn right now.
//
// Two gates before the search, both the reference's. The action has to be an ITEM action -- a
// spell slot is never equipped -- and the item has to be equippable at all, which it decides from
// Item.dbc's inventory type rather than from the cached record. An item with inventory type 0
// equips nowhere and is rejected without looking.
//
// DIVERGENCE: the reference then calls a general item search with a flag word (0x81, or 0xa1 when
// the item resolves through a second path) selecting which storage to cover. What those flags
// admit is not identified, so this walks the player's own equipped slots directly. That covers
// worn gear and the four bags; if either flag also reaches the bank or the keyring, this answers
// false where the reference would answer true.
int32_t Script_IsEquippedAction(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: IsEquippedAction(slot)");

        return 0;
    }

    auto slot = ActionSlot(L, 1);
    auto equipped = false;

    if (slot >= 0 && slot < CGActionBar::NUM_ACTION_BUTTONS
        && CGActionBar::GetActionType(slot) == CGActionBar::ACTION_BUTTON_ITEM) {
        auto entry = static_cast<int32_t>(CGActionBar::GetActionID(slot));
        auto rec = entry ? g_itemDB.GetRecord(entry) : nullptr;

        // Inventory type 0 means the item equips nowhere, so there is nothing to search for.
        if (rec && rec->m_inventoryType) {
            auto player = CGPlayer_C::GetActivePtr();
            auto data = player ? player->Player() : nullptr;

            for (int32_t i = 0; data && i < NUM_INVENTORY_SLOTS && !equipped; i++) {
                auto object = ClntObjMgrObjectPtr(data->invSlots[i], TYPE_ITEM, __FILE__, __LINE__);

                equipped = object && static_cast<CGItem_C*>(object)->GetEntryID() == entry;
            }
        }
    }

    if (equipped) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t Script_ActionHasRange(lua_State* L) {
    // SpellRange.dbc is not loaded, so no action can be range-checked. False keeps
    // ActionButton_UpdateRangeIndicator from colouring the button at all, which is what the
    // reference does for a spell with no range requirement.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_IsActionInRange(lua_State* L) {
    // nil means "no range requirement / cannot tell", and is distinct from 0, which FrameXML draws
    // as out-of-range red.
    lua_pushnil(L);

    return 1;
}

// ref: FUN_005a7f20
int32_t Script_GetBonusBarOffset(lua_State* L) {
    lua_pushnumber(L, CGActionBar::GetBonusBarOffset());

    return 1;
}

int32_t Script_GetMultiCastBarOffset(lua_State* L) {
    // TODO the totem bar page; 0 until the multi cast bar is ported
    lua_pushnumber(L, 0.0);

    return 1;
}

// ref: FUN_005a7f60
// TODO the reference first runs the protected-function check for category 0xe (FUN_005191c0),
// which refuses the change when the script calling it is tainted. Frozen tracks no taint yet,
// so the change always goes through.
int32_t Script_ChangeActionBarPage(lua_State* L) {
    auto page = static_cast<int32_t>(lua_tonumber(L, 1) + 0.5) - 1;

    if (page < 0 || page >= NUM_ACTIONBAR_PAGES) {
        luaL_error(L, "ChangeActionBarPage() needs a page in the range 1 to %d", NUM_ACTIONBAR_PAGES);

        return 0;
    }

    if (page != static_cast<int32_t>(CGActionBar::s_currentPage)) {
        CGActionBar::s_currentPage = page;

        FrameScript_SignalEvent(SCRIPT_ACTIONBAR_PAGE_CHANGED, nullptr);
    }

    return 0;
}

// ref: FUN_005a7fd0
int32_t Script_GetActionBarPage(lua_State* L) {
    if (CGActionBar::s_tempPageActiveFlags) {
        lua_pushinteger(L, 1);
    } else {
        lua_pushinteger(L, CGActionBar::s_currentPage + 1);
    }

    return 1;
}

// Which of the four optional action bars are shown, plus "always show the bar art". The reference
// persists these per character; nothing is persisted yet, so they start off, the way a fresh
// character's do.
bool s_barToggles[5] = { false, false, false, false, true };

int32_t Script_GetActionBarToggles(lua_State* L) {
    for (int32_t i = 0; i < 5; i++) {
        lua_pushboolean(L, s_barToggles[i]);
    }

    return 5;
}

int32_t Script_SetActionBarToggles(lua_State* L) {
    for (int32_t i = 0; i < 5; i++) {
        // FrameXML passes nil for "leave alone" on some calls, so only a real boolean counts.
        if (!lua_isnone(L, i + 1)) {
            s_barToggles[i] = lua_toboolean(L, i + 1) != 0;
        }
    }

    return 0;
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
