#ifndef MODEL_C_M2_SCENE_RENDER_HPP
#define MODEL_C_M2_SCENE_RENDER_HPP

#include "gx/Types.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "model/CM2Scene.hpp"
#include "model/M2Types.hpp"
#include <cstdint>

class CM2Cache;
class CM2Lighting;
class CM2Model;
class CM2Shared;
class CShaderEffect;
struct M2Batch;
struct M2Data;
struct M2Element;
struct M2Material;
struct M2SkinSection;

class CM2SceneRender {
    public:
        // Static variables
        static C44Matrix s_identity;
        static int32_t s_fogModeList[M2BLEND_COUNT];
        static EGxBlend s_gxBlend[M2PASS_COUNT][M2BLEND_COUNT];
        static int32_t s_shadedList[M2BLEND_COUNT];

        // Shadow map caster mode. When set, batches are drawn with the reference's ShadowMap /
        // ShadowMapSL effect instead of their own material effect, and the bone matrices are
        // rebased from the camera's view into the light's. Null means normal rendering.
        //
        // The rebase is needed because frozen bakes the view into the bone matrices
        // (CM2Model::AnimateMT: matrixF4 = matrixB4 * view), which is what the model shaders
        // expect. The reference's ShadowMap vertex shader instead carries a second matrix at
        // c14..c16 for exactly this correction, but only in its bone-blended permutations;
        // rebasing on the CPU covers the unskinned ones too, at the cost of one matrix multiply
        // per bone per caster.
        static const C44Matrix* s_shadowCasterRebase;
        static CShaderEffect* s_shadowCasterEffect;

        // Member variables
        C44Matrix matrix0;
        CM2Scene* m_scene;
        CM2Cache* m_cache;
        M2Data* m_data = nullptr;
        M2PASS m_curPass;
        M2Element* m_curElement = nullptr;
        M2Element* m_prevElement = nullptr;
        uint32_t m_curType = -1u;
        uint32_t m_prevType = -1u;
        CM2Model* m_curModel = nullptr;
        CM2Model* m_prevModel = nullptr;
        CM2Shared* m_curShared = nullptr;
        CM2Shared* m_prevShared = nullptr;
        CM2Lighting* m_curLighting = nullptr;
        CM2Lighting* m_prevLighting = nullptr;
        uint32_t m_curShaded = 0;
        uint32_t m_prevShaded = 0;
        uint32_t m_curFogMode = -1u;
        uint32_t m_prevFogMode = -1u;
        M2Batch* m_curBatch = nullptr;
        M2Batch* m_prevBatch = nullptr;
        M2SkinSection* m_curSkinSection = nullptr;
        M2SkinSection* m_prevSkinSection = nullptr;
        M2Material* m_curMaterial = nullptr;
        M2Material* m_prevMaterial = nullptr;
        // +0xb8: a scratch material owned by the render, which m_curMaterial points at by default.
        // Element types that have no M2Material of their own fill this in and use it -- particles
        // are one, and DrawParticle builds exactly its two uint16s: a flags word from the
        // emitter's material flags, and M2BlendIndexFromGx of its blend.
        M2Material m_scratchMaterial = {};

        // +0xa0 through +0xb4: the six shader effects the constructor looks up by name. The
        // first two are what DrawParticle chooses between on the emitter's material flag bit 0 --
        // which is `!(M2Particle.flags & 0x1)`, so a file flag of 0x1 draws unlit.
        //
        // The four projected ones are not for particles. DrawBatchProj is a stub and CLAUDE.md's
        // first priority lists M2 shadow receivers as still open; those need exactly these.
        //
        // Null is a supported state: GetEffect is a lookup, and an effect the shader list has not
        // been given returns null rather than being created.
        CShaderEffect* m_particleEffect = nullptr;
        CShaderEffect* m_particleUnlitEffect = nullptr;
        CShaderEffect* m_projModModEffect = nullptr;
        CShaderEffect* m_projModModUnlitEffect = nullptr;
        CShaderEffect* m_projModAddEffect = nullptr;
        CShaderEffect* m_projModAddUnlitEffect = nullptr;

        // Member functions
        // matrix0 is identity without being written here: C44Matrix's default constructor makes
        // it one, where the reference stores the four 1.0s itself. Every cur/prev pair below is
        // likewise default-initialised where the reference zeroes it explicitly.
        // ref: FUN_0081f330
        CM2SceneRender(CM2Scene* scene)
            : m_scene(scene)
            , m_cache(scene->m_cache)
            , m_particleEffect(CShaderEffectManager::GetEffect("Particle"))
            , m_particleUnlitEffect(CShaderEffectManager::GetEffect("Particle_Unlit"))
            , m_projModModEffect(CShaderEffectManager::GetEffect("Projected_ModMod"))
            , m_projModModUnlitEffect(CShaderEffectManager::GetEffect("Projected_ModMod_Unlit"))
            , m_projModAddEffect(CShaderEffectManager::GetEffect("Projected_ModAdd"))
            , m_projModAddUnlitEffect(CShaderEffectManager::GetEffect("Projected_ModAdd_Unlit"))
            {};
        void Draw(M2PASS pass, M2Element* elements, uint32_t* a4, uint32_t a5);
        void DrawBatch();
        void DrawBatchDoodad(M2Element* elements, uint32_t* a3);
        void DrawBatchProj();
        void DrawCallback();
        // Put the view into camera-relative space for a particle draw, and reset the world
        // matrix to identity. ref: FUN_0081f620
        void SetupParticleTransform(const C3Vector& cameraPosition);

        int32_t DrawParticle(uint32_t a2, M2Element* elements, uint32_t* a4, uint32_t a5);
        void DrawRibbon();
        void SetBatchVertices(int32_t a2);
        void SetupBatchVertices();
        void SetupLighting();
        void SetupMaterial();
        void SetupTextures();
};

#endif
