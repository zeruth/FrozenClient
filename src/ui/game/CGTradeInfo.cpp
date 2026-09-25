#include "ui/game/CGTradeInfo.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include <common/DataStore.hpp>

uint32_t CGTradeInfo::s_playerMoney;        // ref: DAT_00ca0b74
WOWGUID CGTradeInfo::s_tradingPlayer;
uint32_t CGTradeInfo::s_tradeUpdateCount;   // ref: DAT_00ca0b7c

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
