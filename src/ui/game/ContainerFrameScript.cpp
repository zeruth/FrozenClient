#include "ui/game/ContainerFrameScript.hpp"

#include "object/Types.hpp"
#include <storm/String.hpp>
#include "object/client/ItemCache.hpp"
#include "db/Db.hpp"
#include "object/client/CGBag_C.hpp"
#include "object/client/CGContainer_C.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ItemLink.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScript_Object.hpp"
#include "ui/game/Types.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/Types.hpp"
#include "util/Lua.hpp"

namespace {

// Bag ids as Lua uses them: 0 is the backpack, 1-4 are the equipped bags. The reference also
// accepts -1 and -2 for the bank and the keyring, which frozen has no storage for.
#define BACKPACK_SLOTS 16
#define BANK_SLOTS 28
#define KEYRING_SLOTS 32

// The icon directory, the same constant and the same recorded divergence GameScript.cpp carries:
// the reference reads it from row 3 of an unidentified table through FUN_00634910 rather than
// holding a literal. Kept here as its own copy because the joining code below is inline in the
// reference too -- FUN_005d7180 and FUN_007e7cc0 each format their own path.
static const char* ICON_DIRECTORY = "Interface\\Icons";

// ref: FUN_0070aa00
// An item's inventory icon NAME -- no directory, no extension. Empty when the item has no display
// record yet.
//
// DIVERGENCE in route, not in result. The reference reaches the display record through a pointer
// the object itself carries (object+8, then +0xc for the display id); frozen's CGItem_C holds no
// such pointer, so it goes entry -> item cache -> ItemDisplayInfo and lands on the same row. The
// visible difference is timing: before the item cache answers for an entry, frozen returns empty
// where the reference would already have the icon.
static const char* ItemDisplayIcon(CGItem_C* item) {
    auto info = item ? ItemCacheGet(item->GetEntryID()) : nullptr;
    auto rec = info ? g_itemDisplayInfoDB.GetRecord(info->displayInfoID) : nullptr;

    return (rec && rec->m_inventoryIcon[0]) ? rec->m_inventoryIcon[0] : "";
}

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
    auto iconName = ItemDisplayIcon(item);

    char icon[260] = { 0 };

    if (iconName[0]) {
        SStrPrintf(icon, sizeof(icon), "%s%s%s", ICON_DIRECTORY,
                   *ICON_DIRECTORY ? "\\" : "", iconName);
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

// ref: FUN_005d6f60
// A bag id to the inventory id its BAG SLOT occupies -- what you pass to the equipped-item
// bindings to ask about the bag itself rather than its contents.
//
// Two ranges with different offsets: the four worn bags map to 20-23, and the seven bank bags to
// 68-74. Bag 0, the backpack, is NOT valid here -- it occupies no inventory slot, so the range
// starts at 1 and anything outside 1-11 is an error rather than a nil.
int32_t Script_ContainerIDToInventoryID(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: ContainerIDToInventoryID(containerID)");

        return 0;
    }

    auto bag = static_cast<int32_t>(lua_tonumber(L, 1));

    if (bag < 1 || bag > 11) {
        luaL_error(L, "ContainerIDToInventoryID(): invalid container ID");

        return 0;
    }

    lua_pushnumber(L, static_cast<double>(bag + (bag <= 4 ? 0x13 : 0x3f)));

    return 1;
}

// ref: FUN_005d7ef0
// durability, maxDurability -- or no values for an item that cannot wear out.
//
// The same flag bit 3 that ItemDurability tests for equipped items: an item carrying it reports no
// durability at all, and the reference zeroes BOTH values rather than just the current one. Since
// the max then reads zero, such an item falls into the "no values" path on its own.
int32_t Script_GetContainerItemDurability(lua_State* L) {
    if (!lua_isnumber(L, 1) || !lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: GetContainerItemDurability(index, slot)");

        return 0;
    }

    auto item = ContainerItem(L, 1, 2);
    auto data = item ? item->Item() : nullptr;

    if (!data) {
        return 0;
    }

    auto noDurability = (data->flags & 0x8) != 0;
    auto maxDurability = noDurability ? 0 : data->maxDurability;
    auto durability = noDurability ? 0 : data->durability;

    if (!maxDurability) {
        return 0;
    }

    lua_pushnumber(L, static_cast<double>(durability));
    lua_pushnumber(L, static_cast<double>(maxDurability));

    return 2;
}

// ref: FUN_005d8c70
// The backpack is a global string, not an item: it has no item behind it, so its name comes from
// BACKPACK_TOOLTIP. Every other bag is the name of the bag item sitting in that slot.
int32_t Script_GetBagName(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetBagName(index)");

        return 0;
    }

    auto bag = static_cast<int32_t>(lua_tonumber(L, 1));

    if (bag == 0) {
        lua_pushstring(L, FrameScript_GetText("BACKPACK_TOOLTIP", -1, GENDER_NOT_APPLICABLE));

        return 1;
    }

    // The reference accepts 1-11, the four worn bags and seven bank bags. Frozen has no bank, so
    // the upper seven resolve to nothing rather than being rejected -- the same no-values answer
    // an empty bag slot gives.
    if (bag < 1 || bag > NUM_BAG_SLOTS) {
        return 0;
    }

    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    auto object = data
        ? ClntObjMgrObjectPtr(data->invSlots[INVSLOT_BAGFIRST + bag - 1], TYPE_ITEM,
                              __FILE__, __LINE__)
        : nullptr;

    auto info = object ? ItemCacheGet(static_cast<CGItem_C*>(object)->GetEntryID()) : nullptr;

    if (!info || !info->Name() || !info->Name()[0]) {
        return 0;
    }

    lua_pushstring(L, info->Name());

    return 1;
}

// ref: FUN_005d7590
// Two returns: how many slots are empty, and the bag's FAMILY -- the class of item it will take.
// Not one value, which is what the name suggests.
//
// The backpack's family is literally 0 in the reference: it takes anything, and 0 is how that is
// said. Frozen reports 0 for real bags too, which is right for a generic bag and wrong for a
// quiver or a soul bag -- the family lives on the bag's item record and frozen's ItemInfo does not
// carry it.
int32_t Script_GetContainerNumFreeSlots(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetContainerFreeSlots(index)");

        return 0;
    }

    auto bag = static_cast<int32_t>(lua_tonumber(L, 1));
    int32_t free = 0;

    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    if (data && bag == 0) {
        for (int32_t slot = 0; slot < BACKPACK_SLOTS; slot++) {
            if (!data->packSlots[slot]) {
                free++;
            }
        }
    } else if (data && bag >= 1 && bag <= NUM_BAG_SLOTS) {
        auto bagObject = ClntObjMgrObjectPtr(
            data->invSlots[INVSLOT_BAGFIRST + bag - 1], TYPE_CONTAINER, __FILE__, __LINE__
        );

        auto container = bagObject ? static_cast<CGContainer_C*>(bagObject)->Container() : nullptr;

        for (uint32_t slot = 0; container && slot < container->numSlots; slot++) {
            if (!container->slots[slot]) {
                free++;
            }
        }
    }

    // The bank and keyring answer zero free rather than being rejected: frozen has no storage for
    // them, and from Lua a bank you cannot see and a full one look the same.
    lua_pushnumber(L, static_cast<double>(free));
    lua_pushnumber(L, 0.0);

    return 2;
}

// ref: FUN_005d7820
// GetContainerFreeSlots(index [, returnTable]) -> table of the empty slot numbers.
//
// Returns a TABLE, not a list of values -- the sibling that returns numbers is
// GetContainerNumFreeSlots. A caller may pass its own table in to be filled, which is what the
// second argument is for and why this allocates only when it has to.
//
// The reference does NOT clear a table it was given: it writes indices from 1 upward and leaves
// whatever was past the end. A caller reusing one table for a bag that emptied and then filled
// sees stale entries after the new ones, and FrameXML is written knowing that.
int32_t Script_GetContainerFreeSlots(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetContainerFreeSlots(index [, returnTable])");

        return 0;
    }

    auto bag = static_cast<int32_t>(lua_tonumber(L, 1));

    if (lua_type(L, 2) != LUA_TTABLE) {
        lua_newtable(L);
    } else {
        lua_pushvalue(L, 2);
    }

    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;
    int32_t next = 1;

    auto record = [&](int32_t slot) {
        lua_checkstack(L, 2);
        lua_pushnumber(L, static_cast<double>(next));
        lua_pushnumber(L, static_cast<double>(slot));
        lua_settable(L, -3);
        next++;
    };

    if (data && bag == 0) {
        for (int32_t slot = 0; slot < BACKPACK_SLOTS; slot++) {
            if (!data->packSlots[slot]) {
                record(slot + 1);
            }
        }
    } else if (data && bag >= 1 && bag <= NUM_BAG_SLOTS) {
        auto bagObject = ClntObjMgrObjectPtr(
            data->invSlots[INVSLOT_BAGFIRST + bag - 1], TYPE_CONTAINER, __FILE__, __LINE__
        );

        auto container = bagObject ? static_cast<CGContainer_C*>(bagObject)->Container() : nullptr;

        for (uint32_t slot = 0; container && slot < container->numSlots; slot++) {
            if (!container->slots[slot]) {
                record(static_cast<int32_t>(slot) + 1);
            }
        }
    }

    return 1;
}

// ref: FUN_005d7180
// SetBagPortraitTexture(texture, slot) -- point a texture at an equipped bag's own icon.
//
// Argument order in the reference is not the order the usage string suggests: the texture is
// resolved FIRST, before the slot is even checked for being a number, and a bad texture raises out
// of FrameScript_GetObjectThis rather than reaching the usage message.
//
// The texture is cleared before the bag is looked up, so a slot holding no bag leaves the region
// blank rather than showing the previous bag's icon.
//
// Slots are 1-based here and 1-11 wide: 1-4 are the equipped bags, 5-11 the bank bags. The
// reference resolves 5-11 to a null guid unless the bank is open, and frozen models no bank, so
// those slots taking the same empty path is the reference's own away-from-the-bank behaviour
// rather than a divergence.
//
// DIVERGENCE in the sink, shared with Script_SetPortraitToTexture and recorded against both in
// overrides.json: the reference hands the path to FUN_00619330, which builds a 64x64 texture
// ("Portrait2") from the BLP and multiplies in a circular alpha mask. Frozen has no portrait
// texture cache, so this loads the icon as an ordinary UI texture -- right image, square edges.
int32_t Script_SetBagPortraitTexture(lua_State* L) {
    auto object = FrameScript_GetObjectThis(L, CSimpleTexture::GetObjectType());

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: SetBagPortraitTexture(texture, slot)");

        return 0;
    }

    auto slot = static_cast<int32_t>(lua_tonumber(L, 2)) - 1;

    if (slot < 0) {
        return 0;
    }

    if (slot >= 11) {
        luaL_error(L, "Invalid slot in SetBagPortraitTexture");

        return 0;
    }

    auto texture = static_cast<CSimpleTexture*>(object);
    texture->SetTexture(static_cast<const char*>(nullptr), 0, 0,
                        CSimpleTexture::s_textureFilterMode, ImageMode_UI);

    auto player = CGPlayer_C::GetActivePtr();
    auto data = player ? player->Player() : nullptr;

    auto bag = (data && slot < NUM_BAG_SLOTS)
        ? ClntObjMgrObjectPtr(data->invSlots[INVSLOT_BAGFIRST + slot], TYPE_CONTAINER,
                              __FILE__, __LINE__)
        : nullptr;

    if (!bag) {
        return 0;
    }

    auto iconName = ItemDisplayIcon(static_cast<CGItem_C*>(bag));

    char icon[260];
    SStrPrintf(icon, sizeof(icon), "%s%s%s", ICON_DIRECTORY,
               *ICON_DIRECTORY ? "\\" : "", iconName);

    texture->SetTexture(icon, 0, 0, CSimpleTexture::s_textureFilterMode, ImageMode_UI);

    return 0;
}

FrameScript_Method s_ScriptFunctions[] = {
    { "GetContainerNumSlots",   &Script_GetContainerNumSlots },
    { "GetContainerItemID",     &Script_GetContainerItemID },
    { "GetContainerItemLink",   &Script_GetContainerItemLink },
    { "GetContainerItemInfo",   &Script_GetContainerItemInfo },
    { "ContainerIDToInventoryID", &Script_ContainerIDToInventoryID },
    { "GetContainerItemDurability", &Script_GetContainerItemDurability },
    { "GetBagName",             &Script_GetBagName },
    { "GetContainerNumFreeSlots", &Script_GetContainerNumFreeSlots },
    { "GetContainerFreeSlots",  &Script_GetContainerFreeSlots },
    { "SetBagPortraitTexture",  &Script_SetBagPortraitTexture },
};

} // namespace

void ContainerFrameScriptRegisterFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}

// The item in a bag slot, for callers outside this file. ContainerItem itself stays private
// because it is one of a set of helpers that share the container-slot lookup; this is the only
// one anything else has needed.
CGItem_C* Script_GetContainerItem(lua_State* L, int32_t bagArg, int32_t slotArg) {
    if (!lua_isnumber(L, bagArg) || !lua_isnumber(L, slotArg)) {
        return nullptr;
    }

    return ContainerItem(L, bagArg, slotArg);
}

// The four worn bags then the seven bank bags. Nothing fills it yet: the code that caches the bag
// guids is not ported, so every entry reads 0.
static WOWGUID s_bagGuids[11];  // ref: DAT_00c23540

// ref: FUN_005d6f10
void ContainerSignalBagUpdateCooldown() {
    FrameScript_SignalEvent(SCRIPT_BAG_UPDATE_COOLDOWN, nullptr);
}

// ref: FUN_005d6f20
// The bank bags (4 and up) only while a banker is open.
WOWGUID ContainerGetBagGuid(uint32_t index) {
    if (static_cast<int32_t>(index) > -1 && index < 11
        && (static_cast<int32_t>(index) < 4 || s_bankerGUID != 0)) {
        return s_bagGuids[index];
    }

    return 0;
}
