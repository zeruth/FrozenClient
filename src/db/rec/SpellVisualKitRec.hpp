#ifndef DB_REC_SPELL_VISUAL_KIT_REC_HPP
#define DB_REC_SPELL_VISUAL_KIT_REC_HPP

#include <cstdint>

class SFile;

// SpellVisualKit.dbc: the set of effect models a visual attaches to a unit, one per attachment
// slot (head, chest, base, hands, breath, weapons, three specials), plus an animation and sound.
// Each effect column is a SpellVisualEffectName id. Columns 17..36 are the CharProc/CharParam
// blocks, not read yet; 37 is the flags word.
class SpellVisualKitRec {
    public:
        static const int32_t COLUMN_COUNT = 38;

        int32_t m_ID;
        int32_t m_startAnimID;
        int32_t m_animID;
        int32_t m_headEffect;
        int32_t m_chestEffect;
        int32_t m_baseEffect;
        int32_t m_leftHandEffect;
        int32_t m_rightHandEffect;
        int32_t m_breathEffect;
        int32_t m_leftWeaponEffect;
        int32_t m_rightWeaponEffect;
        int32_t m_specialEffect[3];
        int32_t m_worldEffect;
        int32_t m_soundID;
        int32_t m_shakeID;
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
