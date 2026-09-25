#include "ui/game/TradeInfoScript.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGTradeInfo.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/ItemLink.hpp"
#include "object/client/ObjMgr.hpp"
#include <cmath>

namespace {

// The items the player has put up, one per trade slot. Written by the trade update handler, which
// is not ported, so every slot is empty for now.
WOWGUID s_playerTradeItems[7]; // ref: DAT_00bfa620

// Where each of those items came from: the container and the slot in it. Written beside them.
WOWGUID s_playerTradeItemContainers[7];     // ref: DAT_00bfa5e8
uint8_t s_playerTradeItemSlots[7];          // ref: DAT_00bfa5dc

// The player being traded with, as the trade frame sees it. Written by the trade open and close
// handlers, which are not ported.
WOWGUID s_tradeTarget;                      // ref: DAT_00bfa658

int32_t Script_CloseTrade(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClickTradeButton(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_ClickTargetTradeButton(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetTradeTargetItemInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetTradeTargetItemLink(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetTradePlayerItemInfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00586d00
int32_t Script_GetTradePlayerItemLink(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: GetTradePlayerItemLink(index)");

        return 0;
    }

    auto index = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))) - 1;
    auto guid = index < 7 ? s_playerTradeItems[index] : 0;
    auto item = static_cast<CGItem_C*>(ClntObjMgrObjectPtr(guid, TYPE_ITEM, __FILE__, __LINE__));

    if (item) {
        lua_pushstring(L, ItemLinkFromObject(item));

        return 1;
    }

    return 0;
}

int32_t Script_AcceptTrade(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_CancelTradeAccept(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_GetPlayerTradeMoney(lua_State* L) {
    if (CGTradeInfo::GetTradePartner()) {
        lua_pushnumber(L, CGTradeInfo::GetPlayerTradeMoney());
    } else {
        lua_pushnumber(L, 0);
    }

    return 1;
}

int32_t Script_GetTargetTradeMoney(lua_State* L) {
    // No trade window, so there is never money in one.
    lua_pushnumber(L, 0.0);

    return 1;
}

int32_t Script_PickupTradeMoney(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_AddTradeMoney(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00586870
int32_t Script_SetTradeMoney(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetTradeMoney(amount)");

        return 0;
    }

    CGTradeInfo::SetPlayerTradeMoney(static_cast<uint32_t>(llrint(lua_tonumber(L, 1))));

    return 0;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "CloseTrade",             &Script_CloseTrade },
    { "ClickTradeButton",       &Script_ClickTradeButton },
    { "ClickTargetTradeButton", &Script_ClickTargetTradeButton },
    { "GetTradeTargetItemInfo", &Script_GetTradeTargetItemInfo },
    { "GetTradeTargetItemLink", &Script_GetTradeTargetItemLink },
    { "GetTradePlayerItemInfo", &Script_GetTradePlayerItemInfo },
    { "GetTradePlayerItemLink", &Script_GetTradePlayerItemLink },
    { "AcceptTrade",            &Script_AcceptTrade },
    { "CancelTradeAccept",      &Script_CancelTradeAccept },
    { "GetPlayerTradeMoney",    &Script_GetPlayerTradeMoney },
    { "GetTargetTradeMoney",    &Script_GetTargetTradeMoney },
    { "PickupTradeMoney",       &Script_PickupTradeMoney },
    { "AddTradeMoney",          &Script_AddTradeMoney },
    { "SetTradeMoney",          &Script_SetTradeMoney },
};

// ref: FUN_00586010
void TradeGetPlayerItem(uint32_t slot, WOWGUID* item, WOWGUID* container, uint8_t* containerSlot) {
    WOWGUID guid = slot < 7 ? s_playerTradeItems[slot] : 0;

    *item = guid;

    if (guid != 0) {
        *container = s_playerTradeItemContainers[slot];
        *containerSlot = s_playerTradeItemSlots[slot];

        return;
    }

    *container = 0;
    *containerSlot = 0;
}

// ref: FUN_005864d0
int32_t TradeIsPlayerItem(WOWGUID guid) {
    if (s_tradeTarget == 0) {
        return 0;
    }

    for (uint32_t i = 0; i < 7; i++) {
        if (s_playerTradeItems[i] == guid) {
            return 1;
        }
    }

    return 0;
}

void TradeInfoRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
