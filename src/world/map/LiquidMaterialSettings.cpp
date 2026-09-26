#include "world/map/LiquidMaterialSettings.hpp"
#include "db/Db.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Device.hpp"
#include "gx/Shader.hpp"
#include "gx/RenderState.hpp"
#include <cstring>
#include "gx/Transform.hpp"
#include "model/CM2Lighting.hpp"
#include "world/map/LiquidSurface.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Buffer.hpp"
#include <cmath>
#include <common/Time.hpp>
#include "gx/shader/CGxShader.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Gx.hpp"
#include "util/Log.hpp"
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <common/Handle.hpp>
#include "gx/texture/CGxTex.hpp"
#include <tempest/Vector.hpp>
#include <new>

namespace Liquid {

// The bank, indexed by LiquidType.dbc id. Sparse: only the types the map has asked for are
// built, and the slots between them stay null. DAT_00d43b1c / DAT_00d43b18
static TSGrowableArray<CMaterialSettings*> s_settingsBank;

// ref: FUN_008a27c0
bool CMaterialSettings::LoadFromDbc(int32_t liquidType) {
    auto typeRec = g_liquidTypeDB.GetRecord(liquidType);

    if (!typeRec) {
        return false;
    }

    auto materialRec = g_liquidMaterialDB.GetRecord(typeRec->m_materialID);

    if (!materialRec) {
        return false;
    }

    for (uint32_t i = 0; i < TEXTURE_SLOTS; i++) {
        SStrCopy(this->m_textureName[i], typeRec->m_texture[i], TEXTURE_NAME_SIZE);
    }

    this->m_color[0] = typeRec->m_color[0];
    this->m_color[1] = typeRec->m_color[1];

    for (uint32_t i = 0; i < 4; i++) {
        this->m_int[i] = typeRec->m_int[i];
    }

    // LiquidType's eighteen floats are two stages of nine, laid end to end.
    for (uint32_t stage = 0; stage < STAGE_COUNT; stage++) {
        for (uint32_t i = 0; i < STAGE_FLOATS; i++) {
            this->m_stage[stage][i] = typeRec->m_float[stage * STAGE_FLOATS + i];
        }
    }

    this->m_procedural = materialRec->m_flags & 1;

    this->LoadTextures();

    return true;
}

// How far a numbered animation is followed before the loader gives up. The reference stops at
// thirty, whether or not the files keep going.
static const uint32_t MAX_FRAMES = 30;

// ref: FUN_008a2450
// Procedural water is generated rather than read, so it is sampled without mipmaps and clamped;
// everything else is an ordinary wrapped, trilinear texture.
void CMaterialSettings::LoadTextures() {
    for (uint32_t slot = 0; slot < TEXTURE_SLOTS; slot++) {
        const char* name = this->m_textureName[slot];

        this->m_resident[slot] = 0;

        if (!name[0]) {
            continue;
        }

        bool procedural = SStrStrI(name, "procedural") != nullptr;

        CGxTexFlags flags(procedural ? GxTex_Linear : GxTex_LinearMipLinear,
                          !procedural, !procedural, 0, 0, 0, 1);

        CStatus status;

        if (!SStrStrI(name, "%d")) {
            HTEXTURE texture = nullptr;

            if (procedural) {
                // TODO FUN_004b6f30: the generated texture, looked up by name hash rather than
                // read off disk. Without it the still falls through to the solid below, which
                // is what the reference does only when the generator has nothing for the name.
                CImVector green = { 0, 0xff, 0, 0xff };

                texture = TextureCreateSolid(green);
            } else {
                texture = TextureCreate(name, flags, &status, 0);
            }

            if (texture) {
                this->m_frames[slot].Add(1, &texture);
            }

            continue;
        }

        // An animation: the name is a pattern, and the frames are numbered from one.
        bool any = false;

        for (uint32_t frame = 1; frame < MAX_FRAMES + 1; frame++) {
            char path[CMaterialSettings::TEXTURE_NAME_SIZE];
            SStrPrintf(path, sizeof(path), name, frame);

            HTEXTURE texture = TextureCreate(path, flags, &status, 0);

            this->m_frames[slot].Add(1, &texture);

            if (TextureHasPendingData(texture)) {
                any = true;
            }
        }

        // TODO the second set, FUN_004b8d70 over the same names, kept only when it comes out the
        // same length as the first. Which loader that is, and so what the second set is for, is
        // not established, so m_framesAlt stays empty.
        (void)any;
    }
}

// ref: FUN_008a1d60
// Which frame of a slot's animation is showing, and the thing that makes water move.
//
// Two jobs in one, and the second is the reason for m_framesAlt. Until every frame of the slot has
// arrived the pick comes out of the stand-in set, and each still-pending frame gets its streaming
// priority raised on the way past; once they have all landed the flag latches, the stand-ins are
// closed, and every later call reads the real set directly.
CGxTex* CMaterialSettings::GetFrame(uint32_t slot, uint32_t periodMs) {
    uint32_t count = this->m_frames[slot].Count();

    if (!count) {
        return nullptr;
    }

    // A single still needs none of the machinery below, not even a resident check.
    if (count == 1) {
        return TextureGetGxTex(this->m_frames[slot][0], 0, nullptr);
    }

    TSGrowableArray<HTEXTURE>& frames = this->m_frames[slot];
    TSGrowableArray<HTEXTURE>* pick = &frames;

    if (!this->m_resident[slot]) {
        if (!this->m_framesAlt[slot].Count()) {
            // No stand-ins: the slot simply does not draw until every frame has a GxTex.
            for (uint32_t i = 0; i < count; i++) {
                if (!TextureGetGxTex(frames[i], 0, nullptr)) {
                    return nullptr;
                }
            }

            this->m_resident[slot] = 1;
        } else {
            bool ready = true;

            for (uint32_t i = 0; i < count; i++) {
                if (TextureHasPendingData(frames[i])) {
                    // Ask for it sooner, and show a stand-in this frame.
                    TextureIncreasePriority(TextureGetTexturePtr(frames[i]));

                    ready = false;
                    break;
                }
            }

            if (!ready) {
                pick = &this->m_framesAlt[slot];
            } else {
                this->m_resident[slot] = 1;

                for (uint32_t i = 0; i < this->m_framesAlt[slot].Count(); i++) {
                    HandleClose(this->m_framesAlt[slot][i]);
                }

                this->m_framesAlt[slot].SetCount(0);
            }
        }
    }

    if (!periodMs) {
        periodMs = 1;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    float phase = static_cast<float>(now % periodMs) / static_cast<float>(periodMs);

    // The reference writes this as round(count * phase - 0.5), and nearbyintf is the same
    // half-to-even rounding the x87 ROUND it compiles to uses -- so this is exact, not a
    // simplification to floor. phase < 1 keeps the result inside 0 .. count-1 without a clamp.
    int32_t frame = static_cast<int32_t>(nearbyintf(static_cast<float>(count) * phase - 0.5f));

    return TextureGetGxTex((*pick)[frame], 1, nullptr);
}

// ref: FUN_008a28f0
// Asking twice for the same type gives the same record. A type the DBC does not carry is
// reported once and retried as water, which every map has.
CMaterialSettings* GetMaterialSettings(int32_t liquidType) {
    while (true) {
        if (static_cast<uint32_t>(liquidType) < s_settingsBank.Count()
            && s_settingsBank[liquidType]) {
            return s_settingsBank[liquidType];
        }

        auto settings = static_cast<CMaterialSettings*>(
            SMemAlloc(sizeof(CMaterialSettings), __FILE__, __LINE__, 0x0));

        if (settings) {
            new (settings) CMaterialSettings();
        }

        if (settings && settings->LoadFromDbc(liquidType)) {
            s_settingsBank.GrowToFit(liquidType, 1);
            s_settingsBank[liquidType] = settings;

            return settings;
        }

        if (settings) {
            SMemFree(settings, __FILE__, __LINE__, 0x0);
        }

        SysMsgPrintf(SYSMSG_ERROR, "Settings Bank: Liquid type [%d] not found, defaulting to water!", liquidType);

        // DIVERGENCE, and a deliberate one. The reference loops back to water rather than
        // recursing, on the assumption that a water row always exists -- and if it does not, it
        // spins forever printing this line. That is not hypothetical: it happened here, and a
        // single run wrote ten million copies of this message before the client died. Frozen
        // gives up instead and lets the caller cope with a null.
        if (liquidType == 1) {
            return nullptr;
        }

        liquidType = 1;
    }
}

// The materials, indexed by LiquidMaterial.dbc id rather than by liquid type: every kind of
// water shares one material. DAT_00d43b2c / DAT_00d43b28
static TSGrowableArray<IMaterial*> s_materialBank;

// The shader pairs, one set a material class, loaded once each. The reference keeps them as loose
// globals and guards each with its own instance counter; a static here does the same job.
namespace {

// The procedural water shaders take a suffix. The reference is given one from outside the module
// (a setter at 0x008a1770 copies it into 0x00d439f0 along with a float and four counters); nothing
// in frozen calls that, so the names come out unsuffixed.
// TODO identify the caller and what it passes.
const char* ProcWaterSuffix() {
    return "";
}

void LoadPair(CGxShader** vertex, int32_t vertexCount, const char* vertexName,
              CGxShader** pixel, int32_t pixelCount, const char* pixelName) {
    g_theGxDevicePtr->ShaderCreate(vertex, GxSh_Vertex, "Shaders\\Vertex", vertexName, vertexCount);
    g_theGxDevicePtr->ShaderCreate(pixel, GxSh_Pixel, "Shaders\\Pixel", pixelName, pixelCount);
}

CGxShader* s_vsWater[4];
CGxShader* s_psWater[1];
CGxShader* s_vsWaterNoSpec[4];
CGxShader* s_psWaterNoSpec[1];
CGxShader* s_vsMagma[1];
CGxShader* s_psMagma[1];
CGxShader* s_vsProcWater[4];
CGxShader* s_psProcWater[1];

}

// ------------------------------------------------------------------------------------------------
// The shader constant block
//
// The reference keeps these as loose globals and uploads four ranges out of them. Laying them out
// by REGISTER instead makes the map checkable: a register is (address - base) / 16.
//
//   vertex  c0..c3    the projection                      0x00d44ca8
//   vertex  c5..c8    the world-view                      0x00d44cf8
//   vertex  c9,13,17,21  four texture matrices            0x00d44d38 onwards, 0x40 apart
//   vertex  c25..c28  a scale from stage float 8          0x00d44e38
//   vertex  c29..c32  RotationAroundZ(f10) * Scale(f9)    0x00d44e78
//   vertex  c33..c45  the lighting, from FUN_008a38b0     0x00d44eb8   NOT FILLED YET
//   vertex  c46..c57  from FUN_008a3620/3710/3810         0x00d44f88   NOT FILLED YET
//   pixel   c0..c3    the model-view-projection           0x00b24120
//   pixel   c5        the camera position, w = 1           0x00b24170
//   pixel   c6..c11   the sun terms, from FUN_008a3c90     0x00d44c48   NOT FILLED YET
//
// The three unfilled ranges are the lighting and the wave animation. They are left zeroed rather
// than guessed, so this draws water with the right geometry, textures and transform and flat
// shading; the shape of each is in docs/ref/parity-liquid.md.

namespace {

const uint32_t VS_REGISTERS = 58;
const uint32_t PS_REGISTERS = 6;

const uint32_t VS_PROJECTION = 0;
const uint32_t VS_WORLD_VIEW = 5;
const uint32_t VS_TEX_MATRIX = 9;
const uint32_t VS_SCALE = 25;
const uint32_t VS_ROT_SCALE = 29;
const uint32_t VS_SPLIT = 46;        // where the second vertex upload starts

const uint32_t PS_MVP = 0;
const uint32_t PS_CAMERA = 5;

struct Constants {
    C4Vector vs[VS_REGISTERS];
    C4Vector psMvp[PS_REGISTERS];
    C4Vector psSun[PS_REGISTERS];
};

Constants s_constants;

// Straight copy, NOT transposed. frozen's own terrain and WMO shaders take transposed matrices,
// but these are the reference's shaders out of the archives, and the reference stores each matrix
// into this block with a plain sixteen-dword copy. Matching the shader, not the house style.
void StoreMatrix(C4Vector* dst, const C44Matrix& m) {
    memcpy(dst, &m, sizeof(C44Matrix));
}

// ref: FUN_008a32f0
// The projection, the world-view and the combined transform.
//
// The world-view is the placement with the camera taken out of its translation, times the device's
// VIEW matrix -- and that composes correctly here rather than double-counting the camera, because
// frozen's CCamera builds its view matrix with the camera at the origin, which is the same
// rotation-only convention the reference uses.
//
// DIVERGED: the reference negates the projection's third row when the device's flag at +0x1b4 is
// clear -- a depth-range convention -- and frozen's CGxDevice has no such flag. The negation is
// skipped. If water ends up at the wrong depth or fighting the terrain, this is the one knob.
void SetupTransforms(const C3Vector& cameraPos, const C44Matrix& placement) {
    C44Matrix view;
    g_theGxDevicePtr->XformView(view);

    C44Matrix local = placement;

    local.d0 -= cameraPos.x;
    local.d1 -= cameraPos.y;
    local.d2 -= cameraPos.z;

    C44Matrix worldView = local * view;

    StoreMatrix(&s_constants.vs[VS_WORLD_VIEW], worldView);

    C44Matrix projection;
    g_theGxDevicePtr->XformProjection(projection);

    StoreMatrix(&s_constants.vs[VS_PROJECTION], projection);
    StoreMatrix(&s_constants.psMvp[PS_MVP], worldView * projection);

    s_constants.psMvp[PS_CAMERA].x = cameraPos.x;
    s_constants.psMvp[PS_CAMERA].y = cameraPos.y;
    s_constants.psMvp[PS_CAMERA].z = cameraPos.z;
    s_constants.psMvp[PS_CAMERA].w = 1.0f;
}

// Part of ref: FUN_008a48f0 -- the shared body behind all four shader materials, which differ only
// in the shader pair they hand it.
void DrawShaderMaterial(CGxShader** vertexShaders, CGxShader** pixelShaders,
                        CClientEnvironment* environment, CChunkGeomFactory* geometry,
                        const C3Vector& cameraPos, const C44Matrix* placement,
                        const CAaSphere* sphere, CMaterialSettings* settings) {
    // LIQCOUNT: one counter per rejection point, so a run says which gate the water dies at rather
    // than just "no water". Remove with the lighting constants.
    static uint32_t s_calls = 0;
    static uint32_t s_noInputs = 0;
    static uint32_t s_noFrame = 0;
    static uint32_t s_noGeometry = 0;
    static uint32_t s_drawn = 0;
    static uint32_t s_lastIndices = 0;
    static uint32_t s_lastPerm = 0;
    static uint32_t s_ticks = 0;

    s_calls++;

    if (++s_ticks >= 120) {
        s_ticks = 0;

        SysMsgPrintf(SYSMSG_INFO,
                     "LIQDRAW: calls=%u noInputs=%u noFrame=%u noGeom=%u drawn=%u "
                     "lastIndices=%u lastPerm=%u",
                     s_calls, s_noInputs, s_noFrame, s_noGeometry, s_drawn, s_lastIndices,
                     s_lastPerm);
    }

    if (!geometry || !settings || !environment) {
        s_noInputs++;

        return;
    }

    // Six animation frames. 1250 ms for slots 0, 1 and 4; the other three take their period from
    // m_int. A slot with nothing resolved aborts the whole draw, before any state is pushed.
    CGxTex* frames[CMaterialSettings::TEXTURE_SLOTS];

    static const uint32_t DEFAULT_PERIOD = 1250;

    frames[0] = settings->GetFrame(0, DEFAULT_PERIOD);
    frames[1] = settings->GetFrame(1, DEFAULT_PERIOD);
    frames[2] = settings->GetFrame(2, static_cast<uint32_t>(settings->GetInt(1)));
    frames[3] = settings->GetFrame(3, static_cast<uint32_t>(settings->GetInt(2)));
    frames[4] = settings->GetFrame(4, DEFAULT_PERIOD);
    frames[5] = settings->GetFrame(5, static_cast<uint32_t>(settings->GetInt(3)));

    for (uint32_t i = 0; i < CMaterialSettings::TEXTURE_SLOTS; i++) {
        if (!frames[i]) {
            s_noFrame++;

            return;
        }
    }

    // The stage floats. The first four are scales and the next four angles; the multiplier the
    // reference applies to the scales is a global that rests at 1.0, and the one it applies to the
    // angles is 180/pi, so those four are radians on their way to degrees.
    static const float ANGLE_SCALE = 57.295780181884766f;

    float texScale[4];
    float texAngle[4];

    for (uint32_t i = 0; i < 4; i++) {
        texScale[i] = settings->GetStageFloat(i);
        texAngle[i] = settings->GetStageFloat(i + 4) * ANGLE_SCALE;
    }

    float scaleY = settings->GetStageFloat(8);
    float extraScale = settings->GetStageFloat(9);
    float extraAngle = settings->GetStageFloat(10) * ANGLE_SCALE;

    GxRsPush();

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;

    if (!geometry->Build(GxVBF_PT2, &vertexBuf, &indexBuf, &batch)) {
        s_noGeometry++;

        GxRsPop();

        return;
    }

    GxPrimVertexPtr(vertexBuf, GxVBF_PT2);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    // The six frames do NOT go to the stages in slot order.
    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(frames[0]));
    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<void*>(frames[1]));
    g_theGxDevicePtr->RsSet(GxRs_Texture2, static_cast<void*>(frames[4]));
    g_theGxDevicePtr->RsSet(GxRs_Texture3, static_cast<void*>(frames[5]));
    g_theGxDevicePtr->RsSet(GxRs_Texture4, static_cast<void*>(frames[2]));
    g_theGxDevicePtr->RsSet(GxRs_Texture5, static_cast<void*>(frames[3]));

    SetupTransforms(cameraPos, *placement);

    // The lighting block the environment fills. Its own constants are not written yet, but building
    // it is what selects the shader permutation below.
    CM2Lighting lighting;
    lighting.Initialize(nullptr, *sphere);

    environment->SetupLighting(&lighting);

    // Four texture matrices, one per stage float pair.
    for (uint32_t i = 0; i < 4; i++) {
        C44Matrix m = C44Matrix::RotationAroundZ(texAngle[i]);

        m.Scale(texScale[i]);

        StoreMatrix(&s_constants.vs[VS_TEX_MATRIX + i * 4], m);
    }

    // Then a plain scale, and a rotate-and-scale.
    C44Matrix scale(1.0f);

    scale.b1 = scaleY;

    StoreMatrix(&s_constants.vs[VS_SCALE], scale);

    C44Matrix rotScale = C44Matrix::RotationAroundZ(extraAngle);

    rotScale.Scale(extraScale);

    StoreMatrix(&s_constants.vs[VS_ROT_SCALE], rotScale);

    // Four uploads, two ranges per target.
    GxShaderConstantsSet(GxSh_Vertex, 0,
                         reinterpret_cast<const float*>(&s_constants.vs[0]), VS_SPLIT);
    GxShaderConstantsSet(GxSh_Pixel, 0,
                         reinterpret_cast<const float*>(&s_constants.psMvp[0]), PS_REGISTERS);
    GxShaderConstantsSet(GxSh_Vertex, VS_SPLIT,
                         reinterpret_cast<const float*>(&s_constants.vs[VS_SPLIT]),
                         VS_REGISTERS - VS_SPLIT);
    GxShaderConstantsSet(GxSh_Pixel, PS_REGISTERS,
                         reinterpret_cast<const float*>(&s_constants.psSun[0]), PS_REGISTERS);

    // The vertex permutation is the local light count, clamped to three -- which is why every one
    // of these materials loads four vertex programs and one pixel program.
    uint32_t permutation = lighting.m_lightCount;

    if (permutation > 2) {
        permutation = 3;
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<void*>(vertexShaders[permutation]));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<void*>(pixelShaders[0]));

    g_theGxDevicePtr->Draw(&batch, 1);

    s_drawn++;
    s_lastIndices = batch.m_count;
    s_lastPerm = permutation;

    GxRsPop();
}

}

// ref: FUN_008a3f70
void CMaterialWater::EnsureShaders() {
    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsWater, 4, "vsLiquidWater", s_psWater, 1, "psLiquidWater");
}

// ref: FUN_008a4070
void CMaterialWaterNoSpec::EnsureShaders() {
    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsWaterNoSpec, 4, "vsLiquidWaterNoSpec",
             s_psWaterNoSpec, 1, "psLiquidWaterNoSpec");
}

// ref: FUN_008a4190
// The only one of the four with a single vertex permutation rather than four.
void CMaterialMagma::EnsureShaders() {
    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsMagma, 1, "vsLiquidMagma", s_psMagma, 1, "psLiquidMagma");
}

// ref: FUN_008a3e00
void CMaterialProcWater::EnsureShaders() {
    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    char vertexName[256];
    char pixelName[256];

    SStrPrintf(vertexName, sizeof(vertexName), "vsLiquidProcWater%s", ProcWaterSuffix());
    SStrPrintf(pixelName, sizeof(pixelName), "psLiquidProcWater%s", ProcWaterSuffix());

    LoadPair(s_vsProcWater, 4, vertexName, s_psProcWater, 1, pixelName);
}

void CMaterialWater::Draw(CClientEnvironment* environment, CChunkGeomFactory* geometry, void*,
                          const C3Vector& cameraPos, const C44Matrix* placement,
                          const CAaSphere* sphere, CMaterialSettings* settings) {
    DrawShaderMaterial(s_vsWater, s_psWater, environment, geometry, cameraPos, placement, sphere,
                       settings);
}

void CMaterialWaterNoSpec::Draw(CClientEnvironment* environment, CChunkGeomFactory* geometry, void*,
                                const C3Vector& cameraPos, const C44Matrix* placement,
                                const CAaSphere* sphere, CMaterialSettings* settings) {
    DrawShaderMaterial(s_vsWaterNoSpec, s_psWaterNoSpec, environment, geometry, cameraPos,
                       placement, sphere, settings);
}

void CMaterialMagma::Draw(CClientEnvironment* environment, CChunkGeomFactory* geometry, void*,
                          const C3Vector& cameraPos, const C44Matrix* placement,
                          const CAaSphere* sphere, CMaterialSettings* settings) {
    // Magma loads ONE vertex program rather than four, so the permutation is always 0 -- which is
    // what DrawShaderMaterial would pick anyway only when no local light was found. Passing the
    // one-entry array through the same body would index past it, so magma clamps here.
    CGxShader* shaders[4] = { s_vsMagma[0], s_vsMagma[0], s_vsMagma[0], s_vsMagma[0] };

    DrawShaderMaterial(shaders, s_psMagma, environment, geometry, cameraPos, placement, sphere,
                       settings);
}

void CMaterialProcWater::Draw(CClientEnvironment* environment, CChunkGeomFactory* geometry, void*,
                              const C3Vector& cameraPos, const C44Matrix* placement,
                              const CAaSphere* sphere, CMaterialSettings* settings) {
    DrawShaderMaterial(s_vsProcWater, s_psProcWater, environment, geometry, cameraPos, placement,
                       sphere, settings);
}

// ref: FUN_008a1fa0
// Which of a material's two implementations is used is decided once, from the device: the
// shader ones need vertex shaders at all and pixel shader model 3. The two capability slots the
// reference reads sit sixteen bytes apart, which is the distance from the vertex entry to the
// pixel entry in the shader-target table, and the choice they drive is shader versus
// fixed-function -- so that is the table being read.
IMaterial* GetMaterial(int32_t liquidType) {
    LiquidTypeRec* typeRec = nullptr;

    while (true) {
        typeRec = g_liquidTypeDB.GetRecord(liquidType);

        if (typeRec) {
            break;
        }

        SysMsgPrintf(SYSMSG_ERROR, "Material Bank: Liquid type [%d] not found, defaulting to water!", liquidType);

        // The same divergence as the settings bank above: the reference would spin here if the
        // water row were missing, and frozen's is.
        if (liquidType == 1) {
            return nullptr;
        }

        liquidType = 1;
    }

    uint32_t materialId = typeRec->m_materialID;

    if (materialId < s_materialBank.Count() && s_materialBank[materialId]) {
        return s_materialBank[materialId];
    }

    const CGxCaps& caps = GxCaps();

    bool shaders = caps.m_shaderTargets[GxSh_Vertex] >= 1
                && caps.m_shaderTargets[GxSh_Pixel] >= 3;

    IMaterial* material = nullptr;

    // Material 1 is water, 2 magma and slime, 3 procedural water. Each has a shader flavour and
    // a fixed-function one, and the caps decide which.
    //
    // TODO the specular choice. The reference picks CMaterialWater or CMaterialWaterNoSpec on a
    // setting frozen does not model; with specular is the richer of the two, so it is what this
    // takes.
    switch (materialId) {
    case 2:
        material = shaders ? static_cast<IMaterial*>(new CMaterialMagma())
                           : static_cast<IMaterial*>(new CMaterialMagmaFFP());
        break;

    case 3:
        material = shaders ? static_cast<IMaterial*>(new CMaterialProcWater())
                           : static_cast<IMaterial*>(new CMaterialProcWaterFFP());
        break;

    default:
        material = shaders ? static_cast<IMaterial*>(new CMaterialWater())
                           : static_cast<IMaterial*>(new CMaterialWaterFFP());
        break;
    }

    if (material) {
        material->EnsureShaders();
    }
    s_materialBank.GrowToFit(materialId, 1);
    s_materialBank[materialId] = material;

    return material;
}

// ref: FUN_008a1f50
void ReleaseMaterials() {
    for (uint32_t i = 0; i < s_materialBank.Count(); i++) {
        if (s_materialBank[i]) {
            // TODO each material releases itself through its own vtable.
        }
    }

    s_materialBank.SetCount(0);
}

// ref: part of FUN_008a2380
void ReleaseMaterialSettings() {
    for (uint32_t i = 0; i < s_settingsBank.Count(); i++) {
        if (s_settingsBank[i]) {
            auto settings = s_settingsBank[i];

            for (uint32_t slot = 0; slot < CMaterialSettings::TEXTURE_SLOTS; slot++) {
                for (uint32_t frame = 0; frame < settings->m_frames[slot].Count(); frame++) {
                    if (settings->m_frames[slot][frame]) {
                        HandleClose(settings->m_frames[slot][frame]);
                    }
                }

                settings->m_frames[slot].SetCount(0);
            }

            settings->~CMaterialSettings();
            SMemFree(s_settingsBank[i], __FILE__, __LINE__, 0x0);
            s_settingsBank[i] = nullptr;
        }
    }

    s_settingsBank.SetCount(0);
}

}
