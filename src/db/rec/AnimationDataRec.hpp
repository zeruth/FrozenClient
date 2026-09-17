#ifndef DB_REC_ANIMATION_DATA_REC_HPP
#define DB_REC_ANIMATION_DATA_REC_HPP

#include <cstdint>

class SFile;

// AnimationData.dbc: the table behind every M2 animation id.
//
// Its m_fallback chain is what lets the client ask a model for an animation it does not carry and
// still get something sensible: CM2Model::Sub826350 walks the chain until it reaches an animation
// the model has, accumulating m_flags 0x10/0x20 on the way to decide whether the result should be
// played forwards, backwards, or held.
class AnimationDataRec {
    public:
        int32_t m_ID;
        int32_t m_nameOffset;    // string ref (unused for rendering)
        int32_t m_weaponFlags;
        int32_t m_bodyFlags;
        int32_t m_flags;
        int32_t m_fallback;      // AnimationData id to try when the model lacks this one
        int32_t m_behaviorID;
        int32_t m_behaviorTier;

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
