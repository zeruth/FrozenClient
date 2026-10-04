#ifndef DB_REC_SPELL_ITEM_ENCHANTMENT_CONDITION_REC_HPP
#define DB_REC_SPELL_ITEM_ENCHANTMENT_CONDITION_REC_HPP

#include <cstdint>

class SFile;

// SpellItemEnchantmentCondition.dbc: the gem colour counts a meta gem needs, up to five comparisons joined left to right.
class SpellItemEnchantmentConditionRec {
    public:
        static const int32_t COLUMN_COUNT = 31;

        int32_t m_ID;
        uint8_t m_ltOperandType[5];
        int32_t m_ltOperand[5];
        uint8_t m_operator[5];
        uint8_t m_rtOperandType[5];
        int32_t m_rtOperand[5];
        uint8_t m_logic[5];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
