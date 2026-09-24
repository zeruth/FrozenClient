#ifndef MODEL_C_M2_SCENE_HPP
#define MODEL_C_M2_SCENE_HPP

#include "model/M2Model.hpp"
#include "model/M2Types.hpp"
#include <cstdint>
#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>

class CM2Cache;
class CM2Light;
class CM2Lighting;
class CM2Model;

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
        static int32_t SortTransparent(uint32_t a, uint32_t b, const void* userArg);

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
        uint32_t uint104 = 0;

        // Member functions

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
        int32_t Draw(M2PASS pass);

        // Draw the opaque pass into the shadow map, with the reference's ShadowMap / ShadowMapSL
        // effect in place of each batch's own. lightView maps absolute world space into the light's
        // view; bone matrices are camera-relative, so the rebase applied per bone is
        // m_viewInv * lightView.
        int32_t DrawShadowCasters(const C44Matrix& lightView);
        void SelectLights(CM2Lighting* lighting);
};

#endif
