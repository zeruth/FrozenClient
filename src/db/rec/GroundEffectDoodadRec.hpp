#ifndef DB_REC_GROUND_EFFECT_DOODAD_REC_HPP
#define DB_REC_GROUND_EFFECT_DOODAD_REC_HPP

#include <cstdint>

class SFile;

// GroundEffectDoodad.dbc (3.3.5a, 3 columns): a detail-doodad model name (relative to
// World\NoDXT\Detail\) and its flags.
class GroundEffectDoodadRec {
    public:
        int32_t m_ID;
        const char* m_doodadPath = nullptr;
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
