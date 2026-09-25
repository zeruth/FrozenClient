#include "ui/game/Calendar.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <cstddef>

// The event being viewed. Nothing sets it yet: the calendar code that opens an event is not
// ported, so every reader sees no event.
CalendarEvent* s_calendarEvent;                         // ref: DAT_00c207ec

// Two id lists searched by CalendarFindListIndex. Filled only by code that is not ported yet.
static TSFixedArray<int32_t> s_calendarIDLists[2];      // ref: DAT_00c20f60

// One event the player holds an invite to that the server has not yet settled. Only the event id
// is read by ported code; the rest of the node is not laid out.
struct CalendarPendingInvite {
    TSLink<CalendarPendingInvite> m_link;   // +0x00
    uint64_t m_eventID;                     // +0x08
};

// Filled only by code that is not ported yet.
static STORM_EXPLICIT_LIST(CalendarPendingInvite, m_link) s_calendarPendingInvites; // ref: DAT_00ad0340

// ref: FUN_005b7820
CalendarEventInfo::CalendarEventInfo() {
    this->m_eventID = 0;
    this->m_unk88 = 0;
    this->m_unk8C = 0;
    this->m_unk90 = 0;
    this->m_unk94 = 0;
    this->m_flags = 0;
    this->m_unk9C = 0;
    this->m_unkA0 = -1;
    this->m_unkA4 = 0;
    this->m_unkA8 = 0;
    this->m_inviteID = 0;
    this->m_unkB8 = 0;
    this->m_unkBC = 0;
    this->m_unkC0 = 0;
    this->m_unkC4 = 0;
    this->m_unkC8 = 0;
}

// ref: FUN_005b8d60
CalendarInvite* CalendarEvent::FindInvite(WOWGUID guid) {
    for (uint32_t i = 0; i < this->m_numInvites; i++) {
        if (this->m_invites[i]->m_guid == guid) {
            return this->m_invites[i];
        }
    }

    return nullptr;
}

// ref: FUN_005b8e00
uint32_t CalendarEvent::GetSelectedInviteIndex() {
    for (uint32_t i = 0; i < this->m_numInvites; i++) {
        if (this->m_invites[i]->m_inviteID == this->m_selectedInviteID) {
            return i;
        }
    }

    return 0xFFFFFFFF;
}

// ref: FUN_005b7fd0
const char* CalendarEventTypeName(uint32_t flags) {
    if ((flags & 0x1) == 0) {
        if (flags & 0x40) {
            return "GUILD_ANNOUNCEMENT";
        }

        if (flags & 0x400) {
            return "GUILD_EVENT";
        }

        if (flags & 0x4) {
            return "SYSTEM";
        }

        if (flags & 0x8) {
            return "HOLIDAY";
        }

        if (flags & 0x80) {
            return "RAID_LOCKOUT";
        }

        if (flags & 0x200) {
            return "RAID_RESET";
        }

        if ((flags & 0x2) == 0 && (flags & 0x100) == 0) {
            return "";
        }
    }

    return "PLAYER";
}

// ref: FUN_005b8040
// Where a multi-day event's day falls in its run.
const char* CalendarEventSequenceType(uint8_t flags, int32_t index, uint32_t count) {
    if ((flags & 0x8) == 0 || count < 2) {
        return "";
    }

    if (index == 0) {
        return "START";
    }

    if (static_cast<uint32_t>(index) == count - 1) {
        return "END";
    }

    return "ONGOING";
}

// ref: FUN_005b8080
const char* CalendarModeratorStatus(uint8_t flags) {
    if (flags & 0x4) {
        return "CREATOR";
    }

    const char* status = "MODERATOR";

    if ((flags & 0x2) == 0) {
        status = "";
    }

    return status;
}

// ref: FUN_005b80b0
int32_t CalendarEventIsGuildEventFlag8(const CalendarEventInfo* event) {
    if ((event->m_flags & 0x400) && (event->m_unk9C & 0x8)) {
        return 1;
    }

    return 0;
}

// ref: FUN_005b9700
uint32_t CalendarFindListIndex(int32_t list, int32_t id) {
    int32_t which;

    if (list == 1) {
        which = 0;
    } else {
        if (list != 0) {
            return 0xFFFFFFFF;
        }

        which = 1;
    }

    uint32_t index = 0xFFFFFFFF;

    if (s_calendarIDLists[which].Count() != 0) {
        const int32_t* entry = s_calendarIDLists[which].Ptr();
        index = 0;

        while (id != *entry) {
            index++;
            entry++;

            if (s_calendarIDLists[which].Count() <= index) {
                return 0xFFFFFFFF;
            }
        }
    }

    return index;
}

// ref: FUN_005bd850
bool CalendarEventHasPendingInvite(uint64_t eventID) {
    for (auto invite = s_calendarPendingInvites.Head(); invite; invite = s_calendarPendingInvites.Next(invite)) {
        if (invite->m_eventID == eventID) {
            return true;
        }
    }

    return false;
}
