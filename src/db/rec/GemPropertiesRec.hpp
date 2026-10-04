#ifndef DB_REC_GEM_PROPERTIES_REC_HPP
#define DB_REC_GEM_PROPERTIES_REC_HPP

#include <cstdint>

class SFile;

// GemProperties.dbc: a gem's enchantment and the socket colours it fits.
class GemPropertiesRec {
    public:
        static const int32_t COLUMN_COUNT = 5;

        int32_t m_ID;
        int32_t m_enchantID;
        int32_t m_maxCountInv;
        int32_t m_maxCountItem;
        uint32_t m_type;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
