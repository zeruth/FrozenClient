// CreatureMovementInfo.dbc -- how fast a creature template's model chases its facing.
//
// Two columns of eight bytes: the id a creature template names (CreatureStats_C +0x58) and the
// rate CGUnit_C::UpdateSmoothFacing (FUN_00735f60) springs the model's facing toward the unit's
// at, read from record +0x04. A rate at or under 1e-5 means "use the default smoothing".
#ifndef DB_REC_CREATURE_MOVEMENT_INFO_REC_HPP
#define DB_REC_CREATURE_MOVEMENT_INFO_REC_HPP

#include <cstdint>

class SFile;

class CreatureMovementInfoRec {
    public:
        int32_t m_ID;
        float m_smoothFacingChaseRate;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
