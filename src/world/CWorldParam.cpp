#include "world/CWorldParam.hpp"
#include <storm/String.hpp>
#include "console/Console.hpp"
#include "world/CWorld.hpp"
#include "console/CVar.hpp"
#include "world/ParticleFx.hpp"

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

bool CWorldParam::BaseMipCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::BSPCacheCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
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

bool CWorldParam::ExtShadowQualityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
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

bool CWorldParam::FootstepBiasCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::GroundEffectDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::GroundEffectDistCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
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

bool CWorldParam::HwPCFCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::SkyCloudLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO cloud level of detail. Clouds are generated in src/world/Clouds.cpp at a fixed
    // resolution; this is the knob that would lower it.
    return true;
}

bool CWorldParam::ViolenceLevelCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO selects the blood texture set through UnitBloodLevels.dbc, whose record frozen already
    // reads (src/db/rec/UnitBloodLevelsRec.cpp carries m_violencelevel[3]).
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

    CWorldParam::cvar_hwPCF = CVar::Register(
        "hwPCF",
        "Hardware PCF Filtering",
        0x1,
        "1",
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

bool CWorldParam::LodCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
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

bool CWorldParam::SpecularCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::TerrainAlphaBitDepthCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

bool CWorldParam::TextureCacheSizeCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
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
