#ifndef DB_REC_SKILL_LINE_ABILITY_REC_HPP
#define DB_REC_SKILL_LINE_ABILITY_REC_HPP

#include <cstdint>

class SFile;

// SkillLineAbility.dbc: which skill line each spell belongs to. This is how the spellbook decides
// which tab a known spell goes on; a spell with no class skill line lands on General.
class SkillLineAbilityRec {
    public:
        static const int32_t COLUMN_COUNT = 14;

        int32_t m_ID;
        int32_t m_skillLine;
        int32_t m_spell;
        int32_t m_raceMask;
        int32_t m_classMask;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
