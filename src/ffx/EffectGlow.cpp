#include "ffx/EffectGlow.hpp"
#include "console/CVar.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/shader/CGxShader.hpp"
#include "ui/game/CGCamera.hpp"
#include <common/Handle.hpp>
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstring>

FFX::Target s_waveTarget;

// How many GlowWave passes hold the wave texture (DAT_00d45c6c), the texture's format
// (DAT_00d45c68) and its texels while the device reads them (DAT_00d45c8c).
static int32_t s_waveRefs;
static EGxTexFormat s_waveFormat;
static void* s_waveTexels;

// The position, colour and three texture coordinates GlowWave streams (DAT_00ad88f0).
static CGxVertexAttrib s_waveFormatAttribs[5] = {
    { GxVA_Position,  4, 0x00, 0x28 },
    { GxVA_Color0,    0, 0x0c, 0x28 },
    { GxVA_TexCoord0, 3, 0x10, 0x28 },
    { GxVA_TexCoord1, 3, 0x18, 0x28 },
    { GxVA_TexCoord2, 3, 0x20, 0x28 },
};

static void DestroyShaders(CGxShader** shaders, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        if (shaders[i]) {
            g_theGxDevicePtr->ShaderDestroy(&shaders[i]);
        }
    }
}

static void SetTexture(uint32_t stage, HTEXTURE texture) {
    g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + stage), TextureGetGxTex(texture, 1, nullptr));
}

// ref: FUN_008c1f70
PassBox4::PassBox4(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXBox4", 2);
}

// ref: FUN_008c1fe0
PassBox4::~PassBox4() {
    DestroyShaders(this->m_shaders, 2);
}

// ref: FUN_008c2020
bool PassBox4::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_008c2040
void PassBox4::Render() {
    // The four taps, in texels: a box straddling the pixel (DAT_00d45c44, filled once).
    static const float s_offsets[8] = { -1.5f, -1.5f, 0.5f, -1.5f, 0.5f, 0.5f, -1.5f, 0.5f };

    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_flip);
    FFX::StreamQuad(FFX::s_quadPositions, 0xffffffff, FFX::s_quadTexCoords, s_offsets, &this->m_inputs[0]->m_invTexWidth);

    g_theGxDevicePtr->PrimVertexMask(0x3d1);
    FFX::QuadIndex();

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, this->m_inputs[0]->m_texture);
    }

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_008c1b40
PassGauss4::PassGauss4(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXGauss4", 2);
}

// ref: FUN_008c1bb0
PassGauss4::~PassGauss4() {
    DestroyShaders(this->m_shaders, 2);
}

// The same body as PassBox4::IsValid; the reference's linker folded the two into FUN_008c2020.
bool PassGauss4::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_008c1c20
void PassGauss4::Render() {
    // Across, then down, in texels.
    static const float s_across[8] = { -2.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 2.5f, 0.0f };
    static const float s_down[8] = { 0.0f, -2.5f, 0.0f, -0.5f, 0.0f, 0.5f, 0.0f, 2.5f };

    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_flip);

    g_theGxDevicePtr->PrimVertexMask(0x3d1);
    FFX::QuadIndex();

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    // Across, from the input into the spare quarter target.
    FFX::StreamQuad(FFX::s_quadPositions, 0xffffffff, FFX::s_quadTexCoords, s_across, &this->m_inputs[0]->m_invTexWidth);

    auto& spare = FFX::s_targets[FFX::Target_Quarter1];

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, TextureGetGxTex(spare.m_texture, 1, nullptr), 0);
    GxXformSetViewport(
        0.0f, static_cast<float>(spare.m_width) / static_cast<float>(spare.m_texWidth),
        0.0f, static_cast<float>(spare.m_height) / static_cast<float>(spare.m_texHeight),
        0.0f, 1.0f
    );

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, this->m_inputs[0]->m_texture);
    }

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    // Down, from the spare target back into this pass's own.
    FFX::StreamQuad(FFX::s_quadPositions, 0xffffffff, FFX::s_quadTexCoords, s_down, &this->m_inputs[0]->m_invTexWidth);

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, TextureGetGxTex(this->m_target->m_texture, 1, nullptr), 0);
    GxXformSetViewport(
        0.0f, static_cast<float>(this->m_target->m_width) / static_cast<float>(this->m_target->m_texWidth),
        0.0f, static_cast<float>(this->m_target->m_height) / static_cast<float>(this->m_target->m_texHeight),
        0.0f, 1.0f
    );

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, spare.m_texture);
    }

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_008c21e0
PassGlow::PassGlow(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXGlow", 2);
}

// ref: FUN_008c2250
PassGlow::~PassGlow() {
    DestroyShaders(this->m_shaders, 2);
}

// ref: FUN_008c2290
bool PassGlow::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_008c27b0
void PassGlow::Render() {
    C2Vector sceneCoords[4] = {};
    C2Vector blurCoords[4] = {};

    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, sceneCoords, this->m_flip);
    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[1]->m_width, &this->m_inputs[1]->m_texWidth,
                    FFX::s_quadPositions, blurCoords, this->m_flip);

    SetTexture(0, this->m_inputs[0]->m_texture);
    SetTexture(1, this->m_inputs[1]->m_texture);

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    // The colour is the same at every corner: a stride of zero.
    GxPrimVertexPtr(4, FFX::s_quadPositions, sizeof(C3Vector), nullptr, 0, &this->m_color, 0,
                    sceneCoords, sizeof(C2Vector), blurCoords, sizeof(C2Vector));
    GxPrimIndexPtr(4, FFX::s_quadIndices);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_008c2920
// The wave texture, 128 by 128: two cosines, one across and one down, either as signed UV88
// pairs or, where the device lacks that format, as the green and red of an ARGB texel biased to
// 0..255. The cosine is the reference's own polynomial over the floored phase, its sign flipped
// on odd half turns.
static void WaveTexCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t level,
                            void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    uint32_t texelSize = s_waveFormat == GxTex_Argb8888 ? 4 : 2;

    auto wave = [](uint32_t i, int32_t* whole) {
        float fraction;
        CameraSplitFloor(static_cast<float>(i) * 0.0078125f * 6.2831855f * 0.31830987f - 0.5f, &fraction, whole);

        return 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;
    };

    if (cmd == GxTex_Lock) {
        s_waveTexels = SMemAlloc(texelSize * 0x4000, __FILE__, __LINE__, 0x0);

        if (s_waveFormat == GxTex_Uv88) {
            auto row = static_cast<uint16_t*>(s_waveTexels);

            for (uint32_t y = 0; y < 0x80; y++, row += 0x80) {
                for (uint32_t x = 0; x < 0x80; x++) {
                    auto toByte = [&](uint32_t i) {
                        int32_t whole;
                        float value = wave(i, &whole);

                        if (whole & 1) {
                            value = -value;
                        }

                        value *= 128.0f;
                        value = value < -128.0f ? -128.0f : (127.0f <= value ? 127.0f : value);

                        return static_cast<uint8_t>(static_cast<int32_t>(value));
                    };

                    row[x] = static_cast<uint16_t>(toByte(y) << 8 | toByte(x));
                }
            }

            return;
        }

        auto row = static_cast<uint32_t*>(s_waveTexels);

        for (uint32_t y = 0; y < 0x80; y++, row += 0x80) {
            for (uint32_t x = 0; x < 0x80; x++) {
                auto toByte = [&](uint32_t i) {
                    int32_t whole;
                    float value = wave(i, &whole);

                    if (whole & 1) {
                        value = -value;
                    }

                    value = (0.5f + value * 0.5f) * 255.0f;
                    value = value < 0.0f ? 0.0f : (255.0f <= value ? 255.0f : value);

                    return static_cast<uint32_t>(static_cast<int32_t>(value));
                };

                row[x] = toByte(x) << 16 | toByte(y) << 8;
            }
        }
    } else if (cmd == GxTex_Latch) {
        texels = s_waveTexels;
        texelStrideInBytes = texelSize * 0x80;
    } else if (cmd == GxTex_Unlock) {
        SMemFree(s_waveTexels, __FILE__, __LINE__, 0x0);
        s_waveTexels = nullptr;
    }
}

// ref: FUN_008c2ce0
PassGlowWave::PassGlowWave(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    if (s_waveRefs == 0) {
        s_waveTarget = FFX::Target();
        s_waveTarget.m_texWidth = 0x80;
        s_waveTarget.m_texHeight = 0x80;
        s_waveTarget.m_width = 0x80;
        s_waveTarget.m_height = 0x80;
        s_waveTarget.m_invTexWidth = 0.0078125f;
        s_waveTarget.m_invTexHeight = 0.0078125f;

        CGxTexFlags flags = CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1);

        // The pixel shader profiles that cannot read signed UV88 take the wave as ARGB.
        s_waveFormat = GxTex_Uv88;

        switch (GxCaps().m_shaderTargets[GxSh_Pixel]) {
        case GxShPS_ps_1_4:
        case GxShPS_ps_2_0:
        case GxShPS_ps_3_0:
        case GxShPS_ps_4_0:
        case GxShPS_ps_5_0:
        case GxShPS_nvfp2:
        case GxShPS_arbfp1:
            s_waveFormat = GxTex_Argb8888;
            break;
        default:
            break;
        }

        s_waveTarget.m_texture = TextureCreate(0x80, 0x80, s_waveFormat, s_waveFormat, flags, nullptr,
                                               &WaveTexCallback, "PassGlowWave", 0);
    }

    s_waveRefs++;

    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    if (!GxCaps().m_texFmt[GxTex_Uv88]) {
        this->m_shaderIndex += 2;
    }

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXGlowWave", 4);
}

// ref: FUN_008c22b0
PassGlowWave::~PassGlowWave() {
    DestroyShaders(this->m_shaders, 4);

    s_waveRefs--;

    if (s_waveRefs == 0) {
        if (s_waveTarget.m_texture) {
            HandleClose(s_waveTarget.m_texture);
        }

        s_waveTarget = FFX::Target();
    }
}

// The same body as PassGlow::IsValid (FUN_008c2290), which the GlowWave vtable shares.
bool PassGlowWave::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_008c2350
void PassGlowWave::Render() {
    C2Vector waveCoords[4] = {};
    C2Vector sceneCoords[4] = {};
    C2Vector blurCoords[4] = {};

    FFX::BeginPass(this->m_target);
    FFX::QuadIndex();

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, waveCoords, this->m_flip);
    SetTexture(0, this->m_inputs[0]->m_texture);

    // The wave drifts on two unrelated periods and is stretched to the target, a texel per
    // screen pixel across and a little less down, then turned ten degrees.
    C44Matrix waveMatrix;

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    C3Vector drift = {
        static_cast<float>(static_cast<int32_t>(now % 3174u)) / 3174.0f,
        static_cast<float>(static_cast<int32_t>(now % 2805u)) / 2805.0f,
        0.0f
    };
    waveMatrix.Translate(drift);

    C3Vector stretch = {
        static_cast<float>(this->m_target->m_width) * 1.0f * 0.0078125f,
        static_cast<float>(this->m_target->m_height) * 0.88f * 0.0078125f,
        1.0f
    };
    waveMatrix.Scale(stretch);
    waveMatrix.RotateAroundZ(0.17453292f);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[1]->m_width, &this->m_inputs[1]->m_texWidth,
                    FFX::s_quadPositions, sceneCoords, this->m_flip);
    SetTexture(1, this->m_inputs[1]->m_texture);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[2]->m_width, &this->m_inputs[2]->m_texWidth,
                    FFX::s_quadPositions, blurCoords, this->m_flip);
    SetTexture(2, this->m_inputs[2]->m_texture);

    CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x28, 4);
    auto data = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));

    for (uint32_t v = 0; v < 4; v++) {
        data[0] = FFX::s_quadPositions[v].x;
        data[1] = FFX::s_quadPositions[v].y;
        data[2] = FFX::s_quadPositions[v].z;
        memcpy(&data[3], &this->m_color, sizeof(uint32_t));

        C3Vector wave = { waveCoords[v].x, waveCoords[v].y, 0.0f };
        wave = wave * waveMatrix;

        data[4] = wave.x;
        data[5] = wave.y;
        data[6] = sceneCoords[v].x;
        data[7] = sceneCoords[v].y;
        data[8] = blurCoords[v].x;
        data[9] = blurCoords[v].y;

        data += 10;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;

    g_theGxDevicePtr->PrimVertexFormat(buf, s_waveFormatAttribs, 5);
    g_theGxDevicePtr->PrimVertexMask(0x1d1);

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    // How far the wave bends each lookup: three scene texels and three quarters of a blur texel,
    // or as many texels outright for rectangle targets.
    float sceneBend[4];
    float blurBend[4];

    if (!FFX::s_useRectangle) {
        sceneBend[0] = 3.0f * this->m_inputs[1]->m_invTexWidth;
        sceneBend[3] = this->m_inputs[1]->m_invTexHeight * 3.0f;
        blurBend[0] = 0.75f * this->m_inputs[2]->m_invTexWidth;
        blurBend[3] = this->m_inputs[2]->m_invTexHeight * 0.75f;
    } else {
        sceneBend[0] = 3.0f;
        sceneBend[3] = 3.0f;
        blurBend[0] = 0.75f;
        blurBend[3] = 0.75f;
    }

    sceneBend[1] = 0.0f;
    sceneBend[2] = 0.0f;
    blurBend[1] = 0.0f;
    blurBend[2] = 0.0f;

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 0, sceneBend, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 1, blurBend, 1);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_008bfe80
// The glow: the scene box-filtered down to a quarter, blurred there, and combined with the scene
// onto the screen. Under water a second list runs instead, ending in the wave-bent combine --
// unless rectangle targets are in use on a device without the non-power-of-two restriction,
// where the plain combine stands in for it.
EffectGlow::EffectGlow() {
    this->m_cvar = CVar::Register(
        "ffxGlow",
        "full screen glow effect",
        0x1,
        this->m_defaultOn ? "1" : "0",
        &FFXDeathCallback,
        GRAPHICS
    );

    FFX::Target* scene[1] = { &FFX::s_targets[FFX::Target_Scene] };
    FFX::Target* quarter[1] = { &FFX::s_targets[FFX::Target_Quarter0] };
    FFX::Target* sceneAndBlur[2] = { &FFX::s_targets[FFX::Target_Scene], &FFX::s_targets[FFX::Target_Quarter0] };
    FFX::Target* waveSceneAndBlur[3] = { &s_waveTarget, &FFX::s_targets[FFX::Target_Scene], &FFX::s_targets[FFX::Target_Quarter0] };

    FFX::Target* quarterTarget = &FFX::s_targets[FFX::Target_Quarter0];
    FFX::Target* screenTarget = &FFX::s_targets[FFX::Target_Screen];

    *this->m_passes.New() = STORM_NEW(PassBox4)(scene, 1, quarterTarget, 0, false);
    *this->m_passes.New() = STORM_NEW(PassGauss4)(quarter, 1, quarterTarget, 0, false);
    *this->m_passes.New() = STORM_NEW(PassGlow)(sceneAndBlur, 2, screenTarget, 0, true);

    *this->m_underwaterPasses.New() = STORM_NEW(PassBox4)(scene, 1, quarterTarget, 0, false);
    *this->m_underwaterPasses.New() = STORM_NEW(PassGauss4)(quarter, 1, quarterTarget, 0, false);

    if (FFX::s_useRectangle && GxCaps().m_texNonPow2Conditional) {
        *this->m_underwaterPasses.New() = STORM_NEW(PassGlow)(sceneAndBlur, 2, screenTarget, 0, true);
    } else {
        *this->m_underwaterPasses.New() = STORM_NEW(PassGlowWave)(waveSceneAndBlur, 3, screenTarget, 0, true);
    }

    this->m_underwater = 0;
}

// ref: FUN_008c01d0
EffectGlow::~EffectGlow() {
    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        if (this->m_passes[i]) {
            delete this->m_passes[i];
        }
    }

    for (uint32_t i = 0; i < this->m_underwaterPasses.Count(); i++) {
        if (this->m_underwaterPasses[i]) {
            delete this->m_underwaterPasses[i];
        }
    }
}

// ref: FUN_008bfd30
bool EffectGlow::IsEnabled() {
    bool enabled = this->m_cvar->m_intValue != 0;

    auto& passes = this->m_underwater ? this->m_underwaterPasses : this->m_passes;

    for (uint32_t i = 0; i < passes.Count(); i++) {
        enabled = enabled & passes[i]->IsValid();
    }

    return enabled;
}

// ref: FUN_008bfd90
void EffectGlow::Render() {
    auto& passes = this->m_underwater ? this->m_underwaterPasses : this->m_passes;

    for (uint32_t i = 0; i < passes.Count(); i++) {
        passes[i]->Render();
    }
}

// ref: FUN_008bfde0
// Three words: which list runs, then the combine's alpha and its grey level, written into the
// third pass of both lists.
void EffectGlow::SetParams(uint32_t count, const uint32_t* params) {
    this->m_underwater = params[0];

    uint8_t alpha = static_cast<uint8_t>(params[1]);
    uint8_t grey = static_cast<uint8_t>(params[2]);
    CImVector color = { grey, grey, grey, alpha };

    static_cast<PassGlowWave*>(this->m_underwaterPasses[2])->m_color = color;
    static_cast<PassGlow*>(this->m_passes[2])->m_color = color;
}
