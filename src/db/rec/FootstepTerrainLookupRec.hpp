// FootstepTerrainLookup.dbc -- the footstep sound a creature footstep set (CreatureSoundData
// +0x24) makes on a TerrainTypeSounds row, dry and in shallow water.
//
// Five columns of twenty bytes, the layout FUN_004cf990 reads (+0x04 .. +0x10).
#ifndef DB_REC_FOOTSTEP_TERRAIN_LOOKUP_REC_HPP
#define DB_REC_FOOTSTEP_TERRAIN_LOOKUP_REC_HPP

#include <cstdint>

class SFile;

class FootstepTerrainLookupRec {
    public:
        int32_t m_ID;
        int32_t m_creatureFootstepID;
        int32_t m_terrainSoundID;
        int32_t m_soundID;
        int32_t m_soundIDSplash;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
