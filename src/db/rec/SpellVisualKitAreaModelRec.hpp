#ifndef DB_REC_SPELL_VISUAL_KIT_AREA_MODEL_REC_HPP
#define DB_REC_SPELL_VISUAL_KIT_AREA_MODEL_REC_HPP

#include <cstdint>

class SFile;

// SpellVisualKitAreaModel.dbc: the model an area spell drops in pieces -- the shards of a Blizzard.
// SpellVisuals indexes it by position, not id (FUN_007fbd70).
class SpellVisualKitAreaModelRec {
    public:
        static const int32_t COLUMN_COUNT = 3;

        int32_t m_ID;
        const char* m_modelName;
        int32_t m_flags;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
