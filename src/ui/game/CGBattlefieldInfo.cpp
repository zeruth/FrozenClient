#include "ui/game/CGBattlefieldInfo.hpp"

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

void CGBattlefieldInfo::RequestPlayerPositions() {
    // TODO
}
