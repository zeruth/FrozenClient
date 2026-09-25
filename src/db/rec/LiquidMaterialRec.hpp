#ifndef DB_REC_LIQUID_MATERIAL_REC_HPP
#define DB_REC_LIQUID_MATERIAL_REC_HPP

#include <cstdint>

class SFile;

// LiquidMaterial.dbc (3.3.5a, 3 columns): how a liquid's vertices are laid out on disk. Every
// LiquidType row names one of these, and the chunk carries the number it gives onto each of its
// layers, where the vertex readers switch on it. See LiquidVertexData.hpp for the four layouts.
class LiquidMaterialRec {
    public:
        int32_t m_ID;
        int32_t m_LVF;      // liquid vertex format, 0 to 3
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
