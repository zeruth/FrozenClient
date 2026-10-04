#ifndef DB_REC_SKILL_COSTS_DATA_REC_HPP
#define DB_REC_SKILL_COSTS_DATA_REC_HPP

#include <cstdint>

class SFile;

// SkillCostsData.dbc: what training a skill costs, three tiers to a row.
class SkillCostsDataRec {
    public:
        static const int32_t COLUMN_COUNT = 5;

        int32_t m_ID;
        int32_t m_skillCostsID;
        int32_t m_cost[3];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
