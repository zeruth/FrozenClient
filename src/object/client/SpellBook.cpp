#include "object/client/SpellBook.hpp"
#include "client/ClientServices.hpp"
#include "db/Db.hpp"
#include "ui/FrameScript.hpp"
#include <common/DataStore.hpp>
#include <storm/String.hpp>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace {

// All known spell ids, sorted by id so the book is stable between refreshes.
std::vector<uint32_t> s_known;

// The highest-rank view: one id per distinct spell name, the highest rank the player knows.
std::vector<uint32_t> s_highest;

// Index into s_known for each entry of s_highest, so a slot in the highest-rank view maps back.
std::vector<int32_t> s_highestToKnown;

std::vector<SpellBookTab> s_tabs;

// The class skill line a spell is filed under, or 0 when it has none (which puts it on General).
// SkillLineAbility has no index by spell, so this is a scan; it runs once per rebuild.
int32_t ClassSkillLineOf(uint32_t spellId) {
    for (int32_t i = 0; i < g_skillLineAbilityDB.GetNumRecords(); i++) {
        auto ability = g_skillLineAbilityDB.GetRecordByIndex(i);

        if (!ability || static_cast<uint32_t>(ability->m_spell) != spellId) {
            continue;
        }

        auto line = g_skillLineDB.GetRecord(ability->m_skillLine);

        if (line && line->m_categoryID == SkillLineRec::CATEGORY_CLASS) {
            return line->m_ID;
        }
    }

    return 0;
}

// Spell.dbc rank strings are "Rank N" (or empty for an unranked spell). Anything that does not parse
// counts as rank 0 so it still groups with its name.
int32_t RankOf(const SpellRec* spell) {
    if (!spell || !spell->m_rank || !*spell->m_rank) {
        return 0;
    }

    const char* s = spell->m_rank;

    while (*s && (*s < '0' || *s > '9')) {
        s++;
    }

    return static_cast<int32_t>(strtol(s, nullptr, 10));
}

// Rebuild the highest-rank view from the all view, then tell the interface the book changed.
void Rebuild() {
    std::sort(s_known.begin(), s_known.end());
    s_known.erase(std::unique(s_known.begin(), s_known.end()), s_known.end());

    // Group the book by skill line: General first, then each class line in id order. The all-ranks
    // view is reordered in place so every tab is one contiguous run of slots.
    std::vector<int32_t> lineOf(s_known.size());
    std::vector<int32_t> lines;

    for (size_t i = 0; i < s_known.size(); i++) {
        lineOf[i] = ClassSkillLineOf(s_known[i]);

        if (lineOf[i] && std::find(lines.begin(), lines.end(), lineOf[i]) == lines.end()) {
            lines.push_back(lineOf[i]);
        }
    }

    std::sort(lines.begin(), lines.end());
    lines.insert(lines.begin(), 0);

    std::vector<uint32_t> ordered;
    s_tabs.clear();

    for (int32_t line : lines) {
        SpellBookTab tab = {};
        tab.offset = static_cast<int32_t>(ordered.size());

        for (size_t i = 0; i < s_known.size(); i++) {
            if (lineOf[i] == line) {
                ordered.push_back(s_known[i]);
            }
        }

        tab.count = static_cast<int32_t>(ordered.size()) - tab.offset;

        if (line) {
            auto rec = g_skillLineDB.GetRecord(line);
            tab.name = rec && rec->m_displayName ? rec->m_displayName : "";
            tab.iconID = rec ? rec->m_spellIconID : 0;
        } else {
            tab.name = "General";
            tab.iconID = 0;
        }

        s_tabs.push_back(tab);
    }

    s_known.swap(ordered);

    s_highest.clear();
    s_highestToKnown.clear();

    // For each spell, keep it only if no other known spell shares its name with a higher rank. The
    // list is short (a few hundred at most) so the quadratic scan is fine and needs no map.
    for (int32_t i = 0; i < static_cast<int32_t>(s_known.size()); i++) {
        auto spell = g_spellDB.GetRecord(static_cast<int32_t>(s_known[i]));

        if (!spell || !spell->m_name || !*spell->m_name) {
            continue;
        }

        int32_t rank = RankOf(spell);
        bool best = true;

        for (int32_t j = 0; j < static_cast<int32_t>(s_known.size()); j++) {
            if (j == i) {
                continue;
            }

            auto other = g_spellDB.GetRecord(static_cast<int32_t>(s_known[j]));

            if (!other || !other->m_name || SStrCmp(other->m_name, spell->m_name, STORM_MAX_STR)) {
                continue;
            }

            int32_t otherRank = RankOf(other);

            // A strictly higher rank wins; on a tie the higher id does, so exactly one survives.
            if (otherRank > rank || (otherRank == rank && s_known[j] > s_known[i])) {
                best = false;
                break;
            }
        }

        if (best) {
            s_highest.push_back(s_known[i]);
            s_highestToKnown.push_back(i);
        }
    }

    for (auto& tab : s_tabs) {
        tab.highestOffset = 0;
        tab.highestCount = 0;
        bool started = false;

        for (size_t i = 0; i < s_highestToKnown.size(); i++) {
            int32_t known = s_highestToKnown[i];

            if (known >= tab.offset && known < tab.offset + tab.count) {
                if (!started) {
                    tab.highestOffset = static_cast<int32_t>(i);
                    started = true;
                }

                tab.highestCount++;
            }
        }
    }

    // Both spellbook and action bar re-read their contents on this.
    FrameScript_SignalEvent(242, nullptr); // SPELLS_CHANGED
}

// One packed GUID: a mask byte naming the non-zero bytes, then only those bytes.
void PutPackedGuid(CDataStore& msg, uint64_t guid) {
    uint8_t mask = 0;
    uint8_t bytes[8];
    int32_t count = 0;

    for (int32_t i = 0; i < 8; i++) {
        uint8_t byte = static_cast<uint8_t>(guid >> (i * 8));

        if (byte) {
            mask |= static_cast<uint8_t>(1 << i);
            bytes[count++] = byte;
        }
    }

    msg.Put(mask);

    for (int32_t i = 0; i < count; i++) {
        msg.Put(bytes[i]);
    }
}

} // namespace

int32_t SpellBookCount() {
    return static_cast<int32_t>(s_known.size());
}

int32_t SpellBookHighestRankCount() {
    return static_cast<int32_t>(s_highest.size());
}

uint32_t SpellBookSpellAt(int32_t slot) {
    if (slot < 0) {
        return 0;
    }

    if (slot < static_cast<int32_t>(s_known.size())) {
        return s_known[slot];
    }

    slot -= static_cast<int32_t>(s_known.size());

    if (slot < static_cast<int32_t>(s_highest.size())) {
        return s_highest[slot];
    }

    return 0;
}

int32_t SpellBookKnownSlotFromHighestRankSlot(int32_t slot) {
    int32_t base = static_cast<int32_t>(s_known.size());

    if (slot >= base && slot - base < static_cast<int32_t>(s_highestToKnown.size())) {
        return s_highestToKnown[slot - base];
    }

    return slot;
}

bool SpellBookKnows(uint32_t spellId) {
    // The list is ordered by tab, not by id, so this is a plain scan.
    return std::find(s_known.begin(), s_known.end(), spellId) != s_known.end();
}

// ref: FUN_0053b410
uint8_t SpellCompanionType(const SpellRec* spell) {
    if (spell->m_effect[0] == 28) {
        return 0;
    }

    return (spell->m_effectAura[0] != 78) + 1;
}

int32_t SpellBookTabCount() {
    return static_cast<int32_t>(s_tabs.size());
}

const SpellBookTab* SpellBookTabAt(int32_t index) {
    return index >= 0 && index < static_cast<int32_t>(s_tabs.size()) ? &s_tabs[index] : nullptr;
}

// CMSG_CAST_SPELL: uint8 castCount, uint32 spellId, uint8 castFlags, then SpellCastTargets, which
// starts with a uint32 mask -- 0 for self / no target, TARGET_FLAG_UNIT (0x2) plus a packed GUID
// for a unit target. Format read from the server this client is developed against (AzerothCore
// WorldSession::HandleCastSpellOpcode and SpellCastTargets::Read).
void SpellBookCast(uint32_t spellId, uint64_t target) {
    if (!spellId) {
        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_CAST_SPELL));
    msg.Put(static_cast<uint8_t>(0));
    msg.Put(spellId);
    msg.Put(static_cast<uint8_t>(0));

    if (target) {
        msg.Put(static_cast<uint32_t>(0x00000002));
        PutPackedGuid(msg, target);
    } else {
        msg.Put(static_cast<uint32_t>(0x00000000));
    }

    msg.Finalize();
    ClientServices::Send(&msg);
}

void SpellBookClear() {
    s_tabs.clear();
    s_known.clear();
    s_highest.clear();
    s_highestToKnown.clear();
}

// SMSG_INITIAL_SPELLS: uint8 unused, uint16 count, count x { uint32 spellId, uint16 unused }, then
// the cooldown list, which is not read yet. Format from AzerothCore Player::SendInitialSpells.
int32_t ReceiveInitialSpells(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg || msg->Tell() + 3 > msg->Size()) {
        return 1;
    }

    uint8_t unused = 0;
    uint16_t count = 0;

    msg->Get(unused);
    msg->Get(count);

    s_known.clear();

    for (uint16_t i = 0; i < count; i++) {
        if (msg->Tell() + 6 > msg->Size()) {
            break;
        }

        uint32_t spellId = 0;
        uint16_t slot = 0;

        msg->Get(spellId);
        msg->Get(slot);

        if (spellId) {
            s_known.push_back(spellId);
        }
    }

    Rebuild();

    return 1;
}

// SMSG_LEARNED_SPELL: uint32 spellId, uint16 unused.
int32_t ReceiveLearnedSpell(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg || msg->Tell() + 4 > msg->Size()) {
        return 1;
    }

    uint32_t spellId = 0;
    msg->Get(spellId);

    if (spellId && !SpellBookKnows(spellId)) {
        s_known.push_back(spellId);
        Rebuild();
    }

    return 1;
}

// SMSG_REMOVED_SPELL (frozen names 0x0203 SMSG_UNLEARNED_SPELLS): uint32 spellId.
int32_t ReceiveRemovedSpell(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    if (!msg || msg->Tell() + 4 > msg->Size()) {
        return 1;
    }

    uint32_t spellId = 0;
    msg->Get(spellId);

    auto it = std::find(s_known.begin(), s_known.end(), spellId);

    if (it != s_known.end()) {
        s_known.erase(it);
        Rebuild();
    }

    return 1;
}

void SpellBookRegisterHandlers() {
    ClientServices::SetMessageHandler(SMSG_SEND_KNOWN_SPELLS, &ReceiveInitialSpells, nullptr);
    ClientServices::SetMessageHandler(SMSG_LEARNED_SPELL, &ReceiveLearnedSpell, nullptr);
    ClientServices::SetMessageHandler(SMSG_UNLEARNED_SPELLS, &ReceiveRemovedSpell, nullptr);
}
