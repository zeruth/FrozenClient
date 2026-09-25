#include "ui/game/CGBattlefieldInfo.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "object/client/ObjMgr.hpp"
#include <common/DataStore.hpp>

// Written by the battlefield packet handlers, none of which are ported yet; the reference
// zero-initialises all of it.
char CGBattlefieldInfo::s_teamNames[2][0x60];                   // ref: DAT_00bea0b0
WOWGUID CGBattlefieldInfo::s_flagCarriers[2];                   // ref: DAT_00bea170
CGBattlefieldInfo::ScoreEntry* CGBattlefieldInfo::s_scoreList[80]; // ref: DAT_00bea368
CGBattlefieldInfo::QueueSlot CGBattlefieldInfo::s_queueSlots[2]; // ref: DAT_00bea4b8
int32_t CGBattlefieldInfo::s_selectedInstance;                  // ref: DAT_00bea538
uint8_t CGBattlefieldInfo::s_arenaFactionFlag;                  // ref: DAT_00bea54a
int32_t CGBattlefieldInfo::s_mapID;                             // ref: DAT_00bea564
int32_t CGBattlefieldInfo::s_instanceExpireTime;                // ref: DAT_00bea574
int32_t CGBattlefieldInfo::s_instanceStartTime;                 // ref: DAT_00bea578
uint32_t CGBattlefieldInfo::s_scoreListCount;                   // ref: DAT_00bea57c
uint32_t CGBattlefieldInfo::s_numScores;                        // ref: DAT_00bea580
int32_t CGBattlefieldInfo::s_hasWinner;                         // ref: DAT_00bea588
uint32_t CGBattlefieldInfo::s_winner;                           // ref: DAT_00bea58c
int32_t CGBattlefieldInfo::s_teamOldRating[2];                  // ref: DAT_00bea594
int32_t CGBattlefieldInfo::s_teamNewRating[2];                  // ref: DAT_00bea59c
int32_t CGBattlefieldInfo::s_teamRating[2];                     // ref: DAT_00bea5a4
uint32_t CGBattlefieldInfo::s_numVehicles;                      // ref: DAT_00bea5b8
uint32_t CGBattlefieldInfo::s_numInstances;                     // ref: DAT_00bea5c0
int32_t* CGBattlefieldInfo::s_instanceIDs;                      // ref: DAT_00bea5c4
uint32_t CGBattlefieldInfo::s_numPlayerPositions;               // ref: DAT_00bea5b0
// The capacity is the gap to the flag positions, not stated by any function.
C2Vector CGBattlefieldInfo::s_playerPositions[40];              // ref: DAT_00bea5d0
C2Vector CGBattlefieldInfo::s_flagPositions[2];                 // ref: DAT_00bea710
int32_t CGBattlefieldInfo::s_numBattlegrounds;                  // ref: DAT_00bea724
int32_t* CGBattlefieldInfo::s_battlegroundIDs;                  // ref: DAT_00bea728

// ref: FUN_0054a510
// No lower bound: the caller has already checked the index.
int32_t CGBattlefieldInfo::GetBattlegroundID(int32_t index) {
    if (index < CGBattlefieldInfo::s_numBattlegrounds) {
        return CGBattlefieldInfo::s_battlegroundIDs[index];
    }

    return 0;
}

// ref: FUN_005495b0
// The second carrier only counts when it is set, and the first only when the second is not.
WOWGUID CGBattlefieldInfo::GetFlagCarrier(uint32_t index) {
    uint32_t count;

    if (CGBattlefieldInfo::s_flagCarriers[1] == 0) {
        if (CGBattlefieldInfo::s_flagCarriers[0] == 0) {
            return 0;
        }

        count = 1;
    } else {
        count = 2;
    }

    if (index >= count) {
        return 0;
    }

    return CGBattlefieldInfo::s_flagCarriers[index];
}

// ref: FUN_0054a4a0
// Counted by the carriers, as GetFlagCarrier counts them.
int32_t CGBattlefieldInfo::GetFlagPosition(uint32_t index, C3Vector* position) {
    uint32_t count;

    if (CGBattlefieldInfo::s_flagCarriers[1] == 0) {
        if (CGBattlefieldInfo::s_flagCarriers[0] == 0) {
            return 0;
        }

        count = 1;
    } else {
        count = 2;
    }

    if (index >= count) {
        return 0;
    }

    position->x = CGBattlefieldInfo::s_flagPositions[index].x;
    position->y = CGBattlefieldInfo::s_flagPositions[index].y;
    position->z = 0.0f;

    return 1;
}

// ref: FUN_0054a450
int32_t CGBattlefieldInfo::GetPlayerPosition(uint32_t index, C3Vector* position) {
    if (index < CGBattlefieldInfo::s_numPlayerPositions) {
        position->x = CGBattlefieldInfo::s_playerPositions[index].x;
        position->y = CGBattlefieldInfo::s_playerPositions[index].y;
        position->z = 0.0f;

        return 1;
    }

    return 0;
}

void CGBattlefieldInfo::RequestPlayerPositions() {
    // TODO
}

// ref: FUN_0054d4f0
void CGBattlefieldInfo::SendMgrEntryInviteResponse(uint32_t battleId, int32_t accept) {
    if (!ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BATTLEFIELD_MGR_ENTRY_INVITE_RESPONSE));
    msg.Put(battleId);
    msg.Put(static_cast<uint8_t>(accept));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0054d590
void CGBattlefieldInfo::SendMgrQueueRequest(uint32_t battleId) {
    if (!ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BATTLEFIELD_MGR_QUEUE_REQUEST));
    msg.Put(battleId);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0054d630
void CGBattlefieldInfo::SendMgrQueueInviteResponse(uint32_t battleId, int32_t accept) {
    if (!ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BATTLEFIELD_MGR_QUEUE_INVITE_RESPONSE));
    msg.Put(battleId);
    msg.Put(static_cast<uint8_t>(accept));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_0054d6d0
void CGBattlefieldInfo::SendMgrExitRequest(uint32_t battleId) {
    if (!ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__)) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_BATTLEFIELD_MGR_EXIT_REQUEST));
    msg.Put(battleId);
    msg.Finalize();
    ClientServices::Send(&msg);
}
