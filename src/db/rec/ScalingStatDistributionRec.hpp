#ifndef DB_REC_SCALING_STAT_DISTRIBUTION_REC_HPP
#define DB_REC_SCALING_STAT_DISTRIBUTION_REC_HPP

#include <cstdint>

class SFile;

// ScalingStatDistribution.dbc: the stats an heirloom carries, as shares of a level's budget.
class ScalingStatDistributionRec {
    public:
        static const int32_t COLUMN_COUNT = 22;

        int32_t m_ID;
        int32_t m_statID[10];
        int32_t m_bonus[10];
        int32_t m_maxLevel;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
