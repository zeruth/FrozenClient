#ifndef OBJECT_CLIENT_SPELL_HISTORY_HPP
#define OBJECT_CLIENT_SPELL_HISTORY_HPP

#include "net/Types.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <cstdint>

class CDataStore;
class CGItem_C;
class SpellRec;

// One spell or item cooldown (RTTI SPELLHISTORY, 0x30 bytes): the spell, the item it was used
// from, its own cooldown, its category's, whether it is held until an event ends it, and the
// global cooldown it started.
struct SPELLHISTORY {
    TSLink<SPELLHISTORY> m_link;    // +0x00
    int32_t spellID;                // +0x08
    int32_t itemID;                 // +0x0c
    int32_t start;                  // +0x10
    int32_t duration;               // +0x14
    int32_t category;               // +0x18
    int32_t categoryStart;          // +0x1c
    int32_t categoryDuration;       // +0x20
    bool onHold;                    // +0x24
    int32_t gcdCategory;            // +0x28
    int32_t gcdDuration;            // +0x2c
};

// The cooldowns of the player (index 0) or their pet (index 1), with the entries that have ended
// kept for reuse (0x18 bytes each, at 0x00d3f5ac).
class SpellHistoryList {
    public:
        STORM_EXPLICIT_LIST(SPELLHISTORY, m_link) m_active;    // +0x00
        STORM_EXPLICIT_LIST(SPELLHISTORY, m_link) m_free;      // +0x0c

        void Add(int32_t spellID, int32_t itemID, int32_t start, int32_t duration, int32_t category, int32_t categoryStart, int32_t categoryDuration, bool onHold, int32_t gcdCategory, int32_t gcdDuration);
        void Release(int32_t spellID, int32_t time, bool remove);
        void Shift(int32_t spellID, int32_t delta);
        void Clear();
        void Expire(int32_t time);
        void ClearGlobalCooldown(int32_t category);
        bool GetCooldown(int32_t spellID, int32_t itemID, int32_t* duration, int32_t* start, uint32_t* enabled);
        bool IsOnHold(int32_t spellID, int32_t itemID);
        bool IsRecent(int32_t spellID, int32_t itemID);
        void AddServerCooldown(int32_t spellID, int32_t itemID, int32_t category, int32_t cooldown, int32_t categoryCooldown, bool onHold, int32_t pet);
        // A retired entry reused, or a new one, linked at the tail.
        SPELLHISTORY* NewEntry();
};

extern SpellHistoryList g_spellHistory[2];

// The cooldown of a spell on the player's (0) or pet's (1) list.
bool SpellGetCooldown(int32_t spellID, int32_t pet, int32_t* duration, int32_t* start, uint32_t* enabled);

// The cooldown of the spell an item casts when used.
bool ItemGetCooldown(CGItem_C* item, int32_t* duration, int32_t* start, uint32_t* enabled);

// The cooldown of an item's first "on use" spell, by item id.
bool ItemIDGetCooldown(int32_t itemID, int32_t* duration, int32_t* start, uint32_t* enabled);

// Whether the spell is held on cooldown until an event ends it.
bool SpellIsCooldownHeld(const SpellRec* spell, WOWGUID caster);

// SPELL_UPDATE_COOLDOWN and its siblings.
void SpellSignalCooldownUpdate();

// SPELL_UPDATE_USABLE and its siblings.
void SpellSignalUsableUpdate();

int32_t SpellCooldownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ItemCooldownHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t CooldownEventHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t CooldownCheatHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ResetRangedCombatTimerHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

void SpellHistoryRegisterHandlers();

#endif
