#ifndef MODEL_C_M2_SCENE_HPP
#define MODEL_C_M2_SCENE_HPP

#include "model/M2Model.hpp"
#include "model/M2Types.hpp"
#include <cstdint>
#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>

class CM2Cache;
class CM2Light;
class CM2Lighting;
class CM2Model;
struct M2SkinProfile;
struct M2SkinSection;
struct ubyte4;

// One model a ray query's bounding-sphere pass kept (reference: 16 bytes in the array at scene
// + 0x118): the model, the entry and exit distances along the ray clamped to [0, length], and the
// model's +0x2e4 key.
struct M2SceneRayCandidate {
    CM2Model* model;
    float tNear;
    float tFar;
    uint32_t key;
};

class CM2Scene {
    public:
        // Static variables
        static uint32_t s_optFlags;

        // Static functions
        static void AnimateThread(void* arg);
        static void ComputeElementShaders(M2Element* element);
        static int32_t SortOpaque(uint32_t a, uint32_t b, const void* userArg);
        static int32_t SortOpaqueGeoBatches(M2Element* elementA, M2Element* elementB);
        static int32_t SortOpaqueParticles(M2Element* elementA, M2Element* elementB);
        static int32_t SortOpaqueRibbons(M2Element* elementA, M2Element* elementB);
        // Number one list's additive runs, then sort it. ref: FUN_0081f9e0
        void KeyAndSortElementList(uint32_t listIndex);
        static int32_t SortTransparent(uint32_t a, uint32_t b, const void* userArg);
        // Order the ray candidates near to far. ref: FUN_0081cbc0
        static int32_t SortRayCandidates(uint32_t a, uint32_t b, const void* userArg);
        // The same, with the additive run number as its first key. ref: FUN_0081f0e0
        static int32_t SortTransparentGrouped(uint32_t a, uint32_t b, const void* userArg);

        // Member variables
        CM2Cache* m_cache;
        CM2Model* m_modelList = nullptr;
        uint32_t m_time = 0;
        uint32_t uint10;
        uint32_t uint14 = 0;
        uint32_t m_flags = 0;
        CM2Light* m_lightList = nullptr;
        // Point lights do not go on m_lightList. They go here, into a 64 x 64 hash grid of cells
        // twenty world units across, indexed `(y & 0x3f) << 6 | (x & 0x3f)` so the world wraps
        // every 1280 units. CM2Light::Link files them, CM2Light::SetPosition re-files them and
        // CM2Scene::SelectLights sweeps the cells a model's bounding sphere covers.
        //
        // DIVERGENCE: the reference allocates this lazily with SMemAlloc on first use (0x4000
        // bytes at scene + 0x24) and frozen embeds it. Same 4096 pointers either way; embedding
        // avoids introducing an allocation with no matching free, since CM2Scene has no
        // destructor. It costs 16K (32K on 64-bit) per scene and there is one construction site.
        CM2Light* m_lightGrid[4096] = {};
        CM2Model* m_animateList = nullptr;
        CM2Model* m_drawList = nullptr;
        TSGrowableArray<M2Element> m_elements;
        TSGrowableArray<uint32_t> array44;
        TSGrowableArray<uint32_t> array54[3];
        C44Matrix m_view;
        C44Matrix m_viewInv;
        // +0x104 and +0x108: the projected-decal callback and its context, NOT a number --
        // which is what the type-1 element test below reads. Installed by
        // SetProjectionCallback; the reference's one caller is world init at 0x781340, passing
        // FUN_0077f500.
        //
        // That callback is the blob shadow supplier: it reads the float at 0x009f98d8, the 0.4
        // shadow strength parity-shadows.md records as solved, and passes it on with the flags
        // 0x200122 the same doc names for the ground marker. So this pointer being null is
        // exactly why CM2SceneRender::DrawBatchProj is unreachable.
        //
        // Typed void* rather than guessed at: the callback takes at least five cdecl arguments
        // (it reads 0x8, 0xc, 0x10 and 0x18 and cleans up 0x14 bytes), and the meanings are not
        // established.
        void* m_projectionCallback = nullptr;
        void* m_projectionContext = nullptr;

        // The ray query state, reference +0x114 through +0x124. The model list is threaded through
        // CM2Model::m_rayPrev / m_rayNext; its filler (FUN_007a2760, from the map's segment query)
        // is not ported, so it stays empty.
        CM2Model* m_rayModelList = nullptr;                  // +0x114
        M2SceneRayCandidate* m_rayCandidates = nullptr;      // +0x118
        uint32_t* m_rayCandidateOrder = nullptr;             // +0x11c
        uint32_t m_rayCandidateCapacity = 0;                 // +0x120
        // Section vertices projected onto the query plane: in-plane x, y and the height above it.
        C3Vector* m_rayProjected = nullptr;                  // +0x124
        // +0x128. How many m_rayProjected holds. It only grows, doubling from one, and the old
        // contents are not kept -- the same policy as m_rayCandidateCapacity above.
        uint32_t m_rayProjectedCapacity = 0;                 // +0x128

        // +0x144: which draw passes this scene will run, one bit per M2PASS. Draw tests
        // `m_passMask & (1 << pass)` and does nothing when the bit is clear.
        //
        // It is the LAST field of the object: M2CreateScene allocates exactly 0x148 bytes.
        //
        // Born zero here, which is MORE defined than the reference, whose constructor never
        // writes this field at all -- see the long note at CM2Scene::Draw for why that makes the
        // reference's own gate unportable and why frozen does not have it.
        uint32_t m_passMask = 0;

        // Static functions
        // ref: FUN_0081d2c0
        static void BlendBoneMatrices(const C44Matrix* bones, ubyte4 weights, ubyte4 indices, C44Matrix* out);
        // ref: FUN_0081d3d0
        static void BlendBoneMatrices3x4(const C44Matrix* bones, ubyte4 weights, ubyte4 indices, C44Matrix* out);

        // Member functions
        // ref: FUN_0081cac0
        void BeginRayQuery();
        // ref: FUN_0081cad0
        void ReserveRayCandidates();
        // ref: FUN_0081cf20
        int32_t RaySetup(const C3Vector& start, const C3Vector& end, float t, float* length, C3Vector* dir);
        // ref: FUN_0081d510
        // Project one model's COLLISION mesh onto the query plane and test its triangles.
        // ref: FUN_0081dd50
        M2SceneRayCandidate* RayTestModel(CM2Model* model, int32_t preferOther,
                                          const C3Vector& planeNormal, float planeDist,
                                          const C2Vector& point, M2SceneRayCandidate* candidate,
                                          float* bestHeight, M2SceneRayCandidate* best);
        M2SceneRayCandidate* RayTestTriangles(const uint16_t* indices, const uint16_t* indicesEnd, uint32_t vertexBase, const C2Vector& point, int32_t preferOther, M2SceneRayCandidate* candidate, float* bestHeight, M2SceneRayCandidate* best);
        // ref: FUN_0081d9c0
        void ProjectSectionVertices(CM2Model* model, M2SkinProfile* skinProfile, M2SkinSection* section, int32_t addNormal, const C3Vector& planeNormal, float planeDist);

        // Install the projected-decal callback. Until something calls this, the scene emits no
        // type-1 elements and DrawBatchProj cannot be reached. ref: FUN_0081cc30
        void SetProjectionCallback(void* callback, void* context);

        // One axis of a point light's hash-grid index. The reference scales by the 0.05 at
        // 0x00af59d4 -- one twentieth, so twenty world units to a cell -- truncates toward zero
        // and masks to six bits. A negative coordinate masks the same way on x86 and in C++, so
        // the world wraps rather than clamping.
        static int32_t LightGridAxis(float v) {
            return static_cast<int32_t>(v * 0.05f) & 0x3f;
        }

        CM2Scene(CM2Cache* cache)
            : m_cache(cache)
            {};
        void AdvanceTime(uint32_t a2);
        void Animate(const C3Vector& cameraPos);

        // Register one emitter's particles as a draw element, and file it in the right pass.
        // `elementIndex` is the running element count -- the pass lists hold indices into
        // m_elements, so it is read AND incremented here. ref: FUN_00821930
        void AddParticleElement(CM2ParticleEmitter* emitter, CM2Model* model, float distance,
                                float alpha, int32_t aboveLiquid, int32_t& elementIndex,
                                uint32_t& additiveCount);
        CM2Model* CreateModel(const char* file, uint32_t a3);

        // Create a model AGAINST an existing one: it shares `source`'s CM2Shared instead of asking
        // the cache for one, and holds a reference to `source` itself. That reference is the field
        // CM2Model's header describes at ref +0x30 as "always null so far" -- this is the path that
        // makes it non-null.
        CM2Model* CreateModelFrom(CM2Model* source, uint32_t flags);
        int32_t Draw(M2PASS pass);

        // Draw the opaque pass into the shadow map, with the reference's ShadowMap / ShadowMapSL
        // effect in place of each batch's own. lightView maps absolute world space into the light's
        // view; bone matrices are camera-relative, so the rebase applied per bone is
        // m_viewInv * lightView.
        int32_t DrawShadowCasters(const C44Matrix& lightView);
        void SelectLights(CM2Lighting* lighting);
};

#endif
