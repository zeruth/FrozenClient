#ifndef DB_REC_SPELL_REC_HPP
#define DB_REC_SPELL_REC_HPP

#include <cstdint>

class SFile;

// Spell.dbc: only the three fields the action bar needs.
//
// 49839 rows, 234 columns, 936 bytes a row in 3.3.5a. Almost all of it is combat data this client
// has no use for yet, so the row is read as a flat block of 234 dwords and the wanted columns are
// picked out by index rather than by declaring 234 members.
//
// The indices were confirmed against the shipped file rather than taken from a wiki: spell 133 reads
// name "Fireball" at column 136 and icon 185 at column 133, and 585/2050 agree ("Smite",
// "Lesser Heal").
class SpellRec {
    public:
        static const int32_t COLUMN_COUNT = 234;
        static const int32_t COLUMN_ATTRIBUTES = 4;   // SPELL_ATTR0_*; 0x40 is PASSIVE
        static const int32_t COLUMN_ICON = 133;
        static const int32_t COLUMN_NAME = 136;       // 17 locale columns, then
        static const int32_t COLUMN_RANK = 153;       // "Rank N", or "" for an unranked spell

        int32_t m_ID;
        int32_t m_spellIconID;
        const char* m_name;
        const char* m_rank;
        uint32_t m_attributes;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
