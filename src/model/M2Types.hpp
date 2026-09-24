#ifndef MODEL_M2_TYPES_HPP
#define MODEL_M2_TYPES_HPP

#include "M2Data.hpp"

class CM2Model;
class CShaderEffect;

enum M2BLEND {
    M2BLEND_OPAQUE = 0x0,
    M2BLEND_ALPHA_KEY = 0x1,
    M2BLEND_ALPHA = 0x2,
    M2BLEND_NO_ALPHA_ADD = 0x3,
    M2BLEND_ADD = 0x4,
    M2BLEND_MOD = 0x5,
    M2BLEND_MOD_2X = 0x6,
    M2BLEND_COUNT = 0x7,
};

enum M2COMBINER {
    M2COMBINER_OPAQUE = 0x0,
    M2COMBINER_MOD = 0x1,
    M2COMBINER_DECAL = 0x2,
    M2COMBINER_ADD = 0x3,
    M2COMBINER_MOD2X = 0x4,
    M2COMBINER_FADE = 0x5,
    M2COMBINER_MOD2X_NA = 0x6,
    M2COMBINER_ADD_NA = 0x7,
    M2COMBINER_OP_MASK = 0x7,
    M2COMBINER_ENVMAP = 0x8,
    M2COMBINER_STAGE_SHIFT = 0x4,
};

enum M2LIGHTTYPE {
    M2LIGHT_0 = 0,
    M2LIGHT_1 = 1
};

enum M2PASS {
    M2PASS_0 = 0,
    M2PASS_1 = 1,
    M2PASS_2 = 2,
    M2PASS_COUNT = 3
};

// One entry in CM2Scene's draw list.
//
// SEVENTEEN DWORDS in the reference, 0x44 on x86 -- read off the allocator rather than inferred:
// FUN_00821670 computes an element's address as `data + (index * 17) * 4`. Frozen's struct covered
// only thirteen of them until 2026-09-24, and the four at the end are why the particle element
// emission could not be written down honestly: it writes three of them.
struct M2Element {
    int32_t type;
    CM2Model* model;
    uint32_t flags;
    float alpha;
    float float10;
    float float14;
    // The reference OVERLOADS this slot by element type: for a batch element it is an index, and
    // the particle element builder stores the CM2ParticleEmitter* here instead (0x821980). That
    // does not survive the port as written -- this is 32 bits and frozen's pointers are 64 -- so
    // whatever lands the particle emission has to widen it or carry the emitter separately. It is
    // a real decision, not a transcription detail, which is why it is flagged here rather than
    // discovered mid-port.
    int32_t index;
    int32_t priorityPlane;
    M2Batch* batch;
    M2SkinSection* skinSection;
    CShaderEffect* effect;
    uint32_t vertexPermute;
    uint32_t pixelPermute;

    // +0x34, +0x38, +0x3c and +0x40 in the reference. Their MEANING is not established and is not
    // guessed at here; they are named for their offsets, which is the idiom this codebase already
    // uses for the same situation (CM2Model carries uint74, float88, uint90, float198, matrixB4
    // and ptr2D0 on exactly this basis).
    //
    // What IS known is what the particle element builder writes at 0x821999: dword34 and dword38
    // are set to -1, dword3c to 0, and dword40 is left alone. There is no second writer to read
    // them off -- the batch element path inlines its own allocation instead of calling
    // FUN_00821670, which has exactly one caller.
    uint32_t dword34;
    uint32_t dword38;
    uint32_t dword3c;
    uint32_t dword40;
};

#endif
