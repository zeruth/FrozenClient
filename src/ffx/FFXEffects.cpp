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
