#include "ffx/FFXEffects.hpp"
#include "ffx/EffectGlow.hpp"
#include "console/CVar.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/Gx.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "world/CWorld.hpp"
#include "world/DayNightLight.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstring>
#include <storm/Memory.hpp>
#include <common/Handle.hpp>
#include "gx/buffer/CGxPool.hpp"
#include "gx/buffer/CGxBuf.hpp"

// The phase of the nether swirl's breathing, shared by every NetherBlur pass (DAT_00d38150).
static float s_netherPhase;

// The nether combine's tint, its fade in the last word (DAT_00af4940).
static float s_netherCombineConstant[4] = { 0.6f, 0.6f, 0.78f, 0.0f };

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

static void SetBlend(uint32_t blend) {
    g_theGxDevicePtr->RsSet(GxRs_BlendingMode, static_cast<int32_t>(blend));
    g_theGxDevicePtr->RsSetAlphaRef();
}

// One of the nether swirl's offsets: a random float in [1, 2) taken to [-1, 1).
static float NetherRandom(CRndSeed& seed) {
    uint32_t bits = (CRandom::uint32(seed) & 0x7fffff) | 0x3f800000;
    float value;
    memcpy(&value, &bits, sizeof(value));

    return fmodf((value - 1.0f) * 2.0f - 1.0f, 1.0f);
}

// ref: FUN_007e86a0
PassDeath::PassDeath(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    auto profile = GxCaps().m_shaderTargets[GxSh_Pixel];

    if (profile == GxShPS_nvts || profile == GxShPS_nvts2 || profile == GxShPS_nvts3) {
        const char* name = FFX::s_useRectangle ? "FFXDeath_nvts_rect" : "FFXDeath_nvts_2d";
        g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", name, 1);
        return;
    }

    if (FFX::s_useRectangle) {
        this->m_shaderIndex = 1;
    }

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXDeath", 2);
}

// ref: FUN_007e8770
PassDeath::~PassDeath() {
    DestroyShaders(this->m_shaders, 2);
}

// The shared body at FUN_008c2020.
bool PassDeath::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_007e87b0
// The tint is fixed; its strength is the day's glow.
void PassDeath::Render() {
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

    CImVector color = { 0xa8, 0x93, 0x53, static_cast<uint8_t>(lrintf(DayNightGetBlock()->info.glow * 255.0f)) };

    GxPrimVertexPtr(4, FFX::s_quadPositions, sizeof(C3Vector), nullptr, 0, &color, 0,
                    sceneCoords, sizeof(C2Vector), blurCoords, sizeof(C2Vector));
    GxPrimIndexPtr(4, FFX::s_quadIndices);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_007e8920
PassDeathNvrc::PassDeathNvrc(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    g_theGxDevicePtr->ShaderCreate(&this->m_shader, GxSh_Pixel, "Shaders\\Pixel", "FFXDeath_nvrc", 1);
}

// ref: FUN_007e9950
PassDeathNvrc::~PassDeathNvrc() {
    DestroyShaders(&this->m_shader, 1);
}

// ref: FUN_007e8970
bool PassDeathNvrc::IsValid() {
    return this->m_shader && this->m_shader->Valid();
}

// ref: FUN_007e99a0
void PassDeathNvrc::Render() {
    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_flip);

    CImVector color;
    color.value = 0xff5393a8;

    GxPrimVertexPtr(4, FFX::s_quadPositions, sizeof(C3Vector), nullptr, 0, &color, 0,
                    FFX::s_quadTexCoords, sizeof(C2Vector), nullptr, 0);
    GxPrimIndexPtr(4, FFX::s_quadIndices);

    SetTexture(0, this->m_inputs[0]->m_texture);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shader);

    SetBlend(this->m_blend);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_007e8990
PassNetherBlur::PassNetherBlur(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    if (FFX::s_useRectangle) {
        this->m_shaderIndex = 1;
    }

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXNetherBlur", 2);
    g_theGxDevicePtr->ShaderCreate(&this->m_vertexShader, GxSh_Vertex, "Shaders\\Vertex", "FFXNetherBlur", 1);

    this->m_seed.SetSeed(0xabcdef01);

    for (auto& table : this->m_tables) {
        for (auto& value : table) {
            value = NetherRandom(this->m_seed);
        }
    }

    this->m_from = this->m_tables[0];
    this->m_blend = 0.0f;
    this->m_to = this->m_tables[1];
    this->m_next = this->m_tables[2];
}

// ref: FUN_007e8b30
PassNetherBlur::~PassNetherBlur() {
    DestroyShaders(this->m_shaders, 2);
    DestroyShaders(&this->m_vertexShader, 1);
}

// ref: FUN_007e8b90
bool PassNetherBlur::IsValid() {
    if (!this->m_vertexShader || !this->m_vertexShader->Valid()) {
        return false;
    }

    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_007e9b10
// The swirl turns with the camera: its angle is the view's right vector's heading, breathing
// with a slow cosine. The grid's points drift from one random table to the next, a new table
// dealt each time the drift completes, and the scene is drawn through the grid twice, into the
// spare quarter target and back.
void PassNetherBlur::Render() {
    float tapScale = 8.0f;
    float dt = CWorld::GetTickTimeSec();

    s_netherPhase = static_cast<float>(fmod(static_cast<double>(dt + dt + s_netherPhase), 6.2831854820251465));

    const C44Matrix& view = FFX::SavedView();

    float x = (view.c0 + view.b0) * 0.0f + view.a0;
    float y = (view.c1 + view.b1) * 0.0f + view.a1;
    float z = (view.c2 + view.b2) * 0.0f + view.a2;

    if (0.00000011920929f < std::fabs(z)) {
        float inv = 1.0f / z;
        x = inv * x;
        y = inv * y;
    }

    float inv = 1.0f / sqrtf(x * x + y * y);
    x *= inv;
    y *= inv;

    float heading = acosf(x);

    if (y < 0.0f) {
        heading = 6.2831855f - heading;
    }

    float angle = cosf(s_netherPhase) * 0.5f + heading;

    if (1.0f < this->m_blend) {
        this->m_blend = fmodf(this->m_blend, 1.0f);

        float* from = this->m_from;
        this->m_from = this->m_to;
        this->m_to = this->m_next;
        this->m_next = from;

        for (uint32_t i = 0; i < 36; i++) {
            this->m_next[i] = NetherRandom(this->m_seed);
        }
    }

    for (uint32_t i = 0; i < 36; i++) {
        this->m_current[i] = this->m_to[i] * this->m_blend + (1.0f - this->m_blend) * this->m_from[i];
    }

    this->m_blend = dt * 1.5f + this->m_blend;

    FFX::BeginPass(this->m_target);

    if (!FFX::s_useRectangle) {
        FFX::GridCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                        FFX::s_quadPositions, FFX::s_quadTexCoords, 6, this->m_flip);
        FFX::StreamColored(36, FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_current);
    }

    g_theGxDevicePtr->PrimVertexMask(0x51);
    FFX::GridIndex(6, 6);

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, this->m_vertexShader);
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    float c0[4] = { angle, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, c0, 1);

    // c1 is the blur's reach in texture space and c2 carries the stereo convergence ahead of it,
    // the two overlapping on the reference's stack.
    float c2[5];
    float* c1 = &c2[1];

    if (!FFX::s_useRectangle) {
        c1[1] = this->m_inputs[0]->m_invTexHeight * 8.0f;
        c1[0] = 8.0f * this->m_inputs[0]->m_invTexWidth;
        c1[2] = 0.0f;
        c1[3] = 0.0f;
    } else {
        tapScale = 4.0f;
        c1[0] = 4.0f;
        c1[1] = 4.0f;
        c1[2] = 0.0f;
        c1[3] = 0.0f;
    }

    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 1, c1, 1);

    c2[0] = g_theGxDevicePtr->StereoGetConvergence();
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 2, c2, 1);

    auto& spare = FFX::s_targets[FFX::Target_Quarter1];

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, TextureGetGxTex(spare.m_texture, 1, nullptr), 0);
    GxXformSetViewport(
        0.0f, static_cast<float>(spare.m_width) / static_cast<float>(spare.m_texWidth),
        0.0f, static_cast<float>(spare.m_height) / static_cast<float>(spare.m_texHeight),
        0.0f, 1.0f
    );

    CImVector white;
    white.value = 0xffffffff;
    GxSceneClear(1, white);

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, this->m_inputs[0]->m_texture);
    }

    if (FFX::s_useRectangle) {
        FFX::GridCoords(&spare.m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                        FFX::s_quadPositions, FFX::s_quadTexCoords, 6, this->m_flip);
        FFX::StreamColored(36, FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_current);
    }

    g_theGxDevicePtr->Draw(FFX::GridBatch(), 1);

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, TextureGetGxTex(this->m_target->m_texture, 1, nullptr), 0);
    GxXformSetViewport(
        0.0f, static_cast<float>(this->m_target->m_width) / static_cast<float>(this->m_target->m_texWidth),
        0.0f, static_cast<float>(this->m_target->m_height) / static_cast<float>(this->m_target->m_texHeight),
        0.0f, 1.0f
    );

    GxSceneClear(1, white);

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, spare.m_texture);
    }

    if (FFX::s_useRectangle) {
        float reach[4] = {
            (this->m_inputs[0]->m_invTexWidth / spare.m_invTexWidth) * tapScale,
            (this->m_inputs[0]->m_invTexHeight / spare.m_invTexHeight) * tapScale,
            0.0f,
            0.0f
        };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 1, reach, 1);

        FFX::GridCoords(&this->m_target->m_width, &spare.m_width, &spare.m_texWidth,
                        FFX::s_quadPositions, FFX::s_quadTexCoords, 6, this->m_flip);
        FFX::StreamColored(36, FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_current);
    }

    g_theGxDevicePtr->Draw(FFX::GridBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_007e8bd0
PassNetherCombine::PassNetherCombine(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    if (FFX::s_useRectangle) {
        this->m_shaderIndex = 1;
    }

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXNetherCombine", 2);

    this->m_fade = 0.0f;
}

// ref: FUN_007e8c40
PassNetherCombine::~PassNetherCombine() {
    DestroyShaders(this->m_shaders, 2);
}

// The shared body at FUN_008c2020.
bool PassNetherCombine::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_007e8c80
void PassNetherCombine::Render() {
    C2Vector sceneCoords[4] = {};
    C2Vector swirlCoords[4] = {};

    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, sceneCoords, this->m_flip);
    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[1]->m_width, &this->m_inputs[1]->m_texWidth,
                    FFX::s_quadPositions, swirlCoords, this->m_flip);

    SetTexture(0, this->m_inputs[0]->m_texture);
    SetTexture(1, this->m_inputs[1]->m_texture);

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    CImVector white;
    white.value = 0xffffffff;

    if (this->m_fade < 0.75f) {
        float fade = this->m_fade + CWorld::GetTickTimeSec();
        this->m_fade = 0.75f <= fade ? 0.75f : fade;
    }

    s_netherCombineConstant[3] = this->m_fade;
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 0, s_netherCombineConstant, 1);

    GxPrimVertexPtr(4, FFX::s_quadPositions, sizeof(C3Vector), nullptr, 0, &white, 0,
                    sceneCoords, sizeof(C2Vector), swirlCoords, sizeof(C2Vector));
    GxPrimIndexPtr(4, FFX::s_quadIndices);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_007ea260
// The scene blurred down to a quarter and combined into the death view, or the one-pass view on
// nvrc hardware. A device with no pixel shaders at all gets no death view.
EffectDeath::EffectDeath() {
    this->m_cvar = CVar::Register(
        "ffxDeath",
        "full screen death effect",
        0x1,
        this->m_defaultOn ? "1" : "0",
        &FFXDeathCallback,
        GRAPHICS
    );

    auto profile = GxCaps().m_shaderTargets[GxSh_Pixel];

    FFX::Target* sceneAndBlur[2] = { &FFX::s_targets[FFX::Target_Scene], &FFX::s_targets[FFX::Target_Quarter0] };
    FFX::Target* quarterTarget = &FFX::s_targets[FFX::Target_Quarter0];
    FFX::Target* screenTarget = &FFX::s_targets[FFX::Target_Screen];

    if (profile == GxShPS_nvrc) {
        *this->m_passes.New() = STORM_NEW(PassDeathNvrc)(sceneAndBlur, 1, screenTarget, 0, true);
        return;
    }

    if (profile > GxShPS_none) {
        *this->m_passes.New() = STORM_NEW(PassBox4)(sceneAndBlur, 1, quarterTarget, 0, false);
        *this->m_passes.New() = STORM_NEW(PassGauss4)(&sceneAndBlur[1], 1, quarterTarget, 0, false);
        *this->m_passes.New() = STORM_NEW(PassDeath)(sceneAndBlur, 2, screenTarget, 0, true);
    }
}

// ref: FUN_007ea420
EffectDeath::~EffectDeath() {
    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        if (this->m_passes[i]) {
            delete this->m_passes[i];
        }
    }
}

// ref: FUN_007ea470
EffectNether::EffectNether() {
    this->m_cvar = CVar::Register(
        "ffxNetherWorld",
        "full screen nether world effect (for invisibility)",
        0x1,
        "1",
        &FFXDeathCallback,
        GRAPHICS
    );

    auto profile = GxCaps().m_shaderTargets[GxSh_Pixel];

    if (profile == GxShPS_nvrc || profile <= GxShPS_none) {
        return;
    }

    FFX::Target* sceneAndBlur[2] = { &FFX::s_targets[FFX::Target_Scene], &FFX::s_targets[FFX::Target_Quarter0] };

    *this->m_passes.New() = STORM_NEW(PassNetherBlur)(sceneAndBlur, 1, &FFX::s_targets[FFX::Target_Quarter0], 0, false);
    *this->m_passes.New() = STORM_NEW(PassNetherCombine)(sceneAndBlur, 2, &FFX::s_targets[FFX::Target_Screen], 0, true);
}

// ref: FUN_007ea5a0
EffectNether::~EffectNether() {
    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        if (this->m_passes[i]) {
            delete this->m_passes[i];
        }
    }
}

// ref: FUN_007e8e20
// Leaving the nether starts the next visit's fade from nothing.
void EffectNether::Deactivate() {
    if (this->m_passes.Count() < 2) {
        return;
    }

    static_cast<PassNetherCombine*>(this->m_passes[1])->m_fade = 0.0f;
}

// ref: FUN_009852a0
// The lattice value at (x, y), smoothed over its eight neighbours: a quarter of itself, an
// eighth of each side and a sixteenth of each corner.
static float NoiseSmoothed(int32_t x, int32_t y) {
    auto lattice = [](uint32_t n) {
        n = n ^ (n << 13);

        return 1.0f - static_cast<float>(((n * n * 0x3d73u + 0xc0ae5u) * n + 0xd208dd03u) & 0x7fffffffu) * 9.313225746154785e-10f;
    };

    uint32_t n = static_cast<uint32_t>(y * 0x39 + x);

    float corners = lattice(n - 0x3a) + lattice(n - 0x38) + lattice(n + 0x38) + lattice(n + 0x3a);
    float sides = lattice(n - 1) + lattice(n + 1) + lattice(n - 0x39) + lattice(n + 0x39);

    return lattice(n) * 0.25f + corners * 0.0625f + sides * 0.125f;
}

// ref: FUN_009854d0
static float NoiseInterpolated(float x, float y) {
    float floorX = floorf(x);
    float fracX = x - floorX;
    float floorY = floorf(y);

    int32_t ix = static_cast<int32_t>(floorX);
    int32_t iy = static_cast<int32_t>(floorY);

    float v00 = NoiseSmoothed(ix, iy);
    float v01 = NoiseSmoothed(ix, iy + 1);
    float v10 = NoiseSmoothed(ix + 1, iy);
    float bottom = (v10 - v00) * fracX + v00;
    float v11 = NoiseSmoothed(ix + 1, iy + 1);
    float top = (v11 - v01) * fracX + v01;

    return bottom + (top - bottom) * (y - floorY);
}

// ref: FUN_00985580
// Octaves of interpolated value noise, each twice the frequency and half the weight of the last.
static float NoiseOctaves(float x, float y, int32_t octaves) {
    float weight = 1.0f;
    float total = 0.0f;

    for (int32_t i = 0; i < octaves; i++) {
        float scale = static_cast<float>(1 << i);
        total = NoiseInterpolated(scale * x, y * scale) * weight + total;
        weight *= 0.5f;
    }

    return total;
}

// Set by the device restore callback so the source pass clears its field once the device is
// back (DAT_00af4938).
static int32_t s_fieldLost;

// The noise texture's texels between lock and unlock (DAT_00d38140), and the texel steps that
// cover the 64-unit noise across it, taken once (DAT_00d38148, DAT_00d38144).
static uint32_t* s_noiseTexels;

// ref: FUN_007e7fe0
void FFXFieldRestored() {
    s_fieldLost = 1;
}

// ref: FUN_007e8e40
static void NoiseTexCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t face, uint32_t level,
                             void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    static float s_stepX = 64.0f / static_cast<float>(width);
    static float s_stepY = 64.0f / static_cast<float>(height);

    if (cmd == GxTex_Lock) {
        s_noiseTexels = static_cast<uint32_t*>(SMemAlloc(width * height * 4, __FILE__, __LINE__, 0x0));

        for (uint32_t y = 0; y < height; y++) {
            float noiseY = static_cast<float>(y) * s_stepY;

            for (uint32_t x = 0; x < width; x++) {
                float value = NoiseOctaves(static_cast<float>(x) * s_stepX, noiseY, 5);
                auto alpha = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int32_t>((value + 3.0f) * 0.25f * 255.0f + 0.5f))) & 0xff;

                s_noiseTexels[y * width + x] = alpha << 24 | 0x00ffffff;
            }
        }
    } else if (cmd == GxTex_Latch) {
        texels = s_noiseTexels;
        texelStrideInBytes = width * 4;
    } else if (cmd == GxTex_Unlock) {
        SMemFree(s_noiseTexels, __FILE__, __LINE__, 0x0);
        s_noiseTexels = nullptr;
    }
}

PassFogSource::PassFogSource(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    s_fieldLost = 1;
    g_theGxDevicePtr->CallbackAddRestored(&FFXFieldRestored);
}

// ref: FUN_007e9260
PassFogSource::~PassFogSource() {
    g_theGxDevicePtr->CallbackRemoveRestored(&FFXFieldRestored);
}

// The source has nothing to load (FUN_008a1420, a shared return-true).
bool PassFogSource::IsValid() {
    return true;
}

// ref: FUN_007e92a0
void PassFogSource::Render() {
    const FFX::Target* noise = this->m_inputs[0];
    float fieldWidth = static_cast<float>(this->m_target->m_width);

    float invTexWidth = 1.0f / static_cast<float>(noise->m_texWidth);
    float invTexHeight = 1.0f / static_cast<float>(noise->m_texHeight);
    float u0 = invTexWidth * 0.5f;
    float v0 = 0.5f * invTexHeight;
    float u1 = invTexWidth * (static_cast<float>(noise->m_width) + 0.5f);
    float v = v0 + static_cast<float>(this->m_row) * ((invTexHeight * (static_cast<float>(noise->m_height) + 0.5f) - v0) / static_cast<float>(noise->m_height));

    // The row, laid one pixel high between y 3 and 4.
    CGxBuf* rowBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x14, 4);
    auto row = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(rowBuf));

    const float rowVerts[4][5] = {
        { 0.0f,       3.0f, 0.0f, u0, v },
        { 0.0f,       4.0f, 0.0f, u0, v },
        { fieldWidth, 4.0f, 0.0f, u1, v },
        { fieldWidth, 3.0f, 0.0f, u1, v },
    };
    memcpy(row, rowVerts, sizeof(rowVerts));

    g_theGxDevicePtr->BufUnlock(rowBuf, 0);
    rowBuf->unk1C = 1;

    FFX::BeginPass(this->m_target);

    if (s_fieldLost) {
        CImVector black;
        black.value = 0;
        GxSceneClear(1, black);
        s_fieldLost = 0;
    }

    GxPrimVertexPtr(rowBuf, GxVBF_PT);
    g_theGxDevicePtr->PrimVertexMask(0x41);
    FFX::QuadIndex();

    g_theGxDevicePtr->RsSet(GxRs_MatDiffuse, static_cast<int32_t>(this->m_color.value));

    SetTexture(0, noise->m_texture);

    for (uint32_t stage = 1; stage < 16; stage++) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + stage), static_cast<void*>(nullptr));
    }

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<void*>(nullptr));

    SetBlend(this->m_blend);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    // The three rows below it cleared to the tint alone, untextured.
    CGxBuf* bandBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0xc, 4);
    auto band = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(bandBuf));

    const float bandVerts[4][3] = {
        { 0.0f,       0.0f, 0.0f },
        { 0.0f,       3.0f, 0.0f },
        { fieldWidth, 3.0f, 0.0f },
        { fieldWidth, 0.0f, 0.0f },
    };
    memcpy(band, bandVerts, sizeof(bandVerts));

    g_theGxDevicePtr->BufUnlock(bandBuf, 0);
    bandBuf->unk1C = 1;

    GxPrimVertexPtr(bandBuf, GxVBF_P);
    g_theGxDevicePtr->PrimVertexMask(0x1);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(nullptr));
    FFX::QuadIndex();

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();

    if (static_cast<uint32_t>(noise->m_texHeight) <= this->m_row) {
        this->m_row = 0;
    } else {
        this->m_row++;
    }
}

// ref: FUN_007e80b0
PassPropagateFog::PassPropagateFog(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, const float* offsets, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    this->m_strength = 0.0f;
    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    memcpy(this->m_offsets, offsets, sizeof(this->m_offsets));

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXPropagateFog", 2);
}

// ref: FUN_007e8140
PassPropagateFog::~PassPropagateFog() {
    DestroyShaders(this->m_shaders, 2);
}

// ref: FUN_007e8180
bool PassPropagateFog::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_007e9080
void PassPropagateFog::Render() {
    FFX::BeginPass(this->m_target);

    FFX::QuadCoords(&this->m_target->m_width, &this->m_inputs[0]->m_width, &this->m_inputs[0]->m_texWidth,
                    FFX::s_quadPositions, FFX::s_quadTexCoords, this->m_flip);
    FFX::StreamQuad(FFX::s_quadPositions, 0xffffffff, FFX::s_quadTexCoords, this->m_offsets, &this->m_inputs[0]->m_invTexWidth);

    g_theGxDevicePtr->PrimVertexMask(0x3d1);
    FFX::QuadIndex();

    for (uint32_t stage = 0; stage < 4; stage++) {
        SetTexture(stage, this->m_inputs[0]->m_texture);
    }

    for (uint32_t stage = 4; stage < 16; stage++) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + stage), static_cast<void*>(nullptr));
    }

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    float strength[4] = { this->m_strength, this->m_strength, this->m_strength, this->m_strength };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 0, strength, 1);

    SetBlend(this->m_blend);

    g_theGxDevicePtr->Draw(FFX::QuadBatch(), 1);

    FFX::EndPass();
}

// ref: FUN_007e81b0
PassFogCombine::PassFogCombine(FFX::Target* const* inputs, uint32_t inputCount, FFX::Target* target, uint32_t blend, bool flip)
    : FFX::Pass(inputs, inputCount, target, blend, flip) {
    this->m_vertexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Static, 0x8820, GxPoolHintBit_Unk0, nullptr);
    this->m_indexPool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Index, GxPoolUsage_Static, 0x3000, GxPoolHintBit_Unk0, nullptr);
    this->m_vertexBuf = g_theGxDevicePtr->BufCreate(this->m_vertexPool, 0x20, 0x441, 0);
    this->m_indexBuf = g_theGxDevicePtr->BufCreate(this->m_indexPool, 2, 0x1800, 0);

    this->m_shaderIndex = FFX::s_useRectangle ? 1 : 0;

    g_theGxDevicePtr->ShaderCreate(this->m_shaders, GxSh_Pixel, "Shaders\\Pixel", "FFXFogCombine", 2);
}

// ref: FUN_007e82a0
PassFogCombine::~PassFogCombine() {
    DestroyShaders(this->m_shaders, 2);

    if (this->m_vertexBuf && this->m_vertexBuf != g_theGxDevicePtr->BufStream(this->m_vertexBuf->m_pool->m_target, 0, 0)) {
        g_theGxDevicePtr->BufDestroy(this->m_vertexBuf);
    }

    if (this->m_indexBuf && this->m_indexBuf != g_theGxDevicePtr->BufStream(this->m_indexBuf->m_pool->m_target, 0, 0)) {
        g_theGxDevicePtr->BufDestroy(this->m_indexBuf);
    }

    if (this->m_vertexPool) {
        g_theGxDevicePtr->PoolDestroy(this->m_vertexPool);
    }

    if (this->m_indexPool) {
        g_theGxDevicePtr->PoolDestroy(this->m_indexPool);
    }
}

// The shared body at FUN_007e8180.
bool PassFogCombine::IsValid() {
    CGxShader* shader = this->m_shaders[this->m_shaderIndex];

    return shader && shader->Valid();
}

// ref: FUN_007e8380
// A point of the unit square in polar form about its centre: the angle over pi, and the distance
// scaled so the corners sit at one.
static C2Vector FieldPolar(float x, float y) {
    float dx = (x - 0.5f) * 1.4142135f;
    float dy = 1.4142135f * (y - 0.5f);
    float distance = sqrtf(dx * dx + dy * dy);

    float cosine = distance == 0.0f ? 1.0f : dy * (1.0f / distance);

    return { acosf(cosine) * 0.31830987f, distance };
}

// ref: FUN_007e8410
void PassFogCombine::BuildVertices(const int32_t* targetSize, FFX::Target* field, FFX::Target* scene) {
    float fieldHalfU = 0.5f / static_cast<float>(field->m_width);
    float fieldHalfV = 0.5f / static_cast<float>(field->m_height);
    float sceneHalfU = 0.5f / static_cast<float>(scene->m_width);
    float sceneHalfV = 0.5f / static_cast<float>(scene->m_height);

    float stepX = static_cast<float>(targetSize[0]) * 0.03125f;
    float stepY = static_cast<float>(targetSize[1]) * 0.03125f;

    bool flipped = this->m_flip && GxDevApi() == GxApi_OpenGl;

    auto data = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(this->m_vertexBuf));

    for (uint32_t row = 0; row < 0x21; row++) {
        float y = stepY * static_cast<float>(row);
        float v = static_cast<float>(row) * 0.03125f;

        if (!flipped) {
            v = 1.0f - v;
        }

        for (uint32_t column = 0; column < 0x21; column++, data += 8) {
            float u = static_cast<float>(column) * 0.03125f;

            data[0] = stepX * static_cast<float>(column);
            data[1] = y;
            data[2] = 0.0f;
            reinterpret_cast<uint32_t*>(data)[3] = 0xffffffff;

            C2Vector polar = FieldPolar(u, v);

            data[4] = (polar.x + fieldHalfU) * field->m_invTexWidth * static_cast<float>(field->m_width);
            data[5] = (polar.y + fieldHalfV) * static_cast<float>(field->m_height) * field->m_invTexHeight;
            data[6] = (u + sceneHalfU) * scene->m_invTexWidth * static_cast<float>(scene->m_width);
            data[7] = (v + sceneHalfV) * static_cast<float>(scene->m_height) * scene->m_invTexHeight;
        }
    }

    g_theGxDevicePtr->BufUnlock(this->m_vertexBuf, 0);
    this->m_vertexBuf->unk1C = 1;
}

// ref: FUN_007e8600
void PassFogCombine::BuildIndices() {
    auto data = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(this->m_indexBuf));

    for (uint32_t row = 0; row < 0x20; row++) {
        auto base = static_cast<uint16_t>(row * 0x21);

        for (uint32_t column = 0; column < 0x20; column++, data += 6) {
            auto c = static_cast<uint16_t>(column);

            data[0] = static_cast<uint16_t>(base + 1 + c);
            data[1] = static_cast<uint16_t>(base + 0x21 + c);
            data[2] = static_cast<uint16_t>(base + c);
            data[3] = static_cast<uint16_t>(base + 1 + c);
            data[4] = static_cast<uint16_t>(base + 0x22 + c);
            data[5] = static_cast<uint16_t>(base + 0x21 + c);
        }
    }

    g_theGxDevicePtr->BufUnlock(this->m_indexBuf, 0);
    this->m_indexBuf->unk1C = 1;
}

// ref: FUN_007e9670
void PassFogCombine::Render() {
    FFX::BeginPass(this->m_target);

    bool verticesValid = this->m_vertexBuf->unk1C && this->m_vertexBuf->unk1D;
    bool indicesValid = this->m_indexBuf->unk1C && this->m_indexBuf->unk1D;

    if (this->m_builtWidth != this->m_target->m_width || this->m_builtHeight != this->m_target->m_height) {
        this->m_builtWidth = this->m_target->m_width;
        this->m_builtHeight = this->m_target->m_height;
        this->BuildVertices(&this->m_target->m_width, this->m_inputs[0], this->m_inputs[1]);
    } else if (!verticesValid) {
        this->BuildVertices(&this->m_target->m_width, this->m_inputs[0], this->m_inputs[1]);
    }

    if (!indicesValid) {
        this->BuildIndices();
    }

    SetTexture(0, this->m_inputs[0]->m_texture);
    SetTexture(1, this->m_inputs[1]->m_texture);

    for (uint32_t stage = 2; stage < 16; stage++) {
        g_theGxDevicePtr->RsSet(static_cast<EGxRenderState>(GxRs_Texture0 + stage), static_cast<void*>(nullptr));
    }

    g_theGxDevicePtr->RsSet(GxRs_PixelShader, this->m_shaders[this->m_shaderIndex]);

    // Coming in, the fog fades up over three seconds.
    float scale;
    float strength;

    if (this->m_fadeIn <= 0.0f) {
        strength = this->m_strength;
        scale = 0.1f;
    } else {
        float fraction = 1.0f - this->m_fadeIn * 0.33333334f;
        float left = this->m_fadeIn - CWorld::GetTickTimeSec();
        this->m_fadeIn = left < 0.0f ? 0.0f : left;
        strength = this->m_strength * fraction;
        scale = fraction * 0.1f;
    }

    float c0[4] = { strength, strength, strength, strength };
    float c1[4] = { scale, scale, scale, scale };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 0, c0, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Pixel, 1, c1, 1);

    GxPrimVertexPtr(this->m_vertexBuf, GxVBF_PCT2);
    g_theGxDevicePtr->PrimIndexPtr(this->m_indexBuf);

    SetBlend(this->m_blend);

    CGxBatch batch = { GxPrim_Triangles, 0, 0x1800, 0, 0x440 };
    g_theGxDevicePtr->Draw(&batch, 1);

    FFX::EndPass();
}

// ref: FUN_007ea5f0
// The full-screen test effect: a noise texture fed row by row into a fog field that rises
// through itself, wrapped around the screen and laid over the scene.
EffectSpecial::EffectSpecial() {
    int32_t noiseSize[2] = { 0x100, 0x100 };
    int32_t fieldSize[2] = { 0x100, 0x80 };

    FFX::CreateTarget(&this->m_noise, noiseSize, false, false);
    FFX::CreateTarget(&this->m_field, fieldSize, false, true);

    TextureSetUpdateCallback(this->m_noise.m_texture, &NoiseTexCallback, nullptr);

    CiRect all = { 0, 0, 0, 0 };
    GxTexUpdate(TextureGetGxTex(this->m_noise.m_texture, 1, nullptr), all, 1);

    this->m_cvar = CVar::Register(
        "ffxSpecial",
        "full screen test effect",
        0x1,
        this->m_defaultOn ? "1" : "0",
        &FFXDeathCallback,
        GRAPHICS
    );

    // The propagation's taps: the three texels below and the one below those (DAT_00d38154).
    static const float s_propagateOffsets[8] = { -1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f, 2.0f };

    FFX::Target* noise[1] = { &this->m_noise };
    FFX::Target* fieldAndScene[2] = { &this->m_field, &FFX::s_targets[FFX::Target_Scene] };

    *this->m_passes.New() = STORM_NEW(PassFogSource)(noise, 1, &this->m_field, 0, false);
    *this->m_passes.New() = STORM_NEW(PassPropagateFog)(fieldAndScene, 1, &this->m_field, s_propagateOffsets, 0, false);
    *this->m_passes.New() = STORM_NEW(PassFogCombine)(fieldAndScene, 2, &FFX::s_targets[FFX::Target_Screen], 0, true);
}

// ref: FUN_007ea850
EffectSpecial::~EffectSpecial() {
    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        if (this->m_passes[i]) {
            delete this->m_passes[i];
        }
    }

    if (this->m_field.m_texture) {
        HandleClose(this->m_field.m_texture);
    }

    this->m_field = FFX::Target();

    if (this->m_noise.m_texture) {
        HandleClose(this->m_noise.m_texture);
    }

    this->m_noise = FFX::Target();
}

// ref: FUN_007e9010
// A ScreenEffect record's three words: the source's tint, the propagation's strength (a byte
// over 255) and the combine's (a percentage), the combine fading in over three seconds.
void EffectSpecial::SetParams(uint32_t count, const uint32_t* params) {
    static_cast<PassFogSource*>(this->m_passes[0])->m_color.value = params[0];
    static_cast<PassPropagateFog*>(this->m_passes[1])->m_strength = static_cast<float>(static_cast<int32_t>(params[1])) * 0.003921569f;

    auto combine = static_cast<PassFogCombine*>(this->m_passes[2]);
    combine->m_strength = static_cast<float>(static_cast<int32_t>(params[2])) * 0.01f;
    combine->m_fadeIn = 3.0f;
}
