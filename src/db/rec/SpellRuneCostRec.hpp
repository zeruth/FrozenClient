#ifndef DB_REC_SPELL_RUNE_COST_REC_HPP
#define DB_REC_SPELL_RUNE_COST_REC_HPP

#include <cstdint>

class SFile;

// SpellRuneCost.dbc: the runes a death knight spell spends, and the runic power it gives.
class SpellRuneCostRec {
    public:
        static const int32_t COLUMN_COUNT = 5;

        int32_t m_ID;
        int32_t m_blood;
        int32_t m_unholy;
        int32_t m_frost;
        int32_t m_runicPower;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
