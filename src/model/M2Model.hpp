#ifndef MODEL_M2_MODEL_HPP
#define MODEL_M2_MODEL_HPP

#include "gx/Camera.hpp"
#include "model/CM2Light.hpp"
#include <cstdint>
#include <tempest/Quaternion.hpp>
#include <tempest/Vector.hpp>

template<class T>
class M2Track;

class CM2Model;
class CGxPool;
class CGxBuf;
class CShaderEffect;
struct M2Batch;
struct M2SkinSection;

template<class T>
struct M2ModelTrack {
    uint32_t currentKey = 0;
    // The current key for the SECONDARY sequence, which is the state the animation blend
    // walks. This slot held an `M2Track<T>* sourceTrack` until 2026-09-24 that nothing in
    // the tree ever wrote or read -- its declaration was the only hit. The reference uses
    // it as a second key index in both halves of M2AnimateTrack (FUN_00828680 and
    // FUN_0082b0a0), so the field was not merely unused: it was standing where something
    // real belongs. Nothing advances it yet -- see the TODO at the end of M2AnimateTrack.
    uint32_t currentKey2 = 0;
    T currentValue;
};

struct M2ModelAttachment {
    M2ModelTrack<uint8_t> visibilityTrack;
};

// How far a bone's blend from its secondary sequence to its primary one has run, in 0..1,
// scaled by the bone's own ceiling. Shared by CM2Model::AnimateMT, which stores it, and
// StopBoneSequence, which tests it to decide whether a fade is far enough along to be
// worth restarting. ref: inline in both
float M2BoneBlendWeight(const struct M2ModelBone& modelBone, uint32_t sceneTime);

struct M2ModelBoneSeq {
    uint32_t uint0 = 0;
    uint16_t uint4 = -1;
    uint16_t uint6 = -1;
    uint16_t uint8 = -1;
    uint8_t uintA = 1;
    uint8_t uintB = 1;
    uint32_t uintC = 0;
    uint32_t uint10 = 0;
    float float14 = 0.0;
    float float18 = 0.0;
    uint32_t uint1C = 0;
    uint32_t uint20 = 0;
};

struct M2ModelBone {
    M2ModelBone();

    M2ModelTrack<C3Vector> translationTrack;
    M2ModelTrack<C4Quaternion> rotationTrack;
    M2ModelTrack<C3Vector> scaleTrack;
    M2ModelBoneSeq sequence;
    M2ModelBoneSeq secondarySequence;
    uint32_t flags = 0;
    uint32_t uint90 = -1;
    uint16_t uint94 = 0;

    // Intrusive list of the bones that currently carry a sequence, threaded through the model's
    // m_boneSeqList head. CM2Model::ProcessCallbacks walks it once a frame to notice a sequence
    // that has just ended, so it does not have to scan every bone of every model.
    uint16_t word96 = 0xFFFF;
    uint16_t* dword98 = nullptr;

    uint32_t uint9C = 0;
    float floatA0 = 0.0f;
    float floatA4 = 1.0f;
    float floatA8 = 0.0f;
};

// The optimized-geometry block, the reference's model +0x2d0. One allocation holding five
// arrays: a model whose visible skin sections have just been chosen gets its adjacent
// mergeable batches collapsed into fewer, larger ones so a character draws in a handful of
// calls instead of one per geoset.
//
// THE LAYOUT IS PROVEN, three independent ways rather than inferred:
//
//  1. the builder's own allocation size. FUN_0082c970 asks SMemAlloc for
//     `(batchCount + sectionCount * 2) * 0x1c + 0x20`, and the five arrays it then carves out
//     need `0x20 + batchCount * (0x18 + 4) + sectionCount * (0x30 + 8)`. Those are the same
//     expression -- 0x1c is sizeof(M2Batch) + sizeof(void*) and 0x38 is sizeof(M2SkinSection)
//     plus the 8-byte range pair. An array placed wrongly would not balance.
//  2. the free, FUN_00825d70, which releases +0x18 as a buffer and +0x14 as a pool and then
//     SMemFrees the block whole, naming ".\M2Model.cpp" line 0xad6.
//  3. the consumers. CM2Scene reads batchCount at +0x04; the batch comparator FUN_00824b70
//     indexes +0x00 by 0x18 and takes the uint16 at batch+4 (skinSectionIndex) to index +0x08
//     by 0x30 -- exactly the strides of M2Batch and M2SkinSection.
struct M2OptimizedGeometry {
    // The merged batches. Each is a copy of the first source batch of its run, with
    // skinSectionIndex repointed at the merged section below.
    M2Batch* batches;
    uint32_t batchCount;
    // The merged sections, one per merged batch. Built by copying the first source section
    // and then folding the rest of the run into it: index counts add, bone counts and
    // influences take the maximum, and the vertex range either adds or takes the UNION of the
    // two ranges depending on the cache flag the builder latches.
    M2SkinSection* skinSections;
    uint32_t skinSectionCount;
    // Per merged section, the first and last index into the ORIGINAL skin profile's batch
    // array that the merge covered. This is the span SetIndices walks to gather the indices
    // that go in the buffer below.
    uint32_t (*sourceBatchRange)[2];
    // +0x14 and +0x18: the index pool and the buffer in it, sized to the total index count of
    // every visible batch. GxPoolCreate(GxPoolTarget_Index, GxPoolUsage_Static, count * 2,
    // GxPoolHintBit_Unk0, shared->ext) then GxBufCreate(pool, 2, count, 0).
    CGxPool* m_indexPool;
    CGxBuf* m_indexBuf;
    // One shader effect per merged batch, from CM2Shared::GetEffect.
    CShaderEffect** effects;
};

struct M2ModelCamera {
    M2ModelTrack<C3Vector> positionTrack;
    M2ModelTrack<C3Vector> targetTrack;
    M2ModelTrack<float> rollTrack;
    HCAMERA m_camera = nullptr;
};

// One emitter's animated state, the runtime half of an M2Particle. 0x88 bytes in the reference:
// ten 12-byte tracks from +0x08, then the four flags the driver latches.
//
// The tracks pair one-for-one with M2Particle's ten M2Track<float> members in file order, which is
// how the reference walks them. frozen's M2ModelTrack holds the same three fields in a different
// order and widens with 64-bit pointers, so this is structurally the reference's block rather than
// byte-identical to it -- the same divergence every other M2Model* runtime struct here carries.
struct M2ModelParticle {
    M2ModelTrack<float> speedTrack;
    M2ModelTrack<float> variationTrack;
    M2ModelTrack<float> latitudeTrack;
    M2ModelTrack<float> longitudeTrack;
    M2ModelTrack<float> gravityTrack;
    M2ModelTrack<float> lifeTrack;
    M2ModelTrack<float> emissionRateTrack;
    M2ModelTrack<float> widthTrack;
    M2ModelTrack<float> lengthTrack;
    M2ModelTrack<float> zsourceTrack;

    // +0x80: the emitter ran this frame. The driver only refreshes the tracks above when this is
    // set or the model has never animated, so a culled emitter keeps its last values.
    uint8_t enabled = 0;
    // +0x84: the emission rate is live. Clear means the driver hands the emitter a rate of zero
    // rather than the animated one.
    uint8_t rateActive = 0;
    // +0x85: the emitter is placed and stepped this frame.
    uint8_t active = 0;
    // +0x86: burst latch, for emitters with M2Particle flag 0x8000. The driver raises the
    // emitter's own 0x40 bit on the frame this goes from clear to set, so a burst fires once
    // rather than every frame it stays enabled.
    uint8_t burstLatch = 0;
};

struct M2ModelColor {
    M2ModelTrack<C3Vector> colorTrack;
    M2ModelTrack<float> alphaTrack;
};

struct M2ModelLight {
    M2ModelLight();

    M2ModelTrack<C3Vector> ambientColorTrack;
    M2ModelTrack<float> ambientIntensityTrack;
    M2ModelTrack<C3Vector> diffuseColorTrack;
    M2ModelTrack<float> diffuseIntensityTrack;
    M2ModelTrack<uint8_t> visibilityTrack;
    uint32_t uint64 = 1;
    CM2Light light;
};

// One vertex as the doodad-batch path streams it: 32 bytes, which is the stride
// CM2SceneRender::DrawBatchDoodad asks BufStream for (0x20). The skinning is already applied,
// so there are no weights or indices here -- position and normal arrive in model space with the
// bone blend folded in, and one of the two authored texture coordinate sets is copied through
// verbatim.
//
// The reference has no struct for this; it writes the eight floats through a raw pointer. Named
// here because two functions write it and getting the order wrong is silent.
struct M2BatchDoodadVertex {
    C3Vector position;
    C3Vector normal;
    C2Vector texcoord;
};
struct M2ModelTextureTransform {
    M2ModelTrack<C3Vector> translationTrack;
    M2ModelTrack<C4Quaternion> rotationTrack;
    M2ModelTrack<C3Vector> scaleTrack;
};

struct M2ModelTextureWeight {
    M2ModelTrack<float> weightTrack;
};

#endif
