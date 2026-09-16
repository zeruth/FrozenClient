#ifndef DB_REC_LIQUID_TYPE_REC_HPP
#define DB_REC_LIQUID_TYPE_REC_HPP

#include <cstdint>

class SFile;

// LiquidType.dbc (3.3.5a, 45 columns): the liquid kinds the map data (MH2O / MCLQ / MLIQ) refers
// to, with the material class (water, ocean, magma, slime) and the animated surface textures.
class LiquidTypeRec {
    public:
        int32_t m_ID;
        const char* m_name = nullptr;
        int32_t m_flags;
        int32_t m_type;             // 0 water, 1 ocean, 2 magma, 3 slime
        int32_t m_soundID;
        int32_t m_spellID;
        float m_maxDarkenDepth;
        float m_fogDarkenIntensity;
        float m_ambDarkenIntensity;
        float m_dirDarkenIntensity;
        int32_t m_lightID;
        float m_particleScale;
        int32_t m_particleMovement;
        int32_t m_particleTexSlots;
        int32_t m_materialID;
        const char* m_texture[6] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr }; // e.g. "XTextures\\river\\lake_a.%d.blp"
        int32_t m_color[2];
        float m_float[18];
        int32_t m_int[4];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
