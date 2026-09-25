#ifndef UI_GAME_TRADE_INFO_SCRIPT_HPP
#define UI_GAME_TRADE_INFO_SCRIPT_HPP

#include "util/GUID.hpp"
#include <cstdint>

void TradeGetPlayerItem(uint32_t slot, WOWGUID* item, WOWGUID* container, uint8_t* containerSlot);

int32_t TradeIsPlayerItem(WOWGUID guid);

void TradeInfoRegisterScriptFunctions();

#endif
