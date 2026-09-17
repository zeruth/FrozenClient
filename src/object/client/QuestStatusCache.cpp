#include "object/client/QuestStatusCache.hpp"
#include "client/ClientServices.hpp"
#include <common/DataStore.hpp>
#include <map>

namespace {

std::map<WOWGUID, uint8_t> s_status;



// The client ships three overhead marker textures, and the server has ten dialog statuses. Map them
// the way the reference does: anything that means "has a quest to give" shows the "!", a quest in
// progress shows the grey "?", and one ready to hand in shows the yellow "?".
int32_t IconFor(uint8_t status) {
    switch (status) {
        case QUEST_DIALOG_LOW_LEVEL_AVAILABLE:
        case QUEST_DIALOG_LOW_LEVEL_AVAILABLE_REP:
        case QUEST_DIALOG_AVAILABLE_REP:
        case QUEST_DIALOG_AVAILABLE:
            return 1;

        case QUEST_DIALOG_INCOMPLETE:
            return 2;

        case QUEST_DIALOG_LOW_LEVEL_REWARD_REP:
        case QUEST_DIALOG_REWARD_REP:
        case QUEST_DIALOG_REWARD2:
        case QUEST_DIALOG_REWARD:
            return 3;

        default:
            return 0;
    }
}

void Store(WOWGUID guid, uint8_t status) {
    if (!guid) {
        return;
    }

    // NONE and UNAVAILABLE both mean "no marker": drop the entry rather than keep a zero, so the
    // map does not grow by one for every creature the player ever walks past.
    if (status == QUEST_DIALOG_NONE || status == QUEST_DIALOG_UNAVAILABLE) {
        s_status.erase(guid);

        return;
    }

    s_status[guid] = status;
}

} // namespace

// Next time the multiple-status query may go out, on the OsGetAsyncTimeMs clock.
static uint32_t s_nextQueryMs = 0;

int32_t QuestStatusIconFor(WOWGUID guid) {
    auto it = s_status.find(guid);

    return it == s_status.end() ? 0 : IconFor(it->second);
}

void QuestStatusClear() {
    s_status.clear();
    s_nextQueryMs = 0;
}

// The reference polls this about once a second while in the world; the reply is the only source of
// questgiver status, so the markers appear a beat after a questgiver comes into range.
void QuestStatusUpdate(uint32_t nowMs) {
    if (nowMs < s_nextQueryMs) {
        return;
    }

    s_nextQueryMs = nowMs + 1000;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_QUESTGIVER_STATUS_MULTIPLE_QUERY));
    msg.Finalize();
    ClientServices::Send(&msg);
}

// SMSG_QUESTGIVER_STATUS: uint64 guid, uint8 status.
int32_t ReceiveQuestgiverStatus(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg || msg->Tell() + 9 > msg->Size()) {
        return 1;
    }

    uint64_t guid = 0;
    uint8_t status = 0;

    msg->Get(guid);
    msg->Get(status);

    Store(guid, status);

    return 1;
}

// SMSG_QUESTGIVER_STATUS_MULTIPLE: uint32 count, then count x { uint64 guid, uint8 status }.
//
// The count is written into the packet after the fact by the server (a placeholder patched at the
// end), so it is authoritative -- but the length is still checked per record, because a truncated
// packet would otherwise read past the end.
int32_t ReceiveQuestgiverStatusMultiple(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg || msg->Tell() + 4 > msg->Size()) {
        return 1;
    }

    uint32_t count = 0;
    msg->Get(count);

    for (uint32_t i = 0; i < count; i++) {
        if (msg->Tell() + 9 > msg->Size()) {
            break;
        }

        uint64_t guid = 0;
        uint8_t status = 0;

        msg->Get(guid);
        msg->Get(status);

        Store(guid, status);
    }

    return 1;
}

void QuestStatusRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_QUESTGIVER_STATUS, &ReceiveQuestgiverStatus, nullptr);
    ClientServices::SetMessageHandler(SMSG_QUESTGIVER_STATUS_MULTIPLE, &ReceiveQuestgiverStatusMultiple, nullptr);
}
