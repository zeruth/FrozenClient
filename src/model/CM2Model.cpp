#include "model/CM2Model.hpp"
#include "util/Log.hpp"
#include <storm/String.hpp>
#include <cstdio>
#include "db/Db.hpp"
#include <cstdlib>
#include "async/AsyncFileRead.hpp"
#include "math/Types.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Animate.hpp"
#include "model/CM2Cache.hpp"
#include "gx/Device.hpp"
#include "gx/Buffer.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/buffer/CGxPool.hpp"
#include "model/M2Data.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Ribbon.hpp"
#include "model/M2Internal.hpp"
#include "model/M2Model.hpp"
#include <common/DataMgr.hpp>
#include <common/ObjectAlloc.hpp>
#include <tempest/Math.hpp>
#include <cmath>
#include <cstring>
#include <new>

// Alignment helpers
#define ALIGN(addr, type) ((addr + alignof(type) - 1) & ~(alignof(type) - 1))
#define ALIGN_PAD(addr, type) (ALIGN(addr, type) - addr)
#define ALIGN_SIZE(addr, type, count) ALIGN(addr + sizeof(type) * count, type) - addr
#define ALIGN_BUFFER(current, start, type) (current + ALIGN_PAD((ptrdiff_t)((char*)current - (char*)start), type))

uint32_t CM2Model::s_loadingSequence = 0xFFFFFFFF;
uint8_t* CM2Model::s_sequenceBase;
uint32_t CM2Model::s_sequenceBaseSize;
uint32_t CM2Model::s_skinProfileBoneCountMax[] = { 256, 64, 53, 21 };

// ref: FUN_0081cd70
// One model out of an ObjectAlloc heap, constructed in place. Pins m_memHandle at +0x2e8, which
// is where frozen already had it.
//
// The reference guards the constructor call on the allocation having produced a pointer, which
// is redundant inside a branch ObjectAlloc already reported success from; the placement new here
// is unconditional for that reason.
//
// This became findable only once CM2Model's constructor was moved out of line -- the matcher had
// nothing to key on while it was inlined into every creation site.
CM2Model* CM2Model::AllocModel(uint32_t* heapId) {
    uint32_t memHandle;
    void* mem = nullptr;

    if (ObjectAlloc(*heapId, &memHandle, &mem, false)) {
        auto model = new (mem) CM2Model();
        model->m_memHandle = memHandle;

        return model;
    }

    return nullptr;
}

bool CM2Model::Sub825E00(M2Data* data, uint32_t a2) {
    if (data->sequenceIdxHashById.Count() == 0) {
        for (int32_t i = 0; i < data->sequences.Count(); i++) {
            auto& sequence = data->sequences[i];

            if (sequence.id == a2) {
                return i < data->sequences.Count();
            }
        }

        return data->sequences.Count() > 0xFFFF;
    }

    uint32_t v8 = a2 % data->sequenceIdxHashById.Count();
    uint16_t v5 = data->sequenceIdxHashById[v8];
    if (v5 == 0xFFFF) {
        return data->sequences.Count() > 0xFFFF;
    }
    if (data->sequences[v5].id == a2) {
        return v5 < data->sequences.Count();
    }

    int32_t v10 = 1;
    while (1) {
        v8 = (v8 + v10 * v10) % data->sequenceIdxHashById.Count();
        v5 = data->sequenceIdxHashById[v8];
        if (v5 == 0xFFFF) {
            return data->sequences.Count() > 0xFFFF;
        }

        ++v10;

        if (data->sequences[v5].id == a2) {
            return v5 < data->sequences.Count();
        }
    }
}

uint16_t CM2Model::Sub8260C0(M2Data* data, uint32_t sequenceId, int32_t a3) {
    // Resolve an animation id to a sequence index, then walk `a3` links down the variation chain.
    //
    // The sequences of one animation are stored as a linked list threaded through variationNext:
    // the hash (or the linear scan when the model carries no hash) finds variation 0, and each
    // further variation is one hop along that chain. Returning 0xFFFF when the chain is shorter
    // than asked is what lets SetBoneSequence fall back to picking a variation at random.
    uint32_t index;

    if (data->sequenceIdxHashById.Count() == 0) {
        index = 0xFFFF;

        for (uint32_t i = 0; i < data->sequences.Count(); i++) {
            if (data->sequences[i].id == sequenceId) {
                index = i;
                break;
            }
        }
    } else {
        uint32_t slot = sequenceId % data->sequenceIdxHashById.Count();
        uint16_t probe = data->sequenceIdxHashById[slot];

        index = 0xFFFF;

        if (probe != 0xFFFF) {
            int32_t step = 1;

            while (data->sequences[probe].id != sequenceId) {
                slot = (slot + step * step) % data->sequenceIdxHashById.Count();
                probe = data->sequenceIdxHashById[slot];

                if (probe == 0xFFFF) {
                    break;
                }

                step++;
            }

            if (probe != 0xFFFF) {
                index = probe;
            }
        }
    }

    uint32_t count = data->sequences.Count();

    if (index < count) {
        while (a3) {
            index = data->sequences[index].variationNext;
            a3--;

            if (index >= count) {
                break;
            }
        }

        if (index < count && a3 == 0) {
            return static_cast<uint16_t>(index);
        }
    }

    return 0xFFFF;
}

// M2 blend mode to EGxBlend for a ribbon material, the reference's table at 0x00a45570. It is a
// pure enum translation and the ONE out-of-sequence entry is the proof: raw value 10 at index 3,
// which is M2BLEND_NO_ALPHA_ADD mapping to GxBlend_NoAlphaAdd. Written as the named constants it
// resolves to rather than the seven raw numbers.
static const EGxBlend s_ribbonBlend[M2BLEND_COUNT] = {
    GxBlend_Opaque,      // M2BLEND_OPAQUE
    GxBlend_AlphaKey,    // M2BLEND_ALPHA_KEY
    GxBlend_Alpha,       // M2BLEND_ALPHA
    GxBlend_NoAlphaAdd,  // M2BLEND_NO_ALPHA_ADD
    GxBlend_Add,         // M2BLEND_ADD
    GxBlend_Mod,         // M2BLEND_MOD
    GxBlend_Mod2x,       // M2BLEND_MOD_2X
};
// Bone weights are bytes that stand for a fraction of 255. The reference keeps the reciprocal as
// a constant at 0x00a45564 and multiplies, rather than dividing.
static const float M2_BONE_WEIGHT_SCALE = 0.0039215689f;

// ref: FUN_0082a4e0
// Pack a skin section whose vertices each ride exactly ONE bone. Slot 1 of the packer table.
//
// With a single influence there is no blend to build: the bone's matrix is used directly, so this
// skips the weighted accumulation entirely and is why the reference bothers to keep a second
// function at all. Position transforms with translation, the normal with the 3x3 part only.
static void M2PackBatchVerticesSingleBone(CM2Model* model, const M2SkinSection* section, void* dst,
                                          uint32_t texCoordSet) {
    const C44Matrix* boneMatrices = model->m_boneMatrices;
    const M2Vertex* vertices = model->m_shared->m_data->vertices.Data();

    const M2Vertex* v = &vertices[section->vertexStart];
    auto out = static_cast<M2BatchDoodadVertex*>(dst);

    for (uint32_t i = 0; i < section->vertexCount; i++, v++, out++) {
        const C44Matrix& m = boneMatrices[v->indices.b[0]];

        out->position.x = v->position.x * m.a0 + v->position.y * m.b0 + v->position.z * m.c0 + m.d0;
        out->position.y = v->position.x * m.a1 + v->position.y * m.b1 + v->position.z * m.c1 + m.d1;
        out->position.z = v->position.x * m.a2 + v->position.y * m.b2 + v->position.z * m.c2 + m.d2;

        out->normal.x = v->normal.x * m.a0 + v->normal.y * m.b0 + v->normal.z * m.c0;
        out->normal.y = v->normal.x * m.a1 + v->normal.y * m.b1 + v->normal.z * m.c1;
        out->normal.z = v->normal.x * m.a2 + v->normal.y * m.b2 + v->normal.z * m.c2;

        out->texcoord = v->texcoord[texCoordSet];
    }
}

// ref: FUN_0082a210
// Pack a skin section with up to FOUR weighted bone influences. Slots 0, 2, 3 and 4 of the packer
// table -- the reference points all four at this one function, so slot 1 above is the only
// specialisation and the table is really a pair.
//
// The blended matrix is CACHED across the run. The reference remembers the previous vertex's
// weight word and index word and rebuilds only when either changes, because neighbouring vertices
// in a skin section overwhelmingly share their skinning. The cache is born holding identity with
// both remembered words zero, so a vertex whose weight and index words are both bit-zero reuses
// identity instead of blending -- an all-zero-weight vertex is degenerate and the reference does
// not special-case it.
//
// The reference compares those two words as FLOATS (it reads the vertex's weight and index bytes
// through a float pointer); this compares them as the 32-bit words they are. The two disagree on
// exactly two families of bit pattern, and neither changes the output: a NaN pattern never
// compares equal, so the reference rebuilds a matrix identical to the one it already had, and
// -0.0 against +0.0 compares equal while the bytes differ, which can only happen when weight byte
// 0 is zero -- and a zero leading weight scales the whole matrix to zero either way. So this is
// the same function with the aliasing removed, not a divergence.
static void M2PackBatchVerticesBlended(CM2Model* model, const M2SkinSection* section, void* dst,
                                       uint32_t texCoordSet) {
    const C44Matrix* boneMatrices = model->m_boneMatrices;
    const M2Vertex* vertices = model->m_shared->m_data->vertices.Data();

    const M2Vertex* v = &vertices[section->vertexStart];
    auto out = static_cast<M2BatchDoodadVertex*>(dst);

    // The fourth column is never accumulated: the reference sets it to (0, 0, 0, 1) up front and
    // the blend only ever writes the twelve entries of the first three.
    C44Matrix blend(1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f);

    uint32_t prevWeights = 0;
    uint32_t prevIndices = 0;

    for (uint32_t i = 0; i < section->vertexCount; i++, v++, out++) {
        if (v->weights.u != prevWeights || v->indices.u != prevIndices) {
            prevWeights = v->weights.u;
            prevIndices = v->indices.u;

            // Bone 0 ASSIGNS, so the identity the matrix was born with is discarded rather than
            // added to.
            float w = static_cast<float>(v->weights.b[0]) * M2_BONE_WEIGHT_SCALE;
            const C44Matrix& m = boneMatrices[v->indices.b[0]];

            blend.a0 = m.a0 * w; blend.a1 = m.a1 * w; blend.a2 = m.a2 * w;
            blend.b0 = m.b0 * w; blend.b1 = m.b1 * w; blend.b2 = m.b2 * w;
            blend.c0 = m.c0 * w; blend.c1 = m.c1 * w; blend.c2 = m.c2 * w;
            blend.d0 = m.d0 * w; blend.d1 = m.d1 * w; blend.d2 = m.d2 * w;

            // Bones 1..3 add, stopping at the first zero weight -- the weights are authored in
            // descending order, so a zero means there are no more.
            for (uint32_t b = 1; b < 4 && v->weights.b[b] != 0; b++) {
                float bw = static_cast<float>(v->weights.b[b]) * M2_BONE_WEIGHT_SCALE;
                const C44Matrix& bm = boneMatrices[v->indices.b[b]];

                blend.a0 += bm.a0 * bw; blend.a1 += bm.a1 * bw; blend.a2 += bm.a2 * bw;
                blend.b0 += bm.b0 * bw; blend.b1 += bm.b1 * bw; blend.b2 += bm.b2 * bw;
                blend.c0 += bm.c0 * bw; blend.c1 += bm.c1 * bw; blend.c2 += bm.c2 * bw;
                blend.d0 += bm.d0 * bw; blend.d1 += bm.d1 * bw; blend.d2 += bm.d2 * bw;
            }
        }

        // The reference runs the position through the shared C3Vector * C44Matrix operator and
        // does the normal by hand, because the normal must not pick up the translation row.
        out->position = v->position * blend;

        out->normal.x = v->normal.x * blend.a0 + v->normal.y * blend.b0 + v->normal.z * blend.c0;
        out->normal.y = v->normal.x * blend.a1 + v->normal.y * blend.b1 + v->normal.z * blend.c1;
        out->normal.z = v->normal.x * blend.a2 + v->normal.y * blend.b2 + v->normal.z * blend.c2;

        out->texcoord = v->texcoord[texCoordSet];
    }
}

// The packer table the reference installs at 0x00d4118c. Five slots, indexed by the skin section's
// boneInfluences field, holding two distinct functions: slot 1 is the single-bone specialisation
// and every other slot is the general weighted blend.
//
// Filled at namespace scope rather than lazily. The reference builds it in the CM2Model
// constructor tail, guarded on slot 0 being null so only the first model pays for it; there is
// nothing to defer here because neither function needs anything initialised first.
//
// NOT PORTED: the reference swaps in SSE variants (FUN_0082a600 and FUN_0082ac10) when
// 0x00d3fcec has bit 4. They compute the same thing with packed arithmetic, so leaving them out
// costs throughput and nothing else.
static const M2PackBatchVerticesFn s_packBatchVertices[5] = {
    M2PackBatchVerticesBlended,
    M2PackBatchVerticesSingleBone,
    M2PackBatchVerticesBlended,
    M2PackBatchVerticesBlended,
    M2PackBatchVerticesBlended,
};

M2PackBatchVerticesFn M2GetPackBatchVerticesFn(uint32_t boneInfluences) {
    // The reference indexes the table with boneInfluences raw and trusts the .m2 to keep it in
    // range. Clamping instead of trusting, because a malformed model would otherwise call through
    // whatever follows the table.
    if (boneInfluences >= 5) {
        boneInfluences = 0;
    }

    return s_packBatchVertices[boneInfluences];
}
// ref: FUN_0082be60
// Out of line ON PURPOSE, the same reasoning as CMapBaseObj's: the reference has a real
// constructor here -- 855 bytes of it -- and an implicit one is inlined into every creation site
// and leaves no function to match it against.
//
// Only the bitfield block is listed, because the reference's own constructor is almost entirely
// field initialisation that frozen already expresses as default member initializers. The word it
// builds is `(flags & 0xff800340) | 0x340`, which is exactly m_flag40, m_flag100 and m_flag200
// set and every other bit below 0x800000 clear -- what this list says.
//
// NOT PORTED from it: the tail installs the five-entry bone-blend packer table at 0x00d4118c,
// once, guarded on the first slot being null, and swaps in SSE variants when 0x00d3fcec has bit 4.
//
// The table itself IS ported -- s_packBatchVertices above -- so what is missing here is only the
// lazy installation, which frozen does not need because it fills the table at namespace scope.
// What indexes it was established 2026-09-26 by reading the initialiser at 0x0082c15b against the
// call site in DrawBatchDoodad: the index is the skin section's `boneInfluences` field, and the
// five slots hold only TWO functions -- slot 1 is FUN_0082a4e0, the single-bone specialisation,
// and the rest are FUN_0082a210, the weighted blend. This is unrelated to
// CM2Scene::BlendBoneMatrices, which an earlier version of this note guessed at.
CM2Model::CM2Model()
    : m_loaded(0)
    , m_flag2(0)
    , m_flag4(0)
    , m_flag8(0)
    , m_flag10(0)
    , m_flag20(0)
    , m_flag40(1)
    , m_flag80(0)
    , m_flag100(1)
    , m_flag200(1)
    , m_flag400(0)
    , m_flag800(0)
    , m_flag1000(0)
    , m_flag2000(0)
    , m_flag4000(0)
    , m_flag8000(0)
    , m_flag10000(0)
    , m_flag20000(0)
    , m_flag40000(0)
    , m_flag80000(0)
    , m_flag100000(0)
    , m_flag200000(0)
    , m_flag400000(0)
    {}

CM2Model::~CM2Model() {
    // Give back the reference Initialize took on the model this one was created against. The
    // reference does this first thing (FUN_00832640 at 0x0083264e).
    if (this->m_parentModel) {
        this->m_parentModel->Release();
        this->m_parentModel = nullptr;
    }

    // TODO

    // Any bone-sequence request still parked in CM2Shared's load list holds a raw pointer to this
    // model, and the callback that applies them dereferences it. The reference retires them here,
    // before anything else is torn down (FUN_00832640 calls FUN_00831e20 at 0x00832676).
    this->CancelAllDeferredSequences();

    // Unlink from lists

    this->UnlinkFromCallbackList();
    this->UnlinkFromAnimateList();
    this->UnlinkFromDrawList();

    // TODO

    this->DetachFromScene();

    if (this->m_shared) {
        this->FreeExternalResources();
        this->FreeInternalResources();

        this->m_shared->Release();
        this->m_shared = nullptr;
    }

    // Let go of every model attached to this one. Each child holds a counted reference taken when
    // it was attached, so without this they are never released and never destroyed, and each is
    // left with m_attachParent pointing at freed memory.
    //
    // DetachFromParent already does the whole of what the reference does to each child here
    // (FUN_00832640 at 0x0083274c): unlink it, clear m_flag40000, null its parent and attach id,
    // and Release it -- which destroys it in place when the count reaches zero, recursing into its
    // own children exactly as the reference recurses. It unlinks before releasing, so the head has
    // already advanced by the time the child can be freed, and this loop terminates.
    while (this->m_attachList) {
        this->m_attachList->DetachFromParent();
    }

    // TODO

    this->UnlinkFromAttachList();

    // TODO

    this->m_attachParent = nullptr;
    this->m_currentLighting = nullptr;
}

void CM2Model::AddRef() {
    this->m_refCount++;
}

// How far a bone has blended out of its secondary sequence.
//
// `uint9C` is when the blend ENDS and floatA0 its reciprocal duration, so t counts DOWN
// from 1 to 0 as the blend completes -- the weight is how much of the SECONDARY sequence
// still applies. The curve is the classic smoothstep, `(3 - 2t) * t * t`, clamped at both
// ends, and floatA4 is a per-bone ceiling: 0.75 for a normal sequence start, 1.0 for a
// blended stop.
float M2BoneBlendWeight(const M2ModelBone& modelBone, uint32_t sceneTime) {
    float t = static_cast<float>(
        static_cast<int32_t>(modelBone.uint9C) - static_cast<int32_t>(sceneTime))
        * modelBone.floatA0;

    float weight = 0.0f;

    if (t > 1.0f) {
        weight = 1.0f;
    } else if (t >= 0.0f) {
        weight = (3.0f - (t + t)) * t * t;
    }

    return weight * modelBone.floatA4;
}

// ref: FUN_006f1d20
// scene + 0xc4 is m_viewInv.
C44Matrix CM2Model::AnimateAndGetWorldMatrix() {
    this->Animate();

    return this->matrixF4 * this->m_scene->m_viewInv;
}

// ref: FUN_00830dc0
// Bring this model's transform and bone matrices up to date for the scene's current frame, on
// demand rather than from the scene's animate pass. A model attached to another one cannot be
// animated on its own -- its transform starts at the parent's attachment point -- so the parent is
// animated first and this model is then animated onto the attachment matrix. When the animation
// could not run (not loaded, or the parent has no fresh bone matrices) the transform still has to
// be defined, so it falls back to the parent's, or to this model's own world transform.
void CM2Model::Animate() {
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    // The reference tests this POSITIVELY and recurses in the true branch, which puts
    // Animate ahead of the two Animate*MT calls in its call order. Inverting the test
    // reverses that for no gain, so the arms are this way round on purpose.
    if (this->m_attachParent) {
        this->m_attachParent->Animate();
    } else {
        C3Vector diffuse = { 1.0f, 1.0f, 1.0f };
        C3Vector emissive = { 0.0f, 0.0f, 0.0f };

        if (this->m_flag1000) {
            this->AnimateMTSimple(&this->m_scene->m_view, diffuse, emissive, 1.0f, 1.0f);
        } else {
            this->AnimateMT(&this->m_scene->m_view, diffuse, emissive, 1.0f, 1.0f);
        }
    }

    auto scene = this->m_scene;

    if (this->m_animCounter == scene->uint14) {
        return;
    }

    auto parent = this->m_attachParent;

    if (parent && this->m_loaded) {
        C44Matrix view;

        const C44Matrix* attachView = &parent->matrixF4;

        if (parent->m_loaded && parent->m_animCounter == scene->uint14 && this->m_attachIndex != 0xFFFF) {
            auto& attachment = parent->m_shared->m_data->attachments[this->m_attachIndex];

            view = parent->m_boneMatrices[attachment.boneIndex];
            view.Translate(attachment.position);

            attachView = &view;
        }

        if (this->m_flag1000) {
            this->AnimateMTSimple(attachView, parent->m_currentDiffuse, parent->m_currentEmissive, parent->float198, parent->alpha19C);
        } else {
            this->AnimateMT(attachView, parent->m_currentDiffuse, parent->m_currentEmissive, parent->float198, parent->alpha19C);
        }

        if (this->m_animCounter == scene->uint14) {
            return;
        }
    }

    if (this->m_attachParent) {
        this->matrixF4 = this->m_attachParent->matrixF4;

        return;
    }

    this->matrixF4 = this->matrixB4 * scene->m_view;
}

// ref: FUN_0082e550
void CM2Model::AnimateAttachmentsMT() {
    // Animate attachment visibility

    for (int32_t i = 0; i < this->m_shared->m_data->attachments.Count(); i++) {
        auto& attachment = this->m_shared->m_data->attachments[i];
        auto& modelAttachment = this->m_attachments[i];
        auto& modelBone = this->m_bones[attachment.boneIndex];

        if (
            attachment.visibilityTrack.sequenceTimes.Count() > 1
            || (attachment.visibilityTrack.sequenceTimes.Count() == 1 && attachment.visibilityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            uint8_t defaultValue = 1;
            M2AnimateTrack<uint8_t, uint8_t>(this, &modelBone, attachment.visibilityTrack, modelAttachment.visibilityTrack, defaultValue);
        }
    }

    // Animate attached models

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        C44Matrix view;

        if (model->m_attachIndex == 0xFFFF) {
            if (!model->m_flag40000) {
                continue;
            }

            view = this->m_boneMatrices[0];
        } else {
            auto& attachment = this->m_shared->m_data->attachments[model->m_attachIndex];
            auto& modelAttachment = this->m_attachments[model->m_attachIndex];

            // Attachment not currently visible
            if (!modelAttachment.visibilityTrack.currentValue) {
                continue;
            }

            view = this->m_boneMatrices[attachment.boneIndex];
            view.Translate(attachment.position);
        }

        if (model->m_flag1000) {
            model->AnimateMTSimple(&view, this->m_currentDiffuse, this->m_currentEmissive, this->float198, this->alpha19C);
        } else {
            model->AnimateMT(&view, this->m_currentDiffuse, this->m_currentEmissive, this->float198, this->alpha19C);
        }
    }
}

void CM2Model::AnimateCamerasST() {
    for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
        auto& camera = this->m_shared->m_data->cameras[i];
        auto& modelCamera = this->m_cameras[i];

        C3Vector v56 = modelCamera.positionTrack.currentValue + camera.positionPivot;
        C3Vector cameraPos = (v56 * this->matrixF4) * this->m_scene->m_viewInv;
        DataMgrSetCoord(modelCamera.m_camera, 7, cameraPos, 0x0);

        C3Vector v57 = modelCamera.targetTrack.currentValue + camera.targetPivot;
        C3Vector targetPos = (v57 * this->matrixF4) * this->m_scene->m_viewInv;
        DataMgrSetCoord(modelCamera.m_camera, 8, targetPos, 0x0);

        DataMgrSetFloat(modelCamera.m_camera, 5, modelCamera.rollTrack.currentValue);
    }
}

// ref: FUN_0082f0f0
void CM2Model::AnimateMT(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6) {
    if (!this->m_loaded) {
        return;
    }

    // Already animated for this frame of the scene.
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    // Handle attachment visibility

    if (this->m_attachParent) {
        this->m_flag8 = this->m_attachParent->m_flag8 && this->m_flag80;
        this->m_flag10000 = this->m_attachParent->m_flag10000 && this->m_flag20000;

        // STILL MISSING: the reference also copies the parent's dword at +0x174, the field
        // immediately before m_baseAlpha. frozen has nothing declared there and no reader for it
        // has been traced, so adding a field would only inherit an unknown value.
    }

    // Fold the tint handed down by the caller into this model's current values. The BASE values
    // are the model's own (nothing writes them yet -- the setters are unported -- so they are
    // neutral), and the current ones are what the render reads: CM2Scene takes alpha19C as each
    // element's alpha, and an attached child is animated with its PARENT's current values, so
    // this is also the step that carries a tint down an attachment chain.
    //
    // It was missing entirely, which is why m_currentDiffuse and m_currentEmissive sat at their
    // initial values however the caller was animated. That is invisible today -- every call site
    // passes {1,1,1}, {0,0,0}, 1, 1 -- and stops being invisible the moment a base-tint setter
    // lands or a chain gets more than one link.
    //
    // Data flag 0x4 means the model does not take a handed-down tint at all: it folds only its
    // own base values and ignores all four arguments.
    if (this->m_shared->m_data->flags & 0x4) {
        this->float198 = this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * this->m_baseAlpha;
        this->m_currentDiffuse = this->m_baseDiffuse;
        this->m_currentEmissive = this->m_baseEmissive;
    } else {
        this->m_currentDiffuse = {
            a3.x * this->m_baseDiffuse.x,
            a3.y * this->m_baseDiffuse.y,
            a3.z * this->m_baseDiffuse.z
        };

        this->m_currentEmissive = this->m_baseEmissive;

        // Flag 0x100000 holds the model's own alpha against the one it was handed; the SCALE
        // below is never held, which is the asymmetry the reference has and not a transcription
        // slip -- a5 gates and a6 always multiplies.
        this->float198 = this->m_flag100000 ? this->m_baseAlpha : a5 * this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * a6 * this->m_baseAlpha;

        // And 0x80000 holds the emissive term against the handed-down one.
        if (!this->m_flag80000) {
            this->m_currentEmissive = this->m_currentEmissive + a4;
        }
    }

    for (int32_t i = 0; i < this->m_shared->m_data->loops.Count(); i++) {
        auto loopLength = this->m_shared->m_data->loops[i].length;
        this->m_loops[i] = loopLength ? (this->m_scene->m_time - this->uint74) % loopLength : 0;
    }

    this->matrixF4 = this->matrixB4 * *view;


    this->float88 = !this->m_attachParent || this->m_attachParent->m_flags & 0x1
        ? this->matrixF4.d2 * this->matrixF4.d2 + this->matrixF4.d1 * this->matrixF4.d1 + this->matrixF4.d0 * this->matrixF4.d0
        : this->m_attachParent->float88;

    C44Matrix v237;
    C44Matrix v224;
    C3Vector v236;

    // TODO

    uint32_t elapsedTime = 0;
    if (this->m_time && this->m_scene->m_time) {
        elapsedTime = this->m_scene->m_time - this->m_time;
        this->m_time = this->m_scene->m_time;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
        auto& bone = this->m_shared->m_data->bones[i];
        auto& modelBone = this->m_bones[i];

        if (modelBone.sequence.uint8 == 0xFFFF) {
            if (bone.parentIndex >= this->m_shared->m_data->bones.Count()) {
                if (i != 0) {
                    modelBone.sequence.uint0 = this->m_bones[0].sequence.uint0;
                    modelBone.sequence.uint4 = this->m_bones[0].sequence.uint4;
                    modelBone.sequence.uint6 = this->m_bones[0].sequence.uint6;
                }
            } else {
                modelBone.sequence.uint0 = this->m_bones[bone.parentIndex].sequence.uint0;
                modelBone.sequence.uint4 = this->m_bones[bone.parentIndex].sequence.uint4;
                modelBone.sequence.uint6 = this->m_bones[bone.parentIndex].sequence.uint6;
            }
        } else {
            if (this->m_time) {
                modelBone.sequence.uintC += elapsedTime;
                modelBone.sequence.uint10 += elapsedTime;
            }

            auto v45 = this->m_scene->m_time;
            auto& v46 = this->m_shared->m_data->sequences[modelBone.sequence.uint8];
            uint32_t v47 = 0;

            if (v46.flags & 0x1) {
                if (modelBone.sequence.uint10 - v45 <= 0) {
                    auto v234 = modelBone.sequence.uint10 - modelBone.sequence.uintC;
                    auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                    v47 = modelBone.sequence.uint1C + v235;
                    v47 = std::min(v47, v46.duration);
                } else {
                    if (modelBone.sequence.uintC - v45 > 0) {
                        v45 = modelBone.sequence.uintC;
                    }

                    if (v46.duration) {
                        auto v234 = v45 - modelBone.sequence.uintC;
                        auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                        v47 = (modelBone.sequence.uint1C + v235) % v46.duration;
                    }
                }
            } else {
                if (v46.duration) {
                    auto v234 = v45 - modelBone.sequence.uintC;
                    auto v235 = CMath::fuint(v234 * modelBone.sequence.float14);
                    v47 = (modelBone.sequence.uint1C + v235) % v46.duration;
                }
            }

            modelBone.sequence.uint0 = v47;
            modelBone.sequence.uint4 = modelBone.sequence.uint8;
            modelBone.sequence.uint6 = i;
        }

        // How far this bone has blended from its secondary sequence into its primary one.
        // Ported 2026-09-24; this was a bare TODO, and with it the weight stayed zero and
        // M2AnimateTrack's blend had nothing to work from, so every animation transition
        // snapped.
        if (modelBone.sequence.uint8 == 0xFFFF && modelBone.secondarySequence.uint8 == 0xFFFF) {
            // No sequence on either slot: inherit, so a whole unanimated subtree fades
            // with whatever is driving its root rather than snapping against it.
            if (bone.parentIndex < this->m_shared->m_data->bones.Count()) {
                modelBone.floatA8 = this->m_bones[bone.parentIndex].floatA8;
            } else {
                modelBone.floatA8 = 0.0f;
            }
        } else if (modelBone.sequence.uint0 == modelBone.secondarySequence.uint0
                && modelBone.sequence.uint4 == modelBone.secondarySequence.uint4) {
            // Both slots are playing the same thing; there is nothing to blend between.
            modelBone.floatA8 = 0.0f;
        } else {
            modelBone.floatA8 = M2BoneBlendWeight(modelBone, this->m_scene->m_time);
        }

        uint32_t boneFlags = bone.flags | modelBone.flags;

        C44Matrix* boneParentMatrix;

        if (bone.parentIndex == 0xFFFF) {
            boneParentMatrix = &this->matrixF4;
        } else {
            boneParentMatrix = &this->m_boneMatrices[bone.parentIndex];

            if (boneFlags & (0x1 | 0x2 | 0x4)) {
                // NOT PORTED: the ignore-parent-transform branch, 0x82f843..0x82fc2e.
                //
                // The bone's own world matrix is copied aside, and then bits 1 and 2 select
                // between three variants through `boneFlags & 6`:
                //
                //   2  0x82faa9  normalises the copy's three rows, then combines with matrixF4
                //   4  0x82f8ff  each row becomes the matching matrixF4 row rescaled to the
                //                copy's row length, or left alone when that row is shorter
                //                than 1e-5 (0x009ea558)
                //   6  0x82f8ac  the three matrixF4 rows verbatim
                //
                // and bit 0 is handled separately at 0x82fb69: the translation comes from
                // matrixF4's row 3 instead of being transformed.
                //
                // The twelve C3Vector::Normalize calls --diff reports missing are all in here.
                // The gate and the selector are certain; which variant means "ignore rotation"
                // and which "ignore scale" is NOT yet certain, and porting bone math on a
                // reading that is only nearly right would be worse than leaving the branch out.
            }
        }

        if (boneFlags & (0x80 | 0x200)) {
            C44Matrix boneLocalMatrix;

            if (bone.rotationTrack.sequenceTimes.Count()) {
                auto& rotationTrack = bone.rotationTrack;

                if (
                    rotationTrack.sequenceTimes.Count() > 1
                    || (rotationTrack.sequenceTimes.Count() == 1 && rotationTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C4Quaternion defaultValue = { 0.0f, 0.0f, 0.0f, 1.0f };
                    M2AnimateTrack<M2CompQuat, C4Quaternion>(this, &modelBone, rotationTrack, modelBone.rotationTrack, defaultValue);
                }

                boneLocalMatrix = C44Matrix(modelBone.rotationTrack.currentValue);
            } else {
                // TODO
            }

            if (bone.scaleTrack.sequenceTimes.Count()) {
                auto& scaleTrack = bone.scaleTrack;

                if (
                    scaleTrack.sequenceTimes.Count() > 1
                    || (scaleTrack.sequenceTimes.Count() == 1 && scaleTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C3Vector defaultValue = { 1.0f, 1.0f, 1.0f };
                    M2AnimateTrack<C3Vector, C3Vector>(this, &modelBone, scaleTrack, modelBone.scaleTrack, defaultValue);
                }

                boneLocalMatrix.Scale(modelBone.scaleTrack.currentValue);
            }

            // TODO
            // conditional involving bone flags and a matrix member of M2ModelBone

            C3Vector translation;

            if (bone.translationTrack.sequenceTimes.Count()) {
                auto& translationTrack = bone.translationTrack;

                if (
                    translationTrack.sequenceTimes.Count() > 1
                    || (translationTrack.sequenceTimes.Count() == 1 && translationTrack.sequenceTimes[0].times.Count() > this->uint90)
                ) {
                    C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
                    M2AnimateTrack<C3Vector, C3Vector>(this, &modelBone, translationTrack, modelBone.translationTrack, defaultValue);
                }

                translation = modelBone.translationTrack.currentValue + bone.pivot;
            } else {
                translation = bone.pivot;
            }

            boneLocalMatrix.d0 += translation.x;
            boneLocalMatrix.d1 += translation.y;
            boneLocalMatrix.d2 += translation.z;

            C3Vector negPivot = {
                -bone.pivot.x,
                -bone.pivot.y,
                -bone.pivot.z
            };

            boneLocalMatrix.Translate(negPivot);

            this->m_boneMatrices[i] = boneLocalMatrix * *boneParentMatrix;
        } else {
            this->m_boneMatrices[i] = *boneParentMatrix;
        }

        // BOTH BILLBOARD BRANCHES BELOW ARE REASONED, NOT PORTED. They were worked out from what
        // a glow sprite ought to look like -- the comment below still says so -- and CLAUDE.md is
        // explicit that guessing an implementation from what the screen looks like is how the
        // graphics bugs got in.
        //
        // AND THE REFERENCE'S AnimateMT DOES NO BILLBOARDING AT ALL. It tests boneFlags bits 0,
        // 1 and 2 and nothing else: there is no test of 0x8, 0x10, 0x20 or 0x40 anywhere in the
        // function. So this is not a port that drifted, it is reasoned code standing in a
        // function whose reference counterpart has no such branch. WHERE the reference
        // billboards, if it does, is not established -- find that before touching this.
        //
        // (An earlier note here pointed at 0x82f930..0x8302c0 and called that the reference's
        // billboard. It is not; see the TODO above, which is what that region actually is.)
        if (boneFlags & 0x8) {
            // Spherical billboard. The bone matrix is already in view space (its parent chain roots
            // at matrixF4 = model x view), so replacing its rotation with the view axes makes the
            // geometry face the screen from any camera angle -- exactly what a glow/flare sprite
            // needs. Keep the animated per-axis scale (row lengths) and the view-space position;
            // drop only the orientation, mapping the sprite's local X/Y onto camera right/up like the
            // reference does. Non-billboard bones are untouched.
            C44Matrix& m = this->m_boneMatrices[i];

            float sx = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
            float sy = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
            float sz = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);

            m.a0 = sx;   m.a1 = 0.0f; m.a2 = 0.0f;
            m.b0 = 0.0f; m.b1 = sy;   m.b2 = 0.0f;
            m.c0 = 0.0f; m.c1 = 0.0f; m.c2 = sz;
        } else if (boneFlags & (0x10 | 0x20 | 0x40)) {
            // Cylindrical billboard: keep one axis locked (e.g. a candle flame stays vertical) and
            // spin the geometry around it to face the screen. Still in view space, so the locked
            // axis is its own row and "toward the screen" is view +Z. By cyclic order the locked
            // axis is the sprite's up, the next axis its width (right), the third its normal:
            // lockX -> up=X,right=Y,normal=Z ; lockY -> up=Y,right=Z,normal=X ; lockZ -> up=Z,right=X,normal=Y.
            C44Matrix& m = this->m_boneMatrices[i];

            float ux, uy, uz;
            if (boneFlags & 0x10) { ux = m.a0; uy = m.a1; uz = m.a2; }
            else if (boneFlags & 0x20) { ux = m.b0; uy = m.b1; uz = m.b2; }
            else { ux = m.c0; uy = m.c1; uz = m.c2; }

            float ul = sqrtf(ux * ux + uy * uy + uz * uz);

            if (ul > 1e-6f) {
                ux /= ul; uy /= ul; uz /= ul; // normalized locked (up) axis

                // right = normalize(cross(up, viewZ)), viewZ = (0,0,1): a horizontal screen axis
                // perpendicular to up. If up is parallel to the view direction, pick any horizontal.
                float rx = uy, ry = -ux, rz = 0.0f;
                float rl = sqrtf(rx * rx + ry * ry + rz * rz);

                if (rl < 1e-6f) { rx = 1.0f; ry = 0.0f; rz = 0.0f; rl = 1.0f; }

                rx /= rl; ry /= rl; rz /= rl;

                // normal = cross(up, right) -> keeps X x Y = Z (right handed) and faces the screen
                float nx = uy * rz - uz * ry;
                float ny = uz * rx - ux * rz;
                float nz = ux * ry - uy * rx;

                if (boneFlags & 0x10) {        // lock X: right -> Y row, normal -> Z row
                    float rs = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
                    float ns = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);
                    m.b0 = rx * rs; m.b1 = ry * rs; m.b2 = rz * rs;
                    m.c0 = nx * ns; m.c1 = ny * ns; m.c2 = nz * ns;
                } else if (boneFlags & 0x20) { // lock Y: right -> Z row, normal -> X row
                    float rs = sqrtf(m.c0 * m.c0 + m.c1 * m.c1 + m.c2 * m.c2);
                    float ns = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
                    m.c0 = rx * rs; m.c1 = ry * rs; m.c2 = rz * rs;
                    m.a0 = nx * ns; m.a1 = ny * ns; m.a2 = nz * ns;
                } else {                       // lock Z: right -> X row, normal -> Y row
                    float rs = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);
                    float ns = sqrtf(m.b0 * m.b0 + m.b1 * m.b1 + m.b2 * m.b2);
                    m.a0 = rx * rs; m.a1 = ry * rs; m.a2 = rz * rs;
                    m.b0 = nx * ns; m.b1 = ny * ns; m.b2 = nz * ns;
                }
            }
        }

        // TODO
    }

    for (int32_t i = 0; i < this->m_shared->m_data->colors.Count(); i++) {
        auto& color = this->m_shared->m_data->colors[i];
        auto& modelColor = this->m_colors[i];

        auto& colorTrack = color.colorTrack;
        if (
            colorTrack.sequenceTimes.Count() > 1
            || (colorTrack.sequenceTimes.Count() == 1 && colorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                this->m_bones,
                color.colorTrack,
                modelColor.colorTrack,
                defaultValue
            );
        }

        auto& alphaTrack = color.alphaTrack;
        if (
            alphaTrack.sequenceTimes.Count() > 1
            || (alphaTrack.sequenceTimes.Count() == 1 && alphaTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                color.alphaTrack,
                modelColor.alphaTrack,
                defaultValue
            );
        }
    }

    for (int32_t i = 0; i < this->m_shared->m_data->textureWeights.Count(); i++) {
        auto& textureWeight = this->m_shared->m_data->textureWeights[i];
        auto& modelTextureWeight = this->m_textureWeights[i];

        auto& weightTrack = textureWeight.weightTrack;
        if (
            weightTrack.sequenceTimes.Count() > 1
            || (weightTrack.sequenceTimes.Count() == 1 && weightTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                textureWeight.weightTrack,
                modelTextureWeight.weightTrack,
                defaultValue
            );
        }
    }

    if (this->m_shared->m_data->textureTransforms.count) {
        this->AnimateTextureTransformsMT();
    }

    for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
        auto& light = this->m_shared->m_data->lights[i];
        auto& modelLight = this->m_lights[i];

        if (modelLight.uint64) {
            uint8_t defaultValue = 1;
            M2AnimateTrack<uint8_t, uint8_t>(
                this,
                &this->m_bones[light.boneIndex],
                light.visibilityTrack,
                modelLight.visibilityTrack,
                defaultValue
            );
        }

        if ((modelLight.uint64 == 0 || modelLight.visibilityTrack.currentValue == 0) && this->uint90) {
            continue;
        }

        auto& ambientIntensityTrack = light.ambientIntensityTrack;
        if (
            ambientIntensityTrack.sequenceTimes.Count() > 1
            || (ambientIntensityTrack.sequenceTimes.Count() == 1 && ambientIntensityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateTrack<float, float>(
                this,
                &this->m_bones[light.boneIndex],
                light.ambientIntensityTrack,
                modelLight.ambientIntensityTrack,
                defaultValue
            );
        }

        auto& ambientColorTrack = light.ambientColorTrack;
        if (
            ambientColorTrack.sequenceTimes.Count() > 1
            || (ambientColorTrack.sequenceTimes.Count() == 1 && ambientColorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                &this->m_bones[light.boneIndex],
                light.ambientColorTrack,
                modelLight.ambientColorTrack,
                defaultValue
            );

            float mul = modelLight.ambientIntensityTrack.currentValue * this->float198;

            modelLight.light.m_ambColor.x = modelLight.ambientColorTrack.currentValue.x * mul;
            modelLight.light.m_ambColor.y = modelLight.ambientColorTrack.currentValue.y * mul;
            modelLight.light.m_ambColor.z = modelLight.ambientColorTrack.currentValue.z * mul;
        }

        auto& diffuseIntensityTrack = light.diffuseIntensityTrack;
        if (
            diffuseIntensityTrack.sequenceTimes.Count() > 1
            || (diffuseIntensityTrack.sequenceTimes.Count() == 1 && diffuseIntensityTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateTrack<float, float>(
                this,
                &this->m_bones[light.boneIndex],
                light.diffuseIntensityTrack,
                modelLight.diffuseIntensityTrack,
                defaultValue
            );
        }

        auto& diffuseColorTrack = light.diffuseColorTrack;
        if (
            diffuseColorTrack.sequenceTimes.Count() > 1
            || (diffuseColorTrack.sequenceTimes.Count() == 1 && diffuseColorTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateTrack<C3Vector, C3Vector>(
                this,
                &this->m_bones[light.boneIndex],
                light.diffuseColorTrack,
                modelLight.diffuseColorTrack,
                defaultValue
            );

            float mul = modelLight.diffuseIntensityTrack.currentValue * this->float198;

            // CORRECTED 2026-09-23: these three read ambientColorTrack until now, so a light's
            // diffuse colour was its AMBIENT colour scaled by the diffuse intensity, and the M2's
            // diffuse colour was parsed, animated and thrown away. The evidence that it is a slip
            // rather than the reference's behaviour: this block is guarded on diffuseColorTrack and
            // animates it into modelLight.diffuseColorTrack immediately above, and that value is
            // then read NOWHERE in the codebase -- it is the only animated track with no reader.
            // The line also predates the recomp effort; it arrives in the initial commit, inherited
            // from the upstream fork rather than transcribed from the reference.
            //
            // NOT confirmed against the reference: the corresponding block was not located in the
            // disassembly, so this is reasoned from frozen's own structure. It matters as of this
            // week, because CShaderEffect::ComputeLocalLights reads m_dirColor and local lights
            // now reach it.
            modelLight.light.m_dirColor.x = modelLight.diffuseColorTrack.currentValue.x * mul;
            modelLight.light.m_dirColor.y = modelLight.diffuseColorTrack.currentValue.y * mul;
            modelLight.light.m_dirColor.z = modelLight.diffuseColorTrack.currentValue.z * mul;
        }
    }

    for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
        auto& camera = this->m_shared->m_data->cameras[i];
        auto& modelCamera = this->m_cameras[i];

        auto& positionTrack = camera.positionTrack;
        if (
            positionTrack.sequenceTimes.Count() > 1
            || (positionTrack.sequenceTimes.Count() == 1 && positionTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateSplineTrack<M2SplineKey<C3Vector>, C3Vector>(
                this,
                this->m_bones,
                camera.positionTrack,
                modelCamera.positionTrack,
                defaultValue
            );
        }

        auto& targetTrack = camera.targetTrack;
        if (
            targetTrack.sequenceTimes.Count() > 1
            || (targetTrack.sequenceTimes.Count() == 1 && targetTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };
            M2AnimateSplineTrack<M2SplineKey<C3Vector>, C3Vector>(
                this,
                this->m_bones,
                camera.targetTrack,
                modelCamera.targetTrack,
                defaultValue
            );
        }

        auto& rollTrack = camera.rollTrack;
        if (
            rollTrack.sequenceTimes.Count() > 1
            || (rollTrack.sequenceTimes.Count() == 1 && rollTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 0.0f;
            M2AnimateSplineTrack<M2SplineKey<float>, float>(
                this,
                this->m_bones,
                camera.rollTrack,
                modelCamera.rollTrack,
                defaultValue
            );
        }
    }

    this->m_flag400 = 0;

    // TODO particles

    if (this->m_attachments || this->m_attachList) {
        this->AnimateAttachmentsMT();
    }

    this->m_animCounter = this->m_scene->uint14;
}

// ref: FUN_0082e140
// The cut-down animate: the path a model takes when it carries the 0x1000 flag. It shares
// AnimateMT's prologue and epilogue but skips every bone, colour, light and camera track, doing
// only what the model needs to be placed and tinted. Note it still writes matrixF4, which is why
// leaving this empty left anything on this path drawing with a stale transform.
void CM2Model::AnimateMTSimple(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6) {
    if (!this->m_loaded) {
        return;
    }

    // Already animated for this frame of the scene.
    if (this->m_animCounter == this->m_scene->uint14) {
        return;
    }

    auto data = this->m_shared->m_data;

    // Attachment visibility, inherited from the parent exactly as AnimateMT does it

    if (this->m_attachParent) {
        this->m_flag8 = this->m_attachParent->m_flag8 && this->m_flag80;
        this->m_flag10000 = this->m_attachParent->m_flag10000 && this->m_flag20000;

        // TODO dword174, copied from the parent's own
    }

    // The tint this model passes on. Data flag 0x4 means it ignores what its parent handed down
    // and stands on its own values; otherwise the parent's diffuse scales this model's and the
    // parent's emissive is added on top, unless flag 0x80000 opts out of the addition.
    if (data->flags & 0x4) {
        this->float198 = this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * this->m_baseAlpha;
        this->m_currentDiffuse = this->m_baseDiffuse;
        this->m_currentEmissive = this->m_baseEmissive;
    } else {
        this->m_currentDiffuse = {
            a3.x * this->m_baseDiffuse.x,
            a3.y * this->m_baseDiffuse.y,
            a3.z * this->m_baseDiffuse.z
        };

        this->m_currentEmissive = this->m_baseEmissive;

        this->float198 = this->m_flag100000 ? this->m_baseAlpha : a5 * this->m_baseAlpha;
        this->alpha19C = this->m_baseAlphaScale * a6 * this->m_baseAlpha;

        if (!this->m_flag80000) {
            this->m_currentEmissive.x += a4.x;
            this->m_currentEmissive.y += a4.y;
            this->m_currentEmissive.z += a4.z;
        }
    }

    // Global sequences

    for (int32_t i = 0; i < data->loops.Count(); i++) {
        auto loopLength = data->loops[i].length;
        this->m_loops[i] = loopLength ? (this->m_scene->m_time - this->uint74) % loopLength : 0;
    }

    this->matrixF4 = this->matrixB4 * *view;

    this->float88 = !this->m_attachParent || this->m_attachParent->m_flags & 0x1
        ? this->matrixF4.d2 * this->matrixF4.d2 + this->matrixF4.d1 * this->matrixF4.d1 + this->matrixF4.d0 * this->matrixF4.d0
        : this->m_attachParent->float88;

    if (this->m_time && this->m_scene->m_time) {
        this->m_time = this->m_scene->m_time;
    }

    // TODO the sequence playback record the reference advances here (its own +0x94), which
    // retimes the model's current sequence. frozen has no counterpart for that record yet.

    for (int32_t i = 0; i < data->textureWeights.Count(); i++) {
        auto& textureWeight = data->textureWeights[i];
        auto& modelTextureWeight = this->m_textureWeights[i];

        auto& weightTrack = textureWeight.weightTrack;

        if (
            weightTrack.sequenceTimes.Count() > 1
            || (weightTrack.sequenceTimes.Count() == 1 && weightTrack.sequenceTimes[0].times.Count() > this->uint90)
        ) {
            float defaultValue = 1.0f;
            M2AnimateTrack<fixed16, float>(
                this,
                this->m_bones,
                textureWeight.weightTrack,
                modelTextureWeight.weightTrack,
                defaultValue
            );
        }
    }

    if (data->textureTransforms.Count()) {
        this->AnimateTextureTransformsMT();
    }

    this->m_flag400 = 0;

    if (this->m_attachments || this->m_attachList) {
        this->AnimateAttachmentsMT();
    }

    this->m_animCounter = this->m_scene->uint14;
}

// Identified from its calls rather than its position: it reaches CM2Light::SetPosition
// (0x00835690), SetDirection (0x00834ae0) and SetVisible (0x008356f0), which is this
// function's light loop and nothing else in the class. Its entry test, `[+0x10] & 1`, is
// m_loaded.
// The runtime half of every emitter, animated through the same M2AnimateTrack that drives every
// other per-model track, against the emitter's own bone -- so a torch on a moving arm emits along
// the arm rather than along the model.
//
// The reference does this inside AnimateST. Here it is called from the particle system instead, and
// the difference is coverage rather than behaviour: CM2Scene::Animate unlinks each model from
// m_animateList as it walks it, so AnimateST reaches only the models that re-registered through
// SetAnimating this frame, while the particle system is driven for every model it holds a
// simulation for. A model in the second set and not the first would sit on default values, and the
// default emission rate is zero -- its fires would go out. When CM2Model::AnimateParticleEmitter
// lands it brings the reference's own driver and coverage, and this moves back.
// Does this track drive its emitter field this frame?
//
// Off the instructions at 0x830a99, because the decompilation renders it as a double dereference
// and loses the shape. What `uint90` means is not established -- frozen named it for its offset --
// so the comparison is transcribed rather than given an interpretation.
static bool M2ParticleTrackDrives(const M2Track<float>& track, uint32_t uint90) {
    uint32_t count = track.sequenceTimes.Count();

    if (count > 1) {
        return true;
    }

    if (count != 1) {
        return false;
    }

    return uint90 < track.sequenceTimes[0].times.Count();
}

// The basis swap between bone space and the emitter's frame: a +90 degree rotation about Z.
//
// The reference builds this once into a static at 0x00d411e0 behind a "already initialised" bit, which
// is why it reads as sixteen unrelated stores in the decompilation. Its -1.0 is 0x009e2ef4.
static const C44Matrix s_particleBasis(0.0f, 1.0f, 0.0f, 0.0f,
                                       -1.0f, 0.0f, 0.0f, 0.0f,
                                       0.0f, 0.0f, 1.0f, 0.0f,
                                       0.0f, 0.0f, 0.0f, 1.0f);

// Push this frame's animated values into one emitter, then place and step it.
//
// ref: FUN_008309c0
void CM2Model::AnimateParticleEmitter(float dt, int32_t index) {
    if (!this->m_loaded) {
        return;
    }

    const M2Particle& file = this->m_shared->m_data->particles[index];
    M2ModelParticle& runtime = this->m_particles[index];
    CM2ParticleEmitter* emitter = this->m_particleEmitters[index];

    // Frozen-only. The reference dereferences this unconditionally because its factory always
    // builds an emitter; frozen leaves a null for emitter type 3, which is unported.
    if (!emitter) {
        return;
    }

    if (!(file.flags & 0x8000)) {
        // Continuous: the emitter's own enable bit follows the rate's.
        if (runtime.rateActive) {
            emitter->m_flags |= 0x1;
        } else {
            emitter->m_flags &= ~0x1u;
        }
    } else if (!runtime.enabled || runtime.emissionRateTrack.currentValue <= 0.0f) {
        runtime.burstLatch = 0;
    } else {
        // A burst fires on the EDGE, not while held: the emitter's 0x40 is raised only on the
        // frame the latch goes from clear to set, and Emit clears 0x40 itself once it has spent
        // it.
        if (!runtime.burstLatch) {
            emitter->m_flags |= 0x40;
        }

        runtime.burstLatch = 1;
    }

    // Zero unless the rate is live, so a disabled emitter is told the rate rather than left with
    // its last one -- and SetEmissionRate ignores non-positive values, which is what makes that
    // "stop emitting" rather than "emit at zero".
    emitter->SetEmissionRate(runtime.rateActive ? runtime.emissionRateTrack.currentValue : 0.0f);

    // A culled emitter keeps last frame's values; a model that has never animated gets them
    // anyway, which is what seeds an emitter on its first frame.
    if (runtime.enabled || this->uint90 == 0) {
        if (M2ParticleTrackDrives(file.speedTrack, this->uint90)) {
            emitter->m_speed = runtime.speedTrack.currentValue;
        }

        if (M2ParticleTrackDrives(file.variationTrack, this->uint90)) {
            emitter->m_variation = runtime.variationTrack.currentValue;
        }

        if (M2ParticleTrackDrives(file.latitudeTrack, this->uint90)) {
            emitter->SetLatitude(runtime.latitudeTrack.currentValue);
        }

        if (M2ParticleTrackDrives(file.longitudeTrack, this->uint90)) {
            emitter->SetLongitude(runtime.longitudeTrack.currentValue);
        }

        if (M2ParticleTrackDrives(file.gravityTrack, this->uint90)) {
            emitter->m_gravity = runtime.gravityTrack.currentValue;
        }

        if (M2ParticleTrackDrives(file.lifeTrack, this->uint90)) {
            emitter->m_lifespan = runtime.lifeTrack.currentValue;
        }

        if (M2ParticleTrackDrives(file.widthTrack, this->uint90)) {
            emitter->SetWidth(runtime.widthTrack.currentValue);
        }

        if (M2ParticleTrackDrives(file.lengthTrack, this->uint90)) {
            emitter->SetLength(runtime.lengthTrack.currentValue);
        }

        if (M2ParticleTrackDrives(file.zsourceTrack, this->uint90)) {
            emitter->SetZSource(runtime.zsourceTrack.currentValue);
        }

        // Clamped in that order: the negative test first, then the ceiling.
        float alpha = this->float198;

        if (!(alpha >= 0.0f)) {
            alpha = 0.0f;
        } else if (alpha >= 1.0f) {
            alpha = 1.0f;
        }

        emitter->m_alpha = alpha;
    }

    if (!runtime.active) {
        return;
    }

    // FROZEN-ONLY GUARD. The reference indexes m_boneMatrices with no check, because its loader
    // guarantees the array is there and the index is in range. Frozen's does not: the bone matrix
    // array is allocated inside a `bones.Count()` branch, so a model with emitters and no bones
    // leaves it null, and nothing validates boneIndex against the bone count on the way in. Both
    // would fault here, and this runs for every model every frame now that the driver is wired.
    // Skipping the placement leaves the emitter un-stepped for the frame, which is the same thing
    // that happens to a culled one.
    if (!this->m_boneMatrices
            || file.boneIndex >= this->m_shared->m_data->bones.Count()) {
        return;
    }

    // All three matrix helpers here have the same receiver -- this local -- which the
    // decompilation does not show; see the note at the ribbon/particle block above.
    C44Matrix matrix = this->m_boneMatrices[file.boneIndex];

    matrix.Translate(file.position);
    matrix *= this->m_scene->m_viewInv;
    matrix = s_particleBasis * matrix;

    // The camera position is the view-inverse's translation row, which is what the reference
    // passes as `scene + 0xf4` (0xc4 + 0x30). Not a field of its own.
    C3Vector cameraPosition = { this->m_scene->m_viewInv.d0,
                                this->m_scene->m_viewInv.d1,
                                this->m_scene->m_viewInv.d2 };

    // The reference passes `model + 0x174`, the matrix this model is placed relative to. Frozen
    // has no such field, so null -- which Place treats as "store the transform as it is", correct
    // for every model today because nothing would set it.
    emitter->Update(dt, matrix, cameraPosition, nullptr);

    // Animate the emitter's subtree of spawned models.
    //
    // FindSpawnedModel CONSUMES the index as it descends, so each iteration needs a fresh copy --
    // passing the loop variable itself would leave it wrecked.
    //
    // The receiver for both Animate calls is the SPAWNED model rather than the owner. Ghidra
    // loses that; the disassembly reloads ecx from the FindSpawnedModel result at 0x830d66.
    //
    // This finds nothing today: nothing allocates the 0x40-byte pool, so no emitter carries
    // models. It is here because the four functions it needs are all ported now -- the subtree
    // walkers came with that pool's element type -- and leaving the gap would mean a silent hole
    // the moment an allocator lands.
    uint32_t spawned = emitter->CountSpawnedModels();

    for (uint32_t j = 0; j < spawned; j++) {
        uint32_t index = j;

        CM2Model* model = emitter->FindSpawnedModel(index);

        // The reference does not check; frozen does, because a count and a walk that disagree
        // would fault here rather than skip.
        if (!model) {
            continue;
        }

        model->AnimateMT(&this->m_scene->m_view, this->m_currentDiffuse, this->m_currentEmissive,
                         this->float198, this->alpha19C);
        model->AnimateST();

        // The spawned model inherits the owner's lighting rather than resolving its own.
        model->m_currentLighting = this->m_currentLighting;
    }
}

void CM2Model::AnimateParticleTracks() {
    if (!this->m_particles || !this->m_shared || !this->m_shared->m_data) {
        return;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
        auto& particle = this->m_shared->m_data->particles[i];
        auto& modelParticle = this->m_particles[i];

        auto bone = particle.boneIndex < this->m_shared->m_data->bones.Count()
            ? &this->m_bones[particle.boneIndex]
            : nullptr;

        // speed and life default to 1.0, not 0.0: an emitter whose track carries no keys still
        // emits, and a particle with no speed and no lifespan is not a particle. Both defaults are
        // carried over from the stand-in sampler this replaces; the reference's own have not been
        // read yet.
        M2AnimateTrack<float, float>(this, bone, particle.speedTrack, modelParticle.speedTrack, 1.0f);
        M2AnimateTrack<float, float>(this, bone, particle.variationTrack, modelParticle.variationTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.latitudeTrack, modelParticle.latitudeTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.longitudeTrack, modelParticle.longitudeTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.gravityTrack, modelParticle.gravityTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.lifeTrack, modelParticle.lifeTrack, 1.0f);
        M2AnimateTrack<float, float>(this, bone, particle.emissionRateTrack, modelParticle.emissionRateTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.widthTrack, modelParticle.widthTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.lengthTrack, modelParticle.lengthTrack, 0.0f);
        M2AnimateTrack<float, float>(this, bone, particle.zsourceTrack, modelParticle.zsourceTrack, 0.0f);

        // The gates the reference's driver latches and reads: whether the emitter ran at all, and
        // whether it is handed the animated rate or zero.
        modelParticle.enabled = 1;
        modelParticle.rateActive = modelParticle.emissionRateTrack.currentValue > 0.0f ? 1 : 0;
        modelParticle.active = this->m_flag10000 || this->m_flag20000 ? 1 : 0;
    }
}

// The tag below spent at least two sessions on the wrong function. It sat above
// AnimateParticleTracks -- itself probably displaced by an earlier insertion -- and moved again
// onto M2ParticleTrackDrives when that static was added in front of it, which is when the
// callgraph diff on CM2Scene::Animate made it visible by listing a brand-new static as one of the
// reference's own callees.
//
// It belongs here: FUN_008309c0's tail calls FUN_00828a00 on each spawned model, which is
// AnimateST's job, and AnimateST had no tag at all.
//
// ref: FUN_00828a00
void CM2Model::AnimateST() {
    if (!this->m_loaded) {
        return;
    }

    auto attachParent = this->m_attachParent;

    if (!attachParent) {
        this->m_currentLighting = &this->m_lighting;
    } else {
        this->m_flag8000 = attachParent->m_flag8000;

        if (this->m_flag8000 && attachParent->m_flags & 0x1) {
            this->m_currentLighting = &this->m_lighting;
        } else {
            this->m_currentLighting = attachParent->m_currentLighting;
        }
    }

    if (!this->m_currentLighting) {
        this->m_currentLighting = &this->m_lighting;
    }

    for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
        auto& light = this->m_shared->m_data->lights[i];
        auto& modelLight = this->m_lights[i];

        int32_t visible = 0;
        if (modelLight.uint64 && modelLight.visibilityTrack.currentValue) {
            visible = 1;

            if (light.lightType == M2LIGHT_1) {
                // The reference chains two transforms: the light's model-space position through
                // its bone, which lands in camera space here as every bone matrix does, and then
                // through m_viewInv back out to world space -- the same m_viewInv the directional
                // branch below uses, at scene + 0xc4.
                C3Vector bone = light.position * this->m_boneMatrices[light.boneIndex];
                C3Vector world = bone * this->m_scene->m_viewInv;

                modelLight.light.SetPosition(world);
            } else {
                float v10 = -this->m_boneMatrices[light.boneIndex].c0;
                float v11 = -this->m_boneMatrices[light.boneIndex].c1;
                float v12 = -this->m_boneMatrices[light.boneIndex].c2;

                float x = this->m_scene->m_viewInv.a0 * v10
                        + this->m_scene->m_viewInv.b0 * v11
                        + this->m_scene->m_viewInv.c0 * v12;
                float y = this->m_scene->m_viewInv.a1 * v10
                        + this->m_scene->m_viewInv.b1 * v11
                        + this->m_scene->m_viewInv.c1 * v12;
                float z = this->m_scene->m_viewInv.a2 * v10
                        + this->m_scene->m_viewInv.b2 * v11
                        + this->m_scene->m_viewInv.c2 * v12;

                C3Vector dir = { x, y, z };

                modelLight.light.SetDirection(dir);
            }
        }

        modelLight.light.SetVisible(visible);

        // Stamped every frame, visible or not. CM2Scene::SelectLights compares it against the
        // same counter and switches off any point light that has fallen behind.
        modelLight.light.m_updateStamp = this->m_scene->uint14;
    }

    if (this->m_shared->m_data->cameras.Count()) {
        this->AnimateCamerasST();
    }

    // MISSING: the ribbon and particle emitter update. This is the single largest gap in this
    // function -- `--diff 00828a00` scores it 46% and the whole shortfall is one contiguous block
    // that belongs right here, between the camera update above and the draw-list link below.
    // Decompiled 2026-09-23 so the next attempt does not start cold:
    //
    //   1. Delta time, which nothing else in frozen computes:
    //          now = m_scene->time (scene + 0xc)
    //          ticks = now - this->[0x8c]           // a per-model last-update stamp frozen lacks
    //          dt = (float)ticks; if (ticks < 0) dt += 4294967296.0f;   // unsigned fixup
    //          dt *= 0.001f;                        // the 1/1000 at 0x009e1134
    //          this->[0x8c] = now
    //      The fixup is the reference's own way of reading the subtraction as unsigned; it matters
    //      only across a wrap, but it is one instruction and there is no reason to drop it.
    //
    //   2. For each of m_data->ribbons (count at data + 0x120, array at + 0x124, stride 0xb0):
    //      take the per-model runtime state (this + 0x2b8, stride 0x50) and the emitter object
    //      (this + 0x2bc, an array of pointers), then push the animated tracks into the emitter --
    //      colour, alpha SCALED BY this->float198, height above, height below, texture slot. Each
    //      push is guarded on the track actually having keys. Then copy the bone matrix
    //      (m_boneMatrices[ribbon->boneIndex], 0x40 bytes), transform by it, transform by
    //      m_scene->m_viewInv (scene + 0xc4), and if this->m_flags & 0x8000 advance the emitter by
    //      dt.
    //
    //   3. For each of m_data->particles (count at data + 0x128): this->FUN_008309c0(dt, i).
    //
    // STATUS, 2026-09-24. The particle half is no longer blocked on the object graph: the emitter
    // runtime is ported (src/model/CM2ParticleEmitter.*), m_particles at +0x2c0 and
    // m_particleEmitters at +0x2c4 both exist, and InitializeLoaded's factory builds a plane or
    // sphere emitter per M2Particle. What is left is FUN_008309c0 itself, and the ribbon half,
    // which still has no runtime state array.
    //
    // FUN_008309c0 reads as two halves. The first pushes this frame's animated values into the
    // emitter: the enable bits from the runtime block's bytes at +0x80/+0x84/+0x86 against file
    // flag 0x8000, then the emission rate through vtable[10], and speed, variation, latitude,
    // longitude, gravity, life, width, length and z source -- each through its own setter or
    // field, and each guarded on the track having more than one key, or one key whose time is
    // still ahead of the model's current time. Finally the model's alpha, clamped to 0..1.
    //
    // The second half places and steps it. Decoded in full 2026-09-24; it reads as
    //
    //     C44Matrix matrix = this->m_boneMatrices[file.boneIndex];
    //     matrix.Translate(file.position);
    //     matrix *= this->m_scene->m_viewInv;
    //     matrix = PARTICLE_BASIS * matrix;
    //     emitter->Update(dt, matrix, <camera position>, <relative matrix>);
    //     <then the spawned-model subtree walk>
    //
    // with four things that are not apparent from the decompilation:
    //   - PARTICLE_BASIS is the static matrix at 0x00d411e0, built once on first use. Its rows are
    //     (0,1,0,0), (-1,0,0,0), (0,0,1,0), (0,0,0,1) -- the -1.0 is 0x009e2ef4 -- so it is a +90
    //     degree rotation about Z, the basis swap between bone space and the emitter's frame. It
    //     reads as noise until the constant is looked up.
    //   - Ghidra drops ECX on the three matrix helpers (0x004c1b30 Translate, 0x004c2370 which is
    //     operator*=, 0x00407f80 the copy). All three have the SAME receiver: the local copy of
    //     the bone matrix. Read off the disassembly at 0x830c5a, 0x830c6c and 0x830d09.
    //   - `scene + 0xf4` is not a field. It is 0xc4 + 0x30, the TRANSLATION ROW of m_viewInv,
    //     which for a view-inverse is the camera position -- which is what Update's second
    //     parameter wants.
    //   - `model + 0x174` is a C44Matrix* the model is expressed relative to; the setter at
    //     0x00824479 takes its AffineInverse, the same call Place makes of the same argument.
    //     FROZEN HAS NO SUCH FIELD, so that argument has to be null until it does -- which is
    //     correct for every model today, because nothing would set it.
    //
    // The subtree tail (FUN_0097ba30 counts, FUN_0097ba70 reaches the i-th spawned model) is no
    // longer blocked: both are ported, along with the 0x40-byte model pool they read.
    //
    // What is still missing for the model-carrying path alone is FUN_0097e8d0, the spawned-model
    // placement pass, which reads a matrix array off the global at 0x00c5df88. Emitters that do
    // not spawn models do not need it.
    //
    // src/world/ParticleFx.cpp is still a separate stand-in simulation, not this. BOTH halves are
    // wired below as of 2026-09-27; the line that used to stand here saying the ribbon half had no
    // runtime state array was true when written and is not any more -- m_ribbons and
    // m_ribbonEmitters are built by InitializeLoaded.

    // The emitters' own delta, which nothing else in frozen computes. HOISTED out of the particle
    // block 2026-09-27 because the ribbons need the same value and the reference computes it once
    // before both of them, storing the new timestamp immediately -- so a model with ribbons and
    // particles must not advance it twice.
    //
    // The subtraction is done in unsigned ticks and then fixed up, which only matters across a wrap
    // of the millisecond clock -- but without it a wrap gives a hugely negative dt that Update's
    // guard would swallow silently, so the branch is kept.
    uint32_t now = this->m_scene->m_time;
    int32_t ticks = static_cast<int32_t>(now - this->uint8c);

    float dt = static_cast<float>(ticks);

    if (ticks < 0) {
        dt += 4294967296.0f;
    }

    dt *= 0.001f;

    this->uint8c = now;

    // THE RIBBONS, and they come before the particles because that is the reference's order.
    //
    // Every value pushed at the emitter here is ALREADY animated -- the track pass earlier in this
    // function put it in M2ModelRibbon. This block only decides what to forward and then places and
    // steps the emitter, which is why it calls no M2AnimateTrack of its own.
    if (this->m_ribbonEmitters && this->m_shared->m_data->ribbons.Count()) {
        for (int32_t i = 0; i < this->m_shared->m_data->ribbons.Count(); i++) {
            const M2Ribbon& file = this->m_shared->m_data->ribbons[i];
            M2ModelRibbon& state = this->m_ribbons[i];
            CM2Ribbon* emitter = this->m_ribbonEmitters[i];

            // The same "does this track carry anything for us" gate the attachment, bone and colour
            // passes above already use, five times over. Alpha is the exception: it is pushed
            // unconditionally, scaled by the model's own alpha.
            if (file.colorTrack.sequenceTimes.Count() > 1
                    || (file.colorTrack.sequenceTimes.Count() == 1
                        && file.colorTrack.sequenceTimes[0].times.Count() > this->uint90)) {
                emitter->SetColor(state.colorTrack.currentValue.x, state.colorTrack.currentValue.y,
                                  state.colorTrack.currentValue.z);
            }

            emitter->SetAlpha(state.alphaTrack.currentValue * this->float198);

            if (file.heightAboveTrack.sequenceTimes.Count() > 1
                    || (file.heightAboveTrack.sequenceTimes.Count() == 1
                        && file.heightAboveTrack.sequenceTimes[0].times.Count() > this->uint90)) {
                emitter->SetHeightAbove(state.heightAboveTrack.currentValue);
            }

            if (file.heightBelowTrack.sequenceTimes.Count() > 1
                    || (file.heightBelowTrack.sequenceTimes.Count() == 1
                        && file.heightBelowTrack.sequenceTimes[0].times.Count() > this->uint90)) {
                emitter->SetHeightBelow(state.heightBelowTrack.currentValue);
            }

            if (file.textureSlotTrack.sequenceTimes.Count() > 1
                    || (file.textureSlotTrack.sequenceTimes.Count() == 1
                        && file.textureSlotTrack.sequenceTimes[0].times.Count() > this->uint90)) {
                emitter->SetTextureSlot(state.textureSlotTrack.currentValue);
            }

            // FROZEN-ONLY GUARD, the same one the particle driver carries and for the same reason:
            // the reference indexes m_boneMatrices with no check because its loader guarantees both
            // the array and the index, and frozen's allocates the array inside a `bones.Count()`
            // branch and never validates boneIndex. Skipping leaves the ribbon unplaced for the
            // frame, which is the same thing the visibility gate below already does.
            if (!this->m_boneMatrices
                    || file.boneIndex >= this->m_shared->m_data->bones.Count()) {
                continue;
            }

            // The placement: the ribbon's bone, moved to the ribbon's own position on that bone,
            // then back out of view space. frozen bakes the view into its bone matrices exactly as
            // the reference does, so the same m_viewInv undoes it -- the trail's geometry is kept in
            // world space.
            C44Matrix placement = this->m_boneMatrices[file.boneIndex];

            placement.Translate(file.position);
            placement *= this->m_scene->m_viewInv;

            // Visibility drives BOTH of these, in opposite senses: the trail is raised while it is
            // visible, and emission is suppressed while it is not. Stepping an invisible ribbon
            // rather than skipping it is what keeps its trail ageing out instead of freezing.
            emitter->SetAbove(state.visibilityTrack.currentValue != 0);

            if (this->m_flag8000) {
                C3Vector offset = { 0.0f, 0.0f, 0.0f };

                emitter->SetPosition(placement, offset, this->m_particleRelative);
                emitter->Update(dt, state.visibilityTrack.currentValue == 0);
            }
        }
    }

    if (this->m_particleEmitters && this->m_shared->m_data->particles.Count()) {
        for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
            this->AnimateParticleEmitter(dt, i);
        }
    }

    if (this->m_flag8) {
        this->m_drawPrev = &this->m_scene->m_drawList;
        this->m_drawNext = this->m_scene->m_drawList;
        this->m_scene->m_drawList = this;

        if (this->m_drawNext) {
            this->m_drawNext->m_drawPrev = &this->m_drawNext;
        }
    }

    // TODO

    // Animate attached models

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        bool animate;

        if (model->m_attachIndex == 0xFFFF) {
            animate = model->m_flag40000;
        } else {
            animate = this->m_attachments[model->m_attachIndex].visibilityTrack.currentValue;
        }

        if (animate) {
            model->AnimateST();
        }
    }

    if (this->float198 == 1.0f) {
        this->uint90 = 1;
    }
}

// ref: FUN_0082d6f0
void CM2Model::AnimateTextureTransformsMT() {
    for (int32_t i = 0; i < this->m_shared->m_data->textureTransforms.Count(); i++) {
        static C3Vector center = { 0.5f, 0.5f, 0.0f };

        auto& textureTransform = this->m_shared->m_data->textureTransforms[i];
        auto& modelTextureTransform = this->m_textureTransforms[i];
        auto& textureMatrix = this->m_textureMatrices[i];

        textureMatrix.Identity();

        // Rotation

        auto& rotationTrack = textureTransform.rotationTrack;
        auto& modelRotationTrack = modelTextureTransform.rotationTrack;

        if (rotationTrack.sequenceTimes.Count() > 0) {
            C4Quaternion defaultValue = { 0.0f, 0.0f, 0.0f, 1.0f };

            M2AnimateTrack(this, this->m_bones, rotationTrack, modelRotationTrack, defaultValue);

            textureMatrix.Translate(center);
            textureMatrix.Rotate(modelRotationTrack.currentValue);
            textureMatrix.Translate(-center);
        }

        // Scale

        auto& scaleTrack = textureTransform.scaleTrack;
        auto& modelScaleTrack = modelTextureTransform.scaleTrack;

        if (scaleTrack.sequenceTimes.Count() > 0) {
            C3Vector defaultValue = { 1.0f, 1.0f, 1.0f };

            M2AnimateTrack(this, this->m_bones, scaleTrack, modelScaleTrack, defaultValue);

            textureMatrix.Translate(center);
            textureMatrix.Scale(modelScaleTrack.currentValue);
            textureMatrix.Translate(-center);
        }

        // Translation

        auto& translationTrack = textureTransform.translationTrack;
        auto& modelTranslationTrack = modelTextureTransform.translationTrack;

        if (translationTrack.sequenceTimes.Count() > 0) {
            C3Vector defaultValue = { 0.0f, 0.0f, 0.0f };

            M2AnimateTrack(this, this->m_bones, translationTrack, modelTranslationTrack, defaultValue);

            textureMatrix.Translate(modelTranslationTrack.currentValue);
        }
    }
}

// ref: FUN_00831630
void CM2Model::AttachToParent(CM2Model* parent, uint32_t id, const C3Vector* position, int32_t a5) {
    if (this->m_attachParent) {
        this->DetachFromParent();
    }

    this->SetAnimating(0);

    // Look up parent attachment index by given ID

    uint16_t attachIndex = 0xFFFF;

    if (parent->m_loaded) {
        auto& parentAttachmentIndicesById = parent->m_shared->m_data->attachmentIndicesById;

        if (id < parentAttachmentIndicesById.Count()) {
            attachIndex = parentAttachmentIndicesById[id];
        }

        if (attachIndex == 0xFFFF && !a5) {
            return;
        }
    }

    this->m_attachIndex = attachIndex;
    this->m_attachId = id;
    this->m_attachParent = parent;

    this->m_flag80 = 1;
    this->m_flag20000 = 1;
    this->m_flag40000 = a5 ? 1 : 0;

    this->m_attachPrev = &parent->m_attachList;
    this->m_attachNext = parent->m_attachList;
    if (parent->m_attachList) {
        parent->m_attachList->m_attachPrev = &this->m_attachNext;
    }
    parent->m_attachList = this;

    if (!this->m_loaded || !this->m_flag100) {
        auto model = this->m_attachParent;
        while (model) {
            model->m_flag100 = 0;
            model = model->m_attachParent;
        }
    }

    if (!this->m_flag2) {
        auto model = this->m_attachParent;
        while (model) {
            model->m_flag200 = 0;
            model = model->m_attachParent;
        }
    }

    if (position && this->m_attachParent->m_loaded && this->m_attachIndex != 0xFFFF) {
        auto transform = parent->GetAttachmentWorldTransform(id);
        auto scale = sqrt(transform.a0 * transform.a0 + transform.a1 * transform.a1 + transform.a2 * transform.a2);
        transform = transform.AffineInverse(scale);

        if (!this->m_flag8000) {
            this->matrixB4.Identity();
        }

        auto transformedPosition = *position * transform;

        this->matrixB4.d0 = transformedPosition.x;
        this->matrixB4.d1 = transformedPosition.y;
        this->matrixB4.d2 = transformedPosition.z;

        this->m_flag8000 = 1;
    }

    this->AddRef();
}

// ref: FUN_00834540
void CM2Model::AttachToScene(CM2Scene* scene) {
    this->DetachFromScene();

    this->m_scene = scene;

    this->m_scenePrev = &this->m_scene->m_modelList;
    this->m_sceneNext = this->m_scene->m_modelList;
    this->m_scene->m_modelList = this;
    if (this->m_sceneNext) {
        this->m_sceneNext->m_scenePrev = &this->m_sceneNext;
    }

    if (this->m_loaded) {
        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            this->m_lights[i].light.Initialize(this->m_scene);
        }

        // Start every bone on Stand: id 0 when animation 0 resolves to something the model
        // carries, otherwise the model's first sequence.
        auto data = this->m_shared->m_data;

        M2SequenceFallback fallback;
        this->Sub826350(fallback, 0);

        uint16_t sequenceId = CM2Model::Sub825E00(data, fallback.uint0) ? 0 : data->sequences[0].id;

        this->SetBoneSequence(0xFFFFFFFF, sequenceId, 0xFFFFFFFF, 0, 1.0f, 0, 1);
    } else {
        for (auto modelCall = this->m_modelCallList; modelCall; modelCall = modelCall->modelCallNext) {
            modelCall->time += this->m_scene->m_time;
        }
    }
}

// Drop this model's parked bone-sequence requests for one bone, so a request that has been
// superseded does not fire later and overwrite the sequence that replaced it. SetBoneSequence
// calls it immediately before parking or applying a new one.
//
// It was an empty body with two live callers, which made the cancel a no-op: every deferred
// request still landed when its .anim data arrived, however many times the bone had been
// re-sequenced in the meantime.
//
// The `primary` argument selects which half of the records to drop, and it is compared against
// bit 1 of the record's flags -- the bit SetBoneSequenceDeferred sets from its own a9. A primary
// request never cancels a secondary one or the other way round.
//
// Two branches, because the records cannot always be unlinked. CM2Shared::SequenceLoadedCallback
// raises m_flag10 while it walks these same lists, so during that walk the records are MARKED with
// bit 8 and the callback drops them as it passes -- which it already does. Outside the walk they
// are unlinked and freed here. The reference's removal helper captures the next pointer before
// freeing, so this does too.
// Drop every one of this model's parked bone-sequence requests, whatever bone or slot they are
// for. CancelDeferredSequences below is the same walk with a narrower predicate; this one matches
// on the model alone, because the model is going away.
//
// **Without this, destroying a model with a deferred request pending is a use-after-free.** The
// record keeps a raw CM2Model* and CM2Shared::SequenceLoadedCallback calls
// playback->model->ApplySequencePlayBack() on it when the .anim data lands. frozen already
// implemented the consumer half of the protocol -- the callback drops records carrying flag 8 --
// and this is the producer that was missing, so nothing ever set the flag for a dying model.
//
// The same two branches as CancelDeferredSequences, and for the same reason: while
// SequenceLoadedCallback is walking these lists it raises m_flag10, and records may then only be
// marked, not unlinked.
// ref: FUN_00831e20
void CM2Model::CancelAllDeferredSequences() {
    if (!this->m_shared) {
        return;
    }

    auto shared = this->m_shared;

    for (auto load = shared->m_sequenceLoads.Head(); load; load = shared->m_sequenceLoads.Next(load)) {
        for (auto playback = load->playbacks.Head(); playback;) {
            auto next = load->playbacks.Next(playback);

            if (playback->model == this) {
                if (shared->m_flag10) {
                    playback->flags |= 8;
                } else {
                    load->playbacks.UnlinkNode(playback);
                    STORM_FREE(playback);
                }
            }

            playback = next;
        }
    }
}

// ref: FUN_00831ec0
void CM2Model::CancelDeferredSequences(uint32_t boneIndex, bool primary) {
    auto shared = this->m_shared;

    for (auto load = shared->m_sequenceLoads.Head(); load; load = shared->m_sequenceLoads.Next(load)) {
        for (auto playback = load->playbacks.Head(); playback;) {
            auto next = load->playbacks.Next(playback);

            bool match = playback->model == this
                && playback->boneIndex == boneIndex
                && ((playback->flags >> 1) & 1) == (primary ? 1 : 0);

            if (match) {
                if (shared->m_flag10) {
                    playback->flags |= 8;
                } else {
                    load->playbacks.UnlinkNode(playback);
                    STORM_FREE(playback);
                }
            }

            playback = next;
        }
    }
}

// ref: FUN_00827560
void CM2Model::DetachAllChildrenById(uint32_t id) {
    // Hang on to attachNext in case model is freed during detach
    CM2Model* attachNext = nullptr;

    // Detach any model matching provided attach ID
    for (auto model = this->m_attachList; model; model = attachNext) {
        attachNext = model->m_attachNext;

        if (model->m_attachId == id) {
            model->DetachFromParent();
        }
    }
}

// ref: FUN_008274f0
void CM2Model::DetachFromParent() {
    if (this->m_attachPrev) {
        *this->m_attachPrev = this->m_attachNext;
    }

    if (this->m_attachNext) {
        this->m_attachNext->m_attachPrev = this->m_attachPrev;
    }

    this->m_flag40000 = 0;

    this->m_attachPrev = nullptr;
    this->m_attachNext = nullptr;
    this->m_attachParent = nullptr;
    this->m_attachId = -1;

    // TODO this->dword174 = 0

    this->Release();
}

void CM2Model::DetachFromScene() {
    // Unlink from scene list

    if (this->m_scenePrev) {
        *this->m_scenePrev = this->m_sceneNext;
    }

    if (this->m_sceneNext) {
        this->m_sceneNext->m_scenePrev = this->m_scenePrev;
    }

    this->m_scenePrev = nullptr;
    this->m_sceneNext = nullptr;

    // TODO

    this->m_scene = nullptr;
}

// ref: FUN_008284d0
void CM2Model::FindKey(M2ModelBoneSeq* sequence, const M2TrackBase& track, uint32_t& currentKey, uint32_t& nextKey, float& ratio) {
    if (!track.sequenceTimes.Count()) {
        currentKey = 0;
        nextKey = 0;

        ratio = 0.0f;

        return;
    }

    uint32_t sequenceTime = sequence->uint0;
    uint32_t sequenceIndex = sequence->uint4;

    if (track.loopIndex == 0xFFFF) {
        if (sequenceIndex >= track.sequenceTimes.Count()) {
            sequenceIndex = 0;
        }
    } else {
        sequenceTime = this->m_loops[track.loopIndex];
        sequenceIndex = 0;
    }

    auto& sequenceTimes = track.sequenceTimes[sequenceIndex];
    auto numKeys = sequenceTimes.times.Count();
    auto keyTimes = sequenceTimes.times.Data();

    if (numKeys <= 1) {
        currentKey = 0;
        nextKey = 0;

        ratio = 0.0f;

        return;
    }

    if (currentKey >= numKeys) {
        currentKey = 0;
    }

    uint32_t foundKey = currentKey;
    auto v16 = sequenceTime - keyTimes[currentKey];

    if (v16 >= 500) {
        if (v16 < 0xFFFFFE0C) {
            if (sequenceTime >= 500) {
                // Perform binary search for key containing sequence time

                int32_t lowKey = 0;
                int32_t highKey = numKeys - 1;

                while (lowKey < highKey) {
                    int32_t midKey = (lowKey + highKey) / 2;

                    if (midKey + 1 >= numKeys) {
                        lowKey = midKey;
                        break;
                    }

                    if (sequenceTime >= keyTimes[midKey] && sequenceTime < keyTimes[midKey + 1]) {
                        lowKey = midKey;
                        break;
                    }

                    if (sequenceTime >= keyTimes[midKey]) {
                        lowKey = midKey + 1;
                    } else {
                        highKey = midKey - 1;
                    }
                }

                foundKey = lowKey;
            } else {
                // Perform linear search forward from zero for key containing sequence time

                uint32_t key = 0;

                while (key < numKeys - 1 && sequenceTime >= keyTimes[key + 1]) {
                    key++;
                }

                foundKey = key;
            }
        } else if (currentKey > 0) {
            // Perform linear search backward from current key for key containing sequence time

            uint32_t key = currentKey;

            while (key > 0 && sequenceTime < keyTimes[key]) {
                key--;
            }

            foundKey = key;
        }
    } else if (currentKey < numKeys - 1) {
        // Perform linear search forward from current key for key containing sequence time

        uint32_t key = currentKey;

        while (key < numKeys - 1 && sequenceTime >= keyTimes[key + 1]) {
            key++;
        }

        foundKey = key;
    }

    if (foundKey + 1 >= numKeys) {
        currentKey = foundKey;
        nextKey = foundKey;

        ratio = 0.0f;
    } else {
        currentKey = foundKey;
        nextKey = foundKey + 1;

        auto currentKeyTime = keyTimes[currentKey];
        auto nextKeyTime = keyTimes[nextKey];

        ratio = static_cast<float>(sequenceTime - currentKeyTime) / static_cast<float>(nextKeyTime - currentKeyTime);
    }
}

void CM2Model::FreeExternalResources() {
    if (!this->m_loaded) {
        return;
    }

    // TODO

    if (this->m_textures) {
        for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            auto texture = this->m_textures[i];

            if (texture) {
                HandleClose(texture);
            }
        }
    }

    // TODO
}

void CM2Model::FreeInternalResources() {
    if (!this->m_internalResources) {
        return;
    }

    if (this->m_bones) {
        this->m_bones = nullptr;
    }

    if (this->m_loops) {
        this->m_loops = nullptr;
    }

    if (this->m_skinSections) {
        this->m_skinSections = nullptr;
    }

    if (this->m_colors) {
        this->m_colors = nullptr;
    }

    if (this->m_textureWeights) {
        this->m_textureWeights = nullptr;
    }

    if (this->m_textureTransforms) {
        this->m_textureTransforms = nullptr;
    }

    if (this->m_attachments) {
        this->m_attachments = nullptr;
    }

    if (this->m_lights) {
        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            this->m_lights[i].light.~CM2Light();
        }

        this->m_lights = nullptr;
    }

    if (this->m_cameras) {
        this->m_cameras = nullptr;
    }

    // TODO
    // if (this->m_ribbons) {
    //     this->m_ribbons = nullptr;
    // }

    // Every emitter owns three TSGrowableArray allocations -- the particle pool and the two index
    // arrays -- and those live in Storm's heap, NOT in the pooled buffer this block is about to
    // release. So they have to be destructed explicitly, the same way m_lights is above; freeing
    // the buffer alone would leak a pool per emitter on every model destroyed, which for a busy
    // scene is most models. The destructor is virtual, so this reaches the plane or sphere
    // subclass's.
    if (this->m_particleEmitters) {
        for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
            // Null for any emitter type frozen does not build -- see the factory in
            // InitializeLoaded.
            if (this->m_particleEmitters[i]) {
                this->m_particleEmitters[i]->~CM2ParticleEmitter();
            }
        }

        this->m_particleEmitters = nullptr;
    }

    if (this->m_particles) {
        this->m_particles = nullptr;
    }

    // The two matrix arrays are NOT part of the pooled internal-resources block: InitializeLoaded
    // allocates each with its own SMemAlloc. Nothing freed them, so every model destroyed leaked
    // 64 bytes per bone plus 64 per texture transform -- a few kilobytes for a character, on every
    // unit that despawns and every model the character screen builds and throws away.
    //
    // Found on 2026-09-23 by following the fidelity diff on CM2Model::InitializeLoaded, where the
    // reference calls SequenceBufferAlloc twice and frozen calls SMemAlloc. That is a second,
    // separate divergence and it stands: the reference's allocator returns 16-byte-aligned memory
    // for exactly these two arrays, while frozen's SMemAlloc aligns to 8. Nothing here uses SSE on
    // them, so it is not a correctness problem, and closing it means exporting the alloc/free pair
    // out of the anonymous namespace in CM2Shared.cpp where they currently live.
    if (this->m_boneMatrices) {
        SMemFree(this->m_boneMatrices, __FILE__, __LINE__, 0);
        this->m_boneMatrices = nullptr;
    }

    if (this->m_textureMatrices) {
        SMemFree(this->m_textureMatrices, __FILE__, __LINE__, 0);
        this->m_textureMatrices = nullptr;
    }

    STORM_FREE(this->m_internalResources);
}

// ref: FUN_00831410
C44Matrix CM2Model::GetAttachmentWorldTransform(uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetAttachmentWorldTransform");
    }

    auto& attachmentIndicesById = this->m_shared->m_data->attachmentIndicesById;
    auto& attachments = this->m_shared->m_data->attachments;

    // Look up attachment index

    uint16_t attachIndex = 0xFFFF;
    if (id < attachmentIndicesById.count) {
        attachIndex = attachmentIndicesById[id];
    }

    // Look up bone index

    uint16_t boneIndex = 0xFFFF;
    if (attachIndex < this->m_shared->m_data->attachments.count) {
        boneIndex = attachments[attachIndex].boneIndex;
    }

    // Animate

    this->Animate();

    // Calculate attachment world transform

    C44Matrix transform;

    if (attachIndex == 0xFFFF) {
        transform = this->m_boneMatrices[0];
    } else {
        transform = this->m_boneMatrices[boneIndex];
        transform.Translate(attachments[attachIndex].position);
    }

    return transform * this->m_scene->m_viewInv;
}

// ref: FUN_00830f90
void CM2Model::ForceAnimate() {
    if (!this->m_loaded) {
        return;
    }

    // Animate() is a no-op for a model whose m_animCounter already matches the scene's, which
    // is how a model shared by several draws is only stepped once a frame. Putting the counter
    // one BEHIND defeats exactly that test and nothing else -- so this is "step it again, now",
    // for a caller that has just changed something the current bone matrices were built from.
    this->m_animCounter = this->m_scene->uint14 - 1;

    this->Animate();
}

// ref: FUN_00827780
uint32_t CM2Model::GetEventTimestamp(uint32_t animId, uint32_t eventId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;

    // The reference INLINES the animation-id lookup here -- the same hash with quadratic
    // probing, falling back to a linear scan when the model carries no hash -- rather than
    // calling it. Sub8260C0 with no variation hops is that lookup exactly, so this calls it
    // instead of keeping a third copy of it in this file.
    uint16_t sequenceIndex = CM2Model::Sub8260C0(data, animId, 0);

    if (sequenceIndex >= data->sequences.Count()) {
        return 0;
    }

    for (uint32_t i = 0; i < data->events.Count(); i++) {
        auto& event = data->events[i];

        if (event.eventId != eventId) {
            continue;
        }

        // DIVERGENCE, and a deliberate one. The reference indexes the event's per-sequence
        // timestamp array by the sequence index and then takes element 0 of it, with NO count
        // check on either -- it trusts every event track to carry one entry per sequence and
        // every entry to hold at least one time. An M2Array resolves its data as (its own
        // address + offset), so element 0 of an EMPTY one is a wild pointer rather than null
        // (CLAUDE.md lists this first among the bug classes here), and a model that breaks that
        // trust would read garbage or fault. Both counts are checked, and a miss answers 0 --
        // which is what the reference already returns for an event the model does not have, so
        // no caller can tell the two apart.
        auto& track = event.eventTrack;

        if (sequenceIndex >= track.sequenceTimes.Count()
            || !track.sequenceTimes[sequenceIndex].times.Count()) {
            return 0;
        }

        return track.sequenceTimes[sequenceIndex].times[0];
    }

    return 0;
}

// ref: FUN_008317e0
C3Vector& CM2Model::GetEventWorldPosition(C3Vector& out, uint32_t eventId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    C3Vector* local;
    uint16_t boneIndex;

    if (!this->GetEvent(eventId, &local, &boneIndex)) {
        out = C3Vector(0.0f, 0.0f, 0.0f);

        return out;
    }

    this->Animate();

    // The same model-to-world step GetAttachmentWorldTransform ends on, which is what identifies
    // the scene matrix the reference reaches for here (+0xc4) as m_viewInv.
    out = (*local * this->m_boneMatrices[boneIndex]) * this->m_scene->m_viewInv;

    return out;
}

// ref: FUN_004f5e20
CAaBox& CM2Model::GetBoundingBox(CAaBox& bounds) {
    if (!this->m_shared->m_m2DataLoaded) {
        this->WaitForLoad(nullptr);
    }

    bounds = this->m_shared->m_data->bounds.extent;

    return bounds;
}

// ref: FUN_004f5e80
// The sphere around the model's authored bounds: the box's midpoint and the authored radius.
void CM2Model::GetBoundingSphere(CAaSphere& sphere) {
    if (!this->m_shared->m_m2DataLoaded) {
        this->WaitForLoad(nullptr);
    }

    auto& bounds = this->m_shared->m_data->bounds;
    sphere.c.x = (bounds.extent.b.x + bounds.extent.t.x) * 0.5f;
    sphere.c.y = (bounds.extent.t.y + bounds.extent.b.y) * 0.5f;
    sphere.c.z = (bounds.extent.t.z + bounds.extent.b.z) * 0.5f;
    sphere.r = bounds.radius;
}

HCAMERA CM2Model::GetCameraByIndex(uint32_t index) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetCameraByIndex");
    }

    return this->m_cameras[index].m_camera;
}

// The reference calls Animate() first, and frozen did not. Added 2026-09-23 with the tag: without
// it this returns whatever matrixF4 held when the model last animated, which for a model that has
// not animated yet this frame is the previous frame's position.
//
// It is inert at the only call site today -- SetupLighting runs after CM2Scene::Animate's loop, so
// the model is already current and Animate() early-outs on the frame counter -- but the point of
// the call is that GetPosition is self-sufficient for any future caller, which is how the
// reference wrote it.
//
// The offsets line up exactly: the reference transforms the vector at model + 0x124 by the scene's
// m_viewInv at scene + 0xc4, and frozen's matrixF4 sits at 0xF4, so matrixF4.d0 is 0xF4 + 0x30 =
// 0x124. It writes through a caller-supplied out pointer and returns it; returning by value here
// is the same thing.
// ref: FUN_004e2790
C3Vector CM2Model::GetPosition() {
    this->Animate();

    return reinterpret_cast<C3Vector&>(this->matrixF4.d0) * this->m_scene->m_viewInv;
}

// ref: FUN_0082ced0
// Report the sequence the model would actually play for `sequenceId`: the fallback chain is walked
// first (the model may not carry the animation asked for), then the requested variation of what it
// resolved to. The caller gets that sequence's header and its authored bounding box; the box is
// what the blob-shadow pass projects as a doodad's footprint, which is why the footprint follows
// the animation.
//
// Nothing in frozen calls this, so `M2SequenceInfo::moveSpeed`, `center` and `radius` are computed
// and discarded -- which is how tools/deaddata.py surfaced it. The port is not the problem; the
// consumers are. Measured 2026-09-23 from the reference call graph: FUN_0082ced0 has 20 callers
// there and NOT ONE of them is linked in frozen.
//
//     007385c0  4083 bytes, 57 callers   the big one; everything else here is downstream of it
//     0082dd80   819 bytes,  7 callers   the only caller inside the M2 module itself
//     007022d0  1931 bytes,  5 callers
//     00604e00 / 00619580 / 0070d1e0     3 callers each
//     007015d0 / 0071df30 / 00737ef0 / 0073c8e0        2 each
//     00606f90 / 006f80b0 / 00702fc0 / 0073adc0 / 00756040 / 00793980   1 each
//     0052f9b0 / 005995d0 / 0070fa70 / 0070fe10        0 (reached indirectly)
//
// The 0x70xxxx and 0x73xxxx cluster is unit animation state, which CLAUDE.md already records as
// unported, so most of this list is blocked behind that rather than behind anything render-side.
//
// FUN_0082dd80 looked like the exception, being inside CM2Model's own address range, and it was
// decompiled 2026-09-23 to settle that. It is not worth porting yet, and here is why, so that the
// next cycle does not spend another Ghidra run on it. What it does: reset the model's 4x4 matrix at
// +0xb4 to identity (the diagonal writes at 0xb4 / 0xc8 / 0xdc / 0xf0 are 20 bytes apart, which is
// the row-major 0, 5, 10, 15), scale it, drop the position argument into the translation row at
// +0xe4, and when its mode argument has (mode & 3) == 1 build the orientation from a direction
// vector with two cross products. Then -- only if the animating flag +0x10 & 1 is set -- it queries
// the current sequence state through FUN_008266b0, calls THIS function for that sequence's header,
// and reads `info.flags & 0xe`: 2 or 4 means fade the new matrix against the copy of the old one it
// saved before overwriting (4 inverts the factor), 8 means take it whole, anything else means skip
// the blend. Finally it sets +0x10 |= 0x8000. So it is the animation-blended world transform.
//
// The reason to leave it: its own seven callers are unlinked too, exactly like this function's
// twenty. Porting it would add a method nothing in frozen calls and move the dead end up one level
// instead of closing it. The chain is dead from the top, not from here, and the top is unit
// animation state. FUN_008266b0 (296 bytes, 22 callers) is the current-sequence-state query and
// computes animation time as `(scene->time - seq->startTime) * seq->rate + seq->offset`; it is the
// better seed of the two if this area is picked up again.
void CM2Model::GetSequenceInfo(uint32_t sequenceId, int32_t variationIndex, M2SequenceInfo& info) {
    if (!this->m_loaded) {
        this->WaitForLoad("GetSequenceInfo");
    }

    M2SequenceFallback fallback;
    this->Sub826350(fallback, sequenceId);

    info.sequenceId = fallback.uint0;
    info.playMode = fallback.uint2;

    auto data = this->m_shared->m_data;

    uint16_t sequenceIndex = CM2Model::Sub8260C0(data, fallback.uint0, variationIndex);

    // The model has no such variation: everything but the resolved fallback is cleared.
    if (sequenceIndex == 0xFFFF) {
        info.flags = 0;
        info.duration = 0;
        info.moveSpeed = 0.0f;
        info.extent.b = { 0.0f, 0.0f, 0.0f };
        info.extent.t = { 0.0f, 0.0f, 0.0f };
        info.center = { 0.0f, 0.0f, 0.0f };
        info.radius = 0.0f;

        return;
    }

    auto& sequence = data->sequences[sequenceIndex];

    info.flags = sequence.flags;
    info.duration = sequence.duration;
    info.moveSpeed = sequence.movespeed;

    // The authored move speed is in the model's own units, so a model carrying a world transform
    // has it scaled by the length of that transform's first row. An attached model has no world
    // transform of its own -- matrixF4 is relative to the scene view -- so the length is taken from
    // the row the view inverse maps back into world space.
    if (this->m_flag8000) {
        if (!this->m_attachParent) {
            float scale = sqrtf(
                this->matrixB4.a0 * this->matrixB4.a0
                + this->matrixB4.a1 * this->matrixB4.a1
                + this->matrixB4.a2 * this->matrixB4.a2
            );

            info.moveSpeed = scale * sequence.movespeed;
        } else {
            auto& viewInv = this->m_scene->m_viewInv;
            auto& transform = this->matrixF4;

            float x = transform.a0 * viewInv.a0
                + transform.a1 * viewInv.b0
                + transform.a2 * viewInv.c0
                + transform.a3 * viewInv.d0;
            float y = transform.a0 * viewInv.a1
                + transform.a1 * viewInv.b1
                + transform.a2 * viewInv.c1
                + transform.a3 * viewInv.d1;
            float z = transform.a0 * viewInv.a2
                + transform.a1 * viewInv.b2
                + transform.a2 * viewInv.c2
                + transform.a3 * viewInv.d2;

            info.moveSpeed = sqrtf(x * x + y * y + z * z) * sequence.movespeed;
        }
    }

    info.extent = sequence.bounds.extent;

    info.center.x = (sequence.bounds.extent.b.x + sequence.bounds.extent.t.x) * 0.5f;
    info.center.y = (sequence.bounds.extent.t.y + sequence.bounds.extent.b.y) * 0.5f;
    info.center.z = (sequence.bounds.extent.t.z + sequence.bounds.extent.b.z) * 0.5f;

    info.radius = sequence.bounds.radius;
}

// ref: FUN_008273d0
bool CM2Model::HasAttachment(uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad("HasAttachment");
    }

    if (id < this->m_shared->m_data->attachmentIndicesById.Count()) {
        return this->m_shared->m_data->attachmentIndicesById[id] < this->m_shared->m_data->attachments.Count();
    }

    return this->m_shared->m_data->attachments.Count() > 0xFFFF;
}

// ref: FUN_00834810
// Bring a freshly allocated model up: attach it to the scene, take references on the shared
// data and on the model it was created against, then ask the shared data to initialise it now
// or call back when it is ready.
int32_t CM2Model::Initialize(CM2Scene* scene, CM2Shared* shared, CM2Model* a4, uint32_t flags) {
    this->AttachToScene(scene);

    // TODO
    // this->dword30[23] = this->m_scene->dwordC;

    this->m_shared = shared;
    this->m_shared->AddRef();

    // The reference is now KEPT, in m_parentModel, and given back in ~CM2Model -- the comment
    // that used to sit here asked for exactly that and FUN_00834810 confirms the slot (+0x30).
    this->m_parentModel = a4;

    if (a4) {
        a4->AddRef();
    }

    // Still missing from this function: the reference then walks a4's attachment list and
    // re-attaches each child to THIS model (FUN_00834810 at 0x0083487e, over a4 +0x58 with the
    // next link at +0x60 and the attachment id at +0x50). Nothing passes a4 yet, so nothing
    // reaches it; it needs the attachment-list layout named before it can be written.

    this->m_flags = flags;

    this->m_modelCallTail = &this->m_modelCallList;

    this->uint74 = this->m_scene->m_time;

    // TODO

    return this->m_shared->CallbackWhenLoaded(this);
}

// ref: FUN_00832ea0
int32_t CM2Model::InitializeLoaded() {
    if (!this->m_shared->m_m2DataLoaded || !this->m_shared->m_skinProfileLoaded) {
        return 1;
    }

    // Allocate a single buffer to hold unique per-model data

    uint32_t bufferSize = 0;
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelBone, this->m_shared->m_data->bones.Count());
    bufferSize += ALIGN_SIZE(bufferSize, uint32_t, this->m_shared->m_data->loops.Count());
    bufferSize += ALIGN_SIZE(bufferSize, uint32_t, this->m_shared->skinProfile->skinSections.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelColor, this->m_shared->m_data->colors.Count());
    bufferSize += ALIGN_SIZE(bufferSize, HTEXTURE, this->m_shared->m_data->textures.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelTextureWeight, this->m_shared->m_data->textureWeights.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelTextureTransform, this->m_shared->m_data->textureTransforms.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelAttachment, this->m_shared->m_data->attachments.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelLight, this->m_shared->m_data->lights.Count());
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelCamera, this->m_shared->m_data->cameras.Count());
    // The ribbons come BEFORE the particles, which is the order the reference carves them
    // (+0x2b8 and +0x2bc against +0x2c0 and +0x2c4). Each ALIGN_SIZE is relative to the running
    // offset, so this order has to agree with the fill below and it now agrees with the reference
    // as well. Both runs are fixed-size -- there is only one kind of ribbon emitter, so unlike the
    // particle factory there is nothing to switch on.
    bufferSize += ALIGN_SIZE(bufferSize, M2ModelRibbon, this->m_shared->m_data->ribbons.Count());
    bufferSize += ALIGN_SIZE(bufferSize, CM2Ribbon*, this->m_shared->m_data->ribbons.Count());
    bufferSize += ALIGN_SIZE(bufferSize, CM2Ribbon, this->m_shared->m_data->ribbons.Count());

    bufferSize += ALIGN_SIZE(bufferSize, M2ModelParticle, this->m_shared->m_data->particles.Count());

    // The emitter pointer array, then the emitter objects themselves. The reference carves both
    // from this same buffer (0x833af5 and the run the factory loop walks), which is why they are
    // sized here rather than allocated separately.
    //
    // The objects are a variable-size run: the reference advances by 0x244, 0x248 or 0x40c
    // depending on the type byte. Frozen's sizes differ, so this walks the same array and sums
    // sizeof() per type. The loop below in InitializeLoaded MUST make the same decisions in the
    // same order -- each ALIGN_SIZE is relative to the running offset, so a disagreement here
    // silently shifts every later array.
    bufferSize += ALIGN_SIZE(bufferSize, CM2ParticleEmitter*,
                             this->m_shared->m_data->particles.Count());

    for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
        switch (this->m_shared->m_data->particles[i].emitterType) {
        case 1:
            bufferSize += ALIGN_SIZE(bufferSize, CM2ParticleEmitterPlane, 1);
            break;
        case 2:
            bufferSize += ALIGN_SIZE(bufferSize, CM2ParticleEmitterSphere, 1);
            break;
        default:
            // Type 3 (0x40c in the reference, constructor 0x009820f0) is a separate hierarchy and
            // is not ported; nothing is reserved and no emitter is built.
            break;
        }
    }


    auto buffer = static_cast<char*>(SMemAlloc(bufferSize, __FILE__, __LINE__, 0));
    auto start = buffer;

    // Allocate and initialize per-model data

    if (this->m_shared->m_data->bones.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelBone);
        this->m_bones = reinterpret_cast<M2ModelBone*>(buffer);
        buffer += sizeof(M2ModelBone) * this->m_shared->m_data->bones.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            new (&this->m_bones[i]) M2ModelBone();
        }

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            this->m_bones[i].flags = this->m_shared->m_data->bones[i].flags;
        }

        // TODO use A16 allocator
        this->m_boneMatrices = static_cast<C44Matrix*>(SMemAlloc(sizeof(C44Matrix) * this->m_shared->m_data->bones.Count(), __FILE__, __LINE__, 0));

        for (int32_t i = 0; i < this->m_shared->m_data->bones.Count(); i++) {
            new (&this->m_boneMatrices[i]) C44Matrix();
        }
    }

    if (this->m_shared->m_data->loops.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, uint32_t);
        this->m_loops = reinterpret_cast<uint32_t*>(buffer);
        buffer += sizeof(uint32_t) * this->m_shared->m_data->loops.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->loops.Count(); i++) {
            if (this->m_loops[i]) {
                this->m_loops[i] = 0;
            }
        }
    }

    if (this->m_shared->skinProfile->skinSections.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, uint32_t);
        this->m_skinSections = reinterpret_cast<uint32_t*>(buffer);
        buffer += sizeof(uint32_t) * this->m_shared->skinProfile->skinSections.Count();

        if (this->model30) {
            memcpy(this->m_skinSections, model30->m_skinSections, sizeof(uint32_t) * this->m_shared->skinProfile->skinSections.Count());
        } else {
            // Mark all skin sections as visible by default
            for (int32_t i = 0; i < this->m_shared->skinProfile->skinSections.Count(); i++) {
                auto modelSkinSection = &this->m_skinSections[i];

                if (modelSkinSection) {
                    *modelSkinSection = 1;
                }
            }
        }
    }

    if (this->m_shared->m_data->colors.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelColor);
        this->m_colors = reinterpret_cast<M2ModelColor*>(buffer);
        buffer += sizeof(M2ModelColor) * this->m_shared->m_data->colors.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->colors.Count(); i++) {
            new (&this->m_colors[i]) M2ModelColor();
        }
    }

    if (this->m_shared->m_data->textures.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, HTEXTURE);
        this->m_textures = reinterpret_cast<HTEXTURE*>(buffer);
        buffer += sizeof(HTEXTURE) * this->m_shared->m_data->textures.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            HTEXTURE textureHandle = this->model30
                ? this->model30->m_textures[i]
                : this->m_shared->textures[i];

            this->m_textures[i] = textureHandle
                ? HandleDuplicate(textureHandle)
                : nullptr;
        }
    }

    if (this->m_shared->m_data->textureWeights.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelTextureWeight);
        this->m_textureWeights = reinterpret_cast<M2ModelTextureWeight*>(buffer);
        buffer += sizeof(M2ModelTextureWeight) * this->m_shared->m_data->textureWeights.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textureWeights.Count(); i++) {
            new (&this->m_textureWeights[i]) M2ModelTextureWeight();
        }
    }

    if (this->m_shared->m_data->textureTransforms.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelTextureTransform);
        this->m_textureTransforms = reinterpret_cast<M2ModelTextureTransform*>(buffer);
        buffer += sizeof(M2ModelTextureTransform) * this->m_shared->m_data->textureTransforms.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->textureTransforms.Count(); i++) {
            new (&this->m_textureTransforms[i]) M2ModelTextureTransform();
        }

        // TODO use A16 allocator
        this->m_textureMatrices = static_cast<C44Matrix*>(SMemAlloc(sizeof(C44Matrix) * this->m_shared->m_data->textureTransforms.Count(), __FILE__, __LINE__, 0x0));
    }

    if (this->m_shared->m_data->attachments.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelAttachment);
        this->m_attachments = reinterpret_cast<M2ModelAttachment*>(buffer);
        buffer += sizeof(M2ModelAttachment) * this->m_shared->m_data->attachments.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->attachments.Count(); i++) {
            new (&this->m_attachments[i]) M2ModelAttachment();

            auto& modelAttachment = this->m_attachments[i];
            modelAttachment.visibilityTrack.currentValue = 1;
        }
    }

    if (this->m_shared->m_data->lights.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelLight);
        this->m_lights = reinterpret_cast<M2ModelLight*>(buffer);
        buffer += sizeof(M2ModelLight) * this->m_shared->m_data->lights.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->lights.Count(); i++) {
            new (&this->m_lights[i]) M2ModelLight();

            auto& light = this->m_shared->m_data->lights[i];
            auto& modelLight = this->m_lights[i];

            modelLight.light.Initialize(this->m_scene);
            modelLight.light.SetLightType(static_cast<M2LIGHTTYPE>(light.lightType));
            modelLight.ambientIntensityTrack.currentValue = 1.0f;
            modelLight.diffuseIntensityTrack.currentValue = 1.0f;
            modelLight.visibilityTrack.currentValue = 1;
        }
    }

    if (this->m_shared->m_data->cameras.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelCamera);
        this->m_cameras = reinterpret_cast<M2ModelCamera*>(buffer);
        buffer += sizeof(M2ModelCamera) * this->m_shared->m_data->cameras.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
            new (&this->m_cameras[i]) M2ModelCamera();
        }

        // The `break` is the reference's, not a simplification: FUN_00832ea0 aborts the whole
        // camera loop on the first bad one rather than skipping it, so a model whose camera 0 is
        // degenerate ends up with NO camera handles at all. That matters more than it looks --
        // CSimpleModel::SetCameraByIndex then stores a null m_camera, the model still draws, and
        // everything gated on having a camera silently does not. The login screen's snow is gated
        // that way.
        //
        // What was NOT the reference's: doing it silently. The reference makes these two separate
        // assertions and prints the failing expression and its value before breaking. frozen had
        // them merged into one condition with no message, so a model that tripped it looked
        // exactly like a model with no cameras. BLIZZARD_ASSERT is not usable here -- it compiles
        // to (void)0 under NDEBUG, and Release is the only build whose visuals are trustworthy --
        // so these report through SysMsgPrintf, which is what the reference's own helper does.
        for (int32_t i = 0; i < this->m_shared->m_data->cameras.Count(); i++) {
            auto& camera = this->m_shared->m_data->cameras[i];
            auto cameraHandle = CameraCreate();

            if (camera.fieldOfView <= 0.0f || camera.fieldOfView >= 3.1415927f) {
                SysMsgPrintf(SYSMSG_ERROR,
                             "M2 camera %d: \"shared->fieldOfView > 0.0f && shared->fieldOfView < PI\","
                             " shared->fieldOfView = %g (%s)",
                             i, camera.fieldOfView, this->m_shared->m_filePath);

                // DIVERGENCE, deliberate: the reference abandons this handle. Closing it costs
                // nothing, cannot change what is drawn -- the loop stops either way -- and leaving
                // a leak in on an error path only makes the next leak harder to find.
                HandleClose(cameraHandle);

                break;
            }

            if (camera.farClip <= camera.nearClip) {
                SysMsgPrintf(SYSMSG_ERROR,
                             "M2 camera %d: \"shared->nearClip < shared->farClip\","
                             " shared->nearClip = %g, shared->farClip = %g (%s)",
                             i, camera.nearClip, camera.farClip, this->m_shared->m_filePath);

                HandleClose(cameraHandle);

                break;
            }

            DataMgrSetFloat(cameraHandle, 4, camera.fieldOfView);
            DataMgrSetFloat(cameraHandle, 3, camera.nearClip);
            DataMgrSetFloat(cameraHandle, 2, camera.farClip);

            this->m_cameras[i].m_camera = cameraHandle;
        }
    }

    // The ribbon emitters, and they come before the particles for the reason given in the sizing
    // pass. Three runs out of the buffer: the animated state, the pointer array, then the objects.
    if (this->m_shared->m_data->ribbons.Count()) {
        auto ribbonCount = static_cast<uint32_t>(this->m_shared->m_data->ribbons.Count());

        buffer = ALIGN_BUFFER(buffer, start, M2ModelRibbon);
        this->m_ribbons = reinterpret_cast<M2ModelRibbon*>(buffer);
        buffer += sizeof(M2ModelRibbon) * ribbonCount;

        for (uint32_t i = 0; i < ribbonCount; i++) {
            new (&this->m_ribbons[i]) M2ModelRibbon();
        }

        buffer = ALIGN_BUFFER(buffer, start, CM2Ribbon*);
        this->m_ribbonEmitters = reinterpret_cast<CM2Ribbon**>(buffer);
        buffer += sizeof(CM2Ribbon*) * ribbonCount;

        // The three lists Initialize copies in. Declared out here rather than per ribbon because
        // the reference reuses three file-static arrays across every ribbon of every model; these
        // are locals instead, so nothing survives the function, but they are still reused across
        // this model's ribbons rather than reallocated per ribbon.
        TSGrowableArray<HTEXTURE> ribbonTextures;
        TSGrowableArray<CM2Ribbon::Material> ribbonMaterials;
        TSGrowableArray<M2Texture*> ribbonRecords;

        for (uint32_t i = 0; i < ribbonCount; i++) {
            const M2Ribbon& file = this->m_shared->m_data->ribbons[i];

            buffer = ALIGN_BUFFER(buffer, start, CM2Ribbon);
            this->m_ribbonEmitters[i] = new (buffer) CM2Ribbon();
            buffer += sizeof(CM2Ribbon);

            CM2Ribbon* emitter = this->m_ribbonEmitters[i];

            // Gated on Count() because element 0 of an EMPTY M2Array is a wild pointer, not null.
            auto passCount = static_cast<uint32_t>(file.textureIndices.Count());

            ribbonTextures.SetCount(passCount);
            ribbonMaterials.SetCount(passCount);
            ribbonRecords.SetCount(passCount);

            for (uint32_t j = 0; j < passCount; j++) {
                uint16_t textureIndex = file.textureIndices[j];

                // The resolved handle, and beside it the FILE record the handle does not carry.
                ribbonTextures[j] = this->m_shared->textures[textureIndex];
                ribbonRecords[j] = &this->m_shared->m_data->textures[textureIndex];

                // The reference walks materialIndices with the SAME index and never checks that
                // it is as long as textureIndices. Transcribed as it stands: a .m2 whose two
                // arrays disagree would over-read here, in the reference too.
                const M2Material& material =
                    this->m_shared->m_data->materials[file.materialIndices[j]];

                // ENABLE bits, each the negation of one of the material's DISABLE flags. The
                // reference builds them with five xor-and-mask instructions over the same word
                // rather than an or-chain, which is the same result written unreadably.
                //
                // Note the crossed pair: ribbon bit 0x10 comes from M2 flag 0x04 (two-sided) and
                // ribbon bits 0x4/0x8 come from M2 flags 0x08/0x10. That is the reference's own
                // ordering, not a transcription slip.
                uint32_t flags = 0;

                if (!(material.flags & 0x01)) { flags |= 0x01; }   // lit
                if (!(material.flags & 0x02)) { flags |= 0x02; }   // fogged
                if (!(material.flags & 0x08)) { flags |= 0x04; }   // depth test
                if (!(material.flags & 0x10)) { flags |= 0x08; }   // depth write
                if (!(material.flags & 0x04)) { flags |= 0x10; }   // culling

                ribbonMaterials[j].m_flags = flags;
                ribbonMaterials[j].m_blend =
                    material.blendMode < M2BLEND_COUNT
                        ? static_cast<uint32_t>(s_ribbonBlend[material.blendMode])
                        : static_cast<uint32_t>(GxBlend_Opaque);
            }

            // White, and the whole texture -- both are constants at the reference's call site
            // (four 0xFF bytes at 0x833a13, and the rect built as 0/0/1/1), not anything off the
            // file record.
            CImVector color;
            color.value = 0xFFFFFFFF;

            float textureRect[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

            emitter->Initialize(file.edgesPerSecond, file.edgeLifetime, color,
                                ribbonTextures, ribbonMaterials, ribbonRecords,
                                textureRect, file.textureRows, file.textureCols);

            emitter->SetGravity(file.gravity);

            // A literal zero at the call site (`pushl $0x0` at 0x833a56), not a field -- so every
            // ribbon starts NOT above, and something else has to raise it later.
            emitter->SetAbove(0);

            // Visible, fully opaque. The two tracks the driver would otherwise leave at zero, so
            // a ribbon that is never animated still draws.
            this->m_ribbons[i].visibilityTrack.currentValue = 1;
            this->m_ribbons[i].alphaTrack.currentValue = 1.0f;
        }
    }

    // The runtime half of every emitter. The reference allocates it here, out of the same buffer
    // and directly after the ribbons, which is why the size list above has it in that position --
    // each ALIGN_SIZE is relative to the running offset, so the two orders have to agree.
    if (this->m_shared->m_data->particles.Count()) {
        buffer = ALIGN_BUFFER(buffer, start, M2ModelParticle);
        this->m_particles = reinterpret_cast<M2ModelParticle*>(buffer);
        buffer += sizeof(M2ModelParticle) * this->m_shared->m_data->particles.Count();

        for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
            new (&this->m_particles[i]) M2ModelParticle();
        }

        // The pointer array, zeroed, then the emitters themselves. The reference memsets the array
        // (0x833b09) before the factory fills it, which is what leaves a null behind for any
        // emitter that does not get built.
        buffer = ALIGN_BUFFER(buffer, start, CM2ParticleEmitter*);
        this->m_particleEmitters = reinterpret_cast<CM2ParticleEmitter**>(buffer);
        buffer += sizeof(CM2ParticleEmitter*) * this->m_shared->m_data->particles.Count();

        memset(this->m_particleEmitters, 0,
               sizeof(CM2ParticleEmitter*) * this->m_shared->m_data->particles.Count());

        // The factory. Switches on the same type byte the reference does, and must visit the array
        // in the same order and make the same decisions as the sizing pass above.
        uint32_t unsupported = 0;

        for (int32_t i = 0; i < this->m_shared->m_data->particles.Count(); i++) {
            switch (this->m_shared->m_data->particles[i].emitterType) {
            case 1:
                buffer = ALIGN_BUFFER(buffer, start, CM2ParticleEmitterPlane);
                this->m_particleEmitters[i] = new (buffer) CM2ParticleEmitterPlane();
                buffer += sizeof(CM2ParticleEmitterPlane);
                break;
            case 2:
                buffer = ALIGN_BUFFER(buffer, start, CM2ParticleEmitterSphere);
                this->m_particleEmitters[i] = new (buffer) CM2ParticleEmitterSphere();
                buffer += sizeof(CM2ParticleEmitterSphere);
                break;
            default:
                unsupported++;
                break;
            }

            // The emitter's construction-time setup, straight out of the file record -- the
            // reference does this at 0x833eaf, right after the same factory switch.
            CM2ParticleEmitter* emitter = this->m_particleEmitters[i];

            if (emitter) {
                const M2Particle& file = this->m_shared->m_data->particles[i];

                // 0x833bde, the first thing the reference does with a freshly built emitter:
                // clear the emit-enable bit the constructor did not set. Emission needs
                // (flags & 3) == 3, and the driver raises this one per frame.
                emitter->m_flags &= ~0x1u;

                // The material, from the same block at 0x833e08. The flags word is seeded
                // with 0x7 and then two of its bits are taken from the file's flags INVERTED --
                // the reference does it with an xor-and-xor dance that amounts to
                // `bit = !(file.flags & mask)`.
                uint32_t materialFlags = 0x7;

                uint32_t blendMode = M2ParticleBlendToGx(file.blendMode, materialFlags);

                materialFlags = (materialFlags & ~0x1u) | ((file.flags & 0x1) ? 0 : 0x1);
                materialFlags = (materialFlags & ~0x2u) | ((file.flags & 0x8) ? 0 : 0x2);

                // Through the setter the reference uses, rather than writing the fields: it
                // also takes the texture reference, which DrawParticle needs.
                HTEXTURE texture = this->m_shared->textures
                    && file.textureIndex < this->m_shared->m_data->textures.Count()
                        ? this->m_shared->textures[file.textureIndex]
                        : nullptr;

                emitter->SetMaterial(blendMode, materialFlags, texture);

                // Seed the emitter from each track's FIRST value, so it has something sensible
                // before the driver animates it. Guarded on the sequence actually having keys:
                // the reference dereferences values.data with no check, which is the M2Array trap
                // -- element 0 of an empty array is a wild pointer, not null.
                //
                // These are not decoration. m_lifespan and m_rate decide whether the emitter
                // emits at all -- PrepareStep sizes the pool from
                // `(lifespan + variation) * (rate + rateVariation)`, so at the constructor's
                // zeroes the capacity is zero and the free list never fills. And the driver does
                // not cover for it: it writes these only for tracks that pass
                // M2ParticleTrackDrives, so a static track reaches the emitter only from here.
                if (file.emissionRateTrack.sequenceKeys.Count()
                        && file.emissionRateTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetEmissionRate(file.emissionRateTrack.sequenceKeys[0].keys[0]);
                }

                emitter->m_rateVariation = file.emissionRateVariation;

                if (file.speedTrack.sequenceKeys.Count()
                        && file.speedTrack.sequenceKeys[0].keys.Count()) {
                    emitter->m_speed = file.speedTrack.sequenceKeys[0].keys[0];
                }

                if (file.variationTrack.sequenceKeys.Count()
                        && file.variationTrack.sequenceKeys[0].keys.Count()) {
                    emitter->m_variation = file.variationTrack.sequenceKeys[0].keys[0];
                }

                if (file.latitudeTrack.sequenceKeys.Count()
                        && file.latitudeTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetLatitude(file.latitudeTrack.sequenceKeys[0].keys[0]);
                }

                if (file.longitudeTrack.sequenceKeys.Count()
                        && file.longitudeTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetLongitude(file.longitudeTrack.sequenceKeys[0].keys[0]);
                }

                if (file.gravityTrack.sequenceKeys.Count()
                        && file.gravityTrack.sequenceKeys[0].keys.Count()) {
                    emitter->m_gravity = file.gravityTrack.sequenceKeys[0].keys[0];
                }

                if (file.lifeTrack.sequenceKeys.Count()
                        && file.lifeTrack.sequenceKeys[0].keys.Count()) {
                    emitter->m_lifespan = file.lifeTrack.sequenceKeys[0].keys[0];
                }

                emitter->m_lifespanVariation = file.lifeVariation;

                if (file.widthTrack.sequenceKeys.Count()
                        && file.widthTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetWidth(file.widthTrack.sequenceKeys[0].keys[0]);
                }

                if (file.lengthTrack.sequenceKeys.Count()
                        && file.lengthTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetLength(file.lengthTrack.sequenceKeys[0].keys[0]);
                }

                if (file.zsourceTrack.sequenceKeys.Count()
                        && file.zsourceTrack.sequenceKeys[0].keys.Count()) {
                    emitter->SetZSource(file.zsourceTrack.sequenceKeys[0].keys[0]);
                }

                // Which quads each particle draws, and the tail's length.
                emitter->SetHeadTail(file.flags & 0x20000, file.flags & 0x40000,
                                     file.tailLength, file.flags & 0x400);

                // The file flags, mapped onto the emitter's. None of these were being set, and
                // every one is read by code ported earlier this session: 0x200 is emitter space,
                // 0x800 is drag, 0x2000 is interpolated placement, 0x40000 is ground snap,
                // 0x80000 is the frame-delta scaling.
                //
                // 0x10 is the only one that CLEARS its bit when absent; the rest only ever set.
                if (file.flags & 0x10) {
                    emitter->m_flags |= 0x200;
                } else {
                    emitter->m_flags &= ~0x200u;
                }

                if (file.flags & 0x20) {
                    emitter->m_flags |= 0x400;
                }

                if (file.flags & 0x40) {
                    emitter->m_flags |= 0x800;
                }

                if (file.flags & 0x800) {
                    emitter->m_flags |= 0x2000;
                }

                if (file.flags & 0x1000) {
                    emitter->m_flags |= 0x4000;
                }

                // Sphere emitters only -- the reference tests the type byte for 2 before these.
                if (file.emitterType == 2) {
                    if (file.flags & 0x80) {
                        emitter->m_flags |= 0x1000;
                    }

                    if (file.flags & 0x100) {
                        emitter->m_flags |= 0x8000;
                    }
                }

                if (file.flags & 0x200) {
                    emitter->m_flags |= 0x10000;
                }

                if (file.flags & 0x2000) {
                    emitter->m_flags |= 0x40000;
                }

                if (file.flags & 0x4000) {
                    emitter->m_flags |= 0x80000;
                }

                // The one that clears rather than sets: 0x8000 takes the emitter OUT of the
                // continuous-emission pair the constructor seeds with 0x2.
                if (file.flags & 0x8000) {
                    emitter->m_flags &= ~0x1u;
                }

                if (file.flags & 0x80000) {
                    emitter->m_flags |= 0x800000;
                }

                // The scalar parameters, straight out of the record. m_drag, m_wind, m_windTime
                // and m_velocitySampleScale are read by IntegrateParticle and Update and had
                // never been written by anything -- so drag, wind and the inherited-velocity
                // scale have all been silently zero however carefully those were ported.
                emitter->m_twinkleFps = file.twinkleFPS;
                emitter->m_twinkleOnOff = file.twinkleOnOff;
                emitter->SetTwinkleScale(file.twinkleScale);

                emitter->m_velocitySampleScale = file.ivelScale;
                emitter->m_drag = file.drag;

                emitter->m_initialSpin = file.initialSpin;
                emitter->m_initialSpinVariation = file.initialSpinVariation;
                emitter->m_spin = file.spin;
                emitter->m_spinVariation = file.spinVariation;

                // The tumble box becomes three (min, span) pairs.
                emitter->m_tumble[0].min = file.tumble.b.x;
                emitter->m_tumble[0].span = file.tumble.t.x - file.tumble.b.x;
                emitter->m_tumble[1].min = file.tumble.b.y;
                emitter->m_tumble[1].span = file.tumble.t.y - file.tumble.b.y;
                emitter->m_tumble[2].min = file.tumble.b.z;
                emitter->m_tumble[2].span = file.tumble.t.z - file.tumble.b.z;

                emitter->m_wind = file.windVector;
                emitter->m_windTime = file.windTime;

                // The part-tracks the emitter samples per particle, cached as pointers into the
                // model data. None of these were being set, so the ground snap silently did
                // nothing and the draw would have had no colour, alpha or size to work from.
                emitter->m_colorTrack = &file.colorTrack;
                emitter->m_alphaTrack = &file.alphaTrack;
                emitter->m_scaleTrack = &file.scaleTrack;
                emitter->m_scaleVariation = file.scaleVariation;
                emitter->m_headCellTrack = &file.headCellTrack;
                emitter->m_tailCellTrack = &file.tailCellTrack;

                if (file.flags & 0x2) {
                    emitter->m_flags |= 0x20;
                }

                if (file.flags & 0x4) {
                    emitter->m_flags |= 0x200000;
                }

                emitter->SetTextureGrid(file.rows, file.cols);

                if (file.flags & 0x10000) {
                    emitter->SetTextureAnimated(1);
                }
            }
        }

        if (unsupported) {
            // The reference dereferences m_particleEmitters[i] on the line after the factory
            // without a null check, so in practice the type byte is always 1, 2 or 3 -- this only
            // fires for type 3, whose class is unported. The slot stays null and every consumer
            // has to tolerate that.
            SysMsgPrintf(SYSMSG_ERROR,
                         "CM2Model: %u of %d particle emitters use an emitter type frozen does "
                         "not implement (type 3, reference constructor 0x009820f0); those slots "
                         "are null", unsupported, this->m_shared->m_data->particles.Count());
        }
    }

    // TODO

    // Process model attachments that occurred during load

    CM2Model* attachNext = nullptr;
    for (auto attachModel = this->m_attachList; attachModel; attachModel = attachNext) {
        attachNext = attachModel->m_attachNext;

        uint16_t attachIndex = 0xFFFF;

        if (attachModel->m_attachId < this->m_shared->m_data->attachments.Count()) {
            attachIndex = this->m_shared->m_data->attachmentIndicesById[attachModel->m_attachId];
        }

        if (attachIndex != 0xFFFF || attachModel->m_flag40000) {
            attachModel->m_attachIndex = attachIndex;
        } else {
            attachModel->DetachFromParent();
        }
    }

    // TODO

    this->m_loaded = 1;

    uint32_t savedTime = this->m_scene->m_time;

    while (this->m_modelCallList) {
        auto modelCall = this->m_modelCallList;

        this->m_scene->m_time = modelCall->time;

        switch (modelCall->type) {
            case 0: {
                auto textureId = modelCall->args[0];
                auto texture = *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]);

                this->ReplaceTexture(textureId, texture);

                break;
            }

            case 1: {
                this->SetGeometryVisible(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2]
                );

                break;
            }

            case 2: {
                this->SetGeometryVisibleByIndex(
                    modelCall->args[0],
                    modelCall->args[1],
                    static_cast<int32_t>(modelCall->args[2])
                );

                break;
            }

            case 3: {
                this->OptimizeVisibleGeometry();
                break;
            }

            case 4: {
                this->SetBoneFlags(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2]
                );

                break;
            }

            case 5: {
                this->SetBoneSequence(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2],
                    modelCall->args[3],
                    *reinterpret_cast<float*>(&modelCall->args[4]),
                    modelCall->args[5],
                    modelCall->args[6]
                );

                break;
            }

            case 6: {
                this->UnsetBoneSequence(
                    modelCall->args[0],
                    modelCall->args[1],
                    modelCall->args[2]
                );

                break;
            }

            case 7: {
                this->SetBoneSequenceTime(
                    modelCall->args[0],
                    static_cast<int32_t>(modelCall->args[1])
                );

                break;
            }

            case 8: {
                this->SetBoneSequenceSpeed(
                    modelCall->args[0],
                    *reinterpret_cast<float*>(&modelCall->args[1])
                );

                break;
            }

            case 9: {
                // FUN_008272f0(args[0], &args[1]) -- 224 bytes, 6 callers, and it takes a POINTER
                // into the argument block rather than a value, which is why the call site passes
                // &args[1]. Needs SequenceBufferAlloc, which is linked.
                break;
            }

            case 10: {
                this->SetLightEnabled(
                    modelCall->args[0],
                    static_cast<int32_t>(modelCall->args[1])
                );

                break;
            }

            case 11: {
                // FUN_00825410() -- 205 bytes, 3 callers, takes NO arguments off the call. Blocked
                // on FUN_0097a990.
                break;
            }

            case 12: {
                this->SetParticleEmission(modelCall->args[0]);
                break;
            }

            case 13: {
                // Set flag 8 on every RIBBON emitter (FUN_00824230 queues this type and, once
                // loaded, walks m_shared->m_data->ribbons calling CM2Ribbon::SetFlag8 on each).
                // CM2Ribbon::SetFlag8 exists here; the emitter ARRAY does not -- CM2Model has no
                // m_ribbonEmitters yet.
                //
                // CORRECTED 2026-09-26. This note used to say "nothing creates ribbons yet
                // because unit movement is not ported", and that adding the array would only put
                // a loop over nothing behind this case. Both halves were wrong. What creates the
                // emitters is CM2Model::InitializeLoaded (FUN_00832ea0), which constructs one
                // CM2Ribbon per m_shared->m_data->ribbons entry out of the same carved buffer as
                // the particle emitters, at 0x180 each, and stores them in an array at +0x2bc
                // beside a parallel 0x50-byte state array at +0x2b8 -- the same pair shape as
                // m_particles / m_particleEmitters one slot along. Unit movement drives a ribbon's
                // MOTION, not its creation; the emitters exist as soon as the model loads.
                //
                // BOTH of those landed 2026-09-26 -- the pair is above and the ribbon block is in
                // InitializeLoaded -- so the loop is live rather than a loop over nothing.
                if (this->m_ribbonEmitters) {
                    for (int32_t i = 0; i < this->m_shared->m_data->ribbons.Count(); i++) {
                        this->m_ribbonEmitters[i]->SetFlag8(modelCall->args[0]);
                    }
                }

                break;
            }

            case 14: {
                this->LoadSequence(modelCall->args[0]);

                break;
            }
        }

        this->m_modelCallList = modelCall->modelCallNext;

        if (modelCall->type == 0) {
            auto texture = *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]);

            if (texture) {
                HandleClose(texture);
            }
        }

        STORM_FREE(modelCall);
    }

    this->m_scene->m_time = savedTime;

    this->UpdateLoaded();
    this->m_flag800 = 0;

    return 1;
}

// THE MERGE PREPARATION PASS, mapped 2026-09-27 and not yet ported. Recorded here because this is
// where anyone enabling the doodad path will come looking, and because the call graph took a while
// to untangle:
//
//   FUN_00832dd0 (187 bytes, 2 callers) holds a {data, count} pair of 12-byte elements and does
//   two things: std::sort them, then walk the sorted run grouping neighbours that can merge.
//
//   The element is {CM2Model* model, uint32_t batchIndex, uint32_t <unidentified>}. The third
//   field is read by neither predicate below, so it is deliberately left unnamed here rather than
//   guessed at.
//
//   FUN_00824b70 (237 bytes) is its `operator<`, ordering by the batch's skin section
//   boneInfluences, then ptr2D0, then m_shared, then batchIndex. Sorting on boneInfluences first
//   is the point: that is what picks the vertex shader permutation, so equal-influence batches end
//   up adjacent and one shader serves the whole run.
//
//   FUN_00824c60 (195 bytes) is the grouping test, and what it accepts says what a merge IS: the
//   same m_shared, the same batchIndex, the same ptr2D0, and skin sections with equal
//   boneInfluences. Same batch of the same model on DIFFERENT instances -- which is instanced
//   doodad batching, and is why M2BatchesCanMerge deliberately leaves skinSectionIndex out of its
//   own comparison.
//
// The six std::sort internals underneath FUN_00832dd0 are STL and are marked `excluded` in
// overrides.json, named for the standard function each one is; they are not work.
//
// NOT PORTED, deliberately: FUN_00832dd0's own two callers are unported, so the predicates would
// be two functions nothing calls, with one invented field name between them. They go in with their
// consumer.
//
// Returns 0, which is the safe answer rather than a placeholder: it means "this batch cannot be
// merged into a doodad batch", so CM2Scene::Animate gives every element type 0 and the doodad path
// is uniformly off. That agrees with the rest of frozen -- the M2BatchDoodads CVar reaches
// CM2Cache::m_flags bit 0x20 and nothing reads it.
//
// **Implementing this alone breaks rendering.** A non-zero answer makes Animate emit type 2
// elements, and type 2 dispatches to CM2SceneRender::DrawBatchDoodad, which is an empty body. Those
// batches would then silently not draw. Port the two together, and wire M2BatchDoodads to gate them
// in the same change.
// Identified 2026-09-23 as FUN_00824550, from CM2Scene::Animate's call order -- frozen calls it
// from the element gather at exactly that position, it is a thiscall on a model taking one pointer,
// and two of its reads settle it: `testb $0x10, (%eax)` on the argument is batch->flags & 0x10, and
// `[[esi+0x2c] + 0x150] + 0x2c` compared against 1 is m_shared->m_data->bones.count, the same
// expression DrawBatch uses. Its first call is to 0x0081c0b0, already mapped as M2GetCacheFlags,
// and it tests bit 0x20 of the result -- which is the M2BatchDoodads flag this comment predicted
// before the function was found.
//
// The body, so that whoever ports it does not have to redo this:
//
//     flags = M2GetCacheFlags();
//     if (!(flags & 0x20))                     return 0;   // M2BatchDoodads off
//     if (!(this->[0x10] & 0x10))              return 0;   // m_flag10, see below
//     if (m_shared->m_data->bones.count <= 1
//         && (flags & 0x40))                   return 0;
//     if (!(batch->flags & 0x10))              return 0;
//     other = this->[0x2a8];                               // the shared-animation source pointer
//     if (other && other->[0xa4])              return 0;
//     return 1;
//
// Field mapping, narrowed 2026-09-26 -- ONE field is still open, not two:
//
//   +0x10  RESOLVED. This is the per-frame bitfield block, not the m_flags creation word, so the
//          test is `this->m_flag10`. The header's own note at m_flags spells out why confusing
//          the two is silent, and it is the same pair of storages that kept doodads off screen.
//   +0x2a8 RESOLVED 2026-09-27: it is `CM2Lighting* m_currentLighting`, and the +0xa4 the gate
//          reads is its m_lightCount. Found from FUN_0081cc50, an element render-state hash that
//          dereferences the same +0x2a8 and hashes FIVE fields off it whose offsets this class
//          already documents: m_sunAmbient at +0x54, m_sunDiffuse at +0x60, m_lightCount at
//          +0xa4, m_fogStart and m_fogEnd at +0xa8 and +0xac, and m_fogColor at +0xb8. Five
//          independent matches against offsets written down before this was looked for, which is
//          why the earlier guess that it lay INSIDE CM2Lighting was half right -- it points at
//          one.
//
// So the body is now fully specified, and it reads sensibly: a model carrying LOCAL LIGHTS cannot
// join a doodad batch, because instanced batching draws every instance with one set of light
// constants. That is the same reason the sort in the merge-prep pass (mapped below) groups on
// boneInfluences -- anything that changes per instance has to break the batch.
//
//     if (this->m_currentLighting && this->m_currentLighting->m_lightCount) return 0;
//
// "PORT THE TWO TOGETHER" IS NOT ENOUGH, and that instruction -- which this note carried for a
// long time -- would corrupt rendering if followed. Established 2026-09-27 by walking the chain
// instead of trusting it:
//
//   DrawBatchDoodad loops on element +0x1c as its INSTANCE COUNT. The gather in CM2Scene::Animate
//   never writes that slot -- it writes [1] through [6] and [9] through [0xc], and nothing else.
//   Checked by listing every element store in the function.
//
//   What does write it is the MERGE PREP PASS, FUN_00832dd0, mapped further down this file: it
//   sorts the batch references and collapses runs of mergeable ones, and the collapsed count is
//   what an instanced draw needs.
//
//   FUN_00832dd0 has exactly ONE caller, FUN_007bbc50 -- 2098 bytes of RenderTargetGet,
//   ScissorSet, GxSceneClear, GxXformSetViewport, UpdateProjMatrix and ShadowMapGetShaderLevel.
//   That is the MAP SHADOW MAP render pass, which CLAUDE.md's priority list already carries as an
//   unported item of its own.
//
// So element type 2 exists to draw merged doodad batches INTO THE SHADOW MAP, and the real
// dependency set is four things deep, not two: this gate, DrawBatchDoodad, the merge prep pass,
// and the shadow-map pass that drives it.
//
// AND THE FAILURE IS WORSE THAN THE OLD NOTE SAID. It claimed the merged batches would silently
// not draw. They would not: the element allocator does not zero, so a type-2 element's +0x1c holds
// whatever the recycled slot last had, and DrawBatchDoodad would loop that many times over a
// vertex buffer sized for something else. A wild loop count, not a missing draw.
//
// Everything BELOW that tier is ready and was landed over this session: both vertex packers,
// CM2Shared::SetVertices for the shader arm's buffer, ReserveInstances for its capacity, and the
// three scene render fields DrawBatchDoodad caches into. The gate's own last unknown field is
// resolved above. What remains is the shadow-map pass and the merge pass under it.
//
// One more thing to respect when DrawBatchDoodad is finally written: it caches element +0x28 and
// +0x2c expecting the batch and the skin section, and frozen's fields of those names are two slots
// earlier than the reference's. Write it against frozen's NAMES. See the layout note in
// M2Types.hpp, and the commit that fixed exactly this mistake in the particle and ribbon builders.
//
// **Still not implemented, deliberately, and the reason below has not changed.**
// ref: FUN_00824550
int32_t CM2Model::IsBatchDoodadCompatible(M2Batch* batch) {
    // TODO -- see the decoded body above, and read the warning above that before enabling it

    return 0;
}

// ref: FUN_00824fc0
int32_t CM2Model::IsDrawable(int32_t a2, int32_t a3) {
    if (!this->m_loaded && a2) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_flag2) {
        if (!this->m_loaded) {
            return 0;
        }

        for (uint32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
            auto texture = this->m_textures[i];

            if (!texture) {
                continue;
            }

            if (!TextureGetGxTex(texture, a2, nullptr)) {
                return 0;
            }
        }

        this->m_flag2 = 1;
    }

    if (!this->m_flag200 && a3) {
        // TODO
    }

    return 1;
}

// ref: FUN_00824f00
int32_t CM2Model::IsLoaded(int32_t a2, int32_t attachments) {
    if (this->m_flags & 0x20) {
        if (this->m_loaded) {
            return 1;
        }

        if (a2) {
            this->WaitForLoad(nullptr);
        }

        // Either this model finished loading while we waited, or the shared data it needs is
        // already there. The reference ORs the two, and sign-extends the shared test, so this
        // returns -1 as readily as 1; every caller only asks whether it is non-zero.
        int32_t sharedReady = this->m_shared->m_m2DataLoaded && this->m_shared->m_skinProfileLoaded ? -1 : 0;

        return sharedReady | static_cast<int32_t>(this->m_loaded);
    }

    if (!this->m_loaded && a2) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_loaded) {
        return 0;
    }

    // Every attached model has to be loaded too before this one counts as ready. The answer is
    // cached in the 0x100 flag, so the walk happens once.
    if (attachments && !this->m_flag100) {
        for (auto child = this->m_attachList; child; child = child->m_attachNext) {
            if (!child->IsLoaded(a2, 1)) {
                return 0;
            }
        }

        this->m_flag100 = 1;
    }

    return 1;
}

// ref: FUN_008244f0
void CM2Model::LinkToCallbackListTail() {
    this->m_callbackPrev = this->m_shared->m_callbackListTail;
    this->m_callbackNext = nullptr;
    *this->m_shared->m_callbackListTail = this;
    this->m_shared->m_callbackListTail = &this->m_callbackNext;
}

// ref: FUN_00823f90
// Can these two adjacent batches be drawn as one? Only if every piece of per-draw state they
// would set is already identical: the colour, the material, and the texture, texture-weight and
// texture-transform combos -- plus, on the sections, the same bone combo, so one set of bone
// matrices serves both.
//
// Deliberately NOT a general "are these equal": geosetIndex, materialLayer, textureCount and
// priorityPlane are all left out, and so is skinSectionIndex, which is the whole point -- two
// different sections merging into one is what this enables. The builder's second pass spells
// the same comparison out inline rather than calling here, which is how the field list was
// cross-checked.
bool M2BatchesCanMerge(const M2Batch& a, const M2Batch& b,
                       const M2SkinSection& sectionA, const M2SkinSection& sectionB) {
    return a.colorIndex == b.colorIndex
        && a.materialIndex == b.materialIndex
        && a.textureComboIndex == b.textureComboIndex
        && a.textureWeightComboIndex == b.textureWeightComboIndex
        && a.textureTransformComboIndex == b.textureTransformComboIndex
        && sectionA.boneComboIndex == sectionB.boneComboIndex;
}

// ref: FUN_0082c970
// Collapse the model's VISIBLE skin sections into as few batches as the render state allows, so
// a character whose geosets have just been chosen draws in a handful of calls instead of one per
// section. Two passes over the skin profile's batches: the first counts, the second builds.
//
// It gives up if merging would not actually reduce the batch count, which is the common case for
// a model whose sections all differ -- there is no block at all then, and every consumer falls
// back to the skin profile through its own `if (ptr2D0)` test.
//
// DIVERGENCE, and unavoidable: the reference computes one allocation size with 32-bit arithmetic
// (`(batchCount + sectionCount * 2) * 0x1c + 0x20`, where 0x1c is sizeof(M2Batch) plus a pointer
// and 0x38 is sizeof(M2SkinSection) plus the range pair). frozen's pointers are 8 bytes, so the
// header and the effect array are wider and the size is built from sizeof() instead. The carve-up
// stays 8-aligned throughout because every piece is a multiple of 8.
//
// The reference also keeps its per-section index memo on the stack with alloca. This uses a
// scratch allocation: the length is skinSections.Count(), which is model data rather than
// anything bounded, and an alloca of unbounded size is how a stack overflow gets in.
void CM2Model::OptimizeVisibleGeometry() {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 3;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    this->UnoptimizeVisibleGeometry();
    this->m_flag10 = 0;

    auto shared = this->m_shared;
    auto skinProfile = shared->skinProfile;
    uint32_t cacheFlags = shared->m_cache->m_flags;

    // How two merged sections combine their VERTEX ranges: either the union of the two ranges, or
    // a plain sum of the counts. A single-bone model only qualifies with the second flag, which is
    // why the bone count is in the test.
    bool unionRanges = (cacheFlags & 0x8)
        || (shared->m_data->bones.Count() == 1 && (cacheFlags & 0x40));

    // Pass one: how many visible batches are there, and how many groups do they collapse into?
    // Note that a batch which merges does NOT become the new comparison point -- the whole run is
    // compared against the batch that opened it.
    uint32_t visibleBatches = 0;
    uint32_t groupCount = 0;
    const M2Batch* runBatch = nullptr;
    const M2SkinSection* runSection = nullptr;

    for (uint32_t i = 0; i < skinProfile->batches.Count(); i++) {
        auto& batch = skinProfile->batches[i];

        if (!this->m_skinSections[batch.skinSectionIndex]) {
            continue;
        }

        visibleBatches++;

        auto& section = skinProfile->skinSections[batch.skinSectionIndex];

        if (runBatch && M2BatchesCanMerge(batch, *runBatch, section, *runSection)) {
            continue;
        }

        groupCount++;
        runBatch = &batch;
        runSection = &section;
    }

    if (groupCount >= visibleBatches) {
        return;
    }

    // One allocation, five arrays. groupCount is both the batch and the section count: pass one
    // opens exactly one section per group.
    size_t headerSize = sizeof(M2OptimizedGeometry);
    size_t size = headerSize
        + groupCount * sizeof(M2Batch)
        + groupCount * sizeof(M2SkinSection)
        + groupCount * sizeof(uint32_t) * 2
        + groupCount * sizeof(CShaderEffect*);

    auto block = static_cast<M2OptimizedGeometry*>(SMemAlloc(size, __FILE__, __LINE__, 0x0));

    if (!block) {
        return;
    }

    auto cursor = reinterpret_cast<char*>(block) + headerSize;

    block->batches = reinterpret_cast<M2Batch*>(cursor);
    cursor += groupCount * sizeof(M2Batch);

    block->skinSections = reinterpret_cast<M2SkinSection*>(cursor);
    cursor += groupCount * sizeof(M2SkinSection);

    block->sourceBatchRange = reinterpret_cast<uint32_t(*)[2]>(cursor);
    cursor += groupCount * sizeof(uint32_t) * 2;

    block->effects = reinterpret_cast<CShaderEffect**>(cursor);

    block->batchCount = 0;
    block->skinSectionCount = 0;
    block->m_indexPool = nullptr;
    block->m_indexBuf = nullptr;

    // The reference zeroes the sections' centre vectors and the range pairs in an unrolled loop
    // here. Every slot is written by pass two before anything reads it, so this is the same thing
    // said once.
    memset(block->skinSections, 0, groupCount * sizeof(M2SkinSection));
    memset(block->sourceBatchRange, 0, groupCount * sizeof(uint32_t) * 2);

    // Set BEFORE pass two: the bail-out at the bottom goes through
    // UnoptimizeVisibleGeometry, which reads this.
    this->ptr2D0 = block;

    // Where each SOURCE section's indices start in the new buffer. Sections shared by several
    // groups reuse the first answer rather than getting a second copy.
    uint32_t sourceSectionCount = skinProfile->skinSections.Count();
    auto indexStart = static_cast<uint32_t*>(
        SMemAlloc(sourceSectionCount * sizeof(uint32_t), __FILE__, __LINE__, 0x0));

    for (uint32_t i = 0; i < sourceSectionCount; i++) {
        indexStart[i] = 0xFFFFFFFF;
    }

    // Pass two: build.
    uint32_t indexTotal = 0;
    M2SkinSection* dstSection = nullptr;
    uint32_t (*dstRange)[2] = nullptr;

    runBatch = nullptr;
    runSection = nullptr;

    for (uint32_t i = 0; i < skinProfile->batches.Count(); i++) {
        auto& srcBatch = skinProfile->batches[i];

        if (!this->m_skinSections[srcBatch.skinSectionIndex]) {
            continue;
        }

        auto& srcSection = skinProfile->skinSections[srcBatch.skinSectionIndex];

        // The same test M2BatchesCanMerge makes, spelled out because the reference spells it out
        // here rather than calling its own helper a second time.
        bool newGroup = !runBatch
            || srcBatch.colorIndex != runBatch->colorIndex
            || srcBatch.materialIndex != runBatch->materialIndex
            || srcBatch.textureComboIndex != runBatch->textureComboIndex
            || srcBatch.textureWeightComboIndex != runBatch->textureWeightComboIndex
            || srcBatch.textureTransformComboIndex != runBatch->textureTransformComboIndex
            || srcSection.boneComboIndex != runSection->boneComboIndex;

        if (newGroup) {
            uint32_t out = block->skinSectionCount;

            auto dstBatch = &block->batches[block->batchCount];
            dstSection = &block->skinSections[out];
            dstRange = &block->sourceBatchRange[out];

            *dstBatch = srcBatch;
            dstBatch->skinSectionIndex = static_cast<uint16_t>(out);

            *dstSection = srcSection;
            dstSection->skinSectionId = out;

            if (indexStart[srcBatch.skinSectionIndex] == 0xFFFFFFFF) {
                indexStart[srcBatch.skinSectionIndex] = indexTotal;
            }

            dstSection->indexStart = static_cast<uint16_t>(indexStart[srcBatch.skinSectionIndex]);

            if (!unionRanges) {
                dstSection->vertexStart = 0;
            }

            (*dstRange)[0] = i;
            (*dstRange)[1] = i;

            block->batchCount++;
            block->skinSectionCount++;

            runBatch = &srcBatch;
            runSection = &srcSection;
        } else {
            if (unionRanges) {
                if (srcSection.vertexStart < dstSection->vertexStart) {
                    dstSection->vertexCount += dstSection->vertexStart - srcSection.vertexStart;
                    dstSection->vertexStart = srcSection.vertexStart;
                }

                if (dstSection->vertexStart + dstSection->vertexCount
                    < srcSection.vertexStart + srcSection.vertexCount) {
                    dstSection->vertexCount = static_cast<uint16_t>(
                        srcSection.vertexCount + srcSection.vertexStart - dstSection->vertexStart);
                }
            } else {
                dstSection->vertexCount += srcSection.vertexCount;
            }

            dstSection->indexCount += srcSection.indexCount;

            if (dstSection->boneCount < srcSection.boneCount) {
                dstSection->boneCount = srcSection.boneCount;
            }

            if (dstSection->boneInfluences < srcSection.boneInfluences) {
                dstSection->boneInfluences = srcSection.boneInfluences;
            }

            (*dstRange)[1] = i;
        }

        indexTotal += srcSection.indexCount;
    }

    SMemFree(indexStart, __FILE__, __LINE__, 0x0);

    for (uint32_t i = 0; i < block->batchCount; i++) {
        block->effects[i] = shared->GetEffect(&block->batches[i]);
    }

    block->m_indexPool = GxPoolCreate(
        GxPoolTarget_Index,
        GxPoolUsage_Static,
        indexTotal * 2,
        GxPoolHintBit_Unk0,
        shared->ext
    );

    block->m_indexBuf = GxBufCreate(block->m_indexPool, 2, indexTotal, 0);

    // A pool or buffer the device would not give is not a state to carry: drop the whole block and
    // let every consumer fall back to the skin profile.
    if (!block->m_indexPool || !block->m_indexBuf) {
        this->UnoptimizeVisibleGeometry();
    }
}

int32_t CM2Model::ProcessCallbacks() {
    // Notice the bone sequences that finished during the frame just stepped, and let each one pick
    // its next variation. This is what keeps a standing NPC alive: the model's Stand animation is a
    // chain of variations weighted by frequency, and a new one is rolled every time the current one
    // runs out. Returns 0 when the model was destroyed while handling a callback.
    if (!this->m_flag400000 || !this->m_loaded || !this->m_shared || !this->m_shared->m_m2DataLoaded) {
        return 1;
    }

    auto data = this->m_shared->m_data;
    int32_t now = this->m_scene->m_time;
    int32_t previous = now - this->m_scene->uint10;

    for (uint32_t boneIndex = this->m_boneSeqList; boneIndex != 0xFFFF; ) {
        auto& modelBone = this->m_bones[boneIndex];

        // Read the link before the handler runs: re-issuing a sequence can relink the bone.
        uint32_t next = modelBone.word96;

        if (!modelBone.sequence.uintA && modelBone.sequence.uint8 < data->sequences.Count()) {
            auto& sequence = data->sequences[modelBone.sequence.uint8];

            // The sequence plays at float18's rate, so its wall-clock length is the authored
            // duration scaled by it.
            uint32_t duration = sequence.duration;

            if (fabsf(fabsf(modelBone.sequence.float18) - 1.0f) >= 0.0000099999997f) {
                duration = static_cast<uint32_t>(
                    floorf(static_cast<float>(sequence.duration) * fabsf(modelBone.sequence.float18) + 0.5f));
            }

            if (duration) {
                int32_t endTime;

                if (sequence.flags & 0x1) {
                    // Plays once: SetupBoneSequence already worked out when it stops.
                    endTime = modelBone.sequence.uint10;
                } else {
                    // Loops: the end of whichever repetition the frame is inside.
                    int32_t from = static_cast<int32_t>(modelBone.sequence.uintC);

                    if (previous - from >= 0) {
                        from = previous;
                    }

                    uint32_t loops = static_cast<uint32_t>(from - static_cast<int32_t>(modelBone.sequence.uintC)) / duration;
                    endTime = modelBone.sequence.uintC + (loops + 1) * duration - 1;
                }

                if (endTime - previous > 0 && endTime - now <= 0) {
                    this->SequenceFinished(
                        static_cast<uint16_t>(boneIndex),
                        static_cast<uint32_t>(now - endTime),
                        modelBone.sequence.uint8,
                        modelBone.sequence.uintC
                    );

                    // A callback may have released the model out from under us; the caller holds
                    // one reference of its own, so anything less means it is already gone.
                    if (this->m_refCount <= 1) {
                        return 0;
                    }
                }
            }
        }

        boneIndex = next;
    }

    return 1;
}

// ref: FUN_00831fc0
// One bone sequence has just run out: tell the owner, then roll the next variation of the same
// animation and start it, carrying the overshoot so the new sequence begins where the old one
// actually ended rather than at the frame boundary.
//
// The reference takes one record off a deferred queue instead of four arguments (the queue is
// flushed by FUN_008321e0, which also carries the animation-event records); the computation is the
// same and the record's fields are these four plus the callback and its owner.
//
// THE CALLBACK HERE IS THE ONE THAT REPORTS A NATURAL END. NotifySequenceDone reports a sequence
// being REPLACED and passes 1 for `interrupted`; this passes 0, which is what lets the owner treat
// the animation as finished -- for a unit, that is what hands back its permission to choose the
// next one (CGUnit_C::OnAnimationFinished).
void CM2Model::SequenceFinished(uint16_t boneIndex, uint32_t overshoot, uint16_t seqIndexWas, uint32_t startTimeWas) {
    auto data = this->m_shared->m_data;

    // Only key bones (and the root) drive sequence callbacks in the reference, and the callback is
    // addressed by bone *id*, which resolves back to the canonical bone for that id.
    if (boneIndex >= data->bones.Count()) {
        return;
    }

    if (data->bones[boneIndex].boneId == 0xFFFFFFFF && boneIndex != 0) {
        return;
    }

    uint32_t boneId = (data->bones[boneIndex].parentIndex == 0xFFFF) ? 0xFFFFFFFF : data->bones[boneIndex].boneId;
    uint16_t resolved;

    if (boneId == 0xFFFFFFFF) {
        resolved = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        resolved = data->boneIndicesById[boneId];
    } else {
        return;
    }

    if (resolved >= data->bones.Count()) {
        return;
    }

    auto& modelBone = this->m_bones[resolved];
    uint16_t seqIndex = modelBone.sequence.uint8;

    // The bone may have been handed something else in the meantime; only the sequence that actually
    // ended gets to choose what follows it.
    if (seqIndex != seqIndexWas || modelBone.sequence.uintC != startTimeWas || seqIndex == 0xFFFF) {
        return;
    }

    auto& sequence = data->sequences[seqIndex];

    if (sequence.flags & 0x1) {
        // Plays once and holds its last frame.
        modelBone.sequence.uintA = 1;
    }

    // The owner is told even for a play-once sequence -- it has still ended -- and may hand the
    // bone something else from inside the handler, so the match is re-checked after.
    if (this->m_sequenceDoneCallback) {
        this->m_sequenceDoneCallback(this, boneId, modelBone.uint90, 0, static_cast<int32_t>(overshoot),
                                    this->m_sequenceDoneOwner);

        if (modelBone.sequence.uint8 != seqIndexWas || modelBone.sequence.uintC != startTimeWas) {
            return;
        }
    }

    if (sequence.flags & 0x1) {
        // Nothing follows a play-once sequence.
        return;
    }

    // Nothing to roll when the animation has a single variation, and nothing to roll when the
    // variation was chosen explicitly rather than at random (uintB).
    if (!modelBone.sequence.uintB || (sequence.variationIndex == 0 && sequence.variationNext == 0xFFFF)) {
        return;
    }

    float rate = modelBone.sequence.float14;

    M2SequenceFallback fallback;
    this->Sub826350(fallback, modelBone.uint90);

    uint32_t index = CM2Model::Sub8260C0(data, fallback.uint0, 0);
    uint32_t variation = 0;
    this->Sub826E60(&variation, &index);

    if (index >= data->sequences.Count()) {
        return;
    }

    // The overshoot is measured in scene milliseconds; a sequence playing at a rate other than 1
    // consumes it faster or slower.
    uint32_t time = overshoot;

    if (fabsf(rate - 1.0f) >= 0.0000099999997f) {
        time = static_cast<uint32_t>(floorf(static_cast<float>(overshoot) * modelBone.sequence.float18 + 0.5f));
    }

    if (data->sequences[index].flags & 0x20) {
        modelBone.uint94 = static_cast<uint16_t>(variation);

        this->SetPrimaryBoneSequence(static_cast<uint16_t>(index), resolved, fallback, time, rate, 1);

        return;
    }

    this->SetBoneSequenceDeferred(static_cast<uint16_t>(index), data, resolved, time, rate, fallback, 1, 1, 1);
}

void CM2Model::ProcessCallbacksRecursive() {
    if (!this->m_loaded) {
        return;
    }

    this->AddRef();

    if (this->ProcessCallbacks()) {
        // TODO process attachments
    }

    this->Release();
}

// ref: FUN_00824ed0
uint32_t CM2Model::Release() {
    STORM_ASSERT(this->m_refCount > 0);

    this->m_refCount--;

    if (this->m_refCount > 0) {
        return this->m_refCount;
    }

    this->~CM2Model();
    ObjectFree(*g_modelPool, this->m_memHandle);

    return 0;
}

// ref: FUN_00825260
void CM2Model::ReplaceTexture(uint32_t textureId, HTEXTURE texture) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 0;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = textureId;
        *reinterpret_cast<HTEXTURE*>(&modelCall->args[1]) = texture ? HandleDuplicate(texture) : nullptr;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    // Replace textures

    for (int32_t i = 0; i < this->m_shared->m_data->textures.Count(); i++) {
        // Only replace if texture IDs match
        if (this->m_shared->m_data->textures[i].textureId != textureId) {
            continue;
        };

        auto currentTexture = this->m_textures[i];

        if (currentTexture) {
            HandleClose(currentTexture);
        }

        if (texture) {
            this->m_textures[i] = HandleDuplicate(texture);

            auto gxTexture = TextureGetGxTex(this->m_textures[i], 0, nullptr);

            if (!gxTexture) {
                this->m_flag2 = 0;
            }
        } else {
            this->m_textures[i] = nullptr;
        }
    }

    // TODO replace ribbon textures

    // TODO replace particle textures
}

// ref: FUN_00823f10
void CM2Model::SetAnimating(int32_t animating) {
    if (!animating) {
        if (this->m_animatePrev) {
            *this->m_animatePrev = this->m_animateNext;

            if (this->m_animateNext) {
                this->m_animateNext->m_animatePrev = this->m_animatePrev;
            }

            this->m_animatePrev = nullptr;
            this->m_animateNext = nullptr;
        }

        return;
    }

    if (this->m_flags & 0x20 && !this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    if (!this->m_animatePrev) {
        this->m_animatePrev = &this->m_scene->m_animateList;
        this->m_animateNext = this->m_scene->m_animateList;
        this->m_scene->m_animateList = this;

        if (this->m_animateNext) {
            this->m_animateNext->m_animatePrev = &this->m_animateNext;
        }
    }
}

// ref: FUN_00832ab0
void CM2Model::SetBoneSequence(uint32_t boneId, uint32_t sequenceId, uint32_t a4, uint32_t time, float a6, int32_t a7, int32_t a8) {
    if (sequenceId == -1) {
        this->UnsetBoneSequence(boneId, a7, a8);
        return;
    }

    if (!this->m_loaded) {
        auto m = SMemAlloc(sizeof(CM2ModelCall), __FILE__, __LINE__, 0x0);
        auto modelCall = new (m) CM2ModelCall();

        modelCall->type = 5;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = sequenceId;
        modelCall->args[2] = a4;
        modelCall->args[3] = time;
        *reinterpret_cast<float*>(&modelCall->args[4]) = a6;
        modelCall->args[5] = a7;
        modelCall->args[6] = a8;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    if (this->m_flag800) {
        a7 = 0;
    }

    uint16_t boneIndex;
    if (boneId == -1) {
        boneIndex = 0;
    } else if (boneId < this->m_shared->m_data->boneIndicesById.Count()) {
        boneIndex = this->m_shared->m_data->boneIndicesById[boneId];
    } else {
        boneIndex = -1;
    }

    if (boneIndex >= this->m_shared->m_data->bones.Count()) {
        return;
    }

    M2SequenceFallback fallback;
    this->Sub826350(fallback, sequenceId);
    int32_t v33 = a4 == -1;

    uint16_t v15 = CM2Model::Sub8260C0(this->m_shared->m_data, fallback.uint0, a4 != -1 ? a4 : 0);
    uint32_t v16 = v15;
    uint32_t v32 = v15;
    uint32_t v17;

    if (v15 != 0xFFFF) {
        if (!v33) {
            goto LABEL_30;
        }

        goto LABEL_29;
    }

    v17 = this->m_shared->m_data->sequenceIdxHashById.Count();
    v33 = 1;
    v32 = v17;
    uint16_t v18;

    if (v17) {
        uint32_t v20 = fallback.uint0 % v17;
        v18 = this->m_shared->m_data->sequenceIdxHashById[v20];

        if (v18 != 0xFFFF) {
            uint32_t v21 = 1;

            if (this->m_shared->m_data->sequences[v18].id != fallback.uint0) {
                while (1) {
                    v20 = (v20 + v21 * v21) % v32;
                    v18 = this->m_shared->m_data->sequenceIdxHashById[v20];

                    if (v18 == 0xFFFF) {
                        break;
                    }

                    ++v21;

                    if (this->m_shared->m_data->sequences[v18].id == fallback.uint0) {
                        goto LABEL_26;
                    }
                }

                v32 = 0xFFFF;

                goto LABEL_29;
            }

            goto LABEL_26;
        }

        v32 = 0xFFFF;
    } else {
        v18 = 0;

        if (this->m_shared->m_data->sequences.Count())  {
            while (this->m_shared->m_data->sequences[v18].id != fallback.uint0) {
                ++v18;

                if (v18 >= this->m_shared->m_data->sequences.Count()) {
                    goto LABEL_20;
                }
            }

LABEL_26:
            v32 = v18;
            goto LABEL_29;
        }

LABEL_20:
        v32 = 0xFFFF;
    }

LABEL_29:
    this->Sub826E60(&a4, &v32);
    v16 = v32;

LABEL_30:
    // A sequence the model does not carry leaves the not-found marker (0xFFFF) in v16: the lookup
    // above yields it, and Sub826E60 (which resolves the variation in the original) is still a
    // stub here, so nothing clears it. Indexing sequences[] with it read far out of bounds and
    // crashed the client whenever a unit asked for an animation its model lacks.
    if (v16 >= this->m_shared->m_data->sequences.Count()) {
        return;
    }

    if (this->m_shared->m_data->sequences[v16].flags & 0x20) {
        if (this->NotifySequenceDone(boneId, boneIndex)) {
            this->CancelDeferredSequences(boneIndex, a8 != 0);

            auto& modelBone = this->m_bones[boneIndex];

            if (a8) {
                modelBone.uint90 = sequenceId;
                modelBone.uint94 = a4;

                this->SetPrimaryBoneSequence(v16, boneIndex, fallback, time, a6, a7);
                modelBone.sequence.uintB = v33;
            } else {
                this->SetSecondaryBoneSequence(v16, boneIndex, fallback, time, a6);
                modelBone.secondarySequence.uintB = v33;
            }
        }
    } else {
        this->SetBoneSequenceDeferred(v16, this->m_shared->m_data, boneIndex, time, a6, fallback, a7, a8, v33);
    }
}

// The replay half of FUN_0083d840 (CM2Shared::SequenceLoadedCallback): apply one parked request
// now that the sequence's keyframes are in. 0 when the bone no longer takes sequences, in which
// case the callback keeps the record (the reference leaves it in the list as well).
int32_t CM2Model::ApplySequencePlayBack(uint16_t sequenceIndex, CM2SequencePlayBack* playback) {
    auto data = this->m_shared->m_data;

    if (!this->NotifySequenceDone(data->bones[playback->boneIndex].boneId, playback->boneIndex)) {
        return 0;
    }

    auto& sequence = data->sequences[sequenceIndex];
    auto& modelBone = this->m_bones[playback->boneIndex];
    M2SequenceFallback fallback = { playback->fallbackId, playback->fallbackMode };

    if (playback->flags & 2) {
        modelBone.uint90 = sequence.id;
        modelBone.uint94 = sequence.variationIndex;
        this->SetPrimaryBoneSequence(sequenceIndex, playback->boneIndex, fallback, playback->time, playback->speed, playback->flags & 1);
        modelBone.sequence.uintB = playback->flags & 4;
    } else {
        this->SetSecondaryBoneSequence(sequenceIndex, playback->boneIndex, fallback, playback->time, playback->speed);
        modelBone.secondarySequence.uintB = playback->flags & 4;
    }

    return 1;
}

// ref: FUN_00831c30
// A sequence whose keyframes are still on disk (no flag 0x20): park the request on the shared
// model's load record for it -- the one already in flight when the sequence (or any alias in its
// chain) carries the loading flag 0x10, else a fresh CM2Shared::LoadSequence -- and let
// CM2Shared::SequenceLoadedCallback apply it when the .anim data lands. One record per model per
// load: a repeat request from the same model just overwrites its parked parameters.
void CM2Model::SetBoneSequenceDeferred(uint16_t a2, M2Data* data, uint16_t boneIndex, uint32_t time, float a6, M2SequenceFallback fallback, int32_t a8, int32_t a9, int32_t a10) {
    CM2SequenceLoad* load = nullptr;
    CM2SequencePlayBack* playback = nullptr;

    if (data->sequences[a2].flags & 0x10) {
        uint16_t index = a2;

        for (;;) {
            for (load = this->m_shared->m_sequenceLoads.Head(); load; load = this->m_shared->m_sequenceLoads.Next(load)) {
                if (load->sequenceIndex == index) {
                    break;
                }
            }

            if (load) {
                for (playback = load->playbacks.Head(); playback; playback = load->playbacks.Next(playback)) {
                    if (playback->model == this) {
                        break;
                    }
                }

                break;
            }

            index = data->sequences[index].aliasNext;

            if (index == a2) {
                return;
            }
        }
    } else {
        load = this->m_shared->LoadSequence(a2);

        if (!load) {
            return;
        }
    }

    if (!playback) {
        playback = STORM_NEW(CM2SequencePlayBack);
        load->playbacks.LinkToTail(playback);
        playback->model = this;
    }

    playback->speed = a6;
    playback->boneIndex = boneIndex;
    playback->time = time;
    playback->fallbackId = fallback.uint0;
    playback->fallbackMode = fallback.uint2;
    playback->flags = (a8 ? 1 : 0) | (a9 ? 2 : 0) | (a10 ? 4 : 0);
}

// ref: FUN_0082c7c0
void CM2Model::SetGeometryVisible(uint32_t start, uint32_t end, int32_t visible) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 1;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = start;
        modelCall->args[1] = end;
        modelCall->args[2] = visible;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    // No skin sections

    if (!this->m_shared->skinProfile->skinSections.Count()) {
        return;
    }

    // Update visibility

    bool visibilityChanged = false;

    for (uint32_t i = 0; i < this->m_shared->skinProfile->skinSections.Count(); i++) {
        auto& skinSection = this->m_shared->skinProfile->skinSections[i];
        auto modelSkinSection = &this->m_skinSections[i];

        // Out of range
        if (skinSection.skinSectionId < start || skinSection.skinSectionId > end) {
            continue;
        }

        // No change
        if (*modelSkinSection == 0 && visible == 0 || *modelSkinSection == 1 && visible == 1) {
            continue;
        }

        *modelSkinSection = visible;
        visibilityChanged = true;
    }

    if (visibilityChanged) {
        this->UnoptimizeVisibleGeometry();
    }
}

// Identified from DrawBatch's call order: the reference runs 0x00683560, 0x00683580, 0x00828f90,
// 0x008360a0 and then SetBatchVertices, and frozen runs GxShaderConstantsLock, Unlock,
// m_curModel->SetIndices, m_curShared->SetIndices and then SetBatchVertices. The other three of
// those are pinned independently, and this is the remaining one -- a thiscall on the model,
// reaching through ptr2D0.
// ref: FUN_00828f90
// Fill the merged index buffer, once, and bind it.
//
// Only reachable for a model that HAS an optimized-geometry block, which is why it dereferences
// ptr2D0 without checking it: CM2SceneRender::DrawBatch calls it for an element with flag 0x4,
// and CM2Scene only sets that flag when the block exists.
//
// The fill is skipped once done. unk1C is the `filled` flag and CGxPool::Invalidate clears it, so
// a pool the device had to drop refills itself on the next draw rather than drawing stale
// indices. unk1D is born 1 and is the buffer's own validity.
//
// Whether the indices are copied verbatim or rebased is the SAME condition the builder used to
// decide how to combine vertex ranges, and that is not a coincidence: when the merged section
// took the union of its sources' vertex ranges the original numbering still lands inside it, so
// the indices are already right. When the counts were merely summed, each source's indices have
// to be moved off its own vertexStart and onto a running base.
int32_t CM2Model::SetIndices() {
    auto optGeo = this->ptr2D0;
    auto buf = optGeo->m_indexBuf;

    if (!buf->unk1C || !buf->unk1D) {
        auto dst = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(buf));

        if (!dst) {
            return 0;
        }

        auto shared = this->m_shared;
        auto skinProfile = shared->skinProfile;
        uint32_t cacheFlags = shared->m_cache->m_flags;

        bool singleBone = shared->m_data->bones.Count() == 1 && (cacheFlags & 0x40);
        bool verbatim = (cacheFlags & 0x8) || singleBone;

        for (uint32_t i = 0; i < optGeo->skinSectionCount; i++) {
            auto& range = optGeo->sourceBatchRange[optGeo->skinSections[i].skinSectionId];

            // Where the next source section's vertices begin, relative to the merged section.
            uint16_t vertexBase = 0;

            for (uint32_t b = range[0]; b <= range[1]; b++) {
                auto sourceIndex = skinProfile->batches[b].skinSectionIndex;

                // The run spans a contiguous stretch of the ORIGINAL batches, and an invisible
                // one inside that stretch contributed nothing to the merge.
                if (!this->m_skinSections[sourceIndex]) {
                    continue;
                }

                auto& sourceSection = skinProfile->skinSections[sourceIndex];
                auto sourceIndices = &skinProfile->indices[sourceSection.indexStart];

                if (verbatim) {
                    memcpy(dst, sourceIndices, sourceSection.indexCount * sizeof(uint16_t));
                } else {
                    for (uint32_t k = 0; k < sourceSection.indexCount; k++) {
                        dst[k] = sourceIndices[k] - sourceSection.vertexStart + vertexBase;
                    }
                }

                vertexBase += sourceSection.vertexCount;
                dst += sourceSection.indexCount;
            }
        }

        g_theGxDevicePtr->BufUnlock(buf, 0);

        buf->unk1C = 1;
    }

    g_theGxDevicePtr->PrimIndexPtr(optGeo->m_indexBuf);

    return 1;
}

void CM2Model::SetLightingCallback(void (*lightingCallback)(CM2Model*, CM2Lighting*, void*), void* lightingArg) {
    this->m_lightingCallback = lightingCallback;
    this->m_lightingArg = lightingArg;
}

// ref: FUN_008251b0
void CM2Model::SetLoadedCallback(void (*loadedCallback)(CM2Model*, void*), void* loadedArg) {
    this->m_loadedCallback = loadedCallback;
    this->m_loadedArg = loadedArg;

    this->UpdateLoaded();
}

void CM2Model::SetPrimaryBoneSequence(uint16_t sequenceIndex, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6, int32_t a7) {
    auto& modelBone = this->m_bones[boneIndex];
    auto& sequence = this->m_shared->m_data->sequences[sequenceIndex];

    if (a7) {
        if (!modelBone.sequence.uintA || sequenceIndex != modelBone.sequence.uint8) {
            double v10;
            double v11;
            double v12;

            if (modelBone.secondarySequence.uint8 == 0xFFFF
                || ((v10 = (double)(modelBone.uint9C - this->m_scene->m_time) * modelBone.floatA0, v10 >= 0.0) ? (v10 <= 1.0 ? (v11 = v10 * ((3.0 - (v10 + v10)) * v10)) : (v11 = 1.0)) : (v11 = 0.0), v11 * modelBone.floatA4 <= 0.5)
            ) {
                memcpy(&modelBone.secondarySequence, &modelBone.sequence, sizeof(modelBone.secondarySequence));

                modelBone.uint9C = this->m_scene->m_time + sequence.blendtime;
                if (sequence.blendtime) {
                    v12 = 1.0 / (double)sequence.blendtime;
                } else {
                    v12 = 1.0;
                }
                modelBone.floatA0 = v12;
                modelBone.floatA4 = 1.0f;
            }
        }
    } else {
        modelBone.secondarySequence.uint8 = -1;
    }

    this->SetupBoneSequence(sequenceIndex, fallback, time, a6, &modelBone.sequence);

    int32_t v13 = modelBone.sequence.uint10;
    if (modelBone.sequence.uintC == v13 || ((sequence.flags & 0x1) != 0 && (v13 -= this->m_scene->m_time, v13 <= 0))) {
        modelBone.sequence.uintA = 1;
    }

    // Link the bone into the model's animating-bone list the first time it is given a sequence, so
    // ProcessCallbacks can find it again when that sequence ends. Without this list nothing ever
    // noticed a finished animation, and a unit held whichever variation it was first handed.
    if (!modelBone.dword98) {
        modelBone.dword98 = &this->m_boneSeqList;
        modelBone.word96 = this->m_boneSeqList;

        if (modelBone.word96 != 0xFFFF) {
            this->m_bones[modelBone.word96].dword98 = &modelBone.word96;
        }

        this->m_boneSeqList = boneIndex;
        this->m_flag400000 = 1;
    }
}

// ref: FUN_00826dd0
// Put a sequence straight into the bone's secondary slot and start it fading.
//
// Note it times the fade from the sequence's DURATION, where SetPrimaryBoneSequence uses its
// blendtime, and it starts the weight at 0.75 rather than 1. That reads like a slip but it is
// what the reference does: FUN_00826dd0 takes the field at +0x4 while FUN_00826c40 takes +0x1c.
void CM2Model::SetSecondaryBoneSequence(uint16_t a2, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6) {
    auto& modelBone = this->m_bones[boneIndex];
    auto& sequence = this->m_shared->m_data->sequences[a2];

    modelBone.uint9C = this->m_scene->m_time + sequence.duration;
    modelBone.floatA0 = sequence.duration ? 1.0f / static_cast<float>(sequence.duration) : 1.0f;
    modelBone.floatA4 = 0.75f;

    this->SetupBoneSequence(a2, fallback, time, a6, &modelBone.secondarySequence);
}

void CM2Model::SetupBoneSequence(uint16_t sequenceIndex, M2SequenceFallback fallback, uint32_t a4, float a5, M2ModelBoneSeq* boneSequence) {
    auto& sequence = this->m_shared->m_data->sequences[sequenceIndex];

    int32_t v9 = rand();
    uint32_t v10 = (sequence.replay.l + (sequence.replay.h - sequence.replay.l) * v9 / 0x8000 == 0)
        + sequence.replay.l + (sequence.replay.h - sequence.replay.l) * v9 / 0x8000;
    int32_t v11 = v10 * sequence.duration;

    double v12;
    double v13;
    long double v15;

    if (fallback.uint2 == 1 || fallback.uint2 == 3) {
        v12 = -a5;
    } else {
        v12 = a5;
    }

    v13 = 0.0;

    uint32_t v18 = 0;
    if (v12 < 0.0) {
        v18 = v11;
    }

    if (fallback.uint2 == 2 || fallback.uint2 == 3) {
        v12 = 0.0;
    }

    if (abs(v12) > 0.0000099999997) {
        v13 = 1.0 / v12;
    }

    v15 = abs(v13);
    uint32_t v16 = this->m_scene->m_time - floor((double)a4 * v15);

    if ((~(this->m_scene->m_flags >> 2) & 0x1) != 0) {
        v16++;
    }

    boneSequence->uint8 = sequenceIndex;
    boneSequence->uintC = v16;
    boneSequence->uint20 = v10;
    boneSequence->uintA = 0;
    boneSequence->uint10 = v16 + floor(v15 * (double)(unsigned int)v11);
    boneSequence->uint1C = v18;
    boneSequence->float14 = v12;
    boneSequence->float18 = v13;
}

// Identified beyond doubt from its call list: CM2Lighting::Initialize (0x00834900),
// CM2Scene::SelectLights (0x0081e400), the lighting callback through a pointer,
// CM2Lighting::SetupSunlight (0x00835280), CM2Lighting::CameraSpace (0x008350a0) -- and
// then itself, which is the recursion into m_attachList below.
// ref: FUN_00831af0
void CM2Model::SetupLighting() {
    if (!this->m_attachParent || this->m_attachParent->m_flags & 0x1) {
        this->Animate();

        CAaSphere sphere;
        sphere.c = this->GetPosition();
        sphere.r = 0.0f;

        this->m_lighting.Initialize(this->m_scene, sphere);
        this->m_scene->SelectLights(&this->m_lighting);

        if (this->m_lightingCallback) {
            this->m_lightingCallback(this, &this->m_lighting, this->m_lightingArg);
        }

        this->m_lighting.SetupSunlight();
        this->m_lighting.CameraSpace();
    }

    for (auto model = this->m_attachList; model; model = model->m_attachNext) {
        model->SetupLighting();
    }
}

void CM2Model::SetVisible(int32_t visible) {
    if (this->m_attachParent) {
        this->m_flag80 = visible ? 1 : 0;
    } else {
        this->m_flag8 = visible ? 1 : 0;
    }
}

void CM2Model::SetWorldTransform(const C3Vector& position, float orientation, float scale) {
    this->matrixB4.Identity();
    this->matrixB4.RotateAroundZ(orientation);
    this->matrixB4.Scale(scale);
    this->matrixB4.d0 = position.x;
    this->matrixB4.d1 = position.y;
    this->matrixB4.d2 = position.z;

    this->m_flag8000 = 1;
}

void CM2Model::Sub826350(M2SequenceFallback& fallback, uint32_t sequenceId) {
    auto data = this->m_shared->m_data;

    int32_t v12;
    if (CM2Model::Sub825E00(data, 0)) {
        v12 = 0;
    } else if (CM2Model::Sub825E00(data, 147)) {
        v12 = 147;
    } else {
        v12 = data->sequences[0].id;
    }

    uint32_t v10[506];
    memset(v10, 0, sizeof(v10));

    if (CM2Model::Sub825E00(data, sequenceId)) {
        fallback.uint0 = sequenceId;
        fallback.uint2 = 0;
        return;
    }

    // The model does not carry this animation, so follow AnimationData.dbc's fallback chain until
    // it reaches one the model does have. Two flags on the records passed through decide how the
    // result is played: 0x10 reverses the direction (a "close" animation is the "open" one run
    // backwards), 0x20 stops it dead on its last frame (a hold pose). uint2 encodes that as
    // 0 = forwards, 1 = backwards, 2 = hold at the start, 3 = hold at the end.
    //
    // The visited set is bounded at 506 entries the way the reference bounds it -- its own scratch
    // array is 0x7E8 bytes -- so a cyclic or out-of-range chain terminates instead of spinning.
    uint32_t visited[506];
    memset(visited, 0, sizeof(visited));

    int32_t direction = 1;
    int32_t held = 0;
    uint32_t animID = sequenceId;
    uint32_t result = v12;
    uint32_t next = 0;

    while (1) {
        auto record = g_animationDataDB.GetRecord(animID);

        result = v12;

        if (animID >= 506 || visited[animID] || !record || record->m_fallback == static_cast<int32_t>(animID)) {
            break;
        }

        next = static_cast<uint32_t>(record->m_fallback);
        visited[animID] = 1;

        if (record->m_flags & 0x10) {
            held += direction;
            direction = -direction;
        }

        if (record->m_flags & 0x20) {
            held += direction;
            direction = 0;
        }

        animID = next;

        if (CM2Model::Sub825E00(data, animID)) {
            // Found one the model carries. A still-positive direction means the chain only renamed
            // the animation, so it plays normally and falls through to the plain result below.
            result = animID;

            if (direction < 0) {
                fallback.uint0 = animID;
                fallback.uint2 = 1;
                return;
            }

            if (direction == 0) {
                fallback.uint0 = animID;
                fallback.uint2 = (held > 0) + 2;
                return;
            }

            break;
        }
    }

    fallback.uint0 = result;
    fallback.uint2 = 0;
}

// ref: FUN_008269c0
// Ask the model's sequence-finished callback whether stopping this bone is allowed, and report
// whether the model survived the call.
//
// The reference only does anything when the callback pointer is set: it raises the refcount,
// invokes the callback, releases, and returns 0 if that release destroyed the model. frozen has
// no such callback member yet, so the whole body is skipped and 1 -- "the model is still here,
// carry on" -- is the correct answer rather than a placeholder. It stops being correct the day
// the callback is ported; UnsetBoneSequence is the caller that depends on it.
// ref: FUN_008269c0
int32_t CM2Model::NotifySequenceDone(uint32_t boneId, uint16_t boneIndex) {
    auto callback = this->m_sequenceDoneCallback;

    if (!callback) {
        return 1;
    }

    auto& modelBone = this->m_bones[boneIndex];

    // Nothing to report when the bone holds no sequence, or holds one already parked on its last
    // frame -- that one was reported when it parked.
    if (modelBone.sequence.uint8 == 0xFFFF || modelBone.sequence.uintA != 0) {
        return 1;
    }

    auto data = this->m_shared->m_data;
    auto& bone = data->bones[boneIndex];

    // Only a bone with an id, or the root, reports; and the root reports as id -1.
    if (bone.boneId == 0xFFFFFFFF && boneIndex != 0) {
        return 1;
    }

    uint32_t reportId = (bone.parentIndex == 0xFFFF) ? 0xFFFFFFFF : boneId;

    this->AddRef();
    callback(this, reportId, modelBone.uint90, 1, 0, this->m_sequenceDoneOwner);

    return this->Release() != 0;
}

// ref: FUN_00823fe0
void CM2Model::SetSequenceDoneCallback(M2SequenceDoneCallback callback, WOWGUID owner) {
    if (this->m_sequenceDoneCallback == callback && this->m_sequenceDoneOwner == owner) {
        return;
    }

    this->m_sequenceDoneOwner = owner;
    this->m_sequenceDoneCallback = callback;

    if (!this->m_loaded) {
        return;
    }

    auto data = this->m_shared->m_data;

    // The per-frame callback sweep is only worth running while somebody is listening: a model with a
    // single sequence never finishes one, and one with no events has nothing to report.
    if (callback) {
        this->m_flag400000 = 1;

        return;
    }

    if (data->sequences.Count() < 2 && (!this->m_animEventCallback || data->events.Count() == 0)) {
        this->m_flag400000 = 0;
    }
}

// ref: FUN_00824060
void CM2Model::SetAnimEventCallback(M2AnimEventCallback callback, WOWGUID owner) {
    if (this->m_animEventCallback == callback && this->m_animEventOwner == owner) {
        return;
    }

    this->m_animEventOwner = owner;
    this->m_animEventCallback = callback;

    if (!this->m_loaded) {
        return;
    }

    auto data = this->m_shared->m_data;

    if (callback && data->events.Count() != 0) {
        this->m_flag400000 = 1;

        return;
    }

    if (!this->m_sequenceDoneCallback && data->sequences.Count() < 2) {
        this->m_flag400000 = 0;
    }
}

void CM2Model::Sub826E60(uint32_t* a2, uint32_t* a3) {
    // Choose which variation of an animation to play, weighted by M2Sequence::frequency.
    //
    // The frequencies of one animation's variations sum to 0x7FFF, which is exactly RAND_MAX here,
    // so a single rand() walks the chain subtracting each variation's weight until it lands inside
    // one. This is what gives an NPC its rare idle flourishes -- without it every unit sits on
    // variation 0 forever, which is why the world looked frozen.
    *a2 = 0;

    uint32_t roll = rand();
    uint32_t index = *a3;
    int32_t variation = 0;

    if (index == 0xFFFF) {
        return;
    }

    auto& sequences = this->m_shared->m_data->sequences;

    while (1) {
        uint32_t frequency = sequences[index].frequency;

        if (roll < frequency) {
            break;
        }

        roll -= frequency;
        index = sequences[index].variationNext;
        variation++;

        // Ran off the end of the chain (every variation weighted zero, or weights that do not sum
        // to RAND_MAX): leave the caller's sequence index alone.
        if (index == 0xFFFF) {
            return;
        }
    }

    *a3 = index;
    *a2 = variation;
}

void CM2Model::UnlinkFromAnimateList() {
    if (this->m_animatePrev) {
        *this->m_animatePrev = this->m_animateNext;
    }

    if (this->m_animateNext) {
        this->m_animateNext->m_animatePrev = this->m_animatePrev;
    }
}

void CM2Model::UnlinkFromAttachList() {
    if (this->m_attachPrev) {
        *this->m_attachPrev = this->m_attachNext;
    }

    if (this->m_attachNext) {
        this->m_attachNext->m_attachPrev = this->m_attachPrev;
    }
}

void CM2Model::UnlinkFromCallbackList() {
    if (this->m_callbackPrev) {
        *this->m_callbackPrev = this->m_callbackNext;

        if (this->m_callbackNext) {
            this->m_callbackNext->m_callbackPrev = this->m_callbackPrev;
        } else {
            this->m_shared->m_callbackListTail = this->m_callbackPrev;
        }

        this->m_callbackPrev = nullptr;
        this->m_callbackNext = nullptr;
    }
}

void CM2Model::UnlinkFromDrawList() {
    if (this->m_drawPrev) {
        *this->m_drawPrev = this->m_drawNext;
    }

    if (this->m_drawNext) {
        this->m_drawNext->m_drawPrev = this->m_drawPrev;
    }
}

// ref: FUN_00825d70
// Throw away the optimized-geometry block, which is what every rebuild starts with: the merge
// depends on which skin sections are visible, so changing a geoset invalidates the whole thing.
//
// The buffer is released before the pool because the reference releases it first, and the guard
// in front of it is the reference's too: a buffer that is currently the device's STREAM buffer
// for its target must not be destroyed under the device. For this block that test always passes
// -- the builder makes a pool of its own and the stream buffers live in the device's -- but it
// costs nothing and it is what the reference checks.
//
// The explicit buffer release is then redundant here in a way it is not in the reference, since
// frozen's PoolDestroy walks the pool's buffer list itself. Kept because it is what the
// reference does, and harmless: the second pass finds the list already empty.
void CM2Model::UnoptimizeVisibleGeometry() {
    auto optGeo = this->ptr2D0;

    if (!optGeo) {
        return;
    }

    if (optGeo->m_indexBuf) {
        auto buf = optGeo->m_indexBuf;

        if (buf != g_theGxDevicePtr->BufStream(buf->m_pool->m_target, 0, 0)) {
            GxBufDestroy(buf);
        }
    }

    if (optGeo->m_indexPool) {
        GxPoolDestroy(optGeo->m_indexPool);
    }

    SMemFree(optGeo, __FILE__, __LINE__, 0x0);
    this->ptr2D0 = nullptr;
}

// ref: FUN_00832840
// Stop whatever a bone is playing. a4 picks which slot: the primary sequence, or the secondary
// one alone. a3 asks for the stop to be blended rather than instant, which the model's 0x800 flag
// can veto. SetBoneSequence routes here when handed sequence id -1, and the character component
// calls it directly to clear the face and hair bones.
void CM2Model::UnsetBoneSequence(uint32_t boneId, int32_t a3, int32_t a4) {
    // Waiting for load

    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 6;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = a3;
        modelCall->args[2] = a4;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    if (this->m_flag800) {
        a3 = 0;
    }

    auto data = this->m_shared->m_data;

    // Resolve the bone id to an index

    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count() || boneIndex == 0) {
        return;
    }

    if (data->bones[boneIndex].parentIndex == 0xFFFF) {
        return;
    }

    if (!this->NotifySequenceDone(boneId, boneIndex)) {
        return;
    }

    this->CancelDeferredSequences(boneIndex, a4 != 0);

    auto& modelBone = this->m_bones[boneIndex];

    // Secondary slot only: clear it and leave the primary alone.
    if (!a4) {
        modelBone.secondarySequence.uint8 = 0xFFFF;
        modelBone.secondarySequence.float14 = 0.0f;
        modelBone.secondarySequence.uintC = 0;
        modelBone.secondarySequence.float18 = 0.0f;
        modelBone.secondarySequence.uint10 = 0;
        modelBone.secondarySequence.uint1C = 0;

        return;
    }

    // Unlink this bone from the model's animating-bone list

    if (modelBone.dword98) {
        *modelBone.dword98 = modelBone.word96;
    }

    if (modelBone.word96 != 0xFFFF) {
        this->m_bones[modelBone.word96].dword98 = modelBone.dword98;
    }

    modelBone.dword98 = nullptr;
    modelBone.word96 = 0xFFFF;

    if (!a3) {
        modelBone.secondarySequence.uint8 = 0xFFFF;
    } else {
        // Blended stop: the sequence being stopped is promoted into the secondary slot and faded
        // out over 150ms. If a previous fade is still more than half way through, it wins and the
        // promotion is skipped, so a fast stream of stops cannot keep restarting the blend.
        bool promote = true;

        // The same expression AnimateMT stores into floatA8, which is why it is one
        // function now rather than two copies that could drift.
        if (modelBone.secondarySequence.uint8 != 0xFFFF
                && M2BoneBlendWeight(modelBone, this->m_scene->m_time) > 0.5f) {
            promote = false;
        }

        if (promote) {
            modelBone.secondarySequence = modelBone.sequence;
            modelBone.floatA0 = 1.0f / 150.0f;
            modelBone.uint9C = this->m_scene->m_time + 150;
            modelBone.floatA4 = 1.0f;
        }
    }

    modelBone.sequence.float14 = 0.0f;
    modelBone.sequence.uint8 = 0xFFFF;
    modelBone.sequence.float18 = 0.0f;
    modelBone.uint90 = 0xFFFFFFFF;
    modelBone.uint94 = 0;
    modelBone.sequence.uintC = 0;
    modelBone.sequence.uint10 = 0;
    modelBone.sequence.uint1C = 0;
}

void CM2Model::UpdateLoaded() {
    auto model = this;

    while (model) {
        if (!model->IsLoaded(0, !(this->m_flags & 0x20))) {
            break;
        }

        if (model->m_loadedCallback) {
            model->m_loadedCallback(this, model->m_loadedArg);
            model->m_loadedCallback = nullptr;
        }

        model = model->m_attachParent;
    }
}

void CM2Model::WaitForLoad(const char* a2) {
    if (this->m_shared->asyncObject) {
        AsyncFileReadWait(this->m_shared->asyncObject);
    }

    if (this->m_shared->asyncObject) {
        AsyncFileReadWait(this->m_shared->asyncObject);
    }

    if (this->m_flags & 0x20) {
        this->InitializeLoaded();
    }
}

// ref: FUN_00824320
uint32_t CM2Model::GetSharedUint194() {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    return this->m_shared->uint194;
}

// ref: FUN_00825ee0
bool CM2Model::HasSequence(uint32_t sequenceId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    return CM2Model::Sub825E00(this->m_shared->m_data, sequenceId);
}

// ref: FUN_00825f40
// Follow AnimationData.dbc's fallback chain from sequenceId to the first animation this model
// carries. -1 when the chain leaves the table, revisits an id, points at itself, or passes id 505
// (the reference's scratch array is 0x7e8 bytes). Unlike Sub826350 it keeps no play direction.
uint32_t CM2Model::ResolveSequenceFallback(uint32_t sequenceId) {
    if (sequenceId > 0x1F9) {
        return 0xFFFFFFFF;
    }

    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    int32_t visited[506];
    memset(visited, 0, sizeof(visited));

    auto data = this->m_shared->m_data;
    bool found = CM2Model::Sub825E00(data, sequenceId);

    while (!found) {
        if (sequenceId > 0x1F9) {
            return 0xFFFFFFFF;
        }

        auto record = g_animationDataDB.GetRecord(static_cast<int32_t>(sequenceId));

        if (visited[sequenceId]) {
            return 0xFFFFFFFF;
        }

        if (!record) {
            return 0xFFFFFFFF;
        }

        uint32_t next = static_cast<uint32_t>(record->m_fallback);

        if (sequenceId == next) {
            return 0xFFFFFFFF;
        }

        visited[sequenceId] = 1;
        found = CM2Model::Sub825E00(data, next);
        sequenceId = next;
    }

    return sequenceId;
}

// ref: FUN_00826050
// The question CGUnit_C::ResolveAnimation asks of a model: not "do you carry this animation" but
// "does its fallback chain end anywhere you carry". The reference spells the load wait out inline
// here rather than calling through HasSequence, which is why this is its own function.
bool CM2Model::HasSequenceResolved(uint32_t sequenceId) {
    if (sequenceId > 0x1F9) {
        return false;
    }

    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    return this->ResolveSequenceFallback(sequenceId) != 0xFFFFFFFF;
}

// ref: FUN_008261b0
// How many variations `data` carries of one animation: the sequence the id resolves to plus every
// hop along its variationNext chain. 0 when the id is not there.
// ref: FUN_008262f0
int32_t CM2Model::GetSequenceVariationCount(uint32_t sequenceId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    return CM2Model::GetSequenceVariationCount(this->m_shared->m_data, sequenceId);
}

int32_t CM2Model::GetSequenceVariationCount(M2Data* data, uint32_t sequenceId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    uint32_t index = 0xFFFF;
    uint32_t hashCount = data->sequenceIdxHashById.Count();

    if (hashCount == 0) {
        for (uint32_t i = 0; i < data->sequences.Count(); i++) {
            if (data->sequences[i].id == sequenceId) {
                index = i & 0xFFFF;
                break;
            }
        }
    } else {
        uint32_t slot = sequenceId % hashCount;
        uint16_t probe = data->sequenceIdxHashById[slot];

        if (probe != 0xFFFF) {
            int32_t step = 1;

            while (data->sequences[probe].id != sequenceId) {
                slot = (step * step + slot) % hashCount;
                probe = data->sequenceIdxHashById[slot];

                if (probe == 0xFFFF) {
                    break;
                }

                step++;
            }

            if (probe != 0xFFFF) {
                index = probe;
            }
        }
    }

    if (index < data->sequences.Count()) {
        int32_t count = 1;

        for (uint16_t next = data->sequences[index].variationNext; next != 0xFFFF; next = data->sequences[next].variationNext) {
            count++;
        }

        return count;
    }

    return 0;
}

// ref: FUN_008264b0
// Whether the model has bones at all and, for a real id, whether that key bone exists. -1 asks
// only the first question.
int32_t CM2Model::HasBone(uint32_t boneId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;

    if (data->bones.Count()
        && (boneId == 0xFFFFFFFF
            || (boneId < data->boneIndicesById.Count() && data->boneIndicesById[boneId] != 0xFFFF))
    ) {
        return 1;
    }

    return 0;
}

// ref: FUN_008266b0
// A key bone's primary sequence state. -1 names bone 0. Returns 0, leaving the state untouched,
// when the bone does not exist.
int32_t CM2Model::GetBoneSequenceState(uint32_t boneId, M2BoneSequenceState* state) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint32_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return 0;
    }

    auto& bone = this->m_bones[boneIndex];

    state->uint90 = bone.uint90;
    state->uint94 = bone.uint94;

    int32_t elapsed = static_cast<int32_t>(llrint(
        static_cast<float>(static_cast<int32_t>(this->m_scene->m_time - bone.sequence.uintC)) * bone.sequence.float14
    ));

    state->currentTime = elapsed + bone.sequence.uint1C;
    state->speed = bone.sequence.float14;
    state->startTime = bone.sequence.uintC;
    state->endTime = bone.sequence.uint10;
    state->finished = bone.sequence.uintA;

    if (bone.sequence.uint8 != 0xFFFF
        && data->sequences[bone.sequence.uint8].duration <= static_cast<uint32_t>(state->currentTime)
    ) {
        state->pastDuration = 1;

        return 1;
    }

    state->pastDuration = 0;

    return 1;
}

// ref: FUN_008267e0
uint32_t CM2Model::GetBoneUint90(uint32_t boneId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return 0;
    }

    return this->m_bones[boneIndex].uint90;
}

// ref: FUN_00826870
// The animation id a key bone is playing as its primary sequence, and optionally which variation
// of it. -1 when the bone or its sequence does not exist.
uint32_t CM2Model::GetBoneSequenceId(uint32_t boneId, uint32_t* variationIndex) {
    if (variationIndex) {
        *variationIndex = 0;
    }

    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex < data->bones.Count()) {
        uint32_t sequenceIndex = this->m_bones[boneIndex].sequence.uint8;

        if (sequenceIndex < data->sequences.Count()) {
            if (variationIndex) {
                *variationIndex = data->sequences[sequenceIndex].variationIndex;
            }

            return data->sequences[sequenceIndex].id;
        }
    }

    return 0xFFFFFFFF;
}

// ref: FUN_00826930
// The playback speed of a key bone's primary sequence, 1 when the bone does not exist.
float CM2Model::GetBoneSequenceSpeed(uint32_t boneId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return 1.0f;
    }

    return this->m_bones[boneIndex].sequence.float14;
}

// ref: FUN_00826a60
// Whether a key bone has a parent. Bone 0, which -1 also names, never counts.
bool CM2Model::BoneHasParent(uint32_t boneId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint32_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex < data->bones.Count() && boneIndex != 0) {
        return data->bones[boneIndex].parentIndex != 0xFFFF;
    }

    return false;
}

// ref: FUN_00827000
// Change the playback speed of a key bone's primary sequence without a jump: the start time is
// moved so the bone stays at the same point in the sequence, and the end time is recomputed from
// the sequence's duration times its replay count. A speed within 1e-5 of zero (0x009ea558) holds
// the bone where it is. Before the model has loaded the request is queued as model call 8.
void CM2Model::SetBoneSequenceSpeed(uint32_t boneId, float speed) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 8;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        *reinterpret_cast<float*>(&modelCall->args[1]) = speed;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    auto data = this->m_shared->m_data;
    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return;
    }

    auto& sequence = this->m_bones[boneIndex].sequence;

    if (sequence.uint8 == 0xFFFF) {
        return;
    }

    int32_t time = static_cast<int32_t>(this->m_scene->m_time);
    int32_t elapsed = static_cast<int32_t>(llrint(
        static_cast<float>(time - static_cast<int32_t>(sequence.uintC)) * sequence.float14
    ));
    uint32_t length = data->sequences[sequence.uint8].duration * sequence.uint20;

    float inverse;

    if (fabsf(speed) <= 0.00001f) {
        inverse = 0.0f;
    } else {
        inverse = 1.0f / speed;
    }

    elapsed = static_cast<int32_t>(llrint(
        static_cast<float>(elapsed + static_cast<int32_t>(sequence.uint1C)) * fabsf(inverse)
    ));

    int32_t start = time - elapsed;
    sequence.uintC = start;
    sequence.uint10 = static_cast<int32_t>(llrint(static_cast<float>(length) * fabsf(inverse))) + start;
    sequence.float14 = speed;
    sequence.float18 = inverse;
}

// ref: FUN_008265e0
void CM2Model::SetBoneFlags(uint32_t boneId, uint32_t value, uint32_t mask) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 4;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = value;
        modelCall->args[2] = mask;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    auto data = this->m_shared->m_data;
    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return;
    }

    // The masked merge is 16-bit on BOTH sides and the store is 32-bit, so the reference
    // clears the top half of the word every time it is called. Transcribed rather than
    // tidied: the flag bits Animate reads all live in the low half, and widening the merge
    // would preserve bits the reference drops.
    auto& flags = this->m_bones[boneIndex].flags;
    flags = (flags & 0xFFFF & ~mask) | (value & mask & 0xFFFF);
}

// ref: FUN_00827460
// An attachment's authored position, in bone space. The reference does not check the looked-up
// index against the attachment count, so an id the model lacks reads attachments[0xFFFF]: callers
// must only ask for attachments that exist (HasAttachment).
void CM2Model::GetAttachmentPosition(C3Vector* position, uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint16_t index;

    if (id < data->attachmentIndicesById.Count()) {
        index = data->attachmentIndicesById[id];
    } else {
        index = 0xFFFF;
    }

    *position = data->attachments[index].position;
}

// ref: FUN_008275f0
int32_t CM2Model::HasEvent(uint32_t eventId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;

    for (uint32_t i = 0; i < data->events.Count(); i++) {
        if (data->events[i].eventId == eventId) {
            return 1;
        }
    }

    return 0;
}

// ref: FUN_00827670
// An event's position and bone. Unlike its neighbours this does not wait for the model to load.
int32_t CM2Model::GetEvent(uint32_t eventId, C3Vector** position, uint16_t* boneIndex) {
    auto data = this->m_shared->m_data;

    for (uint32_t i = 0; i < data->events.Count(); i++) {
        if (data->events[i].eventId == eventId) {
            *position = &this->m_shared->m_data->events[i].position;
            *boneIndex = this->m_shared->m_data->events[i].boneIndex;

            return 1;
        }
    }

    return 0;
}

// ref: FUN_008278e0
int32_t CM2Model::HasCamera(uint32_t cameraId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;

    if (cameraId < data->cameraIndicesById.Count() && data->cameraIndicesById[cameraId] != 0xFFFF) {
        return 1;
    }

    return 0;
}

// ref: FUN_00827960
HCAMERA CM2Model::GetCameraById(uint32_t cameraId) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;

    if (cameraId < data->cameraIndicesById.Count()) {
        uint16_t index = data->cameraIndicesById[cameraId];

        if (index != 0xFFFF) {
            return this->m_cameras[index].m_camera;
        }
    }

    return nullptr;
}

// ref: FUN_0082c8a0
// Show or hide a run of skin sections, selected BY INDEX.
//
// This and SetGeometryVisible (FUN_0082c7c0, model call 1) are near-twins and the difference is
// the selector, which is the only thing worth knowing about either: that one compares each
// section's authored skinSectionId against the range -- so a caller can say "hide geoset 1301"
// without knowing where it sits -- and this one compares the section's POSITION in the profile.
// Two different questions, and mixing them up would hide the wrong geometry on any model whose
// ids are not its indices, which is most of them.
//
// The no-change test is the reference's, and it is a BOOLEAN comparison: `(*slot == 0) !=
// (visible == 0)`, so any non-zero value counts as shown and writing 2 over 1 is not a change.
// SetGeometryVisible spells its own test out as two equality pairs instead, which differs for
// values outside {0, 1}; left alone rather than harmonised, because that one is tagged against its
// own reference function and this is not the commit to change its behaviour in.
//
// UnoptimizeVisibleGeometry runs ONLY if something actually changed. That is the point of tracking
// it: the optimized-geometry cache is rebuilt from scratch when dropped, so dropping it on a
// no-op call would be pure waste every frame a caller reasserted the same visibility.
void CM2Model::SetGeometryVisibleByIndex(uint32_t first, uint32_t last, int32_t visible) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 2;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = first;
        modelCall->args[1] = last;
        modelCall->args[2] = static_cast<uint32_t>(visible);

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    auto count = static_cast<uint32_t>(this->m_shared->skinProfile->skinSections.Count());

    bool visibilityChanged = false;

    for (uint32_t i = 0; i < count; i++) {
        if (i < first || i > last) {
            continue;
        }

        uint32_t* modelSkinSection = &this->m_skinSections[i];

        if ((*modelSkinSection == 0) == (visible == 0)) {
            continue;
        }

        *modelSkinSection = static_cast<uint32_t>(visible);
        visibilityChanged = true;
    }

    if (visibilityChanged) {
        this->UnoptimizeVisibleGeometry();
    }
}
// ref: FUN_00827190
// Preload an animation: resolve the id, then walk its VARIATION CHAIN asking the shared data for
// each variation whose keyframes live outside the .m2.
//
// Three things about it are worth stating because none is obvious from the shape:
//
// The id is resolved through Sub826350 FIRST, so a model that does not carry the animation asked
// for still preloads whatever AnimationData.dbc's fallback chain lands on rather than nothing. The
// reference passes a raw uint32 where that function wants an M2SequenceFallback, which works
// because the two uint16s it writes are exactly four bytes -- it is reusing the argument slot as
// the output. frozen uses the struct.
//
// `flags & 0x30` clear is the test for "this variation's data is EXTERNAL". A sequence carrying
// its keyframes inside the .m2 needs no load, so those are skipped rather than requested and
// ignored.
//
// The loop condition is the chain terminator: variationNext holds an index, and a value at or past
// sequences.Count() ends the walk. There is no separate sentinel.
void CM2Model::LoadSequence(uint32_t sequenceId) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 14;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = sequenceId;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    M2SequenceFallback fallback = {};

    this->Sub826350(fallback, sequenceId);

    M2Data* data = this->m_shared->m_data;

    // The reference inlines the id-to-index lookup -- the hash probe, or a linear scan when the
    // model carries no hash. Sub8260C0 with a hop count of ZERO is that same lookup, so this calls
    // it rather than spelling the probe out a second time.
    uint32_t index = CM2Model::Sub8260C0(data, fallback.uint0, 0);

    while (index < static_cast<uint32_t>(data->sequences.Count())) {
        M2Sequence& sequence = data->sequences[index];

        if (!(sequence.flags & 0x30)) {
            this->m_shared->LoadSequence(static_cast<uint16_t>(index));
        }

        index = sequence.variationNext;
    }
}
// ref: FUN_008240f0
// The per-light enable, which is M2ModelLight::uint64 -- born 1, and read in three places in this
// file alongside the light's animated visibilityTrack: a light draws only when BOTH are set. So
// this is the static half of a light's on/off and the track is the animated half.
//
// The branches are the other way round from SetParticleEmission and SetRibbonFlag8: the reference
// tests LOADED first here and queues in the else. Same two outcomes, and transcribed in the
// reference's order rather than normalised to match its siblings.
void CM2Model::SetLightEnabled(uint32_t lightIndex, int32_t enable) {
    if (this->m_loaded) {
        // FROZEN-ONLY GUARD. The reference indexes m_lights with no check at all; frozen allocates
        // that array inside a lights.Count() branch in InitializeLoaded, so a model with no lights
        // has none to index. Every reader in this file bounds the same way.
        if (!this->m_lights
                || lightIndex >= static_cast<uint32_t>(this->m_shared->m_data->lights.Count())) {
            return;
        }

        this->m_lights[lightIndex].uint64 = static_cast<uint32_t>(enable);

        return;
    }

    auto modelCall = STORM_NEW(CM2ModelCall);

    modelCall->type = 10;
    modelCall->modelCallNext = nullptr;
    modelCall->time = this->m_scene->m_time;
    modelCall->args[0] = lightIndex;
    modelCall->args[1] = static_cast<uint32_t>(enable);

    *this->m_modelCallTail = modelCall;
    this->m_modelCallTail = &modelCall->modelCallNext;
}
// ref: FUN_00826ed0
// Back-date a bone's sequence so that `elapsed` of it has already played -- a seek, expressed by
// moving the START time earlier rather than by keeping a cursor.
//
// The bone id is a LOOKUP id, not an index: it goes through m_data->boneIndicesById, with two
// special cases the reference spells out -- 0xFFFFFFFF means bone 0, and an id past the end of the
// lookup yields 0xFFFF, which then fails the bones.Count() test and does nothing.
//
// The end time is recomputed rather than shifted: the sequence's authored duration times its repeat
// count at sequence.uint20, scaled by the same |speed| the start offset used. Both scalings take
// the ABSOLUTE value, so a negative speed -- a sequence playing backwards -- still produces a
// forward-running window.
void CM2Model::SetBoneSequenceTime(uint32_t boneId, int32_t elapsed) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 7;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = boneId;
        modelCall->args[1] = static_cast<uint32_t>(elapsed);

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    M2Data* data = this->m_shared->m_data;

    uint16_t boneIndex;

    if (boneId == 0xFFFFFFFF) {
        boneIndex = 0;
    } else if (boneId < data->boneIndicesById.Count()) {
        boneIndex = data->boneIndicesById[boneId];
    } else {
        boneIndex = 0xFFFF;
    }

    if (boneIndex >= data->bones.Count()) {
        return;
    }

    M2ModelBone& bone = this->m_bones[boneIndex];

    // No sequence on this bone, nothing to seek. 0xFFFF is the unset value the bone is born with.
    if (bone.sequence.uint8 == 0xFFFF) {
        return;
    }

    float speed = std::fabs(bone.sequence.float18);

    auto offset = static_cast<int32_t>(static_cast<float>(elapsed) * speed);
    int32_t start = static_cast<int32_t>(this->m_scene->m_time) - offset;

    bone.sequence.uintC = static_cast<uint32_t>(start);

    // The authored duration times the repeat count. Widened through float the way the reference
    // does, which is where its unsigned-to-float fixup comes from; the product is small enough here
    // that the fixup never fires.
    uint32_t span = data->sequences[bone.sequence.uint8].duration * bone.sequence.uint20;

    auto total = static_cast<int32_t>(static_cast<float>(span) * speed);

    bone.sequence.uint10 = static_cast<uint32_t>(total + start);
}
// ref: FUN_00824230
// The ribbon twin of SetParticleEmission, and the ENQUEUE side of model call type 13 -- the case
// whose handler in the dispatch walks m_ribbonEmitters calling SetFlag8. Both halves exist now.
//
// A model that has not finished loading has no emitters to set the flag on, so the request is
// recorded as a model call and replayed by InitializeLoaded's dispatch once they exist. That is the
// whole reason the model-call list exists.
//
// The reference stores 0xFFFFFFFF into the type field and then immediately overwrites it with 13.
// That is a constructor's default being assigned over, not two meaningful writes; STORM_NEW plus
// the assignment below is the same thing.
void CM2Model::SetRibbonFlag8(int32_t enable) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 13;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = enable;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    // Reaching here means InitializeLoaded has run, so a non-zero ribbon count implies the emitter
    // array exists. The guard costs nothing and matches the dispatch's own.
    if (!this->m_ribbonEmitters) {
        return;
    }

    uint32_t count = this->m_shared->m_data->ribbons.Count();

    for (uint32_t i = 0; i < count; i++) {
        this->m_ribbonEmitters[i]->SetFlag8(enable);
    }
}

// ref: FUN_008279f0
// Raise or clear bit 0x2 of every emitter's flags, the half of the (flags & 3) == 3 test the step
// emits on. Before the model has loaded the request is queued as model call 12.
//
// DIVERGENCE: the reference touches every emitter unconditionally; frozen leaves a null where it
// has no emitter class for the model's type (see InitializeLoaded), so those are skipped.
void CM2Model::SetParticleEmission(int32_t enable) {
    if (!this->m_loaded) {
        auto modelCall = STORM_NEW(CM2ModelCall);

        modelCall->type = 12;
        modelCall->modelCallNext = nullptr;
        modelCall->time = this->m_scene->m_time;
        modelCall->args[0] = enable;

        *this->m_modelCallTail = modelCall;
        this->m_modelCallTail = &modelCall->modelCallNext;

        return;
    }

    uint32_t count = this->m_shared->m_data->particles.Count();

    for (uint32_t i = 0; i < count; i++) {
        auto emitter = this->m_particleEmitters[i];

        if (!emitter) {
            continue;
        }

        if (enable) {
            emitter->m_flags |= 0x2;
        } else {
            emitter->m_flags &= ~0x2u;
        }
    }
}

// ref: FUN_00831330
// An attachment's position in world space: its authored position through its bone's current
// matrix, then out of camera space. Animates the model first. Like GetAttachmentPosition, the
// reference does not guard an id the model lacks.
C3Vector CM2Model::GetAttachmentWorldPosition(uint32_t id) {
    if (!this->m_loaded) {
        this->WaitForLoad(nullptr);
    }

    auto data = this->m_shared->m_data;
    uint32_t boneIndex = 0xFFFF;
    uint16_t index;

    if (id < data->attachmentIndicesById.Count()) {
        index = data->attachmentIndicesById[id];
    } else {
        index = 0xFFFF;
    }

    if (index < data->attachments.Count()) {
        boneIndex = data->attachments[index].boneIndex;
    }

    this->Animate();

    return (data->attachments[index].position * this->m_boneMatrices[boneIndex & 0xFFFF]) * this->m_scene->m_viewInv;
}

// ref: FUN_00824a80
// Rebuild the three rows of a 4-float-stride basis around `axis`: the second row becomes
// row0 x axis, normalised unless it is degenerate (squared length at or below 2^-22, the constant
// at 0x009ea27c), the first becomes row1 x axis, and the third is the axis itself.
void M2BuildBasisFromAxis(float* basis, const float* axis) {
    float z = axis[2];
    float x0 = axis[0];
    float x1 = axis[0];
    float y = axis[1];

    basis[4] = basis[2] * axis[1] - basis[1] * axis[2];
    basis[5] = basis[0] * z - basis[2] * x0;
    basis[6] = basis[1] * x1 - y * basis[0];

    float lengthSq = basis[4] * basis[4] + basis[5] * basis[5] + basis[6] * basis[6];

    if (0.00000023841858f < lengthSq) {
        float scale = 1.0f / sqrtf(lengthSq);

        basis[4] = basis[4] * scale;
        basis[5] = scale * basis[5];
        basis[6] = scale * basis[6];
    }

    x0 = axis[0];
    z = axis[2];
    y = axis[1];
    x1 = axis[0];

    basis[0] = basis[5] * axis[2] - axis[1] * basis[6];
    basis[1] = x0 * basis[6] - basis[4] * z;
    basis[2] = basis[4] * y - x1 * basis[5];

    basis[8] = axis[0];
    basis[9] = axis[1];
    basis[10] = axis[2];
}
