#ifndef WORLD_C_WORLD_HPP
#define WORLD_C_WORLD_HPP

#include "event/Event.hpp"
#include "util/GUID.hpp"
#include "util/GUID.hpp"
#include "world/Types.hpp"
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class Particulates;
class CWFrustum;
struct CFacetList;
class CM2Model;
class CM2Lighting;
class CM2Scene;
class Weather;
class CMapStaticEntity;
class LiquidTypeRec;
class CAaBox;
class CAaSphere;

// The five model distance bands the reference keeps at DAT_00adf350: per band a default near and
// far distance and a fade width, and the environmentDetail-scaled results derived from them (far,
// far squared, fade start, fade start squared). The first and last bands are never scaled.
struct WorldDetailBands {
    float defaultNear[5];
    float defaultFar[5];
    float nearDist[5];
    float fadeWidth[5];
    float farDist[5];
    float farDistSq[5];
    float fadeStart[5];
    float fadeStartSq[5];
};

// One object fading out of the world (reference SWModelFadeout, 0x60 bytes). The records are pooled:
// a finished one goes on a free list rather than back to the heap.
class CMapEntity;

struct SWModelFadeout {
    CMapEntity* entity = nullptr;         // +0x00
    uint32_t startMs = 0;                 // +0x04, OsGetAsyncTimeMs when the fade began
    float alpha = 0.0f;                   // +0x08, where it starts
    WOWGUID transport = 0;                // +0x10, the game object it rides, or 0
    C44Matrix relative;                   // +0x18, the model's matrix in the transport's space
    TSLink<SWModelFadeout> m_link;        // +0x58
};

struct SMOFog;
class CMapObjDef;

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
            // Liquid. All four of the reference's tests of this bit are on the liquid path: the
            // row visit FUN_007935a0, the per-chunk row insertion at 0x00799378, and both of the
            // two-bucket draws.
            Enable_Liquid = 0x1000000,
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
        static int32_t s_updateCount;           // DAT_00cd7690
        static uint32_t s_enables2;
        // The reference's THIRD enables word, 0x00cd7754, and it is not a general flag set: one
        // bit per M2 draw pass. World init raises the low three (there are three passes), and the
        // frame copies it into the scene immediately before CM2Scene::Draw, which tests
        // `mask & (1 << pass)`. Its only writer and its only reader in the whole binary.
        static uint32_t s_m2PassMask;
        // The eight scrolling texture offsets (DAT_00cd77f8): one (x, y, 0, 1) per MCLY animation
        // direction, advanced every update along s_textureScrollDir and wrapped at 64
        static float s_textureScroll[8][4];
        static const float s_textureScrollDir[8][2];   // DAT_00adee78
        // The shadowLevel CVar as read at world start (DAT_00cd7550): non-zero makes every
        // terrain chunk take its half-size alpha map
        static int32_t s_terrainShadowLevel;
        static Weather* s_weather;
        // DAT_00cd7548
        static Particulates* s_particulates;
        // The settings the world console commands change (CWorld::Initialize registers them):
        // the terrain level of detail (0x00adeec4, 2 or 3), the water ripple setting (0x00adf7f0),
        // the grass alpha cutoff (0x00cd766c), the character ambient multiplier and whether it is
        // in force (0x00adeebc, 0x00cd7740), and whether simple doodads draw (0x00cb753c).
        static uint32_t s_maxLod;
        static uint32_t s_waterRipples;
        static uint32_t s_detailDoodadAlpha;
        static float s_characterAmbient;
        static uint32_t s_characterAmbientActive;
        static uint32_t s_showSimpleDoodads;

        // Public static functions
        static HWORLDOBJECT AddObject(CM2Model* model, void* handler, void* handlerParam, uint64_t param64, uint32_t param32, uint32_t objFlags);
        static void RemoveObject(HWORLDOBJECT object);
        // The day/night step of the frame: the light, the sky's inputs, the fog, the sun's light.
        static void UpdateDayNight(int32_t force, const C3Vector* cameraPos);
        static void PublishDayNight();
        // The Northrend zones that force a light on (DAT_00adef58), parsed once from their outlines.
        static void InitializeLightZones();
        static void UpdateLightZones(const C3Vector& cameraPos);
        // The building fog the camera stands in, unless the player is a ghost.
        static int32_t QueryMapObjFog(SMOFog* fog, CMapObjDef** def, uint8_t* inside, TSGrowableArray<uint32_t>** groups, float* distance);
        // A full day/night update is owed (DAT_00cd7678), set when a map loads.
        static int32_t s_forceDayNight;
        // The terrain shadow's colour (FUN_00780660), kept as a one-colour texture.
        static void SetShadowColor(const CImVector& color);
        // The fog end the frame's lighting tests against (DAT_00cd7668).
        static float s_frameFogEnd;
        // Takes the object out of the world over two seconds instead of at once: its model keeps
        // drawing while its alpha runs from `alpha` to zero, and (given a transport) keeps riding
        // it. A model that cannot draw, an alpha under 0.01, or entity flag 0x4 removes it now.
        static void FadeOutObject(HWORLDOBJECT object, float alpha, WOWGUID transport);
        // Every frame, from CMap::Render: advance each fade, removing the finished ones.
        static void UpdateFadeouts();
        // Removes every fading object at once.
        static void ClearFadeouts();
        // Frees the spare fade records.
        static void FreeFadeoutPool();
        // An object moved: its placement, collision centre, scale, box and sphere, and a relink
        // into the map when any of them moved enough (unless `noRelink`). ref: FUN_00780240
        static void UpdateObject(HWORLDOBJECT object, const C44Matrix& matrix, const CAaBox& box,
                                 const CAaSphere& sphere, const C3Vector& collisionCenter,
                                 int32_t noRelink, uint32_t param);
        // The zone an entity stands in: its building group's WMOAreaTable zone, or the chunk's.
        // ref: FUN_00782560
        static int32_t GetEntityAreaID(CMapStaticEntity* entity, uint32_t* areaID);
        // A liquid type as the zone overrides it (AreaTable LiquidTypeID, the parent zone's when
        // the zone has none). ref: FUN_009905c0
        static const LiquidTypeRec* GetAreaLiquidType(uint32_t areaID, uint32_t liquidType);
        // Every face in the hit-record pool as a world-space facet, with the owner pair beside
        // each one added. ref: FUN_00782740
        static void AddHitFacets(CFacetList& list, uint32_t owner0, uint32_t owner1);
        static void SetObjectHandler(HWORLDOBJECT object, void* handler, void* handlerParam);
        static int32_t GetObjectFloor(HWORLDOBJECT object, uint32_t* fieldBC, float* height, uint32_t* a4);
        static void UpdateWindowAndMap(const C3Vector& targetPos);
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
        static float GetFogRate();
        // How bright a self-illuminated surface draws right now. The reference keeps
        // it in the day/night block (+0x1dc) and rolls it with the sun; nothing in
        // frozen writes it yet, so it holds at full.
        static float GetSidnScale();
        static const WorldDetailBands& GetDetailBands();
        static void SetupFogRenderStates();
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
        // How far the horizon reaches: the far clip times the horizon scale (DAT_00adeecc *
        // DAT_00cd7748, as the low-detail passes multiply them).
        static float GetHorizonDistance();
        static void Initialize();
        static void ProjectionCallback(const CAaBox& bounds, const CImVector& color, uint32_t shaded, void* context, uint32_t force);
        static void LoadMap(const char* mapName, const C3Vector& position, int32_t mapID);
        static void UpdateWindow(const C3Vector& targetPos);
        static void UpdateTextureCacheSize();

        // The position the map streams around (DAT_00cd7778) and the two boxes UpdateWindow
        // builds round it: 150 yards each way (DAT_00cd7784), and the far clip each way
        // (DAT_00cd779c)
        static C3Vector s_targetPos;
        static CAaBox s_nearBox;
        static CAaBox s_farBox;
        // The far clip the map was last updated with (DAT_00cd7744): a jump of more than ten
        // yards reloads behind a loading screen
        static float s_updateFarClip;
        // The chunk window of the previous update (DAT_00cd77e8 minRow, DAT_00cd77ec minCol,
        // DAT_00cd77f0 maxRow, DAT_00cd77f4 maxCol); when the new one no longer overlaps it the
        // map is reloaded outright (DAT_00cd767c)
        static int32_t s_prevWindowMinY;
        static int32_t s_prevWindowMinX;
        static int32_t s_prevWindowMaxY;
        static int32_t s_prevWindowMaxX;
        static int32_t s_reloadMap;
        static int32_t s_mapDirty;              // DAT_00d4314c
        // The groundEffectDensity and groundEffectDist CVars, as their callbacks store them.
        static int32_t s_groundEffectDensity;   // DAT_00cd773c
        static float s_groundEffectDistSq;      // DAT_00cd7674
        // gxTextureCacheSize in bytes (0 = let the client choose), and the flag that asks the
        // world update (FUN_0077f900) to choose again.
        static int32_t s_textureCacheSize;      // DAT_00cd7760
        static int32_t s_textureCacheDirty;     // DAT_00adeee0
        static int32_t OnTick(const EVENT_DATA_TICK* data, void* param);
        static void SetGroundEffectDensity(int32_t density);
        static void SetGroundEffectDist(float dist);
        static void SetFarClip(float farClip);
        static void SetNearClip(float nearClip);
        static void SetHorizonFarClipScale(float scale);
        static void SetHorizonNearClipScale(float scale);
        static void SetEnvironmentDetail(float detail);
        static void SetLoadProgressCallback(void (*callback)(float));
        static void LightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg);
        static void SetUpdateTime(float tickTimeSec, uint32_t curTimeMs);
        static void Update(const C3Vector& cameraPos, const C3Vector& cameraTarget, const C3Vector& targetPos);
        static void RenderWeather();
        // The underwater motes, when the camera is in a liquid that takes them. ref: FUN_0077f9d0
        static void RenderParticulates();
        // Where the load barriers measure their reach from. ref: FUN_0077f9a0
        static void SetBarrierPoint(const C3Vector& position);
        // ref: FUN_0077f980
        static void RenderBarriers(float dt);
        // A ripple on the water at `position` (WaterRipples::Add). ref: FUN_0077f400
        static void AddRipple(const C3Vector& position, float angle, float radius, float alphaPeak, float life, float radiusRate, int32_t kind, int32_t reserved);

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
        // waterShallow, waterDeep, oceanShallow, oceanDeep -- LightParams columns 5..8, blended
        // between lights the same way every other light value is.
        static float s_liquidAlpha[4];
        static C3Vector s_skyColors[6];
        static C3Vector s_fogColor;
        static float s_fogStart;
        static float s_fogEnd;
        static int32_t s_outdoorParamsID;
        static void ComputeOutdoorLight(int32_t mapID);

        // Private static functions
        static uint32_t GetFixedPrecisionTime(float timeSec);
};

// The world triangles a frustum touches, as the rest of the client calls it.
// ref: FUN_0077f330
int32_t WorldQueryFrustumFacets(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags);

// The world segment query, as the rest of the client calls it. ref: FUN_0077f310
int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result);

#endif
