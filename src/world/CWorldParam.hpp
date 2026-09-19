#ifndef WORLD_C_WORLD_PARAM_HPP
#define WORLD_C_WORLD_PARAM_HPP

#include "console/CVar.hpp"

class CWorldParam {
    public:
        // Static variables
        static int32_t s_maxLights;       // MaxLights (reference DAT_00cd7660)
        static uint32_t s_mapObjLightLOD; // mapObjLightLOD (DAT_00d1c414)
        static int32_t s_waterLOD;        // waterLOD, always 0 in 3.3.5 (DAT_00cdffd0)
        static bool s_mapShadows;         // mapShadows: bit 0x40 of the world flags (DAT_00cd774c)
        static CVar* cvar_baseMip;
        static CVar* cvar_bspCache;
        static CVar* cvar_environmentDetail;
        static CVar* cvar_extShadowQuality;
        static CVar* cvar_farClip;
        static CVar* cvar_farClipOverride;
        static CVar* cvar_footstepBias;
        static CVar* cvar_groundEffectDensity;
        static CVar* cvar_groundEffectDist;
        static CVar* cvar_gxTextureCacheSize;
        static CVar* cvar_horizonFarClip;
        static CVar* cvar_horizonNearClip;
        static CVar* cvar_hwPCF;
        static CVar* cvar_lod;
        static CVar* cvar_mapObjLightLOD;
        static CVar* cvar_mapShadows;
        static CVar* cvar_maxLights;
        static CVar* cvar_nearClip;
        static CVar* cvar_objectFade;
        static CVar* cvar_objectFadeZFill;
        static CVar* cvar_occlusion;
        static CVar* cvar_particleDensity;
        static CVar* cvar_projectedTextures;
        static CVar* cvar_shadowLevel;
        static CVar* cvar_showFootprints;
        static CVar* cvar_specular;
        static CVar* cvar_terrainAlphaBitDepth;
        static CVar* cvar_texLodBias;
        static CVar* cvar_waterLOD;
        static CVar* cvar_worldPoolUsage;

        // Static functions
        static bool BaseMipCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool BSPCacheCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool EnvironmentDetailCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ExtShadowQualityCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool FarClipCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool FarClipOverrideCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool FootstepBiasCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool GroundEffectDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool GroundEffectDistCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool HorizonFarClipScaleCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool HorizonNearClipScaleCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool HwPCFCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static void Initialize();
        static bool LodCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool MapObjLightLODCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool MapShadowsCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool MaxLightsCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool NearClipCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ObjectFadeCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ObjectFadeZFillCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool OcclusionCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ParticleDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ProjectedTexturesCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ShadowLevelCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool ShowFootprintsCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool SpecularCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool TerrainAlphaBitDepthCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool TextureCacheSizeCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool TextureLodBiasCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool WaterLODCallback(CVar* var, const char* oldValue, const char* value, void* arg);
        static bool WorldPoolUsageCallback(CVar* var, const char* oldValue, const char* value, void* arg);
};

#endif
