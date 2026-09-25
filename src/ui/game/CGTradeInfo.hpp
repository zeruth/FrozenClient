#ifndef UI_GAME_C_G_TRADE_INFO_HPP
#define UI_GAME_C_G_TRADE_INFO_HPP

#include "util/GUID.hpp"
#include <cstdint>

class CGTradeInfo {
    public:
        // Public static functions
        static uint32_t GetPlayerTradeMoney();
        static WOWGUID GetTradePartner();
        static int32_t SetPlayerTradeMoney(uint32_t money);

    private:
        // Private static variables
        static uint32_t s_playerMoney;
        static WOWGUID s_tradingPlayer;
        static uint32_t s_tradeUpdateCount;
};

#endif
