#ifndef WORLD_C_WORLD_HPP
#define WORLD_C_WORLD_HPP

#include "event/Event.hpp"
#include "world/Types.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class CM2Model;
class CM2Lighting;
class CM2Scene;
class Weather;

class CWorld {
    public:
        // Bands 14..17, the liquid gradient endpoints (reference FUN_008a2bf0). Type 0 is OCEAN
        // (bands 14/15), type 1 is river water (bands 16/17) -- established from which alpha pair
        // each type takes, not from the colours.
        static const C3Vector& GetLiquidShallow(int32_t oceanic);
        static const C3Vector& GetLiquidDeep(int32_t oceanic);

        // LightParams' own water/ocean shallow and deep alphas, which the reference interpolates
        // across the same depth ramp as the colours.
        static float GetLiquidAlpha(int32_t oceanic, int32_t deep);

        // Which LightParams set the outdoor light is currently using. Liquid colours are baked per
        // vertex, so the terrain watches this to know when to rebake.
        static int32_t GetOutdoorParamsID();

        enum Enables {
            Enable_1 = 0x1,
            Enable_2 = 0x2,
            Enable_Lod = 0x4,
            Enable_8 = 0x8,
            Enable_10 = 0x10,
            Enable_Culling = 0x20,
            Enable_Shadow = 0x40,
            Enable_80 = 0x80,
            Enable_100 = 0x100,
            Enable_200 = 0x200,
            Enable_Footprints = 0x400,      // showfootprints
            Enable_800 = 0x800,
            Enable_1000 = 0x1000,
            Enable_ObjectFade = 0x4000,     // objectFade
            Enable_ObjectFadeZFill = 0x8000, // objectFadeZFill
            Enable_10000 = 0x10000,
            Enable_20000 = 0x20000,
            Enable_40000 = 0x40000,
            Enable_80000 = 0x80000,
            Enable_DetailDoodads = 0x100000,
            Enable_200000 = 0x200000,
            Enable_400000 = 0x400000,
            Enable_800000 = 0x800000,
            Enable_1000000 = 0x1000000,
            Enable_Particulates = 0x2000000,
            Enable_LowDetail = 0x4000000,
            Enable_8000000 = 0x8000000,
            Enable_PixelShader = 0x10000000
        };

        enum Enables2 {
            Enable_VertexShader = 0x1,
            Enable_HwPcf = 0x2,
            Enable_ProjectedTextures = 0x4  // projectedTextures
        };

        // Public static variables
        static uint32_t s_enables;
        static uint32_t s_enables2;
        static Weather* s_weather;

        // Public static functions
        static HWORLDOBJECT AddObject(CM2Model* model, void* handler, void* handlerParam, uint64_t param64, uint32_t param32, uint32_t objFlags);
        static uint32_t GetCurTimeMs();
        static float GetCurTimeSec();
        static float GetFarClip();
        static float GetHorizonFarClip();
        static uint32_t GetGameTimeFixed();
        static float GetGameTimeSec();
        static CM2Scene* GetM2Scene();
        static const C3Vector& GetOutdoorAmbient();
        static const C3Vector& GetOutdoorDiffuse();
        static const C3Vector& GetOutdoorDirection();
        static const C3Vector& GetSkyColor(int32_t index); // 0 = zenith .. 4 = horizon, 5 = fog band
        static const char* GetSkyboxPath();                // sky model for the current light, or null
        static float GetDayProgress();                     // 0..1 fraction of the day, for the skybox
        static const C3Vector& GetFogColor();
        static float GetFogStart();
        static float GetFogEnd();
        static void UpdateOutdoorLight();                  // recompute colours at the current time
        static void SetCameraUnderLiquid(bool under);      // selects the underwater LightParams set
        static bool IsCameraUnderLiquid();
        static const C3Vector& GetCameraDir();             // unit view direction of the last Update
        static const C3Vector& GetCameraPos();             // camera position of the last Update
        static const C3Vector& GetBodyTint();              // LightIntBand band 9: sun/moon disc tint
        static float GetCloudDensity();                    // LightFloatBand band 3: cloud cover
        static float GetSkyHighlight();                    // LightParams.highlightSky: 0 or 1
        static float GetNearClip();
        static uint32_t GetTickTimeFixed();
        static uint32_t GetTickTimeMs();
        static float GetTickTimeSec();
        static void Initialize();
        static void LoadMap(const char* mapName, const C3Vector& position, int32_t mapID);
        static int32_t OnTick(const EVENT_DATA_TICK* data, void* param);
        static void SetFarClip(float farClip);
        static void SetNearClip(float nearClip);
        static void SetHorizonFarClipScale(float scale);
        static void SetHorizonNearClipScale(float scale);
        static void SetEnvironmentDetail(float detail);
        static void SetLoadProgressCallback(void (*callback)(float));
        static void LightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg);
        static void SetUpdateTime(float tickTimeSec, uint32_t curTimeMs);
        static void Update(const C3Vector& cameraPos, const C3Vector& cameraTarget, const C3Vector& targetPos);

    private:
        // Private static variables
        static uint32_t s_curTimeMs;
        static float s_curTimeSec;
        static float s_farClip;
        static float s_horizonFarClipScale;   // horizonFarclipScale (reference DAT_00adeecc)
        static float s_horizonNearClipScale;  // horizonNearclipScale (DAT_00adeed0)
        static uint32_t s_gameTimeFixed;
        static float s_gameTimeSec;
        static CM2Scene* s_m2Scene;
        static float s_nearClip;
        static void (*s_loadProgressCallback)(float);
        static float s_prevFarClip;
        static uint32_t s_tickTimeFixed;
        static uint32_t s_tickTimeMs;
        static float s_tickTimeSec;
        static C3Vector s_outdoorAmbient;
        static C3Vector s_outdoorDiffuse;
        static C3Vector s_outdoorDirection;
        static bool s_cameraUnderLiquid;
        static C3Vector s_cameraDir;
        static float s_fogRate;
        static float s_floatBand2;
        static float s_floatBand4;
        static float s_floatBand5;
        static C3Vector s_bodyTint;
        static C3Vector s_sunColor;    // LightIntBand band 8
        static C3Vector s_cloudColor1; // LightIntBand band 10
        static C3Vector s_cloudColor2; // LightIntBand band 11
        static C3Vector s_lightBands12to17[6]; // LightIntBand bands 12..17


        static float s_cloudDensity;
        static float s_skyHighlight;
        static C3Vector s_skyColors[6];
        static C3Vector s_fogColor;
        static float s_fogStart;
        static float s_fogEnd;
        static int32_t s_outdoorParamsID;
        static void ComputeOutdoorLight(int32_t mapID);

        // Private static functions
        static uint32_t GetFixedPrecisionTime(float timeSec);
};

#endif
