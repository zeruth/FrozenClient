#include "ffx/FFX.hpp"
#include "console/Console.hpp"
#include <cstdlib>
#include "console/CVar.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/texture/CGxTex.hpp"
#include <common/Handle.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Rect.hpp>
#include <cmath>
#include <cstring>

// ref: FUN_008c02a0
bool FFXDeathCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (atol(value)) {
        ConsoleWrite("enabled", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("disabled", DEFAULT_COLOR);

    return true;
}

namespace FFX {

Target s_targets[Targets_Last];
bool s_useRectangle;
CVar* s_ffxCvar;
CVar* s_ffxRectangleCvar;
CVar* s_gxMultisampleCvar;
uint16_t s_quadIndices[4];
C3Vector s_quadPositions[36];
C2Vector s_quadTexCoords[36];
Effect* s_activeEffect;

// What BeginPass found in the device, put back by EndPass (DAT_00b24aa0 and DAT_00b24ae0).
static C44Matrix s_savedProjection;
static C44Matrix s_savedView;

// The quad every pass draws: a four-index strip (DAT_00b24a80).
static CGxBatch s_quadBatch = { GxPrim_TriangleStrip, 0, 4, 0, 3 };

// The six-by-six grid: 25 cells of two triangles over 36 points (DAT_00b24a90).
static CGxBatch s_gridBatch = { GxPrim_Triangles, 0, 150, 0, 35 };

// The grid's indices, built in place before upload (DAT_00d45b18).
static uint16_t s_gridIndices[6 * 6 * 6];

// A position, a colour and a texture coordinate (DAT_00ad88c0).
static CGxVertexAttrib s_coloredFormat[3] = {
    { GxVA_Position,  4, 0x00, 0x18 },
    { GxVA_Color0,    0, 0x0c, 0x18 },
    { GxVA_TexCoord0, 3, 0x10, 0x18 },
};

// A position, a colour and four texture coordinates (DAT_00ad8940).
static CGxVertexAttrib s_quadFormat[6] = {
    { GxVA_Position,  4, 0x00, 0x30 },
    { GxVA_Color0,    0, 0x0c, 0x30 },
    { GxVA_TexCoord0, 3, 0x10, 0x30 },
    { GxVA_TexCoord1, 3, 0x18, 0x30 },
    { GxVA_TexCoord2, 3, 0x20, 0x30 },
    { GxVA_TexCoord3, 3, 0x28, 0x30 },
};

// The targets' textures carry no data of their own: the reference hands TextureCreate its
// one-instruction no-op (FUN_005eeb70).
static void TargetTexCallback(EGxTexCommand, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t&, const void*&) {
}

// ref: FUN_008c0300
Pass::Pass(Target* const* inputs, uint32_t inputCount, Target* target, uint32_t blend, bool flip) {
    for (uint32_t i = 0; i < inputCount; i++) {
        this->m_inputs[i] = inputs[i];
    }

    this->m_target = target;
    this->m_blend = blend;
    this->m_flip = flip;
}

// ref: FUN_007e8080
Pass::~Pass() {
}

// ref: FUN_008c1870
Effect::Effect() {
}

// ref: FUN_007ea220
// The base frees the pass list's storage and nothing in it; an effect deletes its own passes.
Effect::~Effect() {
}

// ref: FUN_008c1270
// On when every pass is ready and the effect's switch is on.
bool Effect::IsEnabled() {
    if (!this->m_passes.Count()) {
        return false;
    }

    bool valid = true;

    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        valid = valid && this->m_passes[i]->IsValid();
    }

    // An effect without a switch is on whenever its passes are.
    if (!this->m_cvar) {
        return valid;
    }

    return valid && this->m_cvar->m_intValue;
}

// ref: FUN_008c12c0
void Effect::Render() {
    for (uint32_t i = 0; i < this->m_passes.Count(); i++) {
        this->m_passes[i]->Render();
    }
}

// The base's two remaining slots are the reference's shared no-ops (FUN_005eeb70, FUN_00653a10).
void Effect::Deactivate() {
}

void Effect::SetParams(uint32_t count, const uint32_t* params) {
}

// ref: FUN_008c12f0
void Init() {
    FFX::s_ffxCvar = CVar::Register(
        "ffx",
        "full screen effects",
        0x1,
        "1",
        &FFXDeathCallback,
        GRAPHICS
    );

    FFX::s_ffxRectangleCvar = CVar::Register(
        "ffxRectangle",
        "use rectangle texture for full screen effects",
        0x1,
        "1",
        &FFXDeathCallback,
        GRAPHICS
    );

    FFX::s_gxMultisampleCvar = CVar::Lookup("gxMultisample");

    for (auto& target : FFX::s_targets) {
        target = Target();
    }

    FFX::s_quadIndices[0] = 0;
    FFX::s_quadIndices[1] = 3;
    FFX::s_quadIndices[2] = 1;
    FFX::s_quadIndices[3] = 2;

    FFX::s_activeEffect = nullptr;

    FFX::s_useRectangle = FFX::TargetKind() == GxTex_Rectangle;
}

// ref: FUN_008c0360
void ReleaseTargets() {
    for (auto& target : FFX::s_targets) {
        if (target.m_texture) {
            HandleClose(target.m_texture);
        }

        target = Target();
    }
}

// ref: FUN_008c10b0
EGxTexTarget TargetKind() {
    if (FFX::s_ffxRectangleCvar->m_intValue && !GxCaps().m_texNonPow2Conditional) {
        if (GxCaps().m_texTarget[GxTex_NonPow2]) {
            return GxTex_NonPow2;
        }

        return GxCaps().m_texTarget[GxTex_Rectangle] ? GxTex_Rectangle : GxTex_2d;
    }

    return GxTex_2d;
}

// ref: FUN_008c1100
void CreateTarget(Target* target, const int32_t* size, bool bit8, bool renderTarget) {
    // The first colour format the device can render to: ARGB, then ABGR, then 565.
    EGxTexFormat format;

    if (GxCaps().m_texFmtRtt[GxTex_Argb8888]) {
        format = GxTex_Argb8888;
    } else if (GxCaps().m_texFmtRtt[GxTex_Abgr8888]) {
        format = GxTex_Abgr8888;
    } else {
        format = GxCaps().m_texFmtRtt[GxTex_Rgb565] ? GxTex_Rgb565 : GxTex_Unknown;
    }

    CGxTexFlags flags = CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, renderTarget, 1, bit8);

    EGxTexTarget kind = FFX::TargetKind();

    int32_t texWidth = size[0];
    int32_t texHeight = size[1];

    if (kind == GxTex_2d) {
        texHeight = 1;

        while (texHeight < size[1]) {
            texHeight *= 2;
        }

        texWidth = 1;

        while (texWidth < size[0]) {
            texWidth *= 2;
        }
    }

    int32_t maxSize = static_cast<int32_t>(GxCaps().m_texMaxSize[kind]);

    if (maxSize <= texHeight) {
        texHeight = maxSize;
    }

    if (maxSize <= texWidth) {
        texWidth = maxSize;
    }

    if (texHeight < 9) {
        texHeight = 8;
    }

    if (texWidth < 9) {
        texWidth = 8;
    }

    if (texWidth != target->m_texWidth || texHeight != target->m_texHeight) {
        if (target->m_texture) {
            HandleClose(target->m_texture);
            target->m_texture = nullptr;
        }

        target->m_texture = TextureCreate(
            kind,
            texWidth,
            texHeight,
            0,
            format,
            format,
            flags,
            nullptr,
            &TargetTexCallback,
            "FFX::CreateTex",
            0
        );
    }

    target->m_width = size[0];
    target->m_texWidth = texWidth;
    target->m_texHeight = texHeight;
    target->m_height = size[1];
    target->m_invTexWidth = 1.0f / static_cast<float>(target->m_texWidth);
    target->m_invTexHeight = 1.0f / static_cast<float>(target->m_texHeight);
}

// ref: FUN_008c15f0
void UpdateTargets() {
    CRect window = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(window);

    int32_t width = static_cast<int32_t>(window.maxX - window.minX);
    int32_t height = static_cast<int32_t>(window.maxY - window.minY);

    auto& screen = FFX::s_targets[Target_Screen];
    screen.m_texWidth = width;
    screen.m_texHeight = height;
    screen.m_width = width;
    screen.m_height = height;
    screen.m_invTexWidth = 1.0f / static_cast<float>(width);
    screen.m_invTexHeight = 1.0f / static_cast<float>(height);

    int32_t maxSize = static_cast<int32_t>(GxCaps().m_texMaxSize[FFX::TargetKind()]);

    int32_t sceneHeight = maxSize <= height ? maxSize : height;
    int32_t sceneWidth = width < maxSize ? width : maxSize;

    if (sceneHeight < 9) {
        sceneHeight = 8;
    }

    if (sceneWidth < 9) {
        sceneWidth = 8;
    }

    int32_t size[2] = { sceneWidth, sceneHeight };
    FFX::CreateTarget(&FFX::s_targets[Target_Scene], size, true, true);
    FFX::CreateTarget(&FFX::s_targets[Target_Full], size, false, true);

    size[0] = sceneWidth / 2;
    size[1] = sceneHeight / 2;
    FFX::CreateTarget(&FFX::s_targets[Target_Half0], size, false, true);
    FFX::CreateTarget(&FFX::s_targets[Target_Half1], size, false, true);

    size[0] = sceneWidth / 4;
    size[1] = sceneHeight / 4;
    FFX::CreateTarget(&FFX::s_targets[Target_Quarter0], size, false, true);
    FFX::CreateTarget(&FFX::s_targets[Target_Quarter1], size, false, true);
}

// ref: FUN_008c02e0
void SetEffect(Effect* effect) {
    Effect* previous = FFX::s_activeEffect;
    FFX::s_activeEffect = effect;

    if (previous) {
        previous->Deactivate();
    }
}

// ref: FUN_008c1770
void BeginScene() {
    if (!FFX::s_ffxCvar->m_intValue || !FFX::s_activeEffect || !FFX::s_activeEffect->IsEnabled()) {
        return;
    }

    CRect window = { 0.0f, 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->CapsWindowSize(window);

    int32_t width = static_cast<int32_t>(window.maxX - window.minX);
    int32_t height = static_cast<int32_t>(window.maxY - window.minY);

    if (width != FFX::s_targets[Target_Screen].m_width || height != FFX::s_targets[Target_Screen].m_height) {
        FFX::UpdateTargets();
    }

    float maxX = 1.0f;
    float maxY = 1.0f;

    const auto& scene = FFX::s_targets[Target_Scene];

    if (static_cast<float>(scene.m_width) < window.maxX - window.minX) {
        maxX = static_cast<float>(scene.m_width) / (window.maxX - window.minX);
    }

    if (static_cast<float>(scene.m_height) < window.maxY - window.minY) {
        maxY = static_cast<float>(scene.m_height) / (window.maxY - window.minY);
    }

    GxXformSetViewport(0.0f, maxX, 0.0f, maxY, 0.0f, 1.0f);
}

// ref: FUN_008c1010
void EndScene() {
    if (!FFX::s_ffxCvar->m_intValue || !FFX::s_activeEffect || !FFX::s_activeEffect->IsEnabled()) {
        return;
    }

    const auto& scene = FFX::s_targets[Target_Scene];

    C2iVector dstPos = { 0, 0 };
    C2iVector srcPos = { 0, 0 };
    C2iVector size = { scene.m_width, scene.m_height };

    CGxTex* texture = TextureGetGxTex(scene.m_texture, 1, nullptr);
    GxTexCopyFromTarget(texture, dstPos, srcPos, size, 0, 0);

    FFX::s_activeEffect->Render();

    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
}

// ref: FUN_008c1890
void BeginPass(Target* target) {
    FFX::s_savedProjection = g_theGxDevicePtr->m_projection;

    C44Matrix ortho;
    GxuXformCreateOrtho(0.0f, static_cast<float>(target->m_width), 0.0f, static_cast<float>(target->m_height), -1.0f, 1.0f, ortho);
    g_theGxDevicePtr->XformSetProjection(ortho);

    FFX::s_savedView = g_theGxDevicePtr->m_xforms[GxXform_View].Top();

    C44Matrix identity;
    g_theGxDevicePtr->XformSetView(identity);

    g_theGxDevicePtr->XformPush(GxXform_World);
    g_theGxDevicePtr->m_xforms[GxXform_World].SetIdentity();

    GxRsPush();
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Fog, 0);

    CGxTex* texture = target->m_texture ? TextureGetGxTex(target->m_texture, 1, nullptr) : nullptr;

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, texture, 0);

    GxXformSetViewport(
        0.0f,
        static_cast<float>(target->m_width) / static_cast<float>(target->m_texWidth),
        0.0f,
        static_cast<float>(target->m_height) / static_cast<float>(target->m_texHeight),
        0.0f,
        1.0f
    );
}

// ref: FUN_008c0290
const C44Matrix& SavedView() {
    return FFX::s_savedView;
}

// ref: FUN_008c1520
void EndPass() {
    GxRsPop();

    g_theGxDevicePtr->XformSetView(FFX::s_savedView);
    g_theGxDevicePtr->XformSetProjection(FFX::s_savedProjection);
    g_theGxDevicePtr->XformPop(GxXform_World);

    GxRsSet(GxRs_ScissorTest, 0);
    g_theGxDevicePtr->RenderTargetSet(GxBuffers_Color, nullptr, 0);

    GxXformSetViewport(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
}

// ref: FUN_008c0590
// The quad in the target's pixels, and where its corners fall in the source's texture. D3D9
// samples texel centres, so the texture coordinates are moved half a texel in; the GL paths move
// the quad itself by three eighths of a pixel instead.
void QuadCoords(const int32_t* targetSize, const int32_t* sourceSize, const int32_t* sourceTexSize,
                C3Vector* positions, C2Vector* texCoords, bool flip) {
    float maxY = static_cast<float>(targetSize[1]);
    float maxX = static_cast<float>(targetSize[0]);
    float minY;
    float minX;

    EGxApi api = GxDevApi();

    if (api == GxApi_OpenGl) {
        maxY = maxY - 0.375f;
        maxX = maxX + 0.375f;
        minY = -0.375f;
        minX = 0.375f;

        if (flip) {
            minY = 1.0f + maxY;
            maxY = 1.0f - 0.375f;
        }
    } else if (api == GxApi_GLL) {
        maxY = maxY - 0.375f;
        maxX = maxX + 0.375f;
        minY = -0.375f;
        minX = 0.375f;
    } else {
        minY = 0.0f;
        minX = 0.0f;
    }

    positions[0] = { minX, minY, 0.0f };
    positions[1] = { minX, maxY, 0.0f };
    positions[2] = { maxX, maxY, 0.0f };
    positions[3] = { maxX, minY, 0.0f };

    float u0 = (1.0f / static_cast<float>(sourceTexSize[0])) * 0.5f;
    float v0 = (1.0f / static_cast<float>(sourceTexSize[1])) * 0.5f;
    float u1 = (1.0f / static_cast<float>(sourceTexSize[0])) * (static_cast<float>(sourceSize[0]) + 0.5f);
    float v1 = (1.0f / static_cast<float>(sourceTexSize[1])) * (static_cast<float>(sourceSize[1]) + 0.5f);

    texCoords[0] = { u0, v1 };
    texCoords[1] = { u0, v0 };
    texCoords[2] = { u1, v0 };
    texCoords[3] = { u1, v1 };
}

// ref: FUN_008c0c90
void StreamQuad(const C3Vector* positions, uint32_t color, const C2Vector* texCoords,
                const float* offsets, const float* invTexSize) {
    CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x30, 4);
    auto data = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));

    for (uint32_t v = 0; v < 4; v++) {
        data[0] = positions[v].x;
        data[1] = positions[v].y;
        data[2] = positions[v].z;
        reinterpret_cast<uint32_t*>(data)[3] = color;

        for (uint32_t k = 0; k < 4; k++) {
            data[4 + k * 2] = offsets[k * 2] * invTexSize[0] + texCoords[v].x;
            data[5 + k * 2] = offsets[k * 2 + 1] * invTexSize[1] + texCoords[v].y;
        }

        data += 12;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;

    g_theGxDevicePtr->PrimVertexFormat(buf, FFX::s_quadFormat, 6);
}

// ref: FUN_008c0ec0
void QuadIndex() {
    CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, 4);
    GxBufData(buf, FFX::s_quadIndices, 8, 0);
    g_theGxDevicePtr->PrimIndexPtr(buf);
}

// The quad's batch, for the passes' draws.
CGxBatch* QuadBatch() {
    return &FFX::s_quadBatch;
}

CGxBatch* GridBatch() {
    return &FFX::s_gridBatch;
}

// ref: FUN_008c0740
void GridCoords(const int32_t* targetSize, const int32_t* sourceSize, const int32_t* sourceTexSize,
                C3Vector* positions, C2Vector* texCoords, int32_t n, bool flip) {
    float maxY = static_cast<float>(targetSize[1]);
    float maxX = static_cast<float>(targetSize[0]);
    float minX = 0.0f;
    float minY;

    float invWidth = 1.0f / std::fabs(static_cast<float>(targetSize[0]));
    float invHeight = 1.0f / std::fabs(static_cast<float>(targetSize[1]));
    float clipX = invWidth * 2.0f;
    float clipY = invHeight * 2.0f;

    EGxApi api = GxDevApi();

    if (api == GxApi_OpenGl) {
        minX = 0.5f;
        float shifted = maxY - 0.5f;
        maxX = maxX + 0.5f;
        maxY = shifted;
        minY = -0.5f;

        if (flip) {
            maxY = 1.0f - 0.5f;
            minY = 1.0f + shifted;
        }
    } else if (api == GxApi_GLL) {
        minX = 0.375f;
        maxY = maxY - 0.375f;
        maxX = maxX + 0.375f;
        minY = -0.375f;
    } else {
        minY = 0.0f;
    }

    float u0 = (1.0f / static_cast<float>(sourceTexSize[0])) * 0.5f;
    float v = (1.0f / static_cast<float>(sourceTexSize[1])) * (static_cast<float>(sourceSize[1]) + 0.5f);
    float step = 1.0f / static_cast<float>(n - 1);
    float dx = (maxX - minX) * step;
    float dyTotal = maxY - minY;
    float du = ((1.0f / static_cast<float>(sourceTexSize[0])) * (static_cast<float>(sourceSize[0]) + 0.5f) - u0) * step;
    float dvTotal = (1.0f / static_cast<float>(sourceTexSize[1])) * 0.5f - v;

    float y = minY;
    int32_t index = 0;

    for (int32_t row = 0; row < n; row++) {
        float x = minX;
        float u = u0;
        float clipRow = 1.0f - y * clipY;

        for (int32_t column = 0; column < n; column++, index++) {
            positions[index] = { x * clipX - 1.0f, clipRow, 0.0f };

            if (!FFX::s_useRectangle) {
                texCoords[index] = { u, v };
            } else {
                texCoords[index] = {
                    static_cast<float>(sourceTexSize[0]) * x * invWidth,
                    (1.0f - y * invHeight) * static_cast<float>(sourceTexSize[1])
                };
            }

            x += dx;
            u += du;
        }

        y += dyTotal * step;
        v += dvTotal * step;
    }
}

// ref: FUN_008c0de0
void StreamColored(uint32_t count, const C3Vector* positions, const C2Vector* texCoords, const float* values) {
    CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, count);
    auto data = reinterpret_cast<uint8_t*>(g_theGxDevicePtr->BufLock(buf));

    for (uint32_t i = 0; i < count; i++, data += 0x18) {
        memcpy(data, &positions[i], sizeof(C3Vector));

        uint8_t grey = static_cast<uint8_t>(lrintf((values[i] + 1.0f) * 127.5f));
        data[0xc] = grey;
        data[0xd] = grey;
        data[0xe] = grey;
        data[0xf] = grey;

        memcpy(data + 0x10, &texCoords[i], sizeof(C2Vector));
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;

    g_theGxDevicePtr->PrimVertexFormat(buf, FFX::s_coloredFormat, 3);
}

// ref: FUN_008c0f00
void GridIndex(int32_t columns, int32_t rows) {
    int32_t cells = (columns - 1) * (rows - 1);
    CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, cells * 6);

    int32_t index = 0;
    int32_t base = 0;

    for (int32_t row = 0; row < rows - 1; row++, base += columns) {
        for (int32_t column = 0; column < columns - 1; column++, index += 6) {
            auto corner = static_cast<uint16_t>(base + column);
            auto below = static_cast<uint16_t>(base + columns + column);

            FFX::s_gridIndices[index + 0] = corner;
            FFX::s_gridIndices[index + 1] = static_cast<uint16_t>(below + 1);
            FFX::s_gridIndices[index + 2] = below;
            FFX::s_gridIndices[index + 3] = corner;
            FFX::s_gridIndices[index + 4] = static_cast<uint16_t>(corner + 1);
            FFX::s_gridIndices[index + 5] = static_cast<uint16_t>(below + 1);
        }
    }

    GxBufData(buf, FFX::s_gridIndices, cells * 12, 0);
    g_theGxDevicePtr->PrimIndexPtr(buf);
}

}
