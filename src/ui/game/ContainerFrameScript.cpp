#include "ui/game/ContainerFrameScript.hpp"

#include "object/Types.hpp"
#include <storm/String.hpp>
#include "object/client/ItemCache.hpp"
#include "db/Db.hpp"
#include "object/client/CGContainer_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ItemLink.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"

namespace {

// Bag ids as Lua uses them: 0 is the backpack, 1-4 are the equipped bags. The reference also
// accepts -1 and -2 for the bank and the keyring, which frozen has no storage for.
#define BACKPACK_SLOTS 16
#define BANK_SLOTS 28
#define KEYRING_SLOTS 32

// The backpack is not a container object -- it lives in the player's own inventory, straight after
// the equipped slots and the four bag slots. The reference stores all three runs in one array and
// reaches the backpack at index 23; frozen splits them into invSlots and packSlots, and 23 is
// exactly where invSlots ends, so packSlots[n] is the same storage the reference indexes as 23+n.
WOWGUID ContainerSlotGuid(int32_t bag, int32_t slot) {
    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    if (!data || slot < 0) {
        return 0;
    }

    if (bag == 0) {
        return slot < BACKPACK_SLOTS ? data->packSlots[slot] : 0;
    }

    if (bag < 1 || bag > NUM_BAG_SLOTS) {
        return 0;
    }

    auto bagObject = ClntObjMgrObjectPtr(
        data->invSlots[INVSLOT_BAGFIRST + bag - 1], TYPE_CONTAINER, __FILE__, __LINE__
    );

    if (!bagObject) {
        return 0;
    }

    auto container = static_cast<CGContainer_C*>(bagObject)->Container();

    if (!container || slot >= static_cast<int32_t>(container->numSlots)) {
        return 0;
    }

    return container->slots[slot];
}

CGItem_C* ContainerItem(lua_State* L, int32_t bagArg, int32_t slotArg) {
    // Both arguments are 1-based on the Lua side EXCEPT the bag, which is already 0-based -- the
    // backpack is bag 0. Only the slot is converted.
    auto bag = static_cast<int32_t>(lua_tonumber(L, bagArg));
    auto slot = static_cast<int32_t>(lua_tonumber(L, slotArg)) - 1;

    auto guid = ContainerSlotGuid(bag, slot);

    if (!guid) {
        return nullptr;
    }

    auto object = ClntObjMgrObjectPtr(guid, TYPE_ITEM, __FILE__, __LINE__);

    return object ? static_cast<CGItem_C*>(object) : nullptr;
}

// ref: FUN_005d74a0
// The backpack's size is a constant rather than a field -- it is not a container object, so there
// is nothing to ask. The bank and keyring constants are the reference's own, kept so the numbers
// are not invented later.
int32_t Script_GetContainerNumSlots(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetContainerNumSlots(index)");

        return 0;
    }

    auto bag = static_cast<int32_t>(lua_tonumber(L, 1));
    double slots = 0.0;

    if (bag == 0) {
        slots = BACKPACK_SLOTS;
    } else if (bag == -1) {
        slots = BANK_SLOTS;
    } else if (bag == -2) {
        slots = KEYRING_SLOTS;
    } else if (bag >= 1 && bag <= NUM_BAG_SLOTS) {
        auto player = CGPlayer_C::GetActivePtr();
        auto data = player ? player->Player() : nullptr;

        auto bagObject = data
            ? ClntObjMgrObjectPtr(data->invSlots[INVSLOT_BAGFIRST + bag - 1], TYPE_CONTAINER,
                                  __FILE__, __LINE__)
            : nullptr;

        auto container = bagObject ? static_cast<CGContainer_C*>(bagObject)->Container() : nullptr;

        slots = container ? container->numSlots : 0;
    }

    lua_pushnumber(L, slots);

    return 1;
}

// ref: FUN_005d7d00
// No values at all for an empty slot, not a nil.
int32_t Script_GetContainerItemID(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: GetContainerItemID(index, slot)");

        return 0;
    }

    auto item = ContainerItem(L, 1, 2);

    if (!item) {
        return 0;
    }

    lua_pushnumber(L, static_cast<double>(item->GetEntryID()));

    return 1;
}

// ref: FUN_005d7c80
int32_t Script_GetContainerItemLink(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: GetContainerItemLink(index, slot)");

        return 0;
    }

    auto link = ItemLinkFromObject(ContainerItem(L, 1, 2));

    if (!link) {
        return 0;
    }

    lua_pushstring(L, link);

    return 1;
}

// ref: FUN_005d7a90
// texture, count, locked, quality, readable, lootable, link. Seven values, and no values at all
// for an empty slot.
int32_t Script_GetContainerItemInfo(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: GetContainerItemInfo(index, slot)");

        return 0;
    }

    auto item = ContainerItem(L, 1, 2);
    auto data = item ? item->Item() : nullptr;

    if (!data) {
        return 0;
    }

    auto info = ItemCacheGet(item->GetEntryID());
    auto rec = info ? g_itemDisplayInfoDB.GetRecord(info->displayInfoID) : nullptr;

    char icon[260] = { 0 };

    if (rec && rec->m_inventoryIcon[0] && rec->m_inventoryIcon[0][0]) {
        SStrPrintf(icon, sizeof(icon), "Interface\\Icons\\%s", rec->m_inventoryIcon[0]);
    }

    lua_pushstring(L, icon);
    lua_pushnumber(L, static_cast<double>(data->stackCount));

    // TODO locked. The reference reads a flag on the item OBJECT, not the descriptor -- it is set
    // while the item is mid-move. Frozen has no drag-and-drop, so nothing would ever set it and
    // nil is the honest answer rather than a placeholder.
    lua_pushnil(L);

    // Guarded on the INVENTORY TYPE, not the quality: a record with no inventory type reports -1
    // rather than whatever quality it happens to hold. Same rule as GetInventoryItemQuality.
    auto quality = (info && info->inventoryType) ? info->quality : -1;
    lua_pushnumber(L, static_cast<double>(quality));

    // Readable is flag bit 9 -- a book or a scroll with text. The reference also consults a
    // virtual on the item first; that path is for objects frozen does not model, and the flag is
    // what answers for ordinary items.
    if (data->flags & 0x200) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    // TODO lootable. Another virtual, testing bit 2 of what it returns -- a container item with
    // contents still inside. No frozen counterpart.
    lua_pushnil(L);

    auto link = ItemLinkFromObject(item);
    lua_pushstring(L, link ? link : "");

    return 7;
}

FrameScript_Method s_ScriptFunctions[] = {
    { "GetContainerNumSlots",   &Script_GetContainerNumSlots },
    { "GetContainerItemID",     &Script_GetContainerItemID },
    { "GetContainerItemLink",   &Script_GetContainerItemLink },
    { "GetContainerItemInfo",   &Script_GetContainerItemInfo },
};

} // namespace

void ContainerFrameScriptRegisterFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
