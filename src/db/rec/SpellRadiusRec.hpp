#ifndef DB_REC_SPELL_RADIUS_REC_HPP
#define DB_REC_SPELL_RADIUS_REC_HPP

#include <cstdint>

class SFile;

// SpellRadius.dbc: an effect's radius in yards, how much it grows a level, and its cap.
class SpellRadiusRec {
    public:
        static const int32_t COLUMN_COUNT = 4;

        int32_t m_ID;
        float m_radius;
        float m_radiusPerLevel;
        float m_radiusMax;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
