// ObjectEffectModifier.dbc -- how an effect follows its object: an input (1 the object's speed),
// a mapping (1 a linear ramp over the four parameters) and an output (1 the sound's pitch).
//
// Eight columns of 32 bytes.
#ifndef DB_REC_OBJECT_EFFECT_MODIFIER_REC_HPP
#define DB_REC_OBJECT_EFFECT_MODIFIER_REC_HPP

#include <cstdint>

class SFile;

class ObjectEffectModifierRec {
    public:
        int32_t m_ID;
        int32_t m_inputType;
        int32_t m_mapType;
        int32_t m_outputType;
        float m_param[4];

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
