#ifndef DB_REC_SPELL_VISUAL_REC_HPP
#define DB_REC_SPELL_VISUAL_REC_HPP

#include <cstdint>

class SFile;

// SpellVisual.dbc: which SpellVisualKit plays at each moment of a spell -- precast, cast, impact,
// and the STATE kit that stays up for as long as an aura is on the unit (the swirl under the Lich
// King is one of those). Columns 0..6 are the kits, 13 flags, 14/15 caster/target impact kits,
// 23..25 the area kits; the missile columns in between are not read yet.
class SpellVisualRec {
    public:
        static const int32_t COLUMN_COUNT = 32;

        int32_t m_ID;
        int32_t m_precastKit;
        int32_t m_castKit;
        int32_t m_impactKit;
        int32_t m_stateKit;
        int32_t m_stateDoneKit;
        int32_t m_channelKit;
        int32_t m_hasMissile;
        int32_t m_missileModel;
        int32_t m_missilePathType;
        int32_t m_missileDestinationAttachment;   // +0x28
        int32_t m_missileSound;
        int32_t m_animEventSoundID;
        int32_t m_flags;                          // +0x34
        int32_t m_casterImpactKit;
        int32_t m_targetImpactKit;
        int32_t m_missileAttachment;              // +0x40
        int32_t m_missileFollowGroundHeight;
        int32_t m_missileFollowGroundDropSpeed;
        int32_t m_missileFollowGroundApproach;
        int32_t m_missileFollowGroundFlags;
        int32_t m_missileMotion;
        int32_t m_missileTargetingKit;
        int32_t m_instantAreaKit;
        int32_t m_impactAreaKit;
        int32_t m_persistentAreaKit;
        float m_missileCastOffset[3];             // +0x68
        float m_missileImpactOffset[3];           // +0x74

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
