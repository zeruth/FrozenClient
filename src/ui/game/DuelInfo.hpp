#ifndef UI_GAME_DUEL_INFO_HPP
#define UI_GAME_DUEL_INFO_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <cstdint>

class CDataStore;

// The reference's DuelInfo.cpp: the duel the player is asked to, or is in.

// The player the active player is dueling, 0 when none.
WOWGUID DuelInfoGetOpponent();

void DuelInfoRegisterHandlers();

int32_t DuelRequestedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t DuelOutOfBoundsHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t DuelInBoundsHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t DuelCountdownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t DuelCompleteHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t DuelWinnerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

void DuelInfoSendAccept();
void DuelInfoSendCancel();

struct lua_State;
int32_t Script_AcceptDuel(lua_State* L);
int32_t Script_CancelDuel(lua_State* L);

#endif
