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
// SETTLED 2026-09-27, and the answer is not what the first reading of it suggested. Three
// reference functions agree on the offsets -- the batch-element gather inside CM2Scene::Animate,
// CM2SceneRender::DrawBatchDoodad, and CM2Scene::ComputeElementShaders -- and they give:
//
//     +0x00 type    +0x04 model   +0x08 flags   +0x0c alpha   +0x10/+0x14 the two floats
//     +0x18 index   +0x1c ?       +0x20 ?       +0x24 priorityPlane
//     +0x28 batch   +0x2c skinSection           +0x30 effect
//     +0x34 vertexPermute         +0x38 pixelPermute          +0x3c ?   +0x40 ?
//
// Seventeen slots, 0x44, the element stride. This struct has EIGHTEEN fields -- the extra is the
// deliberate `emitter` divergence at the end -- and its named fields sit TWO POSITIONS EARLIER
// than the reference's meanings: what is called priorityPlane here occupies the slot the reference
// leaves unidentified at +0x1c, `batch` sits at the reference's +0x20, `skinSection` where the
// reference keeps priorityPlane, `effect` where it keeps batch, and so on down to dword34 and
// dword38, which are the reference's vertexPermute and pixelPermute.
//
// THIS IS NOT A BUG ON ITS OWN. frozen is self-consistent: every producer and every consumer here
// refers to these fields BY NAME, so the batch gather and DrawBatch agree, ComputeElementShaders
// and the sort comparators agree, and nothing indexes an element by dword offset. Renaming or
// reordering would be churn with no behavioural gain and would break that agreement mid-flight.
//
// WHAT IT DOES CAUSE is one specific failure, and it has already happened twice: a port that
// transcribes a reference builder SLOT BY SLOT lands each value two fields off. AddParticleElement
// did exactly that -- it wrote 0 to pixelPermute where the reference writes 0 to EFFECT -- and the
// ribbon gather copied the same mistake from it. Both are fixed now. The sort comparators guard on
// `effect` being non-null before indexing a shader array with a permute, so leaving effect as
// whatever a recycled slot held was a live hazard rather than a cosmetic one.
//
// So: port element builders BY MEANING, never by dword index.
//
// TWO OF THE UNNAMED SLOTS ARE IDENTIFIED as of 2026-09-27, both from their consumers rather than
// from any declaration:
//
//   +0x1c is the MERGED INSTANCE COUNT of a doodad-batch element.
//   CM2SceneRender::DrawBatchDoodad loops on it, and frozen's own type-2 dispatch already carries
//   `// i += this->m_curElement->dword1C - 1;` commented out, which is the same slot being used to
//   skip the elements the merge swallowed. NOTHING writes it below the shadow-map tier -- see the
//   chain in CM2Model::IsBatchDoodadCompatible -- which is why enabling that gate early would give
//   the draw a loop count out of a recycled slot.
//
//   +0x40 is an ADDITIVE-RUN GROUP ID. FUN_0081f9e0 walks one of the three pass lists assigning it:
//   the counter advances on every element EXCEPT a second or later consecutive additive one, so a
//   run of additive elements shares a group. FUN_0081f0e0 then sorts primarily on that group, which
//   is what lets additive elements be reordered freely inside a run -- additive blending is
//   order-independent -- without ever crossing a non-additive boundary. That pair is the
//   `// TODO sort additive particles` the tail of CM2Scene::Animate still owes.
//
//   +0x20 and +0x3c still have no writer in anything read, and stay unnamed.
// One batch the shadow-caster pass kept: which model it belongs to and which of its batches.
//
// The reference stores a third dword, always 1, which is why its element is 12 bytes; it is
// carried here so the record means the same thing, and because nothing has yet been seen
// READING it -- dropping a field on that basis would be a guess.
//
// NO SIZE ASSERT: the reference packs this into 12 bytes with a 32-bit pointer and frozen is
// 64-bit, so it measures 16. Nothing reads it from a file or steps it by a baked stride.
struct M2ShadowCaster {
    CM2Model* model = nullptr;
    uint32_t batchIndex = 0;
    uint32_t one = 1;
};

// A fixed-capacity collector of those. The reference keeps TWO of these adjacent in one
// 24-byte block and picks between them by the batch's shader field, so callers allocate them
// as an array of two and hand the base in.
//
// It never grows: Add SILENTLY DROPS a caster once the array is full, which is the
// reference's own behaviour and worth keeping visible rather than turning into an assert.
struct M2ShadowCasterList {
    M2ShadowCaster* data = nullptr;
    uint32_t count = 0;
    uint32_t capacity = 0;

    // ref: FUN_00823d50
    void Add(CM2Model* model, uint32_t batchIndex);
};

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

    // +0x34, +0x38 and +0x3c in the reference. Their MEANING is not established and is not guessed
    // at here; they are named for their offsets, which is the idiom this codebase already uses for
    // the same situation (CM2Model carries uint74, float88, uint90, float198, matrixB4 and ptr2D0
    // on exactly this basis).
    //
    // What IS known is what the particle element builder writes at 0x821999: dword34 and dword38
    // are set to -1 and dword3c to 0.
    uint32_t dword34;
    uint32_t dword38;
    uint32_t dword3c;

    // +0x40, AND IT IS NO LONGER AN UNKNOWN. This comment used to say there was no second writer
    // to read these off; FUN_0081f9e0 is that writer, and it is now
    // CM2Scene::KeyAndSortElementList below.
    //
    // It is a RUN INDEX over consecutive ADDITIVE elements. The keying pass walks a sorted list in
    // order, maps each element's blend onto a GxBlend, and increments a counter for every element
    // EXCEPT one that continues a run of additive blends -- so a stretch of additive elements all
    // carry the same number and everything else gets its own. FUN_0081f0e0 then sorts on this
    // first, which is what keeps an additive run together instead of letting the per-element keys
    // interleave it with the blends around it.
    uint32_t additiveRun;

    // DIVERGENCE, and a deliberate one. A particle element has to carry its emitter, and the
    // reference does that by reusing `index` above -- which frozen cannot, because that slot is
    // 32 bits and a pointer here is 64. So the emitter gets a field of its own and `index` keeps
    // its one meaning. The cost is four bytes per element on a list rebuilt every frame; the
    // alternative is a union whose active member depends on `type`, which is exactly the kind of
    // thing that reads fine and goes wrong once.
    CM2ParticleEmitter* emitter;
};

#endif
