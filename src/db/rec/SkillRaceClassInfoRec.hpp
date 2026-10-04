#ifndef DB_REC_SKILL_RACE_CLASS_INFO_REC_HPP
#define DB_REC_SKILL_RACE_CLASS_INFO_REC_HPP

#include <cstdint>

class SFile;

// SkillRaceClassInfo.dbc: which races and classes a skill line is open to, and on what terms.
class SkillRaceClassInfoRec {
    public:
        static const int32_t COLUMN_COUNT = 8;

        int32_t m_ID;
        int32_t m_skillID;
        int32_t m_raceMask;
        int32_t m_classMask;
        uint32_t m_flags;
        int32_t m_minLevel;
        int32_t m_skillTierID;
        int32_t m_skillCostIndex;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
