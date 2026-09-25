#ifndef UI_GAME_CALENDAR_HPP
#define UI_GAME_CALENDAR_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

// The reference's Calendar.cpp. Only the pieces its ported functions touch are laid out; the
// offsets in the comments are the reference's (32-bit) ones.

// One event in the calendar's day lists (0xd0 bytes, allocated by FUN_005c23c0).
struct CalendarEventInfo {
    uint64_t m_eventID;                 // +0x00
    uint8_t m_unk08[0x80];              // +0x08, left untouched by the constructor
    uint32_t m_unk88;
    uint32_t m_unk8C;
    uint32_t m_unk90;
    uint32_t m_unk94;
    uint32_t m_flags;                   // +0x98
    uint32_t m_unk9C;
    int32_t m_unkA0;
    uint32_t m_unkA4;
    uint8_t m_unkA8;
    uint64_t m_inviteID;                // +0xb0
    uint32_t m_unkB8;
    uint32_t m_unkBC;
    uint32_t m_unkC0;
    uint32_t m_unkC4;
    uint32_t m_unkC8;

    CalendarEventInfo();
};

// One invite of the event being viewed.
struct CalendarInvite {
    uint64_t m_inviteID;                // +0x00
    WOWGUID m_guid;                     // +0x08
};

struct CalendarInviteSortCriterion {
    int32_t m_criterion;                // name, level, class, status, party, notes
    uint8_t m_reverse;
};

// The event being viewed or edited (DAT_00c207ec points at it).
struct CalendarEvent {
    uint8_t m_unk00[0x10];
    CalendarInvite** m_invites;         // +0x10
    uint32_t m_unk14;
    uint32_t m_numInvites;              // +0x18
    uint32_t m_unk1C;
    uint64_t m_selectedInviteID;        // +0x20
    uint8_t m_unk28[0x501 - 0x28];
    uint8_t m_settingsChanged;          // +0x501
    uint8_t m_unk502[0x514 - 0x502];
    CalendarInviteSortCriterion m_sortCriteria[6]; // +0x514
    uint32_t m_sortCriterion;           // +0x544, index into m_sortCriteria

    CalendarInvite* FindInvite(WOWGUID guid);
    uint32_t GetSelectedInviteIndex();
};

extern CalendarEvent* s_calendarEvent;

const char* CalendarEventTypeName(uint32_t flags);
const char* CalendarEventSequenceType(uint8_t flags, int32_t index, uint32_t count);
const char* CalendarModeratorStatus(uint8_t flags);
int32_t CalendarEventIsGuildEventFlag8(const CalendarEventInfo* event);
uint32_t CalendarFindListIndex(int32_t list, int32_t id);
bool CalendarEventHasPendingInvite(uint64_t eventID);

#endif
