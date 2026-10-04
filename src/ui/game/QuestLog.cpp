#include "ui/game/QuestLog.hpp"
#include "console/Console.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/game/ReputationInfo.hpp"
#include "ui/game/ScriptEvents.hpp"
#include "ui/game/Types.hpp"
#include <storm/String.hpp>

// The quest log as the interface lists it. Written by the quest log rebuild, which is not ported,
// so the log is always empty.
static QuestLogEntry s_questLogEntries[50];     // ref: DAT_00c237b0
static int32_t s_numQuestLogEntries;            // ref: DAT_00c23ad0

// ref: FUN_005dec40
int32_t QuestLogGetQuestID(int32_t index) {
    if (index >= 0 && index < s_numQuestLogEntries && !s_questLogEntries[index].m_unk08) {
        return s_questLogEntries[index].m_questID;
    }

    return 0;
}

// ref: FUN_005dee30
int32_t QuestLogEntryHasField0C(uint32_t index) {
    if (index < static_cast<uint32_t>(s_numQuestLogEntries)
        && !s_questLogEntries[index].m_unk08
        && s_questLogEntries[index].m_unk0C
    ) {
        return 1;
    }

    return 0;
}

// Returns the 1-based line of the quest, or 0. Walks all 50 lines, not only the used ones.
// ref: FUN_005deeb0
int32_t QuestLogGetIndexByID(int32_t questID) {
    for (uint32_t i = 0; i < 50; i++) {
        if (!s_questLogEntries[i].m_unk08 && s_questLogEntries[i].m_questID == questID) {
            return i + 1;
        }
    }

    return 0;
}

uint32_t QuestLogGetNumEntries() {
    return static_cast<uint32_t>(s_numQuestLogEntries);
}

namespace {

// The player's quest log fields for the quest on line `index`, or null for a header or a line the
// log does not have.
CQuestLogData* QuestLogFields(CGPlayer_C* player, uint32_t index) {
    if (index >= static_cast<uint32_t>(s_numQuestLogEntries) || s_questLogEntries[index].m_unk08) {
        return nullptr;
    }

    return player->GetQuestLog(s_questLogEntries[index].m_slot);
}

// ref: FUN_005de930
void QuestLogOnQuestArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (!found) {
        ConsoleWrite("Invalid quest log entry", DEFAULT_COLOR);
        return;
    }

    ScriptEventsQueueUnitEvent(0, SCRIPT_QUEST_LOG_UPDATE);
}

// ref: FUN_005dea10
void QuestLogOnGameObjectArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (!found) {
        ConsoleWrite("Invalid object in quest", DEFAULT_COLOR);
        return;
    }

    ScriptEventsQueueUnitEvent(0, SCRIPT_QUEST_LOG_UPDATE);
}

// ref: FUN_005de9e0
void QuestLogOnCreatureArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (!found) {
        ConsoleWrite("Invalid creature in quest", DEFAULT_COLOR);
        return;
    }

    ScriptEventsQueueUnitEvent(0, SCRIPT_QUEST_LOG_UPDATE);
}

// ref: FUN_005dea40
void QuestLogOnItemArrived(uint32_t id, const WOWGUID* guid, void* param, bool found) {
    if (found) {
        ScriptEventsQueueUnitEvent(0, SCRIPT_QUEST_LOG_UPDATE);
    }
}

}

// ref: FUN_005dfd70
QUESTOBJECTIVEINFO::QUESTOBJECTIVEINFO()
    : m_questCallback(&QuestLogOnQuestArrived)
    , m_gameObjectCallback(&QuestLogOnGameObjectArrived)
    , m_creatureCallback(&QuestLogOnCreatureArrived)
    , m_itemCallback(&QuestLogOnItemArrived) {
}

// ref: FUN_005e0ea0
bool QuestLogIsComplete(uint32_t index, bool needObjectives) {
    if (index >= static_cast<uint32_t>(s_numQuestLogEntries) || s_questLogEntries[index].m_unk08) {
        return false;
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        return false;
    }

    auto fields = player->GetQuestLog(s_questLogEntries[index].m_slot);

    WOWGUID none = 0;
    auto quest = g_questCache.GetRecord(DBCACHEKEY32(s_questLogEntries[index].m_questID), &none,
                                        &QuestLogOnQuestArrived, nullptr, false);

    if (!quest || !fields || (fields->state & 0x2) || QuestLogEntryHasField0C(index)) {
        return false;
    }

    // Flags 0x2 and 0x4 are the quest asking to be explored or an event finished, which the server
    // reports through the state's complete bit rather than through counts.
    auto special = quest->m_flags & 0x6;

    if (!special && !quest->m_repObjectiveFaction && !quest->m_repObjectiveFaction2 && !quest->m_rewOrReqMoney) {
        bool objectives = false;

        for (auto id : quest->m_reqCreatureOrGOID) {
            if (id) {
                objectives = true;
                break;
            }
        }

        for (auto id : quest->m_reqItemID) {
            if (id) {
                objectives = true;
                break;
            }
        }

        if (needObjectives && !objectives && !quest->m_playersSlain) {
            return false;
        }
    }

    if (special && !(fields->state & 0x1)) {
        return false;
    }

    if (quest->m_repObjectiveFaction && ReputationGetStanding(quest->m_repObjectiveFaction) < quest->m_repObjectiveValue) {
        return false;
    }

    if (quest->m_repObjectiveFaction2 && ReputationGetStanding(quest->m_repObjectiveFaction2) > quest->m_repObjectiveValue2) {
        return false;
    }

    if (quest->m_rewOrReqMoney < 0 && player->GetMoney() < static_cast<uint32_t>(-quest->m_rewOrReqMoney)) {
        return false;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (quest->m_reqCreatureOrGOID[i]
            && static_cast<uint32_t>(fields->counts[i]) < static_cast<uint32_t>(quest->m_reqCreatureOrGOCount[i])) {
            return false;
        }
    }

    for (int32_t i = 0; i < 6; i++) {
        if (quest->m_reqItemID[i] && player->m_bag.CountItem(quest->m_reqItemID[i], 8) < quest->m_reqItemCount[i]) {
            return false;
        }
    }

    // The players slain are counted in the first creature slot.
    return static_cast<int32_t>(fields->counts[0]) >= quest->m_playersSlain;
}

// ref: FUN_005e2370
bool QuestLogGetCreatureObjective(int32_t questID, int32_t id, QUESTOBJECTIVEINFO* info) {
    if (!info) {
        return false;
    }

    info->m_text[0] = '\0';
    info->m_type[0] = '\0';
    info->m_complete = 0;

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        return false;
    }

    WOWGUID none = 0;
    auto quest = g_questCache.GetRecord(DBCACHEKEY32(questID), &none, info->m_questCallback, info->m_param, false);
    auto fields = QuestLogFields(player, QuestLogGetIndexByID(questID) - 1);

    if (!quest || !fields) {
        return false;
    }

    for (int32_t i = 0; i < 4; i++) {
        if (quest->m_reqCreatureOrGOID[i] != id) {
            continue;
        }

        auto count = static_cast<uint32_t>(fields->counts[i]);
        auto needed = quest->m_reqCreatureOrGOCount[i];
        auto text = quest->m_objectiveText[i];
        char line[0x1000];

        if (text[0]) {
            // The quest's own wording for the objective.
            SStrPrintf(line, sizeof(line), FrameScript_GetText("QUEST_OBJECTS_FOUND", -1, GENDER_NOT_APPLICABLE),
                       text, count, needed);
        } else if (id < 0) {
            WOWGUID requester = 0;
            auto object = g_gameObjectCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(id) & 0x7FFFFFFF), &requester,
                                                      info->m_gameObjectCallback, info->m_param, false);
            auto plural = FrameScript_GetPluralIndex(needed);
            auto format = FrameScript_GetText("QUEST_OBJECTS_FOUND", -1, GENDER_NOT_APPLICABLE);

            // FUN_004fd1e0: the plural name, or the first name when that form is empty.
            const char* name = " ";

            if (object) {
                name = object->m_names[plural] && object->m_names[plural][0] ? object->m_names[plural] : object->m_names[0];
            }

            SStrPrintf(line, sizeof(line), format, name, count, needed);
        } else {
            WOWGUID requester = 0;
            auto creature = g_creatureCache.GetRecord(DBCACHEKEY32(id), &requester, info->m_creatureCallback,
                                                      info->m_param, false);
            auto plural = FrameScript_GetPluralIndex(needed);
            auto format = FrameScript_GetText("QUEST_MONSTERS_KILLED", -1, GENDER_NOT_APPLICABLE);

            // FUN_0053b8e0
            const char* name = " ";

            if (creature) {
                name = creature->m_names[plural] && creature->m_names[plural][0] ? creature->m_names[plural] : creature->m_names[0];
            }

            SStrPrintf(line, sizeof(line), format, name, count, needed);
        }

        info->m_complete = needed <= static_cast<int32_t>(count);

        SStrCopy(info->m_text, line, STORM_MAX_STR);
        SStrCopy(info->m_type, (static_cast<uint32_t>(quest->m_reqCreatureOrGOID[i]) & 0x80000000) ? "object" : "monster",
                 STORM_MAX_STR);

        return true;
    }

    return false;
}

// ref: FUN_005e2630
bool QuestLogGetItemObjective(int32_t questID, int32_t itemID, QUESTOBJECTIVEINFO* info) {
    if (!info) {
        return false;
    }

    info->m_text[0] = '\0';
    info->m_type[0] = '\0';
    info->m_complete = 0;

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        return false;
    }

    WOWGUID none = 0;
    auto quest = g_questCache.GetRecord(DBCACHEKEY32(questID), &none, info->m_questCallback, info->m_param, false);
    auto fields = QuestLogFields(player, QuestLogGetIndexByID(questID) - 1);

    if (!quest || !fields) {
        return false;
    }

    for (int32_t i = 0; i < 6; i++) {
        auto id = quest->m_reqItemID[i];

        if (id != itemID || (id == quest->m_srcItemID && quest->m_reqItemCount[i] <= 1)) {
            continue;
        }

        WOWGUID requester = 0;
        auto item = g_itemCache.GetRecord(DBCACHEKEY32(id), &requester, info->m_itemCallback, info->m_param, false);
        auto needed = quest->m_reqItemCount[i];
        auto plural = FrameScript_GetPluralIndex(needed);

        auto count = needed;
        auto carried = player->m_bag.CountItem(id, 8);

        if (carried < count) {
            count = player->m_bag.CountItem(id, 8);
        }

        auto format = FrameScript_GetText("QUEST_ITEMS_NEEDED", -1, GENDER_NOT_APPLICABLE);

        // FUN_004fd200: the plural name, or the first name when that form is empty.
        const char* name = " ";

        if (item) {
            name = !item->names[plural].empty() ? item->names[plural].c_str() : item->names[0].c_str();
        }

        char line[0x1000];
        SStrPrintf(line, sizeof(line), format, name, count, needed);

        info->m_complete = needed <= count;

        SStrCopy(info->m_text, line, STORM_MAX_STR);
        SStrCopy(info->m_type, "item", STORM_MAX_STR);

        return true;
    }

    return false;
}
