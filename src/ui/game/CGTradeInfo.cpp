#include "ui/game/CGTradeInfo.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include <common/DataStore.hpp>

uint32_t CGTradeInfo::s_playerMoney;        // ref: DAT_00ca0b74
WOWGUID CGTradeInfo::s_tradingPlayer;
uint32_t CGTradeInfo::s_tradeUpdateCount;   // ref: DAT_00ca0b7c

// ref: FUN_00704040
void CGTradeInfo::CancelTrade() {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CANCEL_TRADE));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_00703cb0
uint32_t CGTradeInfo::GetPlayerTradeMoney() {
    return CGTradeInfo::s_playerMoney;
}

WOWGUID CGTradeInfo::GetTradePartner() {
    return CGTradeInfo::s_tradingPlayer;
}

// ref: FUN_007043c0
int32_t CGTradeInfo::SetPlayerTradeMoney(uint32_t money) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player || money > player->GetMoney()) {
        return 0;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SET_TRADE_GOLD));
    msg.Put(money);
    CGTradeInfo::s_tradeUpdateCount++;
    msg.Finalize();
    CGTradeInfo::s_playerMoney = money;
    ClientServices::Send(&msg);

    return 1;
}

// ref: FUN_007041a0
void CGTradeInfo::ClearTradeItem(uint8_t slot) {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CLEAR_TRADE_ITEM));
    msg.Put(slot);
    CGTradeInfo::s_tradeUpdateCount++;
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_00704220
int32_t CGTradeInfo::AddPlayerTradeMoney(uint32_t money) {
    if (!money) {
        return 1;
    }

    uint32_t total = CGTradeInfo::s_playerMoney + money;
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player || total > player->GetMoney()) {
        return 0;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SET_TRADE_GOLD));
    msg.Put(total);
    CGTradeInfo::s_tradeUpdateCount++;
    msg.Finalize();
    CGTradeInfo::s_playerMoney = total;
    ClientServices::Send(&msg);

    return 1;
}

// ref: FUN_00704320
void CGTradeInfo::SubtractPlayerTradeMoney(uint32_t money) {
    if (!money || money > CGTradeInfo::s_playerMoney) {
        return;
    }

    uint32_t total = CGTradeInfo::s_playerMoney - money;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SET_TRADE_GOLD));
    msg.Put(total);
    CGTradeInfo::s_tradeUpdateCount++;
    msg.Finalize();
    CGTradeInfo::s_playerMoney = total;
    ClientServices::Send(&msg);
}
