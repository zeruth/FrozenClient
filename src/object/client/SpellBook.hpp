#ifndef OBJECT_CLIENT_SPELL_BOOK_HPP
#define OBJECT_CLIENT_SPELL_BOOK_HPP

#include "net/Types.hpp"
#include <cstdint>

class CDataStore;

// The player's known spells.
//
// The server sends the full list once on login (SMSG_INITIAL_SPELLS) and then one id at a time as
// spells are learned or removed. Without a handler the spellbook stayed empty and every spellbook
// query answered from a stub.
//
// Two views are exposed, matching the reference's spellbook: every known spell, and only the highest
// known rank of each. FrameXML's GetSpellTabInfo returns an offset and count for each view, and slot
// numbers passed back in are relative to the "all" view. The highest-rank view is laid out AFTER the
// all view in slot space, so a slot number never means two things.

// Spellbook tabs. Tab 0 is General; the rest are the class skill lines (SkillLine.dbc category 7)
// the player knows at least one spell in, in skill line order -- Blood, Frost, Unholy for a death
// knight. Each tab is a contiguous run of slots in both views.
struct SpellBookTab {
    const char* name;
    int32_t iconID;        // SpellIcon.dbc id, 0 for General
    int32_t offset;        // first slot of this tab in the all-ranks view
    int32_t count;
    int32_t highestOffset; // first slot of this tab in the highest-rank view
    int32_t highestCount;
};

int32_t SpellBookTabCount();
const SpellBookTab* SpellBookTabAt(int32_t index);

// Number of spells in the all-ranks view.
int32_t SpellBookCount();

// Number of spells in the highest-rank-only view.
int32_t SpellBookHighestRankCount();

// Spell id at a 0-based slot, or 0. Slots >= SpellBookCount() index the highest-rank view.
uint32_t SpellBookSpellAt(int32_t slot);

// Map a slot in the highest-rank view back to the same spell's slot in the all-ranks view.
int32_t SpellBookKnownSlotFromHighestRankSlot(int32_t slot);

bool SpellBookKnows(uint32_t spellId);

// Companion type of a spell: 0 CRITTER (its first effect is SUMMON), 1 MOUNT (its first aura is
// MOUNTED), 2 neither.
class SpellRec;
uint8_t SpellCompanionType(const SpellRec* spell);

// Send CMSG_CAST_SPELL for a spell id. A zero target casts on self / no target.
void SpellBookCast(uint32_t spellId, uint64_t target);

void SpellBookClear();

void SpellBookRegisterHandlers();

int32_t ReceiveInitialSpells(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveLearnedSpell(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);
int32_t ReceiveRemovedSpell(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
