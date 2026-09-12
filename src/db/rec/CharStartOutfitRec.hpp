#ifndef DB_REC_CHAR_START_OUTFIT_REC_HPP
#define DB_REC_CHAR_START_OUTFIT_REC_HPP

#include <cstdint>

class SFile;

class CharStartOutfitRec {
    public:
        static const int32_t NUM_ITEMS = 24;

        int32_t m_ID;
        uint8_t m_raceID;
        uint8_t m_classID;
        uint8_t m_sexID;
        uint8_t m_outfitID;
        int32_t m_itemID[NUM_ITEMS];
        int32_t m_displayItemID[NUM_ITEMS];
        int32_t m_inventoryType[NUM_ITEMS];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
