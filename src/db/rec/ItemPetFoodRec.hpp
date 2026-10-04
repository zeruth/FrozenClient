#ifndef DB_REC_ITEM_PET_FOOD_REC_HPP
#define DB_REC_ITEM_PET_FOOD_REC_HPP

#include <cstdint>

class SFile;

// ItemPetFood.dbc: the diets a hunter pet's family can eat, by bit.
class ItemPetFoodRec {
    public:
        static const int32_t COLUMN_COUNT = 18;

        int32_t m_ID;
        const char* m_name;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
