#include <cstdlib>
#include "world/CWorldParam.hpp"
#include "world/ShadowMap.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <new>
#include "console/Console.hpp"
#include "world/CWorld.hpp"
#include "console/CVar.hpp"
#include "world/ParticleFx.hpp"
#include "world/map/CMap.hpp"
#include "gx/Device.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Gx.hpp"
#include "gx/Types.hpp"
#include "gx/shader/CShaderEffect.hpp"
#include "gx/Texture.hpp"
#include "client/Client.hpp"
#include "ui/game/PortraitButton.hpp"
#include <cstdio>

CVar* CWorldParam::cvar_baseMip;
CVar* CWorldParam::cvar_bspCache;
CVar* CWorldParam::cvar_environmentDetail;
CVar* CWorldParam::cvar_extShadowQuality;
CVar* CWorldParam::cvar_farClip;
CVar* CWorldParam::cvar_farClipOverride;
CVar* CWorldParam::cvar_footstepBias;
CVar* CWorldParam::cvar_groundEffectDensity;
CVar* CWorldParam::cvar_groundEffectDist;
CVar* CWorldParam::cvar_gxTextureCacheSize;
CVar* CWorldParam::cvar_horizonFarClip;
CVar* CWorldParam::cvar_horizonNearClip;
CVar* CWorldParam::cvar_hwPCF;
CVar* CWorldParam::cvar_lod;
CVar* CWorldParam::cvar_mapObjLightLOD;
CVar* CWorldParam::cvar_mapShadows;
CVar* CWorldParam::cvar_maxLights;
CVar* CWorldParam::cvar_nearClip;
CVar* CWorldParam::cvar_objectFade;
CVar* CWorldParam::cvar_objectFadeZFill;
CVar* CWorldParam::cvar_occlusion;
CVar* CWorldParam::cvar_particleDensity;
CVar* CWorldParam::cvar_projectedTextures;
CVar* CWorldParam::cvar_shadowLevel;
CVar* CWorldParam::cvar_poiShiftComplete;
CVar* CWorldParam::cvar_skyCloudLOD;
CVar* CWorldParam::cvar_showFootprints;
CVar* CWorldParam::cvar_violenceLevel;
CVar* CWorldParam::cvar_specular;
CVar* CWorldParam::cvar_terrainAlphaBitDepth;
CVar* CWorldParam::cvar_texLodBias;
CVar* CWorldParam::cvar_waterLOD;
CVar* CWorldParam::cvar_worldPoolUsage;

// The reference's setter is an empty function in 3.3.5: the bias never reaches the device. It is
// NOT tagged, because the linker folded every empty function in the build onto one address
// (FUN_005eeb70, a single ret with 1692 callers). Tagging it there made that nullsub the named
// callee of every one of those call sites, putting a phantom call into the reference sequence of
// a great many functions and depressing their measured fidelity.
static void TextureLodBiasSet(float bias) {
}

int32_t CWorldParam::s_maxLights = 4;
uint32_t CWorldParam::s_mapObjLightLOD = 0;
int32_t CWorldParam::s_waterLOD = 0;
bool CWorldParam::s_mapShadows = false;

// ref: FUN_0078df00
bool CWorldParam::BaseMipCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t level = SStrToInt(value);

    if (level == g_theGxDevicePtr->DeviceBaseMipLevel()) {
        return true;
    }

    if (level > 1) {
        ConsoleWrite("BaseMip must be 0 or 1", DEFAULT_COLOR);

        return false;
    }

    g_theGxDevicePtr->DeviceSetBaseMipLevel(level);

    // The atlas pages are read again at the new level (the device re-creates every other texture
    // itself, through its callback), and every portrait is drawn again.
    TextureReloadAtlases();
    PortraitButtonInvalidateAll();

    CWorld::s_textureCacheDirty = 1;

    char buffer[256];
    SStrPrintf(buffer, sizeof(buffer), "BaseMip level changed to %d", level);
    ConsoleWrite(buffer, DEFAULT_COLOR);

    return true;
}

// ref: FUN_0078df90
bool CWorldParam::BSPCacheCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (!SStrToInt(value)) {
        if (CMap::s_bspNodeCache) {
            ConsoleWrite("Disabling BSP node cache.", DEFAULT_COLOR);

            CMapBspNodeCache* cache = CMap::s_bspNodeCache;
            cache->Clear();
            SMemFree(cache, "delete", -1, 0);
            CMap::s_bspNodeCache = nullptr;

            return true;
        }

        ConsoleWrite("BSP node cache already disabled.", DEFAULT_COLOR);

        return true;
    }

    if (CMap::s_bspNodeCache) {
        ConsoleWrite("Enabling BSP node cache (already enabled, so clearing content.)", DEFAULT_COLOR);
        CMap::s_bspNodeCache->Clear();

        return true;
    }

    ConsoleWrite("Enabling BSP node cache (first time - starting up)", DEFAULT_COLOR);

    void* memory = SMemAlloc(sizeof(CMapBspNodeCache), __FILE__, __LINE__, 0);
    CMap::s_bspNodeCache = memory ? new (memory) CMapBspNodeCache() : nullptr;

    return true;
}

// ref: FUN_0078dc60
bool CWorldParam::EnvironmentDetailCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float detail = SStrToFloat(value);

    if (detail >= 0.5f && detail <= 1.5f) {
        CWorld::SetEnvironmentDetail(detail);
    } else {
        CWorld::SetEnvironmentDetail(1.5f);
    }

    return true;
}

// ref: FUN_0078dcb0
// The one thing that raises the shadow map quality above zero, and so the one thing that turns on
// the shadowed terrain shader sets. The CVar defaults to "0", so nothing changes until it is set.
//
// The support check runs twice -- here and again inside ShadowMapSetQuality -- because the
// reference does exactly that. It is what decides whether the CVar change is accepted at all, so
// an unsupported value is rejected and the old one stays.
bool CWorldParam::ExtShadowQualityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t quality = SStrToInt(value);

    ConsoleWrite(ShadowMapQualityName(quality), DEFAULT_COLOR);

    if (!ShadowMapQualitySupported(quality)) {
        return false;
    }

    ShadowMapSetQuality(quality);

    return true;
}

bool CWorldParam::FarClipCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    CWorld::SetFarClip(SStrToFloat(value));

    return true;
}

// ref: FUN_0078dc30
bool CWorldParam::FarClipOverrideCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (static_cast<uint32_t>(SStrToInt(value)) < 2) {
        return true;
    }

    ConsoleWrite("farClipOverride must be 0 or 1.", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078d940
// Only validates: the bias itself is read from the CVar by whoever draws footprints.
bool CWorldParam::FootstepBiasCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float bias;
    sscanf(value, "%f", &bias);

    if (bias >= 0.0f && bias <= 1.0f) {
        char buffer[256];
        sprintf(buffer, "Footstep bias set to %f", static_cast<double>(bias));
        ConsoleWrite(buffer, DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Footstep bias must be in the range (0, 1)", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078dab0
bool CWorldParam::GroundEffectDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t density = SStrToInt(value);

    if (static_cast<uint32_t>(density - 16) <= 240) {
        CWorld::SetGroundEffectDensity(density);

        return true;
    }

    char buffer[256];
    sprintf(buffer, "Ground effect density must be in range %d to %d.", 16, 256);
    ConsoleWriteA(buffer, DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078db10
bool CWorldParam::GroundEffectDistCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float dist = SStrToFloat(value);

    if (dist >= 0.0f && dist <= 140.0f) {
        CWorld::SetGroundEffectDist(dist);

        return true;
    }

    char buffer[256];
    sprintf(buffer, "Ground effect distance must be in range %0f to %0f.", 0.0, 140.0);
    ConsoleWriteA(buffer, DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078d7c0
bool CWorldParam::HorizonFarClipScaleCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float scale = SStrToFloat(value);

    if (scale >= 3.0f && scale <= 6.0f) {
        CWorld::SetHorizonFarClipScale(scale);
    } else {
        CWorld::SetHorizonFarClipScale(6.0f);
    }

    return true;
}

// ref: FUN_0078d810
bool CWorldParam::HorizonNearClipScaleCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float scale = SStrToFloat(value);

    if (scale >= 0.01f && scale <= 1.0f) {
        CWorld::SetHorizonNearClipScale(scale);
    } else {
        CWorld::SetHorizonNearClipScale(1.0f);
    }

    return true;
}

// ref: FUN_0078e070
// The hwPCF setting: whether the shadow map is sampled with the hardware's own depth compare
// (a real depth texture, filtered by the card) or compared in the shader against an R32F map.
//
// It is REJECTED outright on a card that cannot do it, which is the one case that returns false
// and so leaves the CVar at its old value. The capability is the D24X8 texture format entry in
// the caps -- caps+0xac, identified 2026-09-26 from the shadow map's own support check -- because
// hardware PCF here means being able to create and sample a depth-stencil surface as a texture.
//
// Nothing happens unless the flag actually moved. When it does, three things follow in the
// reference's order: the shader system takes the new setting, the shadow targets are marked for
// reallocation (they change format between the two modes), and the shadowed terrain pixel
// shaders are reloaded, because each mode has its own pair of names.
bool CWorldParam::HwPCFCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t want = SStrToInt(value);

    uint32_t before = CWorld::s_enables2;

    if (want == 0) {
        ConsoleWrite("Hardware PCF disabled.", DEFAULT_COLOR);
        CWorld::s_enables2 &= ~static_cast<uint32_t>(CWorld::Enables2::Enable_HwPcf);
    } else {
        if (!GxCaps().m_texFmt[GxTex_D24X8]) {
            ConsoleWrite("Hardware PCF not supported by this graphics card.", DEFAULT_COLOR);

            return false;
        }

        ConsoleWrite("Hardware PCF enabled.", DEFAULT_COLOR);
        CWorld::s_enables2 |= static_cast<uint32_t>(CWorld::Enables2::Enable_HwPcf);
    }

    if (CWorld::s_enables2 != before) {
        CShaderEffect::SetPcfFiltering((CWorld::s_enables2 >> 1) & 1);
        ShadowMapDeviceRestore();
        CMap::CreateTerrainShadowShaders();
    }

    return true;
}

bool CWorldParam::SkyCloudLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO cloud level of detail. Clouds are generated in src/world/Clouds.cpp at a fixed
    // resolution; this is the knob that would lower it.
    return true;
}

// Reference 0x00d3920c: the violence level in force.
int32_t CWorldParam::s_violenceLevel;

// ref: FUN_007f39b0
// The violence level, never above what the client's locale allows (the reference's table at
// 0x00af4e14, by the locale index). Below the full level the small blood spurt is drawn with a
// lower-detail texture, through the texture loader's one substitution.
bool CWorldParam::ViolenceLevelCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    static int32_t s_maxViolenceLevel[9] = { 2, 1, 2, 2, 1, 2, 2, 2, 2 };

    static const char* s_bloodSpurtTextures[3] = {
        "BloodSpurtSmall01_Low.blp",
        "BloodSpurtSmall01_Medium.blp",
        "BloodSpurtSmall01.blp"
    };

    CWorldParam::s_violenceLevel = SStrToInt(value);

    if (CWorldParam::s_violenceLevel < 0 || CWorldParam::s_violenceLevel > s_maxViolenceLevel[g_localeIndex]) {
        CWorldParam::s_violenceLevel = s_maxViolenceLevel[g_localeIndex];
    }

    if (CWorldParam::s_violenceLevel == 2) {
        TextureSetSubstitution(nullptr, nullptr);

        return true;
    }

    TextureSetSubstitution(s_bloodSpurtTextures[2], s_bloodSpurtTextures[CWorldParam::s_violenceLevel]);

    return true;
}


void CWorldParam::Initialize() {
    CWorldParam::cvar_farClipOverride = CVar::Register(
        "farClipOverride",
        "Override old world graphic settings",
        0x1,
        "0",
        &CWorldParam::FarClipOverrideCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_lod = CVar::Register(
        "lod",
        "Video option: Toggle Lod",
        0x1,
        "0",
        &CWorldParam::LodCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_mapShadows = CVar::Register(
        "mapShadows",
        "Video option: Toggle map shadows",
        0x1,
        "1",
        &CWorldParam::MapShadowsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_maxLights = CVar::Register(
        "MaxLights",
        "Max number of hardware lights",
        0x1,
        "4",
        &CWorldParam::MaxLightsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_shadowLevel = CVar::Register(
        "shadowLevel",
        "Terrain shadow map mip level",
        0x1,
        "1",
        &CWorldParam::ShadowLevelCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_texLodBias = CVar::Register(
        "texLodBias",
        "Texture LOD Bias",
        0x1,
        "0.0",
        &CWorldParam::TextureLodBiasCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_farClip = CVar::Register(
        "farclip",
        "Far clip plane distance",
        0x1,
        "350",
        &CWorldParam::FarClipCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_nearClip = CVar::Register(
        "nearclip",
        "Near clip plane distance",
        0x1,
        "0.2",
        &CWorldParam::NearClipCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_specular = CVar::Register(
        "specular",
        "Specularity",
        0x1,
        "0",
        &CWorldParam::SpecularCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_mapObjLightLOD = CVar::Register(
        "mapObjLightLOD",
        "Map object light LOD",
        0x1,
        "0",
        &CWorldParam::MapObjLightLODCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_particleDensity = CVar::Register(
        "particleDensity",
        "Video option: Particle density",
        0x1,
        "1.0",
        &CWorldParam::ParticleDensityCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_waterLOD = CVar::Register(
        "waterLOD",
        "Water geometry LOD",
        0x1,
        "0",
        &CWorldParam::WaterLODCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_baseMip = CVar::Register(
        "baseMip",
        "base mipmap level",
        0x1,
        "0",
        &CWorldParam::BaseMipCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_horizonFarClip = CVar::Register(
        "horizonFarclipScale",
        "Far clip plane scale for horizon",
        0x1,
        "4.0",
        &CWorldParam::HorizonFarClipScaleCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_horizonNearClip = CVar::Register(
        "horizonNearclipScale",
        "Near clip plane scale for horizon",
        0x1,
        "0.7",
        &CWorldParam::HorizonNearClipScaleCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // Category DEBUG, no help text, no callback, and a default of "0.6" -- a fraction rather than
    // the flag most cvars here carry. It controls how far a point of interest is allowed to shift
    // on the minimap before it is treated as having arrived. Frozen draws no points of interest
    // yet, so nothing reads it.
    CWorldParam::cvar_poiShiftComplete = CVar::Register(
        "POIShiftComplete",
        "",
        0x0,
        "0.6",
        nullptr,
        DEBUG,
        false,
        nullptr,
        false
    );

    // Registered with NO help text and no flags in the reference, unlike every other graphics
    // cvar here. Reproduced as it is: an empty help string is what the console would show.
    CWorldParam::cvar_skyCloudLOD = CVar::Register(
        "SkyCloudLOD",
        "",
        0x0,
        "0",
        &CWorldParam::SkyCloudLODCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // GAME rather than GRAPHICS, and flag 0x10 rather than the 0x1 the graphics settings use.
    //
    // DIVERGENCE in the default. The reference formats it per locale from a table at 00af4e14 --
    // mostly 2, but 1 for two of the eight entries, which are the locales that ship censored. That
    // table is indexed by a locale id frozen does not plumb through here, so the uncensored 2 is
    // used and the per-locale choice is not reproduced.
    CWorldParam::cvar_violenceLevel = CVar::Register(
        "violenceLevel",
        "Sets the violence level of the game",
        0x10,
        "2",
        &CWorldParam::ViolenceLevelCallback,
        GAME,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_showFootprints = CVar::Register(
        "showfootprints",
        "toggles rendering of footprints",
        0x1,
        "1",
        &CWorldParam::ShowFootprintsCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_bspCache = CVar::Register(
        "bspcache",
        "BSP node caching",
        0x1,
        "1",
        &CWorldParam::BSPCacheCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_footstepBias = CVar::Register(
        "footstepBias",
        "Unit footstep depth bias",
        0x1,
        "0.125",
        &CWorldParam::FootstepBiasCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_occlusion = CVar::Register(
        "occlusion",
        "Use hardware occlusion test",
        0x1,
        "1",
        &CWorldParam::OcclusionCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_worldPoolUsage = CVar::Register(
        "worldPoolUsage",
        "CGxPool usage static/dynamic",
        0x1,
        "Dynamic",
        &CWorldParam::WorldPoolUsageCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_terrainAlphaBitDepth = CVar::Register(
        "terrainAlphaBitDepth",
        "Terrain alpha map bit depth",
        0x1,
        "8",
        &CWorldParam::TerrainAlphaBitDepthCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_groundEffectDensity = CVar::Register(
        "groundEffectDensity",
        "Ground effect density",
        0x1,
        "16",
        &CWorldParam::GroundEffectDensityCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_groundEffectDist = CVar::Register(
        "groundEffectDist",
        "Ground effect dist",
        0x1,
        "70.0",
        &CWorldParam::GroundEffectDistCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_objectFade = CVar::Register(
        "objectFade",
        "Fade objects into view",
        0x1,
        "1",
        &CWorldParam::ObjectFadeCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_objectFadeZFill = CVar::Register(
        "objectFadeZFill",
        "Fade objects using ZFill pass ",
        0x1,
        "0",
        &CWorldParam::ObjectFadeZFillCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_environmentDetail = CVar::Register(
        "environmentDetail",
        "Environment detail",
        0x1,
        "1.0",
        &CWorldParam::EnvironmentDetailCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // FROZEN-ONLY: the shadow dump needs the colour-format maps, so it defaults PCF off.
    CWorldParam::cvar_hwPCF = CVar::Register(
        "hwPCF",
        "Hardware PCF Filtering",
        0x1,
        getenv("FROZEN_SHADOW_DUMP") ? "0" : "1",
        &CWorldParam::HwPCFCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_extShadowQuality = CVar::Register(
        "extShadowQuality",
        "Quality of exterior shadows (0-5)",
        0x1,
        "0",
        &CWorldParam::ExtShadowQualityCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_projectedTextures = CVar::Register(
        "projectedTextures",
        "Projected Textures",
        0x1,
        "0",
        &CWorldParam::ProjectedTexturesCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CWorldParam::cvar_gxTextureCacheSize = CVar::Register(
        "gxTextureCacheSize",
        "GX Texture Cache Size",
        0x1,
        "0",
        &CWorldParam::TextureCacheSizeCallback,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    CVar::Register(
        "spellEffectLevel",
        "Video Option: Spell Effects",
        0x1,
        "9",
        nullptr,
        GRAPHICS,
        false,
        nullptr,
        false
    );

    // TODO the video options callback list (FUN_0076aab0 / FUN_0078e1a0) and the hardware-class
    // defaults for groundEffectDensity and terrain shadows (FUN_0078dd40 / FUN_0078ddf0)
}

// ref: FUN_0078d610
bool CWorldParam::LodCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Terrain LOD enabled.", DEFAULT_COLOR);
        CWorld::s_enables |= CWorld::Enable_Lod;

        return true;
    }

    ConsoleWrite("Terrain LOD disabled.", DEFAULT_COLOR);
    CWorld::s_enables &= ~CWorld::Enable_Lod;

    return true;
}

// ref: FUN_0078ded0
bool CWorldParam::MapObjLightLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t lod = SStrToInt(value);

    if (lod < 3) {
        CWorldParam::s_mapObjLightLOD = lod;

        return true;
    }

    ConsoleWrite("MapObjLightLOD must be 0-2", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078d660
bool CWorldParam::MapShadowsCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Terrain shadows enabled.", DEFAULT_COLOR);
        CWorldParam::s_mapShadows = true;

        return true;
    }

    ConsoleWrite("Terrain shadows disabled.", DEFAULT_COLOR);
    CWorldParam::s_mapShadows = false;

    return true;
}

// ref: FUN_0078d6b0
bool CWorldParam::MaxLightsCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t maxLights = SStrToInt(value);

    if (maxLights - 1 < 4) {
        CWorldParam::s_maxLights = maxLights;

        return true;
    }

    ConsoleWriteA("MaxLights must be in range 1 - %i.", DEFAULT_COLOR, 4);

    return false;
}

// ref: FUN_0078d7a0
bool CWorldParam::NearClipCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    CWorld::SetNearClip(SStrToFloat(value));

    return true;
}

// ref: FUN_0078db90
bool CWorldParam::ObjectFadeCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Object distance fade enabled.", DEFAULT_COLOR);
        CWorld::s_enables |= CWorld::Enable_ObjectFade;

        return true;
    }

    ConsoleWrite("Object distance fade disabled.", DEFAULT_COLOR);
    CWorld::s_enables &= ~CWorld::Enable_ObjectFade;

    return true;
}

// ref: FUN_0078dbe0
bool CWorldParam::ObjectFadeZFillCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Object distance fade ZFill pass enabled.", DEFAULT_COLOR);
        CWorld::s_enables |= CWorld::Enable_ObjectFadeZFill;

        return true;
    }

    ConsoleWrite("Object distance fade ZFill pass disabled.", DEFAULT_COLOR);
    CWorld::s_enables &= ~CWorld::Enable_ObjectFadeZFill;

    return true;
}

// ref: FUN_0078d9d0
bool CWorldParam::OcclusionCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Using hardware occlusion test.", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Disabled hardware occlusion test.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_0078d860
bool CWorldParam::ParticleDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float density = SStrToFloat(value);

    if (density >= 0.1f && density <= 1.0f) {
        ParticleFxSetDensity(density);

        return true;
    }

    ConsoleWrite("Value must be between 0.1 and 1.0.", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078dcf0
bool CWorldParam::ProjectedTexturesCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Projected textures enabled.", DEFAULT_COLOR);
        CWorld::s_enables2 |= CWorld::Enable_ProjectedTextures;

        return true;
    }

    ConsoleWrite("Projected textures disabled.", DEFAULT_COLOR);
    CWorld::s_enables2 &= ~CWorld::Enable_ProjectedTextures;

    return true;
}

// ref: FUN_0078d6f0
bool CWorldParam::ShadowLevelCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value) > 1) {
        ConsoleWrite("Shadow mip level must be in range 0 - 1.", DEFAULT_COLOR);

        return false;
    }

    ConsoleWrite("Shadow mip level changed upon restart.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_0078d8f0
bool CWorldParam::ShowFootprintsCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (SStrToInt(value)) {
        ConsoleWrite("Showing foot prints.", DEFAULT_COLOR);
        CWorld::s_enables |= CWorld::Enable_Footprints;

        return true;
    }

    ConsoleWrite("Hiding foot prints.", DEFAULT_COLOR);
    CWorld::s_enables &= ~CWorld::Enable_Footprints;

    return true;
}

// ref: FUN_0078de60
// Takes effect on restart: the bit is read when the map's shaders are chosen.
bool CWorldParam::SpecularCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (!SStrToInt(value)) {
        ConsoleWrite("Specular disabled on restart.", DEFAULT_COLOR);
        CWorld::s_enables &= ~CWorld::Enable_8000000;

        return true;
    }

    if (GxCaps().m_shaderTargets[GxSh_Pixel] > 0) {
        ConsoleWrite("Specular enabled on restart.", DEFAULT_COLOR);
        CWorld::s_enables |= CWorld::Enable_8000000;

        return true;
    }

    ConsoleWrite("Specular not enabled.  Requires pixel shaders.", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078da50
// Only validates: the depth is read when the map memory is set up, hence "on restart".
bool CWorldParam::TerrainAlphaBitDepthCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t depth = SStrToInt(value);

    if (depth != 4 && depth != 8) {
        ConsoleWrite("Alpha map bit depth must be 4 or 8.", DEFAULT_COLOR);

        return false;
    }

    char buffer[256];
    sprintf(buffer, "Alpha map bit depth set to %dbit on restart.", depth);
    ConsoleWrite(buffer, DEFAULT_COLOR);

    return true;
}

// ref: FUN_0078e110
// The reference hands the size to the device through vtable slot 0xf4, which on the D3D9 device
// is an empty `ret 4` (0x00632050); frozen keeps the size and skips the call.
bool CWorldParam::TextureCacheSizeCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    int32_t megabytes = SStrToInt(value);

    if (megabytes) {
        CWorld::s_textureCacheSize = megabytes << 20;

        char buffer[256];
        sprintf(buffer, "Texture cache size set to %dMB.", megabytes);
        ConsoleWrite(buffer, DEFAULT_COLOR);

        return true;
    }

    CWorld::s_textureCacheSize = 0;
    CWorld::s_textureCacheDirty = 1;
    ConsoleWrite("Texture cache size set to default.", DEFAULT_COLOR);

    return true;
}

// ref: FUN_0078d730
bool CWorldParam::TextureLodBiasCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    float bias = SStrToFloat(value);

    if (bias >= -1.0f && bias <= 1.0f) {
        TextureLodBiasSet(bias);

        return true;
    }

    ConsoleWrite("TexLodBias must be in range -1.0 - 1.0.", DEFAULT_COLOR);

    return false;
}

// ref: FUN_0078d8b0
bool CWorldParam::WaterLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    CWorldParam::s_waterLOD = 0;

    if (SStrToInt(value)) {
        ConsoleWrite("waterLOD fixed to 0", DEFAULT_COLOR);

        return false;
    }

    return true;
}

// ref: FUN_0078da10
bool CWorldParam::WorldPoolUsageCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    if (!SStrCmpI(value, "Dynamic", 0x7FFFFFFF)) {
        ConsoleWrite("WorldPoolUsage set on restart.", DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("WorldPoolUsage must be Stream, Dynamic or Static.", DEFAULT_COLOR);

    return false;
}
