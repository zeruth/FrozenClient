#ifndef MODEL_C_M2_MODEL_HPP
#define MODEL_C_M2_MODEL_HPP

#include "gx/Camera.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Lighting.hpp"
#include "util/GUID.hpp"
#include <cstdint>
#include <tempest/Box.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>

class CAaBox;
class CM2ParticleEmitter;
class CM2Scene;
class CM2Shared;
struct M2Batch;
struct M2Data;
struct M2ModelAttachment;
struct M2Batch;
struct M2SkinSection;
struct M2OptimizedGeometry;
struct M2ModelBone;
struct M2ModelBoneSeq;
struct M2ModelCamera;
struct M2ModelColor;
struct M2ModelLight;
struct M2ModelParticle;
struct M2ModelRibbon;
class CM2Ribbon;
struct M2ModelTextureTransform;
struct M2ModelTextureWeight;
struct M2SequenceFallback;
struct M2TrackBase;
struct CM2SequencePlayBack;

class CM2Model;

// The signature of the bone-blend vertex packers the doodad-batch path dispatches through. The
// model is passed explicitly rather than being a `this`: the reference stores these in a plain
// function pointer table and calls them with the model in the first argument slot.
typedef void (*M2PackBatchVerticesFn)(CM2Model* model, const M2SkinSection* section, void* dst,
                                      uint32_t texCoordSet);

// The packer for a skin section with `boneInfluences` weights per vertex. This is the table the
// reference keeps at 0x00d4118c, whose index is the skin section's boneInfluences field.
M2PackBatchVerticesFn M2GetPackBatchVerticesFn(uint32_t boneInfluences);


// The two callbacks a model's owner may register on it. The sequence-done one fires when a bone's
// animation is replaced or runs out; the anim-event one fires on an authored event key. Both carry
// the owner's GUID rather than a pointer, so a dead owner cannot be called through.
typedef void (*M2SequenceDoneCallback)(CM2Model* model, uint32_t boneId, uint32_t animId,
                                       int32_t a4, int32_t a5, WOWGUID owner);
typedef void (*M2AnimEventCallback)(CM2Model* model, uint32_t boneId, uint32_t eventId,
                                    uint32_t eventData, const C3Vector* position, uint32_t a6,
                                    WOWGUID owner);

struct CM2ModelCall {
    uint32_t type = -1;
    CM2ModelCall* modelCallNext;
    uint32_t time;
    uint32_t args[8];
};

// Everything CM2Model::GetSequenceInfo reports about one animation: how the fallback chain resolved
// the requested id, the sequence header the model would actually play, and that sequence's authored
// bounding box. The blob-shadow pass takes the box out of here as the doodad footprint, so the
// footprint follows the animation rather than being a fixed radius.
struct M2SequenceInfo {
    uint32_t flags;
    uint32_t duration;
    float moveSpeed;
    CAaBox extent;
    C3Vector center;
    float radius;
    uint32_t sequenceId;
    uint32_t playMode;
};

// What CM2Model::GetBoneSequenceState reports about a bone's primary sequence: eight dwords in the
// reference, in this order.
struct M2BoneSequenceState {
    uint32_t uint90;        // the bone's +0x90
    uint32_t uint94;        // the bone's +0x94, zero-extended
    int32_t currentTime;    // how far into the sequence the bone is, in sequence time
    float speed;
    uint32_t startTime;
    uint32_t endTime;
    uint32_t finished;
    uint32_t pastDuration;  // currentTime has reached the sequence's duration
};

// ref: FUN_00823f90 -- see the definition in CM2Model.cpp.
bool M2BatchesCanMerge(const M2Batch& a, const M2Batch& b,
                       const M2SkinSection& sectionA, const M2SkinSection& sectionB);

class CM2Model {
    public:
        // Static variables
        static uint32_t s_loadingSequence;
        static uint8_t* s_sequenceBase;
        static uint32_t s_sequenceBaseSize;
        static uint32_t s_skinProfileBoneCountMax[];

        // Static functions
        static CM2Model* AllocModel(uint32_t* heapId);
        static bool Sub825E00(M2Data* data, uint32_t a2);
        static uint16_t Sub8260C0(M2Data* data, uint32_t sequenceId, int32_t a3);

        // Member variables
        uint32_t m_refCount = 1;

        // THE MODEL HAS TWO FLAG STORAGES AND THEY ARE NOT INTERCHANGEABLE. This one is the
        // CREATION flags, the reference's +0x04: what the caller asked for when the model was made.
        // Bit 0x20 is the streaming bit that IsLoaded consults; bit 0x1 is read by CM2Scene and the
        // simple-model UI.
        //
        // The model's per-frame STATE is the bitfield block below, which is the reference's +0x10 --
        // the word its own code ORs draw bits into. `m_flag8` there is what puts the model on
        // m_scene->m_drawList, so a caller marking a model visible must set THAT, not a bit in the
        // word above. Writing `m_flags |= 0x8` instead is silent: it compiles, it sets a creation
        // flag nobody reads for visibility, and the model simply never draws. That is exactly what
        // kept every terrain doodad off the screen until 2026-09-26.
        uint32_t m_flags = 0;

        CM2Model** m_scenePrev = nullptr;
        CM2Model* m_sceneNext = nullptr;

        // The reference's +0x10 state word, one bit per member, named by bit value.
        uint32_t m_loaded : 1;
        uint32_t m_flag2 : 1;
        uint32_t m_flag4 : 1;
        uint32_t m_flag8 : 1;
        uint32_t m_flag10 : 1;
        uint32_t m_flag20 : 1;
        uint32_t m_flag40 : 1;
        uint32_t m_flag80 : 1;
        uint32_t m_flag100 : 1;
        uint32_t m_flag200 : 1;
        uint32_t m_flag400 : 1;
        uint32_t m_flag800 : 1;
        uint32_t m_flag1000 : 1;
        uint32_t m_flag2000 : 1;
        uint32_t m_flag4000 : 1;
        uint32_t m_flag8000 : 1;
        uint32_t m_flag10000 : 1;
        uint32_t m_flag20000 : 1;
        uint32_t m_flag40000 : 1;
        uint32_t m_flag80000 : 1;
        uint32_t m_flag100000 : 1;
        uint32_t m_flag200000 : 1;
        uint32_t m_flag400000 : 1;
        CM2Model** m_callbackPrev = nullptr;
        CM2Model* m_callbackNext = nullptr;
        void (*m_loadedCallback)(CM2Model*, void*) = nullptr;
        void* m_loadedArg = nullptr;
        CM2Scene* m_scene = nullptr;
        CM2Shared* m_shared = nullptr;
        // ref +0x30: the model this one was created against, held by reference. Initialize takes
        // that reference and ~CM2Model gives it back. Always null so far -- CM2Scene::CreateModel
        // is the only caller and passes nothing -- but the reference keeps it here and releases it
        // first thing in its destructor, so the reference is no longer taken and dropped.
        CM2Model* m_parentModel = nullptr;
        CM2Model* model30 = nullptr;
        CM2ModelCall* m_modelCallList = nullptr;
        CM2ModelCall** m_modelCallTail = nullptr;

        // The value CM2Scene::uint14 had when this model was last animated. AnimateMT stamps it on
        // the way out and Animate() reads it to tell "already done this frame" from "needs work".
        //
        // Born -1, NOT 0, and that is the reference's value (FUN_0082be60 sets +0x3c to
        // 0xffffffff). It matters because 0 is a counter value the scene really takes: a model
        // created before the scene's counter first moved would compare equal and skip its own
        // first animate. -1 is the same sentinel ForceAnimate produces, which is the other half of
        // the evidence that it means "never".
        uint32_t m_animCounter = -1;

        CM2Model** m_animatePrev = nullptr;
        CM2Model* m_animateNext = nullptr;
        CM2Model* m_attachParent = nullptr;
        M2ModelAttachment* m_attachments = nullptr;
        uint32_t m_attachId = -1;
        uint32_t m_attachIndex = 0xFFFF;
        CM2Model* m_attachList = nullptr;
        CM2Model** m_attachPrev = nullptr;
        CM2Model* m_attachNext = nullptr;
        uint32_t m_time = 0;

        // Head of the animating-bone list (see M2ModelBone::word96). 0xFFFF when no bone is playing
        // anything.
        uint16_t m_boneSeqList = 0xFFFF;

        CM2Model** m_drawPrev = nullptr;
        CM2Model* m_drawNext = nullptr;
        uint32_t* m_loops = nullptr;
        // ref +0x78 / +0x80: told when a bone sequence ends. CGUnit_C registers its own handler here
        // so a unit can pick what follows the animation that just finished.
        M2SequenceDoneCallback m_sequenceDoneCallback = nullptr;
        WOWGUID m_sequenceDoneOwner = 0;
        // ref +0x1c4 / +0x1c8: told about the authored animation events (footfalls, weapon swings).
        M2AnimEventCallback m_animEventCallback = nullptr;
        WOWGUID m_animEventOwner = 0;
        // The reference's +0x64: set while an effect owns this model's animation, cleared when that
        // effect goes away (CEffect's teardown and the unit's own reset both write 0 here).
        // CGUnit_C::SetAnimation refuses to touch a model whose animation is owned this way.
        // Nothing in frozen sets it yet, so it reads null and that gate never fires.
        void* m_animationOwner = nullptr;
        uint32_t uint74 = 0;
        float float88 = 0.0f;
        // +0x8c: the scene time this model's emitters were last stepped at. The particle block in
        // Animate subtracts it from the scene's current time to get its own delta -- nothing else
        // in frozen computes one.
        uint32_t uint8c = 0;
        uint32_t uint90 = 0;
        union {
            M2ModelBone* m_bones = nullptr;
            void* m_internalResources;
        };
        C44Matrix* m_boneMatrices = nullptr;
        M2ModelColor* m_colors = nullptr;
        uint32_t* m_skinSections = nullptr;
        HTEXTURE* m_textures = nullptr;
        M2ModelTextureWeight* m_textureWeights = nullptr;
        M2ModelTextureTransform* m_textureTransforms = nullptr;
        C44Matrix* m_textureMatrices = nullptr;
        C44Matrix matrixB4;
        C44Matrix matrixF4;

        // +0x174: the space this model's PARTICLES and RIBBONS are expressed relative to, or null
        // for world space -- which is the normal case, and why CM2ParticleEmitter::Draw takes a
        // nullable matrix. CM2SceneRender::DrawParticle and DrawRibbon both hand it straight to
        // the emitter's Draw.
        //
        // FUN_00824460 is its only setter and is not ported. It walks the attachment tree from
        // this model down (skipping attachments whose index is 0xFFFF) and, on the null ->
        // non-null transition ONLY, transforms the already-live particles through the new
        // matrix's AffineInverse so they do not jump; passing null runs the same transform with
        // the OUTGOING matrix on the way out. Porting it means porting FUN_008243e0 with it.
        // Until then this stays null and every particle draws in world space, which is what the
        // reference does for everything that never calls that setter.
        C44Matrix* m_particleRelative = nullptr;

        // NOT PORTED, and recorded so the gap is visible rather than silently closed: the
        // reference has a THIRD C44Matrix at +0x134, between matrixF4 and the pointer above. The
        // constructor identity-initialises it at 0x82c002..0x82c058 -- the 1.0 stores land on
        // 0x134, 0x148, 0x15c and 0x170, a 0x14 stride, which is a 4x4 diagonal and nothing else.
        // Its readers have not been traced. Frozen's layout therefore diverges here by 0x40
        // bytes, which costs nothing because no offset in frozen is hard-coded.

        // This model's own tint, before anything a parent hands down: the reference keeps these at
        // +0x178 through +0x194 and folds them into the current values every animate. Nothing
        // writes them yet -- the setters that do are not ported -- so they stay neutral, which
        // makes the fold an identity and leaves a parent's tint passing through untouched.
        float m_baseAlpha = 1.0f;
        float m_baseAlphaScale = 1.0f;
        C3Vector m_baseDiffuse = { 1.0f, 1.0f, 1.0f };
        C3Vector m_baseEmissive = { 0.0f, 0.0f, 0.0f };

        float float198 = 1.0f;
        float alpha19C = 1.0f;
        C3Vector m_currentDiffuse = { 1.0f, 1.0f, 1.0f };
        C3Vector m_currentEmissive = { 0.0f, 0.0f, 0.0f };
        M2ModelLight* m_lights;
        CM2Lighting m_lighting;
        CM2Lighting* m_currentLighting = nullptr;
        void (*m_lightingCallback)(CM2Model*, CM2Lighting*, void*) = nullptr;
        void* m_lightingArg = nullptr;
        M2ModelCamera* m_cameras = nullptr;
        // The ribbon pair, reference +0x2b8 and +0x2bc, and it sits BEFORE the particle pair below
        // because that is the order the reference carves them out of the shared buffer. Same shape
        // as that pair one slot along: the animated state parallel to m_shared->m_data->ribbons,
        // then one emitter per M2Ribbon.
        //
        // Unlike the particle emitters, none of these is ever null -- every ribbon gets a
        // CM2Ribbon, because there is only one kind.
        M2ModelRibbon* m_ribbons = nullptr;
        CM2Ribbon** m_ribbonEmitters = nullptr;
        // The animated state of every emitter, parallel to m_shared->m_data->particles. The
        // reference keeps this at +0x2c0 and an array of emitter OBJECTS at +0x2c4; only the state
        // is here so far -- see CM2Model::AnimateParticles.
        M2ModelParticle* m_particles = nullptr;
        // +0x2c4: one emitter per M2Particle, or null where the model asks for an emitter type
        // frozen does not have. Callers MUST tolerate the null -- see the note in
        // InitializeLoaded. The objects themselves are carved out of the same buffer as the array,
        // so neither is owned individually.
        CM2ParticleEmitter** m_particleEmitters = nullptr;
        // The OPTIMIZED VISIBLE GEOMETRY cache, traced 2026-09-23. Nothing in frozen builds
        // it, so it stays null and every path that reads it takes the unoptimized branch; that is
        // the correct resting state rather than a bug, but it is why CM2Model::SetIndices and
        // UnoptimizeVisibleGeometry are stubs.
        //
        // What the reference keeps behind it, read off FUN_00828f90 and FUN_00825d70:
        //
        //     +0x08  array of 0x30-byte group entries, each starting with a range index
        //     +0x0c  group count
        //     +0x10  array of 8-byte {firstBatch, lastBatch} pairs
        //     +0x14  CGxPool*
        //     +0x18  CGxBuf*, the compacted index buffer
        //
        // It is a per-model index buffer holding only the batches whose skin section is visible,
        // rebuilt when section visibility changes. UnoptimizeVisibleGeometry frees the buffer and
        // pool and nulls this, and its SMemFree names ".\M2Model.cpp" line 0xad6, which is how
        // the owning module is known rather than guessed.
        //
        // PORTED 2026-09-26, and verified by a run: the builder is CM2Model::OptimizeVisibleGeometry
        // (FUN_0082c970), the free is UnoptimizeVisibleGeometry (FUN_00825d70), the fill is
        // SetIndices (FUN_00828f90), and CM2Scene selects it at three sites. SetIndices
        // dereferences this WITHOUT a null check, which is safe only because it is reached solely
        // through the callers' own `if (ptr2D0)` guards.
        //
        // A note here used to call FUN_0082be60 a second builder, "the only two functions that
        // store a non-zero value here". That was wrong: FUN_0082be60 is the CM2Model constructor
        // and it sets this slot to ZERO like every other field. FUN_0082c970 is the only builder,
        // so the chain is complete rather than half-done.
        //
        // What had blocked it was not in model at all: frozen could create a Gx pool and buffer
        // and had no way to destroy either, and this block is rebuilt on every visibility change.
        // CGxDevice::PoolDestroy, BufDestroy and the IPoolRelease virtual were added for it.
        //
        // Still owed: the REBASE branch of SetIndices has never run here, because this machine's
        // CM2Cache flags always select the union-of-ranges merge; and nothing has confirmed the
        // result on screen.
        M2OptimizedGeometry* ptr2D0 = nullptr;
        // Membership in the scene's ray query list (CM2Scene::m_rayModelList), reference +0x2d4
        // through +0x2e4. Written by the map's segment query (FUN_007a2760, not ported): the
        // query kind (3 tests the collision box, anything else the current sequence's bounds),
        // the list links, the map object that put the model there, and a key the triangle test
        // compares.
        uint32_t m_rayQueryType = 0;          // +0x2d4
        CM2Model** m_rayPrev = nullptr;       // +0x2d8
        CM2Model* m_rayNext = nullptr;        // +0x2dc
        void* m_rayOwner = nullptr;           // +0x2e0
        uint32_t m_rayKey = 0;                // +0x2e4
        uint32_t m_memHandle;

        // Member functions
        // ref: FUN_0082be60
        CM2Model();
        ~CM2Model();
        void AddRef();
        void Animate();
        // ref: FUN_006f1d20
        // Animate, then matrixF4 carried through the scene's inverse view: this frame's placement
        // of the model in world space.
        C44Matrix AnimateAndGetWorldMatrix();
        void AnimateAttachmentsMT();
        void AnimateCamerasST();
        void AnimateMT(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6);
        void AnimateMTSimple(const C44Matrix* view, const C3Vector& a3, const C3Vector& a4, float a5, float a6);
        void AnimateST();
        // Animate every emitter's tracks into m_particles. Called by the particle system rather
        // than from AnimateST -- see the note at the definition.
        void AnimateParticleTracks();

        // Push this frame's animated values into one emitter, then place and step it.
        // ref: FUN_008309c0
        void AnimateParticleEmitter(float dt, int32_t index);
        void AnimateTextureTransformsMT();
        void AttachToParent(CM2Model* parent, uint32_t id, const C3Vector* position, int32_t a5);
        void AttachToScene(CM2Scene* scene);
        void CancelAllDeferredSequences();
        void CancelDeferredSequences(uint32_t boneIndex, bool a3);
        void DetachAllChildrenById(uint32_t id);
        void DetachFromParent();
        void DetachFromScene();
        void FindKey(M2ModelBoneSeq* sequence, const M2TrackBase& track, uint32_t& currentKey, uint32_t& nextKey, float& ratio);
        void FreeExternalResources();
        void FreeInternalResources();
        C44Matrix GetAttachmentWorldTransform(uint32_t id);
        CAaBox& GetBoundingBox(CAaBox& bounds);
        void GetBoundingSphere(CAaSphere& sphere);
        HCAMERA GetCameraByIndex(uint32_t index);
        C3Vector GetPosition();
        void GetSequenceInfo(uint32_t sequenceId, int32_t variationIndex, M2SequenceInfo& info);
        bool HasAttachment(uint32_t id);
        int32_t Initialize(CM2Scene* scene, CM2Shared* shared, CM2Model* a4, uint32_t flags);
        int32_t InitializeLoaded();
        int32_t IsBatchDoodadCompatible(M2Batch* batch);
        int32_t IsDrawable(int32_t a2, int32_t a3);
        int32_t IsLoaded(int32_t a2, int32_t attachments);
        void LinkToCallbackListTail();
        void OptimizeVisibleGeometry();
        int32_t ProcessCallbacks();
        void ProcessCallbacksRecursive();
        uint32_t Release();
        void ReplaceTexture(uint32_t textureId, HTEXTURE texture);
        void SetAnimating(int32_t animating);
        void SetBoneSequence(uint32_t boneId, uint32_t sequenceId, uint32_t a4, uint32_t time, float a6, int32_t a7, int32_t a8);
        void SetBoneSequenceDeferred(uint16_t a2, M2Data* data, uint16_t boneIndex, uint32_t time, float a6, M2SequenceFallback fallback, int32_t a8, int32_t a9, int32_t a10);
        int32_t ApplySequencePlayBack(uint16_t sequenceIndex, CM2SequencePlayBack* playback);
        void SetGeometryVisible(uint32_t start, uint32_t end, int32_t visible);
        int32_t SetIndices();
        void SetLightingCallback(void (*lightingCallback)(CM2Model*, CM2Lighting*, void*), void* lightingArg);
        void SetLoadedCallback(void (*loadedCallback)(CM2Model*, void*), void* loadedArg);
        void SetPrimaryBoneSequence(uint16_t sequenceIndex, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6, int32_t a7);
        void SetSecondaryBoneSequence(uint16_t a2, uint16_t boneIndex, M2SequenceFallback fallback, uint32_t time, float a6);
        void SetupBoneSequence(uint16_t sequenceIndex, M2SequenceFallback fallback, uint32_t a4, float a5, M2ModelBoneSeq* boneSequence);
        void SetupLighting();
        void SetVisible(int32_t visible);
        void SetWorldTransform(const C3Vector& position, float orientation, float scale);
        void SequenceFinished(uint16_t boneIndex, uint32_t overshoot, uint16_t seqIndexWas, uint32_t startTimeWas);
        void Sub826350(M2SequenceFallback& fallback, uint32_t sequenceId);
        // ref: FUN_008269c0
        // Tell the owner that the sequence on `boneIndex` has ended, and say whether the model
        // survived the call (it may release itself from inside the handler). Every path that
        // replaces or clears a bone's sequence goes through this first.
        int32_t NotifySequenceDone(uint32_t boneId, uint16_t boneIndex);

        // ref: FUN_00823fe0
        void SetSequenceDoneCallback(M2SequenceDoneCallback callback, WOWGUID owner);
        // ref: FUN_00824060
        void SetAnimEventCallback(M2AnimEventCallback callback, WOWGUID owner);
        void Sub826E60(uint32_t* a2, uint32_t* a3);
        void UnlinkFromAnimateList();
        void UnlinkFromAttachList();
        void UnlinkFromCallbackList();
        void UnlinkFromDrawList();
        void UnoptimizeVisibleGeometry();
        void UnsetBoneSequence(uint32_t boneId, int32_t a3, int32_t a4);
        void UpdateLoaded();
        void WaitForLoad(const char* a2);

        // Queries and setters by id. Most wait for the model to load first, as the reference does
        // inline; the setters queue a model call instead while it is still loading.
        // ref: FUN_00824320
        uint32_t GetSharedUint194();
        // ref: FUN_00825ee0
        bool HasSequence(uint32_t sequenceId);
        // ref: FUN_00825f40
        uint32_t ResolveSequenceFallback(uint32_t sequenceId);
        // ref: FUN_00826050
        // HasSequence through the fallback chain: whether the model carries anything the chain from
        // sequenceId reaches. HasSequence itself only answers for the id as asked.
        bool HasSequenceResolved(uint32_t sequenceId);
        // ref: FUN_008261b0
        int32_t GetSequenceVariationCount(M2Data* data, uint32_t sequenceId);

        // ref: FUN_008262f0
        // How many variations this model's data offers for an animation. The same question as the
        // static overload above, asked of a model rather than of raw data, so it waits for the
        // load first -- the answer comes out of m_data, which does not exist until then.
        int32_t GetSequenceVariationCount(uint32_t sequenceId);
        // ref: FUN_008264b0
        int32_t HasBone(uint32_t boneId);
        // ref: FUN_008266b0
        int32_t GetBoneSequenceState(uint32_t boneId, M2BoneSequenceState* state);
        // ref: FUN_008267e0
        uint32_t GetBoneUint90(uint32_t boneId);
        // ref: FUN_00826870
        uint32_t GetBoneSequenceId(uint32_t boneId, uint32_t* variationIndex);
        // ref: FUN_00826930
        float GetBoneSequenceSpeed(uint32_t boneId);
        // ref: FUN_00826a60
        bool BoneHasParent(uint32_t boneId);
        // ref: FUN_00827000
        void SetBoneSequenceSpeed(uint32_t boneId, float speed);

        // ref: FUN_008265e0
        // Force bits of a key bone's runtime flag word. Animate ORs this word into the
        // AUTHORED bone flags, so this is how an owner turns on billboarding or the
        // ignore-parent-transform behaviour for a bone the artist did not mark. Only the bits in
        // `mask` move; the rest keep what they had. Queued as model call 4 before the load.
        void SetBoneFlags(uint32_t boneId, uint32_t value, uint32_t mask);
        // ref: FUN_00827460
        void GetAttachmentPosition(C3Vector* position, uint32_t id);
        // ref: FUN_008275f0
        int32_t HasEvent(uint32_t eventId);
        // ref: FUN_00827670
        int32_t GetEvent(uint32_t eventId, C3Vector** position, uint16_t* boneIndex);

        // ref: FUN_00830f90
        // Step this model's animation again even if it has already been stepped this frame, for
        // a caller that has just changed something the current bone matrices were built from.
        void ForceAnimate();

        // ref: FUN_00827780
        // WHEN an animation event fires, in sequence time: the first timestamp authored for
        // `eventId` inside the animation `animId`. Zero when the model has neither, which is the
        // same answer as "at the very start" -- the reference does not distinguish them either,
        // so a caller that cares has to ask whether the event exists first.
        uint32_t GetEventTimestamp(uint32_t animId, uint32_t eventId);

        // ref: FUN_008317e0
        // Where an authored animation event is RIGHT NOW, in world space: its local position
        // carried through the bone it hangs off and then out of view space. The origin when the
        // model has no such event. Animates first, because the bone matrix has to be current --
        // this is what tells a footfall or a weapon-swing event where to put its effect.
        C3Vector& GetEventWorldPosition(C3Vector& out, uint32_t eventId);
        // ref: FUN_008278e0
        int32_t HasCamera(uint32_t cameraId);
        // ref: FUN_00827960
        HCAMERA GetCameraById(uint32_t cameraId);
        // ref: FUN_008279f0
        void SetParticleEmission(int32_t enable);
        // ref: FUN_00831330
        C3Vector GetAttachmentWorldPosition(uint32_t id);
};

// ref: FUN_00824a80
void M2BuildBasisFromAxis(float* basis, const float* axis);

#endif
