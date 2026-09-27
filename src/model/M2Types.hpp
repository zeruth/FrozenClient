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
class CM2ParticleEmitter;

// LAYOUT WARNING, raised 2026-09-27 and NOT yet resolved. The field ORDER below disagrees with
// what two reference functions read, and it matters because it decides whether the doodad and
// ribbon draws can be transcribed field for field.
//
// This order puts batch at the reference's +0x20, skinSection at +0x24 and effect at +0x28. But
// CM2SceneRender::DrawBatchDoodad (FUN_00820ae0) caches the M2Batch from element+0x28 and the
// M2SkinSection from element+0x2c, and the element hash FUN_0081cc50 reads element+0x28 as a
// pointer whose +8 is a colour index it bounds against m_data->colors.Count(). Both of those want
// batch at +0x28, two slots later than this.
//
// SETTLED, by reading what the reference's own batch-element gather writes field by field inside
// CM2Scene::Animate. Those writes are the authority, and they give:
//
//     +0x00 type        [0], assigned 0, 1 or 2
//     +0x04 model       [1]
//     +0x08 flags       [2], zeroed then OR'd with 2 and 4
//     +0x0c alpha       [3]
//     +0x10, +0x14      the two floats -- confirmed by the particle and ribbon writes
//     +0x18 index       [6]
//     +0x1c, +0x20      TWO SLOTS THIS STRUCT DOES NOT HAVE
//     +0x24 priorityPlane  [9], taken off the batch record
//     +0x28 batch          [10]
//     +0x2c skinSection    [0xb]
//     +0x30 effect         [0xc]
//     +0x34 .. +0x40    four more, the three the particle builder sets among them
//
// Seventeen slots, 0x44, which is the element stride -- so the SIZE was never wrong. The ORDER is:
// everything from priorityPlane down sits TWO SLOTS EARLIER here than in the reference, and the
// two fields at +0x1c and +0x20 are absent. vertexPermute and pixelPermute are parked where the
// reference keeps skinSection and effect, so they are not at the reference's offsets either.
//
// This also CORRECTS a note in CM2Scene.cpp's ribbon gather. That gather stores the ribbon's
// priority plane at dword 9 and the note called it an overload of the skinSection slot. It is not
// an overload -- +0x24 IS priorityPlane in the reference. The "overload" was this discrepancy
// showing through, and the ribbon code writes the semantically right field either way.
//
// NOTHING IS BROKEN TODAY: frozen allocates and indexes its own struct consistently, so every
// ported consumer agrees with every ported producer. What it blocks is transcribing a NEW draw
// against reference offsets -- DrawBatchDoodad caches element+0x28 and +0x2c expecting batch and
// skinSection, which land on effect and vertexPermute here. Fix the struct before porting that,
// and the fix needs what +0x1c and +0x20 hold, which the reference's ComputeElementShaders is the
// place to read.
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

    // DIVERGENCE, and a deliberate one. A particle element has to carry its emitter, and the
    // reference does that by reusing `index` above -- which frozen cannot, because that slot is
    // 32 bits and a pointer here is 64. So the emitter gets a field of its own and `index` keeps
    // its one meaning. The cost is four bytes per element on a list rebuilt every frame; the
    // alternative is a union whose active member depends on `type`, which is exactly the kind of
    // thing that reads fine and goes wrong once.
    CM2ParticleEmitter* emitter;
};

#endif
