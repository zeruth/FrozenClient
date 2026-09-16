#ifndef DB_REC_GROUND_EFFECT_TEXTURE_REC_HPP
#define DB_REC_GROUND_EFFECT_TEXTURE_REC_HPP

#include <cstdint>

class SFile;

// GroundEffectTexture.dbc (3.3.5a, 11 columns): the detail-doodad set a terrain layer scatters
// (MCLY effectId): up to four GroundEffectDoodad ids with weights, and the amount per cell.
class GroundEffectTextureRec {
    public:
        int32_t m_ID;
        int32_t m_doodadID[4];
        int32_t m_doodadWeight[4];
        int32_t m_amount;
        int32_t m_terrainType;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
