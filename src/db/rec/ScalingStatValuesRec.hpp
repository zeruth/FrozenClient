#ifndef DB_REC_SCALING_STAT_VALUES_REC_HPP
#define DB_REC_SCALING_STAT_VALUES_REC_HPP

#include <cstdint>

class SFile;

// ScalingStatValues.dbc: the stat, armor and damage budgets of a character level, one row a level.
class ScalingStatValuesRec {
    public:
        static const int32_t COLUMN_COUNT = 24;

        int32_t m_ID;
        int32_t m_charLevel;
        int32_t m_shoulderBudget;
        int32_t m_trinketBudget;
        int32_t m_weaponBudget1H;
        int32_t m_rangedBudget;
        int32_t m_clothShoulderArmor;
        int32_t m_leatherShoulderArmor;
        int32_t m_mailShoulderArmor;
        int32_t m_plateShoulderArmor;
        int32_t m_weaponDPS1H;
        int32_t m_weaponDPS2H;
        int32_t m_spellcasterDPS1H;
        int32_t m_spellcasterDPS2H;
        int32_t m_rangedDPS;
        int32_t m_wandDPS;
        int32_t m_spellPower;
        int32_t m_primaryBudget;
        int32_t m_tertiaryBudget;
        int32_t m_clothCloakArmor;
        int32_t m_clothChestArmor;
        int32_t m_leatherChestArmor;
        int32_t m_mailChestArmor;
        int32_t m_plateChestArmor;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
