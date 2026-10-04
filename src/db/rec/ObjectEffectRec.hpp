// ObjectEffect.dbc -- one effect of an ObjectEffectGroup: when it fires (TriggerType 1 entering the
// state, 2 while in it, 3 leaving it, 4 on an event), what it is (EffectRecType 1 a SoundEntries
// kit, EffectRecID), where (an attachment and an offset) and the modifier that drives it.
//
// Twelve columns of 48 bytes.
#ifndef DB_REC_OBJECT_EFFECT_REC_HPP
#define DB_REC_OBJECT_EFFECT_REC_HPP

#include <cstdint>

class SFile;

class ObjectEffectRec {
    public:
        int32_t m_ID;
        const char* m_name;
        int32_t m_objectEffectGroupID;
        int32_t m_triggerType;
        int32_t m_eventType;
        int32_t m_effectRecType;
        int32_t m_effectRecID;
        int32_t m_attachment;
        float m_offset[3];
        int32_t m_objectEffectModifierID;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
