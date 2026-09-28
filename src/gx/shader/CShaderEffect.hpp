#ifndef GX_SHADER_C_SHADER_EFFECT_HPP
#define GX_SHADER_C_SHADER_EFFECT_HPP

#include <cstdint>
#include <storm/Hash.hpp>

class C3Vector;
class C4Vector;
class C44Matrix;
class CGxShader;
class CImVector;
class CM2Light;
class CM2Lighting;

class CShaderEffect : public TSHashObject<CShaderEffect, HASHKEY_STRI> {
    public:
        // Structs
        struct LocalLights {
            float float0[44];

            // Zero every slot before ComputeLocalLights fills it. This is NOT housekeeping:
            // ComputeLocalLights writes the colour block for all four slots, but the position
            // and attenuation blocks only for lights of type 1, and its tail loop clears only
            // the colours. Without this, up to 28 of the 44 floats handed to the vertex shader
            // are whatever was in the buffer before -- stack garbage at one call site and the
            // previous frame's lights at the other, which is a static.
            //
            // The reference does exactly this, in its own function, and calls it from both
            // sites. ref: FUN_007a8a60
            void Clear();
        };

        // Static variables
        static CShaderEffect* s_curEffect;
        static int32_t s_enableShaders;
        static C4Vector s_fogColorAlphaRef;
        static float s_fogMul;
        static C4Vector s_fogParams;
        static int32_t s_lightEnabled;

        // Just the lighting flag, without the local-light bookkeeping SetLocalLighting also does.
        // The reference keeps them as separate functions and the ribbon draw calls THIS one.
        static void SetLightEnabled(int32_t lightEnabled);
        static uint32_t s_localLightCount;
        static LocalLights s_localLights;
        static C3Vector s_sunAmbient;
        static C3Vector s_sunDiffuse;
        static C3Vector s_sunDir;
        static int32_t s_useAlphaRef;
        static int32_t s_usePcfFiltering;
        // The reference caches FUN_00873ff0()'s result here (written once a frame by the
        // map render at 0x7a9620) and both permutation halves read it. Nothing sets it in
        // frozen yet, for the same reason CM2Scene::BuildBatchElement carries a
        // `v8 = Sub873FF0()` TODO -- that function is not ported. Zero is what both places
        // therefore use, and they stay consistent because they read the same name.
        static uint32_t s_shadowMode;

        // Static functions
        static void ComputeLocalLights(LocalLights* localLights, uint32_t localLightsCount, CM2Light** lights, const C3Vector* a4);
        static void InitShaderSystem(int32_t enableShaders, int32_t usePcf);

        // Turn hardware PCF filtering on or off after startup. It stays off unless shaders are
        // enabled at all, which is the same condition InitShaderSystem applies.
        // ref: FUN_00872ad0
        static void SetPcfFiltering(int32_t usePcf);
        static void SetAlphaRef(float alphaRef);
        static void SetDiffuse(const C4Vector& diffuse);
        static void SetEmissive(const C4Vector& emissive);
        static void SetFogEnabled(int32_t fogEnabled);
        static void SetFogParams(float fogStart, float fogEnd, float fogRate, const CImVector& fogColor);
        static void SetLocalLighting(CM2Lighting* lighting, int32_t lightEnabled, const C3Vector* a3);
        static void SetShaders(uint32_t vertexPermute, uint32_t pixelPermute);

        // The pixel permutation for the current fog and shadow state. ref: FUN_00872de0
        static uint32_t PixelPermute();

        // Pick and set both shader permutations for geometry with `boneInfluences` bones
        // per vertex, from the lighting state SetLocalLighting last cached. This is the
        // same formula CM2Scene::BuildBatchElement computes per element, which is what
        // identified it. ref: FUN_00873160
        static void SetShadersForGeometry(uint32_t boneInfluences);

        // Upload world * view, transposed, to vertex constants 31 through 34 -- the slot
        // CM2SceneRender::DrawBatch fills with the first BONE matrix, in the same
        // transposed layout. Unskinned geometry supplies its transform there so the M2
        // vertex program needs no separate path for it.
        static void SetWorldViewConstants();
        static void SetTexMtx(const C44Matrix& matrix, uint32_t tcIndex);
        static void SetTexMtx_Identity(uint32_t tcIndex);
        static void SetTexMtx_SphereMap(uint32_t tcIndex);
        static void UpdateProjMatrix();

        // Member variables
        // How many texture stages the effect file's FixedFunc block described, and the
        // blend each stage applies to colour and to alpha. Only the fixed-function draw
        // paths read them, and the device always reports the shader level, so nothing in
        // frozen consumes these yet -- the effect files carry them, so they are parsed.
        uint32_t m_fixedFuncStages = 0;           // +0x18
        uint32_t m_colorOps[2] = { 0, 0 };        // +0x1c
        uint32_t m_alphaOps[2] = { 0, 0 };        // +0x24
        CGxShader* m_vertexShaders[90];
        CGxShader* m_pixelShaders[16];

        // Member functions
        void InitEffect(const char* vsName, const char* psName);
        void SetFixedFunc(const uint32_t* colorOps, const uint32_t* alphaOps, uint32_t stages);
        void SetCurrent();
};

#endif
