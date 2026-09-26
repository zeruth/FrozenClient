#include "world/map/LiquidMaterialSettings.hpp"
#include "db/Db.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Device.hpp"
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
