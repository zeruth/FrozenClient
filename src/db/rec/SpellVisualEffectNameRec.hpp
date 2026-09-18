#ifndef DB_REC_SPELL_VISUAL_EFFECT_NAME_REC_HPP
#define DB_REC_SPELL_VISUAL_EFFECT_NAME_REC_HPP

#include <cstdint>

class SFile;

// SpellVisualEffectName.dbc: the M2 behind a spell visual effect (m_fileName, relative to the
// MPQ root, .mdx in the data) and how it scales against the unit it is attached to.
class SpellVisualEffectNameRec {
    public:
        static const int32_t COLUMN_COUNT = 7;

        int32_t m_ID;
        const char* m_name;
        const char* m_fileName;
        float m_areaEffectSize;
        float m_scale;
        float m_minAllowedScale;
        float m_maxAllowedScale;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
