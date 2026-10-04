#include "ui/game/DuelInfo.hpp"
#include "client/ClientServices.hpp"
#include "object/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/CGGameUI.hpp"
#include <common/DataStore.hpp>

static WOWGUID s_duelArbiter;                       // ref: DAT_00c22b58
static WOWGUID s_duelOpponent;                      // ref: DAT_00c22b60
static uint32_t s_duelCountdown;                    // ref: DAT_00c22b68

static const int32_t SCRIPT_DUEL_REQUESTED = 369;
static const int32_t SCRIPT_DUEL_OUTOFBOUNDS = 370;
static const int32_t SCRIPT_DUEL_INBOUNDS = 371;
static const int32_t SCRIPT_DUEL_FINISHED = 372;

WOWGUID DuelInfoGetOpponent() {
    return s_duelOpponent;
}

// ref: FUN_005cfbd0
void DuelInfoSendAccept() {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_DUEL_ACCEPTED));
    msg.Put(s_duelArbiter);
    msg.Finalize();

    ClientServices::Send(&msg);
}

// ref: FUN_005cfc50
void DuelInfoSendCancel() {
    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_DUEL_CANCELLED));
    msg.Put(s_duelArbiter);
    msg.Finalize();

    ClientServices::Send(&msg);
}

// ref: FUN_005cfcd0
// SMSG_DUEL_REQUESTED: the duel flag and who threw it. One the player threw is accepted at once.
// PARTIAL: the reference declines a challenge from an ignored player (FUN_006b52e0, the friend
// list's ignore entries); frozen keeps no friend list yet, so every challenge is put to the player.
int32_t DuelRequestedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WOWGUID challenger = 0;
    msg->Get(s_duelArbiter);
    msg->Get(challenger);

    if (ClntObjMgrGetActivePlayer() == challenger) {
        CGGameUI::DisplayError(0x148);
        DuelInfoSendAccept();

        return 1;
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(challenger, TYPE_PLAYER, ".\\DuelInfo.cpp", 0x81));

    if (player) {
        FrameScript_SignalEvent(SCRIPT_DUEL_REQUESTED, "%s", player->GetUnitName(nullptr, 1));
    }

    s_duelOpponent = challenger;

    return 1;
}

// ref: FUN_005cf910
int32_t DuelOutOfBoundsHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    FrameScript_SignalEvent(SCRIPT_DUEL_OUTOFBOUNDS, nullptr);

    return 1;
}

// ref: FUN_005cf930
int32_t DuelInBoundsHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    FrameScript_SignalEvent(SCRIPT_DUEL_INBOUNDS, nullptr);

    return 1;
}

// ref: FUN_005cfa50
// SMSG_DUEL_COUNTDOWN: the countdown in milliseconds.
// PARTIAL: the reference then prints each remaining second to the chat frame, once a second
// (FUN_005cf870 through a timer); frozen has no chat frame message path yet.
int32_t DuelCountdownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t milliseconds = 0;
    msg->Get(milliseconds);

    s_duelCountdown = milliseconds / 1000;

    return 1;
}

// ref: FUN_005cfa90
// SMSG_DUEL_COMPLETE: whether it was fought out; one called off before it began is an error.
int32_t DuelCompleteHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t completed = 0;
    msg->Get(completed);

    if (s_duelArbiter != 0) {
        if (!completed) {
            CGGameUI::DisplayError(0x149);
        }

        s_duelArbiter = 0;
        FrameScript_SignalEvent(SCRIPT_DUEL_FINISHED, nullptr);
    }


    s_duelOpponent = 0;

    return 1;
}

// ref: FUN_005cfb20
// SMSG_DUEL_WINNER: how it ended, the winner and the loser.
// PARTIAL: the reference prints DUEL_WINNER_KNOCKOUT / DUEL_WINNER_RETREAT to the chat frame
// (FUN_00509dd0); frozen has no chat frame message path yet, so the message is read and dropped.
int32_t DuelWinnerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t retreat = 0;
    char winner[48];
    char loser[48];

    msg->Get(retreat);
    msg->GetString(winner, sizeof(winner));
    msg->GetString(loser, sizeof(loser));

    return 1;
}

// ref: FUN_005cfdd0
void DuelInfoRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_DUEL_REQUESTED, &DuelRequestedHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_DUEL_OUT_OF_BOUNDS, &DuelOutOfBoundsHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_DUEL_IN_BOUNDS, &DuelInBoundsHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_DUEL_COUNTDOWN, &DuelCountdownHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_DUEL_COMPLETE, &DuelCompleteHandler, nullptr);
    ClientServices::SetMessageHandler(SMSG_DUEL_WINNER, &DuelWinnerHandler, nullptr);
}

// ref: FUN_005cfdb0
int32_t Script_AcceptDuel(lua_State* L) {
    DuelInfoSendAccept();

    return 0;
}

// ref: FUN_005cfdc0
int32_t Script_CancelDuel(lua_State* L) {
    DuelInfoSendCancel();

    return 0;
}
