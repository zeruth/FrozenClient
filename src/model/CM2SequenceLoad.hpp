#ifndef MODEL_C_M2_SEQUENCE_LOAD_HPP
#define MODEL_C_M2_SEQUENCE_LOAD_HPP

#include <cstdint>
#include <storm/List.hpp>

class CAsyncObject;
class CM2Model;
class CM2Shared;

// A bone sequence a model asked for while the sequence's keyframes were still on disk: the
// request is parked here until the .anim file lands, then replayed by
// CM2Shared::SequenceLoadedCallback. Reference layout (0x1C bytes, .?AUCM2SequencePlayBack@@):
// link, model, bone index, flags, time, speed, fallback.
struct CM2SequencePlayBack : TSLinkedNode<CM2SequencePlayBack> {
    CM2Model* model = nullptr;
    uint16_t boneIndex = 0;
    // 1 = SetBoneSequence's a7, 2 = primary (else secondary), 4 = the "no variation given" flag
    // that lands in M2ModelBoneSeq::uintB, 8 = the model went away, drop without applying
    uint16_t flags = 0;
    uint32_t time = 0;
    float speed = 1.0f;
    // M2SequenceFallback by value (uint0 = resolved sequence id, uint2 = play direction/hold),
    // kept as two words so this header does not drag M2Animate.hpp's definitions into every
    // translation unit that sees CM2Shared
    uint16_t fallbackId = 0;
    uint16_t fallbackMode = 0;
};

// One in-flight .anim read for a shared model: which sequence, its async object, the buffer slot
// the data was parked in, and the playbacks waiting on it. Reference layout (0x20 bytes,
// .?AUCM2SequenceLoad@@).
struct CM2SequenceLoad : TSLinkedNode<CM2SequenceLoad> {
    CAsyncObject* asyncObject = nullptr;
    CM2Shared* shared = nullptr;
    uint16_t sequenceIndex = 0;
    uint16_t bufferSlot = 0;
    TSList<CM2SequencePlayBack, TSGetLink<CM2SequencePlayBack>> playbacks;
};

#endif
