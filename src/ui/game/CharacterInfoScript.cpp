#include "ui/game/CharacterInfoScript.hpp"
#include "object/client/ItemLink.hpp"
#include <cstddef>
#include "db/Db.hpp"
#include "ui/FrameScript.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/ItemCache.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/Types.hpp"
#include "ui/game/ScriptUtil.hpp"
#include <storm/String.hpp>
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t Script_GetInventorySlotInfo(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Invalid inventory slot in GetInventorySlotInfo");
        return 0;
    }

    auto slotName = lua_tostring(L, 1);

    PaperDollItemFrameRec* slotRec = nullptr;
    for (int32_t i = 0; i < g_paperDollItemFrameDB.GetNumRecords(); i++) {
        auto paperDollItemFrameRec = g_paperDollItemFrameDB.GetRecordByIndex(i);

        if (paperDollItemFrameRec && !SStrCmpI(slotName, paperDollItemFrameRec->m_itemButtonName)) {
            slotRec = paperDollItemFrameRec;
            break;
        }
    }

    if (!slotRec) {
        luaL_error(L, "Invalid inventory slot in GetInventorySlotInfo");
        return 0;
    }

    // id
    lua_pushnumber(L, slotRec->m_slotNumber);

    // textureName
    lua_pushstring(L, slotRec->m_slotIcon);

    // checkRelic
    if (slotRec->m_slotNumber == EQUIPPED_LAST) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 3;
}

int32_t Script_GetInventoryItemsForSlot(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005e9bc0
// The icon for whatever is in the slot. The chain is the same one GetItemIcon walks: the item's
// entry gives a cache record, the record's display id gives an ItemDisplayInfo row, and that row's
// first inventory icon is the name. Nothing in the slot answers with no values rather than nil.
int32_t Script_GetInventoryItemTexture(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);

    if (!item) {
        return 0;
    }

    auto info = ItemCacheGet(item->GetEntryID());

    if (!info) {
        return 0;
    }

    auto rec = g_itemDisplayInfoDB.GetRecord(info->displayInfoID);

    if (!rec || !rec->m_inventoryIcon[0] || !rec->m_inventoryIcon[0][0]) {
        return 0;
    }

    char icon[260];
    SStrPrintf(icon, sizeof(icon), "Interface\\Icons\\%s", rec->m_inventoryIcon[0]);

    lua_pushstring(L, icon);

    return 1;
}

// TODO the reference gates this on two things beyond a zero durability: a flag bit on the item's
// own record, and a second durability field at +0xdc that frozen's CGItemData does not carry. Both
// have to be identified before this can answer anything but nil, and guessing "durability == 0"
// would call an undamageable item broken.
// The reference reads these four by absolute offset into CGItemData; the asserts hold frozen's
// struct to the same layout. enchantments[12] is what puts durability as far along as 0xd8.
static_assert(offsetof(CGItemData, flags) == 0x3c, "CGItemData layout");
static_assert(offsetof(CGItemData, durability) == 0xd8, "CGItemData layout");
static_assert(offsetof(CGItemData, maxDurability) == 0xdc, "CGItemData layout");

// ref: FUN_00584ac0
// Current durability, except that an item whose flags carry bit 3 reports none at all.
static int32_t ItemDurability(const CGItemData* data) {
    if (data->flags & 0x8) {
        return 0;
    }

    return data->durability;
}

// ref: FUN_005e9d80
// Broken means: the item can have durability at all, it has a maximum, and it is at zero.
//
// The bit-3 test appears twice -- once here and once inside ItemDurability -- which is redundant
// but is how the reference is built, so it is kept rather than folded. Without the maxDurability
// test every item that cannot wear out would read as broken.
int32_t Script_GetInventoryItemBroken(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);
    auto data = item ? item->Item() : nullptr;

    if (data && !(data->flags & 0x8) && data->maxDurability && !ItemDurability(data)) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

// ref: FUN_005e9e40
int32_t Script_GetInventoryItemCount(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);
    auto data = item ? item->Item() : nullptr;

    // An empty slot counts as zero, and a stack that has not arrived yet counts as one, the same
    // way GetItemCount treats it.
    lua_pushnumber(L, data ? (data->stackCount ? data->stackCount : 1) : 0);

    return 1;
}

// TODO the item cache record holds the quality frozen would push, but the reference reads it only
// when another field of the same record is zero, and which field that is has not been established.
// Pushing the quality unconditionally would be right most of the time and wrong silently.
// ref: FUN_005ea040
// Quality from the item cache, or -1 for an item with no inventory type -- the reference guards on
// that field rather than on the quality itself, so a cached record for something unequippable
// reports -1 instead of its real quality.
//
// This one answers nil on a miss where its siblings above return no values at all. That asymmetry
// is the reference's: the failure path here pushes nil and returns 1.
int32_t Script_GetInventoryItemQuality(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);
    auto info = item ? ItemCacheGet(item->GetEntryID()) : nullptr;

    if (!info) {
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, static_cast<double>(info->inventoryType ? info->quality : -1));

    return 1;
}

int32_t Script_GetInventoryItemCooldown(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005ea170
// Two values, current and maximum. An empty slot answers with none at all rather than a pair of
// zeros, which is how FrameXML tells "no item" from "an item at full durability".
int32_t Script_GetInventoryItemDurability(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);
    auto data = item ? item->Item() : nullptr;

    if (!data || !data->maxDurability) {
        return 0;
    }

    lua_pushnumber(L, data->durability);
    lua_pushnumber(L, data->maxDurability);

    return 2;
}

// The hyperlink for an equipped item. No values at all for an empty slot, matching its siblings
// above rather than pushing a nil.
int32_t Script_GetInventoryItemLink(lua_State* L) {
    auto link = ItemLinkFromObject(Script_GetInventoryItem(L, 1, 2));

    if (!link) {
        return 0;
    }

    lua_pushstring(L, link);

    return 1;
}

// ref: FUN_005ea3e0
int32_t Script_GetInventoryItemID(lua_State* L) {
    auto item = Script_GetInventoryItem(L, 1, 2);

    if (!item) {
        return 0;
    }

    lua_pushnumber(L, item->GetEntryID());

    return 1;
}

int32_t Script_GetInventoryItemGems(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_005e7700
// The whole function is an offset. Key ring buttons are numbered from one by the interface and sit
// at inventory slots 0x56 upward, so this adds 86 and hands it back; it does not range-check,
// and neither does the reference.
int32_t Script_KeyRingButtonIDToInvSlotID(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        return luaL_error(L, "Usage: KeyRingButtonIDToInvSlotID(buttonID)");
    }

    lua_pushnumber(L, static_cast<int32_t>(lua_tonumber(L, 1)) + 0x56);

    return 1;
}

int32_t Script_PickupInventoryItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_UseInventoryItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SocketInventoryItem(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_IsInventoryItemLocked(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t Script_PutItemInBag(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PutItemInBackpack(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_PickupBagFromSlot(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CursorCanGoInSlot(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t Script_ShowInventorySellCursor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetInventoryPortraitTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetGuildInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInventoryAlertStatus(lua_State* L) {
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_UpdateInventoryAlertStatus(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_OffhandHasWeapon(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

// Set when the inspected player's honor stats have arrived. Written by the inspect honor handler
// and ClearInspectPlayer, neither ported yet, so it reads 0.
uint32_t s_inspectHonorDataValid = 0; // ref: DAT_00c24228

// ref: FUN_005e7780
int32_t Script_HasInspectHonorData(lua_State* L) {
    if (s_inspectHonorDataValid) {
        lua_pushnumber(L, 1.0);

        return 1;
    }

    lua_pushnil(L);

    return 1;
}

int32_t Script_RequestInspectHonorData(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInspectHonorData(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetInspectArenaTeamData(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClearInspectPlayer(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetWeaponEnchantInfo(lua_State* L) {
    // 6 values, typed from what the caller destructures them into:
    //   hasMainHandEnchant, mainHandExpiration, mainHandCharges, hasOffHandEnchant, offHandExpiration, offHandCharges
    // The data behind this is not available yet, so each position takes the neutral
    // value for its type -- 0 where the caller does arithmetic, false where it
    // branches, nil where it expects a name or a texture and already handles absence.
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushboolean(L, 0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);

    return 6;
}

int32_t Script_HasWandEquipped(lua_State* L) {
    // Not implemented, so it can never be true. Stated rather than left as an implicit nil.
    lua_pushboolean(L, 0);

    return 1;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GetInventorySlotInfo",           &Script_GetInventorySlotInfo },
    { "GetInventoryItemsForSlot",       &Script_GetInventoryItemsForSlot },
    { "GetInventoryItemTexture",        &Script_GetInventoryItemTexture },
    { "GetInventoryItemBroken",         &Script_GetInventoryItemBroken },
    { "GetInventoryItemCount",          &Script_GetInventoryItemCount },
    { "GetInventoryItemQuality",        &Script_GetInventoryItemQuality },
    { "GetInventoryItemCooldown",       &Script_GetInventoryItemCooldown },
    { "GetInventoryItemDurability",     &Script_GetInventoryItemDurability },
    { "GetInventoryItemLink",           &Script_GetInventoryItemLink },
    { "GetInventoryItemID",             &Script_GetInventoryItemID },
    { "GetInventoryItemGems",           &Script_GetInventoryItemGems },
    { "KeyRingButtonIDToInvSlotID",     &Script_KeyRingButtonIDToInvSlotID },
    { "PickupInventoryItem",            &Script_PickupInventoryItem },
    { "UseInventoryItem",               &Script_UseInventoryItem },
    { "SocketInventoryItem",            &Script_SocketInventoryItem },
    { "IsInventoryItemLocked",          &Script_IsInventoryItemLocked },
    { "PutItemInBag",                   &Script_PutItemInBag },
    { "PutItemInBackpack",              &Script_PutItemInBackpack },
    { "PickupBagFromSlot",              &Script_PickupBagFromSlot },
    { "CursorCanGoInSlot",              &Script_CursorCanGoInSlot },
    { "ShowInventorySellCursor",        &Script_ShowInventorySellCursor },
    { "SetInventoryPortraitTexture",    &Script_SetInventoryPortraitTexture },
    { "GetGuildInfo",                   &Script_GetGuildInfo },
    { "GetInventoryAlertStatus",        &Script_GetInventoryAlertStatus },
    { "UpdateInventoryAlertStatus",     &Script_UpdateInventoryAlertStatus },
    { "OffhandHasWeapon",               &Script_OffhandHasWeapon },
    { "HasInspectHonorData",            &Script_HasInspectHonorData },
    { "RequestInspectHonorData",        &Script_RequestInspectHonorData },
    { "GetInspectHonorData",            &Script_GetInspectHonorData },
    { "GetInspectArenaTeamData",        &Script_GetInspectArenaTeamData },
    { "ClearInspectPlayer",             &Script_ClearInspectPlayer },
    { "GetWeaponEnchantInfo",           &Script_GetWeaponEnchantInfo },
    { "HasWandEquipped",                &Script_HasWandEquipped },
};

void CharacterInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
