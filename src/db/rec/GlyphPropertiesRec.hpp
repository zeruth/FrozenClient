#ifndef DB_REC_GLYPH_PROPERTIES_REC_HPP
#define DB_REC_GLYPH_PROPERTIES_REC_HPP

#include <cstdint>

class SFile;

// GlyphProperties.dbc: a glyph's spell and whether it is major or minor.
class GlyphPropertiesRec {
    public:
        static const int32_t COLUMN_COUNT = 4;

        int32_t m_ID;
        int32_t m_spellID;
        uint32_t m_glyphSlotFlags;
        int32_t m_spellIconID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
