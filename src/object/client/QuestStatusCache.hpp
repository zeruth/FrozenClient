#ifndef OBJECT_CLIENT_QUEST_STATUS_CACHE_HPP
#define OBJECT_CLIENT_QUEST_STATUS_CACHE_HPP

#include "net/Types.hpp"
#include "util/guid/Types.hpp"
#include <cstdint>

class CDataStore;

// Which quest marker, if any, belongs over each questgiver's head.
//
// The server publishes this on its own opcodes rather than in the object update blocks, so without a
// handler the state never reaches the client and no marker can be drawn. Neither opcode was even
// declared in src/net/Types.hpp.
//
// Values are the server's DIALOG_STATUS_* (AzerothCore QuestDef.h).
enum QUEST_DIALOG_STATUS {
    QUEST_DIALOG_NONE                    = 0,
    QUEST_DIALOG_UNAVAILABLE             = 1,
    QUEST_DIALOG_LOW_LEVEL_AVAILABLE     = 2,
    QUEST_DIALOG_LOW_LEVEL_REWARD_REP    = 3,
    QUEST_DIALOG_LOW_LEVEL_AVAILABLE_REP = 4,
    QUEST_DIALOG_INCOMPLETE              = 5,
    QUEST_DIALOG_REWARD_REP              = 6,
    QUEST_DIALOG_AVAILABLE_REP           = 7,
    QUEST_DIALOG_AVAILABLE               = 8,
    QUEST_DIALOG_REWARD2                 = 9,
    QUEST_DIALOG_REWARD                  = 10,
};

// The marker to draw for a unit: 0 none, 1 available ("!"), 2 incomplete ("?" grey),
// 3 complete ("?" yellow). Collapses the ten server statuses onto the three icons the client has.
int32_t QuestStatusIconFor(WOWGUID guid);

void QuestStatusClear();

// Ask the server for the status of every questgiver in range. Nothing arrives unless the client
// asks, so this has to be driven from the world update; it rate-limits itself.
void QuestStatusUpdate(uint32_t nowMs);

void QuestStatusRegisterHandlers();

int32_t ReceiveQuestgiverStatus(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

int32_t ReceiveQuestgiverStatusMultiple(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
