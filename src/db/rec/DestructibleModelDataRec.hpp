// DestructibleModelData.dbc -- a destructible building's four states: intact (0), damaged (1),
// destroyed (2) and rebuilding (3). Each state past the first names its own building (a
// GameObjectDisplayInfo row) and the doodad sets it plays: the destruction set shown on entering it,
// the impact set on a hit, the ambient set while in it. Then the ground effect of a rebuild, whether
// it may be highlighted, how it heals (4 none, 0/2/3 the rise variants) and how fast.
//
// Nineteen columns of 76 bytes.
#ifndef DB_REC_DESTRUCTIBLE_MODEL_DATA_REC_HPP
#define DB_REC_DESTRUCTIBLE_MODEL_DATA_REC_HPP

#include <cstdint>

class SFile;

class DestructibleModelDataRec {
    public:
        int32_t m_ID;
        int32_t m_state0ImpactEffectDoodadSet;
        int32_t m_state0AmbientDoodadSet;
        int32_t m_state1Wmo;
        int32_t m_state1DestructionDoodadSet;
        int32_t m_state1ImpactEffectDoodadSet;
        int32_t m_state1AmbientDoodadSet;
        int32_t m_state2Wmo;
        int32_t m_state2DestructionDoodadSet;
        int32_t m_state2ImpactEffectDoodadSet;
        int32_t m_state2AmbientDoodadSet;
        int32_t m_state3Wmo;
        int32_t m_state3InitDoodadSet;
        int32_t m_state3AmbientDoodadSet;
        int32_t m_ejectDirection;
        int32_t m_repairGroundFx;
        int32_t m_doNotHighlight;
        int32_t m_healEffect;
        int32_t m_healEffectSpeed;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
