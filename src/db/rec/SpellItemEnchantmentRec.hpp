#ifndef DB_REC_SPELL_ITEM_ENCHANTMENT_REC_HPP
#define DB_REC_SPELL_ITEM_ENCHANTMENT_REC_HPP

#include <cstdint>

class SFile;

// SpellItemEnchantment.dbc: an enchantment, gem or random property effect on an item; three effects of a type each.
class SpellItemEnchantmentRec {
    public:
        static const int32_t COLUMN_COUNT = 38;

        int32_t m_ID;
        int32_t m_charges;
        int32_t m_effect[3];
        int32_t m_effectPointsMin[3];
        int32_t m_effectPointsMax[3];
        int32_t m_effectArg[3];
        const char* m_name;
        int32_t m_itemVisual;
        uint32_t m_flags;
        int32_t m_srcItemID;
        int32_t m_conditionID;
        int32_t m_requiredSkillID;
        int32_t m_requiredSkillRank;
        int32_t m_minLevel;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
