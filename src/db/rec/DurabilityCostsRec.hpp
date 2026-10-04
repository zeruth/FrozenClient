#ifndef DB_REC_DURABILITY_COSTS_REC_HPP
#define DB_REC_DURABILITY_COSTS_REC_HPP

#include <cstdint>

class SFile;

// DurabilityCosts.dbc: what a point of durability costs to repair, by weapon and armor subclass, for an item level.
class DurabilityCostsRec {
    public:
        static const int32_t COLUMN_COUNT = 30;

        int32_t m_ID;
        int32_t m_weaponSubClassCost[21];
        int32_t m_armorSubClassCost[8];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
