// TerrainType.dbc -- what the ground a unit stands on is: the spray its footsteps kick up (run
// and walk, SpellVisualEffectName ids) and the TerrainTypeSounds row its footsteps sound from.
//
// Six columns of twenty-four bytes. CGUnit_C::OnFootstep (FUN_00723a50) reads +0x08 and +0x0c;
// FUN_004cf100 reads +0x10.
#ifndef DB_REC_TERRAIN_TYPE_REC_HPP
#define DB_REC_TERRAIN_TYPE_REC_HPP

#include <cstdint>

class SFile;

class TerrainTypeRec {
    public:
        int32_t m_ID;
        const char* m_terrainDesc;
        int32_t m_footstepSprayRun;
        int32_t m_footstepSprayWalk;
        int32_t m_soundID;
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
