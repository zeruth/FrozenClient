#ifndef DB_REC_RAND_PROP_POINTS_REC_HPP
#define DB_REC_RAND_PROP_POINTS_REC_HPP

#include <cstdint>

class SFile;

// RandPropPoints.dbc: the random-suffix point budgets of an item level, per quality and slot group.
class RandPropPointsRec {
    public:
        static const int32_t COLUMN_COUNT = 16;

        int32_t m_ID;
        int32_t m_epic[5];
        int32_t m_superior[5];
        int32_t m_good[5];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
