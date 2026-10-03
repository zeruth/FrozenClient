#ifndef DB_REC_SPELL_VISUAL_KIT_MODEL_ATTACH_REC_HPP
#define DB_REC_SPELL_VISUAL_KIT_MODEL_ATTACH_REC_HPP

#include <cstdint>

class SFile;

// SpellVisualKitModelAttach.dbc: an extra effect model a visual kit attaches, with the
// attachment point, an offset and a rotation (CEffect::ApplyModelAttach reads +0x10..+0x24).
class SpellVisualKitModelAttachRec {
    public:
        static const int32_t COLUMN_COUNT = 10;

        int32_t m_ID;
        int32_t m_parentSpellVisualKitID;
        int32_t m_spellVisualEffectNameID;
        int32_t m_attachmentID;
        float m_offset[3];
        float m_yaw;
        float m_pitch;
        float m_roll;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
