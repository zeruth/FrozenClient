#include "model/CM2Shared.hpp"
#include "world/map/CMapObj.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "gx/Texture.hpp"
#include "world/map/CMapLight.hpp"
#include "world/DayNightLight.hpp"
#include "world/LightZonePaths.hpp"
#include "world/map/Particulates.hpp"
#include "world/map/WaterRipples.hpp"
#include "world/map/DetailDoodad.hpp"
#include "util/Log.hpp"
#include "world/CWFrustum.hpp"
#include "world/WorldFacets.hpp"
#include "console/Console.hpp"
#include "console/Command.hpp"
#include "world/Shadow.hpp"
#include "model/CM2Lighting.hpp"
#include <cstdlib>
#include "model/CM2Model.hpp"
#include "world/CWorld.hpp"
#include "world/MapShadow.hpp"
#include "world/ShadowMap.hpp"
#include "world/map/LiquidMaterialSettings.hpp"
#include "world/map/CMapBaseObj.hpp"
#include "world/map/CMapEntity.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/ColorConvert.hpp>
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Gx.hpp"
#include "gx/Shader.hpp"
#include "gx/shader/CShaderEffectManager.hpp"
#include "model/Model2.hpp"
#include "world/CWorldParam.hpp"
#include "world/Map.hpp"
#include "world/MapWeather.hpp"
#include "world/map/CMap.hpp"
#include "gx/LoadingScreen.hpp"
#include "async/AsyncFileRead.hpp"
#include "util/SFile.hpp"
#include "db/Db.hpp"
#include "client/Client.hpp"
#include <storm/Memory.hpp>
#include <common/Time.hpp>
#include <storm/String.hpp>
#include <cstdio>
#include <cmath>
#include <vector>

uint32_t CWorld::s_curTimeMs;
float CWorld::s_curTimeSec;
uint32_t CWorld::s_enables;
uint32_t CWorld::s_enables2;
uint32_t CWorld::s_m2PassMask;
float CWorld::s_farClip;
float CWorld::s_horizonFarClipScale = 1.0f;
float CWorld::s_horizonNearClipScale = 0.7f;
uint32_t CWorld::s_gameTimeFixed;
float CWorld::s_gameTimeSec;
CM2Scene* CWorld::s_m2Scene;
float CWorld::s_nearClip = 0.1f;
void (*CWorld::s_loadProgressCallback)(float) = nullptr;
float CWorld::s_prevFarClip;
uint32_t CWorld::s_tickTimeFixed;
uint32_t CWorld::s_tickTimeMs;
float CWorld::s_tickTimeSec;
float CWorld::s_textureScroll[8][4];
int32_t CWorld::s_terrainShadowLevel;
const float CWorld::s_textureScrollDir[8][2] = {
    { -1.0f,  0.0f },
    { -1.0f,  1.0f },
    {  0.0f,  1.0f },
    {  1.0f,  1.0f },
    {  1.0f,  0.0f },
    {  1.0f, -1.0f },
    {  0.0f, -1.0f },
    { -1.0f, -1.0f },
};
Weather* CWorld::s_weather;
Particulates* CWorld::s_particulates;
C3Vector CWorld::s_outdoorAmbient = { 0.45f, 0.45f, 0.5f };
C3Vector CWorld::s_outdoorDiffuse = { 0.9f, 0.85f, 0.75f };
// Unit-length: CM2Lighting::AddDiffuse and the terrain/WMO bake dot this straight into N.L without
// renormalising, so a non-unit vector would scale every outdoor diffuse term. {-0.4,-0.3,0.86} has
// length 0.9948; normalised it keeps the same (still-placeholder) direction at exactly length 1.
C3Vector CWorld::s_outdoorDirection = { -0.402096f, -0.301572f, 0.864504f };
bool CWorld::s_cameraUnderLiquid = false;
C3Vector CWorld::s_cameraDir = { 1.0f, 0.0f, 0.0f };
C3Vector CWorld::s_targetPos;
CAaBox CWorld::s_nearBox;
CAaBox CWorld::s_farBox;
float CWorld::s_updateFarClip;
int32_t CWorld::s_prevWindowMinY;
int32_t CWorld::s_prevWindowMinX;
int32_t CWorld::s_prevWindowMaxY;
int32_t CWorld::s_prevWindowMaxX;
int32_t CWorld::s_reloadMap;
int32_t CWorld::s_mapDirty;
int32_t CWorld::s_groundEffectDensity;
float CWorld::s_groundEffectDistSq;
int32_t CWorld::s_textureCacheSize;
int32_t CWorld::s_textureCacheDirty = 1;
int32_t CWorld::s_updateCount;
float CWorld::s_frameTimes[30];
uint32_t CWorld::s_frameTimeIndex;
void (*CWorld::s_updateCallback)();
// Zenith first, horizon last, then the fog band -- the order the dome's rings read them in.
C3Vector CWorld::s_skyColors[6] = {
    { 0.2f, 0.35f, 0.65f }, { 0.3f, 0.42f, 0.7f }, { 0.4f, 0.5f, 0.75f },
    { 0.45f, 0.55f, 0.78f }, { 0.5f, 0.6f, 0.8f }, { 0.5f, 0.5f, 0.5f }
};
int32_t CWorld::s_outdoorParamsID = 0;
C3Vector CWorld::s_fogColor = { 0.5f, 0.5f, 0.5f };
// The reference keeps its outdoor fog as [colour, start, end, rate] and holds the rate at 1.5 --
// read live from its fog block, and the same in both copies of it. The three-argument SetFog
// overload frozen was calling defaults the density to 1.0, so fog fell off on a different curve from
// the reference's even once start and end matched exactly.
float CWorld::s_fogRate = 1.5f;
float CWorld::s_floatBand2 = 0.0f;
float CWorld::s_floatBand4 = 0.0f;
float CWorld::s_floatBand5 = 0.0f;
C3Vector CWorld::s_bodyTint = { 1.0f, 1.0f, 1.0f };
C3Vector CWorld::s_sunColor = { 1.0f, 1.0f, 1.0f };
C3Vector CWorld::s_cloudColor1 = { 1.0f, 1.0f, 1.0f };
C3Vector CWorld::s_cloudColor2 = { 1.0f, 1.0f, 1.0f };
C3Vector CWorld::s_lightBands12to17[6] = {};
float CWorld::s_cloudDensity = 0.5f;
float CWorld::s_skyHighlight = 0.0f;
float CWorld::s_liquidAlpha[4] = { 0.75f, 1.0f, 0.75f, 1.0f };
float CWorld::s_fogStart = 0.0f;
float CWorld::s_fogEnd = 0.0f;

namespace {

// Interpolate a LightIntBand colour (band 0 = diffuse, 1 = ambient, 7 = fog) at the given time.
// A LightParams owns 18 consecutive band rows; row IDs are 1-based, so band b of params P is at
// LightIntBand id (P-1)*18 + b + 1. Colours are stored BGRA.
// Band times are half-minutes, so a full day is 1440 x 2.
const int32_t DAY_HALF_MINUTES = 2880;

// Identified 2026-09-23 from the index arithmetic, which leaves no room for doubt: the
// reference computes `band + n * 18 - 0x11` where n is the light param read through a
// pointer, and (P - 1) * 18 + band + 1 expands to exactly P * 18 + band - 17. The table it
// indexes is the global at 0x00af49bc, and the stride of 18 is the LightIntBand band count.
// ref: FUN_007ebf30
void InterpBandColor(int32_t P, int32_t band, int32_t t, C3Vector& out) {
    auto rec = g_lightIntBandDB.GetRecord((P - 1) * 18 + band + 1);

    if (!rec || rec->m_num == 0) {
        return;
    }

    uint32_t num = rec->m_num;
    uint32_t v = rec->m_values[num - 1];

    if (num == 1 || t <= static_cast<int32_t>(rec->m_times[0])) {
        v = rec->m_values[0];
    } else if (t >= static_cast<int32_t>(rec->m_times[num - 1])) {
        // Past the last key the band WRAPS to the first one across the end of the day -- it does
        // not hold. Most params carry only two keys, midnight and noon, so clamping instead left
        // every afternoon and evening frozen at the noon colour: measured against the reference at
        // 18:12, frozen's ambient was 58,99,102 (exactly the noon key) where the reference had
        // 28,88,88, which is that key interpolated 51.7% of the way back toward midnight.
        int32_t t0 = static_cast<int32_t>(rec->m_times[num - 1]);
        int32_t t1 = static_cast<int32_t>(rec->m_times[0]) + DAY_HALF_MINUTES;
        float f = t1 != t0 ? static_cast<float>(t - t0) / (t1 - t0) : 0.0f;
        uint32_t a = rec->m_values[num - 1];
        uint32_t c = rec->m_values[0];
        uint32_t R = static_cast<uint32_t>(((a >> 16) & 0xFF) * (1 - f) + ((c >> 16) & 0xFF) * f);
        uint32_t G = static_cast<uint32_t>(((a >> 8) & 0xFF) * (1 - f) + ((c >> 8) & 0xFF) * f);
        uint32_t B = static_cast<uint32_t>((a & 0xFF) * (1 - f) + (c & 0xFF) * f);
        v = (R << 16) | (G << 8) | B;
    } else {
        for (uint32_t i = 0; i + 1 < num; i++) {
            int32_t t0 = static_cast<int32_t>(rec->m_times[i]);
            int32_t t1 = static_cast<int32_t>(rec->m_times[i + 1]);

            if (t >= t0 && t <= t1) {
                float f = t1 != t0 ? static_cast<float>(t - t0) / (t1 - t0) : 0.0f;
                uint32_t a = rec->m_values[i];
                uint32_t c = rec->m_values[i + 1];
                uint32_t R = static_cast<uint32_t>(((a >> 16) & 0xFF) * (1 - f) + ((c >> 16) & 0xFF) * f);
                uint32_t G = static_cast<uint32_t>(((a >> 8) & 0xFF) * (1 - f) + ((c >> 8) & 0xFF) * f);
                uint32_t B = static_cast<uint32_t>((a & 0xFF) * (1 - f) + (c & 0xFF) * f);
                v = (R << 16) | (G << 8) | B;
                break;
            }
        }
    }

    out.x = ((v >> 16) & 0xFF) / 255.0f;
    out.y = ((v >> 8) & 0xFF) / 255.0f;
    out.z = (v & 0xFF) / 255.0f;
}

// Interpolate a LightFloatBand scalar (band 0 = fog end, band 1 = fog start scalar) at time t.
// A LightParams owns 6 consecutive float-band rows; row IDs are 1-based.
// The float sibling of InterpBandColor above, and identified the same way: the reference
// computes `band + n * 6 - 0x5` against the table at 0x00af49e0, and (P - 1) * 6 + band + 1
// is P * 6 + band - 5. Six is the LightFloatBand band count, against LightIntBand's 18.
// ref: FUN_007ebf90
float InterpFloatBand(int32_t P, int32_t band, int32_t t) {
    auto rec = g_lightFloatBandDB.GetRecord((P - 1) * 6 + band + 1);

    if (!rec || rec->m_num == 0) {
        return 0.0f;
    }

    uint32_t num = rec->m_num;

    if (num == 1 || t <= static_cast<int32_t>(rec->m_times[0])) {
        return rec->m_values[0];
    }

    // Same wrap as the colour bands: past the last key, run back to the first across midnight.
    if (t >= static_cast<int32_t>(rec->m_times[num - 1])) {
        int32_t t0 = static_cast<int32_t>(rec->m_times[num - 1]);
        int32_t t1 = static_cast<int32_t>(rec->m_times[0]) + DAY_HALF_MINUTES;
        float f = t1 != t0 ? static_cast<float>(t - t0) / (t1 - t0) : 0.0f;

        return rec->m_values[num - 1] * (1.0f - f) + rec->m_values[0] * f;
    }

    for (uint32_t i = 0; i + 1 < num; i++) {
        int32_t t0 = static_cast<int32_t>(rec->m_times[i]);
        int32_t t1 = static_cast<int32_t>(rec->m_times[i + 1]);

        if (t >= t0 && t <= t1) {
            float f = t1 != t0 ? static_cast<float>(t - t0) / (t1 - t0) : 0.0f;
            return rec->m_values[i] * (1.0f - f) + rec->m_values[i + 1] * f;
        }
    }

    return rec->m_values[num - 1];
}

// A full set of interpolated outdoor colours for one LightParams at one time of day.
struct LightColors {
    C3Vector ambient;
    C3Vector diffuse;
    C3Vector sky[6];
    C3Vector fog;
    C3Vector bodyTint; // LightIntBand band 9: the sun/moon disc tint
    C3Vector sunColor;   // band 8: the reference keeps this at DayNight slot 2
    C3Vector cloudColor1; // band 10: DayNight slot 10
    C3Vector cloudColor2; // band 11: DayNight slot 11
    C3Vector extraBands[6]; // bands 12..17, at DayNight slots 12..17
    float cloudDensity;  // LightFloatBand band 3
    float floatBand2;    // reference keeps it at 0x00d38c30, immediately before cloud density
    float floatBand4;    // 0x00d38c38
    float floatBand5;    // 0x00d38c3c
    float fogEnd;
    float fogStartScalar;
    // LightParams column 1 (highlightSky), 0 or 1. It gates the sky dome's azimuthal highlight;
    // see SkyRender in Terrain.cpp. The reference keeps it as a FLOAT at DNInfo+0x128, converted
    // from the DBC integer with fildl at 0x007ec1cd, and multiplies the highlight's strength band
    // by it -- so a zone whose row carries 0 gets no highlight at all.
    float highlightSky;
    // LightParams columns 5..8 in that order: waterShallow, waterDeep, oceanShallow, oceanDeep.
    float liquidAlpha[4];
};

// Interpolate every band of one LightParams at time t into a LightColors.
void ComputeLightColors(int32_t P, int32_t t, LightColors& out) {
    InterpBandColor(P, 0, t, out.diffuse);
    InterpBandColor(P, 1, t, out.ambient);
    // Top-to-horizon, which is the order the dome's rings consume them: the reference's sky
    // stack is DNInfo[3..8] = LightIntBand bands 2..7, and band 7 is the fog colour, which is why
    // the dome's bottom two rings and the distance fog converge on the same RGB with no blending.
    // This used to be five entries from bands 6,5,4,3,2 in the opposite order.
    InterpBandColor(P, 2, t, out.sky[0]);
    InterpBandColor(P, 3, t, out.sky[1]);
    InterpBandColor(P, 4, t, out.sky[2]);
    InterpBandColor(P, 5, t, out.sky[3]);
    InterpBandColor(P, 6, t, out.sky[4]);
    InterpBandColor(P, 7, t, out.sky[5]);
    InterpBandColor(P, 7, t, out.fog);
    InterpBandColor(P, 9, t, out.bodyTint);

    // Bands the reference reads and frozen did not. Identified by computing every band from the DBC
    // at the live clock and matching values against the reference's DayNight block, rather than by
    // guessing addresses -- slot 2 is band 8, slots 10 and 11 are bands 10 and 11.
    InterpBandColor(P, 8, t, out.sunColor);
    InterpBandColor(P, 10, t, out.cloudColor1);
    InterpBandColor(P, 11, t, out.cloudColor2);

    // Bands 12..17 sit at DayNight slots 12..17 in the reference and frozen read none of them.
    //
    // Bands 14..17 are the LIQUID COLOURS, confirmed in FUN_008a2bf0: it reads a colour pair at
    // base+0x10c and base+0x110 indexed by `type * 8`, and interpolates each pair across a
    // 512-entry gradient using alphas from base+0x140..0x14c.
    //
    // Which type is which comes from those alphas, not from the colours. base+0x140..0x14c hold
    // LightParams columns 5..8 -- waterShallow, waterDeep, oceanShallow, oceanDeep -- and the
    // function pairs type 0 with 0x148/0x14c (the OCEAN alphas) and type 1 with 0x140/0x144 (the
    // water ones). The byte order is the giveaway: CONCAT11(a, b) puts b at index 0, so index 0
    // selects 0x148 rather than 0x140.
    //
    // So: 14 = ocean shallow, 15 = ocean deep, 16 = river shallow, 17 = river deep -- the OPPOSITE
    // of what the colour values alone suggested. frozen's liquid rendering has never had any of them.
    //
    // **Confirmed from disassembly on 2026-09-23**, which retires the caveat this comment used to
    // carry about resting on the documented column order. FUN_008a2bf0 is a texture callback that
    // builds a 64x8 gradient strip, and the liquid type arrives as its userArg, not as the command
    // code. It reads the four alphas unconditionally into two two-byte arrays, in the order
    // 0x148, 0x140 and then 0x14c, 0x144, and indexes BOTH arrays by that type -- so type 0 takes
    // (0x148, 0x14c) and type 1 takes (0x140, 0x144), exactly as reasoned above.
    //
    // The colours settle it independently. It reads the pair at `base + 0x10c + type * 8` and
    // `base + 0x110 + type * 8`, where base is FUN_007ecef0's return, 0x00d38b00. The DayNight
    // slots begin at +0xd4, so +0x10c is slot 14 and type 1 lands on slots 16 and 17. Ocean is
    // type 0 on bands 14 and 15; river is type 1 on 16 and 17.
    // With this loop frozen reads every one of LightIntBand's eighteen bands -- 0 through 11
    // individually above, 12 through 17 here -- and all six of LightFloatBand's below.
    //
    // Audited against the reference 2026-09-23, once InterpBandColor was identified as
    // FUN_007ebf30: its twenty call sites reach bands 0 through 6, 8 through 10, and 12, 13, 14
    // and 16, which is a SUBSET of what is read here. So there is no band the reference consumes
    // and frozen ignores, and no missing visual feature hiding behind an unread band. Recorded as
    // a negative result because the question is a natural one to ask twice.
    for (int32_t b = 0; b < 6; b++) {
        InterpBandColor(P, 12 + b, t, out.extraBands[b]);
    }
    // Fog distances come straight out of the float band in yards and are clamped to the view
    // distance, so fog always terminates at the far clip rather than at some absolute distance:
    // fogEnd = min(farClip, band0), fogStart = fogEnd * band1, with the start scalar clamped to
    // [-1, 1] (reference: the DayNight fog update FUN_007f16f0 and the fog state setter
    // FUN_00781610). The earlier 1/36 scaling here was inferred from the raw magnitudes rather
    // than from the code, and produced fog that ended well inside the view distance.
    out.cloudDensity = InterpFloatBand(P, 3, t);

    // Bands 2, 4 and 5 are read but not yet used for anything. The reference stores all four
    // consecutively (0x00d38c30..0x00d38c3c, cloud density third), which is how they were located:
    // band 4's 0.95 had exactly one match in that region and the rest fell out of the layout.
    // Reading them makes them comparable, which is the prerequisite for finding out what they drive.
    out.floatBand2 = InterpFloatBand(P, 2, t);
    out.floatBand4 = InterpFloatBand(P, 4, t);
    out.floatBand5 = InterpFloatBand(P, 5, t);
    out.fogEnd = InterpFloatBand(P, 0, t);
    float startScalar = InterpFloatBand(P, 1, t);
    out.fogStartScalar = startScalar < -1.0f ? -1.0f : (startScalar > 1.0f ? 1.0f : startScalar);

    auto params = g_lightParamsDB.GetRecord(P);
    out.highlightSky = params ? static_cast<float>(params->m_highlightSky) : 0.0f;

    // The reference does not read these from the DBC at the point of use. FUN_007ebff0 copies them
    // into the light block at +0x140..+0x14c, and FUN_007f3230 then blends two blocks before the
    // result is published -- so what the liquid gradient callback (FUN_008a2bf0) reads is a blended
    // value, not one record's column. Reading them here puts them through frozen's own blend below.
    out.liquidAlpha[0] = params ? params->m_waterShallowAlpha : 0.75f;
    out.liquidAlpha[1] = params ? params->m_waterDeepAlpha : 1.0f;
    out.liquidAlpha[2] = params ? params->m_oceanShallowAlpha : 0.75f;
    out.liquidAlpha[3] = params ? params->m_oceanDeepAlpha : 1.0f;
}

// One Light.dbc row cached for the current map so per-frame position selection never rescans the DBC.
struct CachedLight {
    int32_t params;   // clear-weather LightParams id
    int32_t paramsUnder; // underwater LightParams id (Light.dbc params[1]), 0 if none
    float x, y, z;    // world anchor (0,0,0 marks the map default)
    float falloffStart;
    float falloffEnd;
    bool isDefault;
};

std::vector<CachedLight> s_mapLights;
int32_t s_defaultParams = 0;
int32_t s_defaultParamsUnder = 0;
C3Vector s_lightPos = { 0.0f, 0.0f, 0.0f };
char s_skyboxPath[260] = { 0 }; // sky model for the current light (LightParams -> LightSkybox), or empty

// Resolve the skybox model path for a LightParams id: LightParams.lightSkyboxID -> LightSkybox.name.
void ResolveSkybox(int32_t params) {
    s_skyboxPath[0] = '\0';

    if (params <= 0) {
        return;
    }

    auto lp = g_lightParamsDB.GetRecord(params);

    if (!lp || lp->m_lightSkyboxID <= 0) {
        return;
    }

    auto sb = g_lightSkyboxDB.GetRecord(lp->m_lightSkyboxID);

    if (sb && sb->m_name && sb->m_name[0]) {
        SStrCopy(s_skyboxPath, sb->m_name, sizeof(s_skyboxPath));
    }
}

}

void CWorld::ComputeOutdoorLight(int32_t mapID) {
    // Cache every Light.dbc row for this map: the default (anchored at the origin) plus the
    // positional lights (each with a falloff radius). Per-frame the update below picks the light
    // for the player's position and blends it over the default, exactly as the reference does, so a
    // zone with its own atmosphere (like the Death Knight start) is lit by its own light, not the
    // generic map default.
    s_mapLights.clear();
    s_defaultParams = 0;
    s_defaultParamsUnder = 0;

    int32_t n = g_lightDB.GetNumRecords();

    for (int32_t i = 0; i < n; i++) {
        auto l = g_lightDB.GetRecordByIndex(i);

        if (!l || l->m_mapID != mapID || l->m_params[0] <= 0) {
            continue;
        }

        bool isDefault = (l->m_x == 0.0f && l->m_y == 0.0f && l->m_z == 0.0f);

        // Light.dbc anchors and falloffs are stored in 1/36-yard units and in the ADT corner frame;
        // convert them to the same centred world coordinates the camera uses with the doodad/WMO
        // placement transform (world.x = CORNER - z, world.y = CORNER - x, world.z = y), so the
        // per-frame distance test below is in real yards. Verified: map 609's Acherus light (params
        // 748) resolves to ~(2452,-5579,496), right on the Death Knight spawn.
        const float CORNER = 17066.666f;
        const float S = 1.0f / 36.0f;
        float wx = CORNER - l->m_z * S;
        float wy = CORNER - l->m_x * S;
        float wz = l->m_y * S;
        s_mapLights.push_back({ l->m_params[0], l->m_params[1], wx, wy, wz, l->m_falloffStart * S, l->m_falloffEnd * S, isDefault });

        if (isDefault) {
            s_defaultParams = l->m_params[0];
            s_defaultParamsUnder = l->m_params[1];
        }
    }

    CWorld::s_outdoorParamsID = s_defaultParams;
    CWorld::UpdateOutdoorLight();

    fprintf(stderr, "Outdoor light map %d default params %d, %zu lights: ambient(%.2f,%.2f,%.2f) diffuse(%.2f,%.2f,%.2f)\n",
        mapID, s_defaultParams, s_mapLights.size(), CWorld::s_outdoorAmbient.x, CWorld::s_outdoorAmbient.y, CWorld::s_outdoorAmbient.z,
        CWorld::s_outdoorDiffuse.x, CWorld::s_outdoorDiffuse.y, CWorld::s_outdoorDiffuse.z);
}

// NO REFERENCE TAG, on purpose, and this is not a tag waiting to be found -- the report lists
// this as one of the largest frozen functions with no reference link, so here is why.
//
// The reference SPLITS what this merges. Its ComputeLightColors is FUN_007ebff0 and the only
// thing that calls it is FUN_007ecd80, all of 117 bytes, which adds a fog clamp and nothing
// else; the positional-light blend is separate again (FUN_007ee5d0, called once per light and
// ACCUMULATING, where this picks the single heaviest -- the divergence already recorded below).
// So no single reference function is this one, and tagging any of them would be claiming an
// identity that is not there.
//
// ComputeLightColors itself IS linked, which is the part of this file that the measurement can
// legitimately account for.
void CWorld::UpdateOutdoorLight() {
    if (s_defaultParams <= 0) {
        return;
    }

    // Actual game time of day (LightIntBand times are in half-minutes, so minutes x 2)
    int32_t minutes = g_clientGameTime.GetHourAndMinutes();
    int32_t t = (minutes >= 0 ? minutes : 720) * 2;

    // Base colours from the map default light. Light.dbc carries eight parameter sets per light;
    // set 1 is the underwater one the reference switches to while the camera is submerged.
    bool under = CWorld::s_cameraUnderLiquid;
    int32_t defaultParams = (under && s_defaultParamsUnder > 0) ? s_defaultParamsUnder : s_defaultParams;

    LightColors result;
    ComputeLightColors(defaultParams, t, result);
    CWorld::s_outdoorParamsID = defaultParams;

    // Pick the positional light with the strongest falloff weight at the player's position and blend
    // it over the default. Weight is 1 inside falloffStart, ramps to 0 at falloffEnd.
    float bestWeight = 0.0f;
    int32_t bestParams = 0;

    for (const CachedLight& cl : s_mapLights) {
        if (cl.isDefault) {
            continue;
        }

        float dx = cl.x - s_lightPos.x;
        float dy = cl.y - s_lightPos.y;
        float dz = cl.z - s_lightPos.z;
        float d = sqrtf(dx * dx + dy * dy + dz * dz);

        float weight;

        if (d <= cl.falloffStart) {
            weight = 1.0f;
        } else if (d < cl.falloffEnd && cl.falloffEnd > cl.falloffStart) {
            weight = (cl.falloffEnd - d) / (cl.falloffEnd - cl.falloffStart);
        } else {
            weight = 0.0f;
        }

        if (weight > bestWeight) {
            bestWeight = weight;
            bestParams = (under && cl.paramsUnder > 0) ? cl.paramsUnder : cl.params;
        }
    }

    // A DIFFERENCE FROM THE REFERENCE, recorded rather than changed: this picks the single
    // heaviest light and blends it once, while the reference blends EVERY light that has any
    // weight, one after another, accumulating into the running result (FUN_007ee5d0 per light,
    // each ending in the block blend FUN_007ed4c0). The two agree wherever one light dominates
    // and differ where zones overlap. Changing it is a visible change to the sky that wants a
    // scene-compare run behind it, not a blind edit.
    if (bestParams > 0 && bestWeight > 0.0f) {
        LightColors local;
        ComputeLightColors(bestParams, t, local);

        float w = bestWeight;
        float iw = 1.0f - w;

        result.ambient.x = result.ambient.x * iw + local.ambient.x * w;
        result.ambient.y = result.ambient.y * iw + local.ambient.y * w;
        result.ambient.z = result.ambient.z * iw + local.ambient.z * w;
        result.diffuse.x = result.diffuse.x * iw + local.diffuse.x * w;
        result.diffuse.y = result.diffuse.y * iw + local.diffuse.y * w;
        result.diffuse.z = result.diffuse.z * iw + local.diffuse.z * w;

        // SIX, not five. sky[] has six rings, the publish loop below copies six, and the
        // reference blends six -- FUN_007ed4c0 walks its sky-state block in groups of 3, 6, 5
        // and 4 colours, and the group of six is this one. Blending only five left the last
        // ring snapping to the default zone's colour at a light-zone boundary while the other
        // five cross-faded.
        for (int32_t k = 0; k < 6; k++) {
            result.sky[k].x = result.sky[k].x * iw + local.sky[k].x * w;
            result.sky[k].y = result.sky[k].y * iw + local.sky[k].y * w;
            result.sky[k].z = result.sky[k].z * iw + local.sky[k].z * w;
        }

        result.fog.x = result.fog.x * iw + local.fog.x * w;
        result.fog.y = result.fog.y * iw + local.fog.y * w;
        result.fog.z = result.fog.z * iw + local.fog.z * w;
        result.bodyTint.x = result.bodyTint.x * iw + local.bodyTint.x * w;
        result.bodyTint.y = result.bodyTint.y * iw + local.bodyTint.y * w;
        result.bodyTint.z = result.bodyTint.z * iw + local.bodyTint.z * w;
        result.sunColor.x = result.sunColor.x * iw + local.sunColor.x * w;
        result.sunColor.y = result.sunColor.y * iw + local.sunColor.y * w;
        result.sunColor.z = result.sunColor.z * iw + local.sunColor.z * w;
        result.cloudColor1.x = result.cloudColor1.x * iw + local.cloudColor1.x * w;
        result.cloudColor1.y = result.cloudColor1.y * iw + local.cloudColor1.y * w;
        result.cloudColor1.z = result.cloudColor1.z * iw + local.cloudColor1.z * w;
        result.cloudColor2.x = result.cloudColor2.x * iw + local.cloudColor2.x * w;
        result.cloudColor2.y = result.cloudColor2.y * iw + local.cloudColor2.y * w;
        result.cloudColor2.z = result.cloudColor2.z * iw + local.cloudColor2.z * w;

        for (int32_t b = 0; b < 6; b++) {
            result.extraBands[b].x = result.extraBands[b].x * iw + local.extraBands[b].x * w;
            result.extraBands[b].y = result.extraBands[b].y * iw + local.extraBands[b].y * w;
            result.extraBands[b].z = result.extraBands[b].z * iw + local.extraBands[b].z * w;
        }
        result.cloudDensity = result.cloudDensity * iw + local.cloudDensity * w;
        result.floatBand2 = result.floatBand2 * iw + local.floatBand2 * w;
        result.floatBand4 = result.floatBand4 * iw + local.floatBand4 * w;
        result.floatBand5 = result.floatBand5 * iw + local.floatBand5 * w;
        result.fogEnd = result.fogEnd * iw + local.fogEnd * w;
        result.fogStartScalar = result.fogStartScalar * iw + local.fogStartScalar * w;
        result.highlightSky = result.highlightSky * iw + local.highlightSky * w;

        for (int32_t k = 0; k < 4; k++) {
            result.liquidAlpha[k] = result.liquidAlpha[k] * iw + local.liquidAlpha[k] * w;
        }

        if (w >= 0.5f) {
            CWorld::s_outdoorParamsID = bestParams;
        }
    }

    CWorld::s_outdoorDiffuse = result.diffuse;
    CWorld::s_outdoorAmbient = result.ambient;

    for (int32_t k = 0; k < 6; k++) {
        CWorld::s_skyColors[k] = result.sky[k];
    }

    CWorld::s_fogColor = result.fog;
    CWorld::s_bodyTint = result.bodyTint;
    CWorld::s_sunColor = result.sunColor;
    CWorld::s_cloudColor1 = result.cloudColor1;
    CWorld::s_cloudColor2 = result.cloudColor2;

    for (int32_t b = 0; b < 6; b++) {
        CWorld::s_lightBands12to17[b] = result.extraBands[b];
    }
    CWorld::s_cloudDensity = result.cloudDensity;
    CWorld::s_skyHighlight = result.highlightSky;

    for (int32_t k = 0; k < 4; k++) {
        CWorld::s_liquidAlpha[k] = result.liquidAlpha[k];
    }
    CWorld::s_floatBand2 = result.floatBand2;
    CWorld::s_floatBand4 = result.floatBand4;
    CWorld::s_floatBand5 = result.floatBand5;
    CWorld::s_fogEnd = result.fogEnd > CWorld::s_farClip ? CWorld::s_farClip : result.fogEnd;
    // The scalar itself is genuinely negative in the data for many params (452 of 838 band-1 rows
    // carry one), and wrapping past the last key can land on it -- but fog may not start behind the
    // camera. Measured at the same spot and clock: the reference reports a fog start of 0 where the
    // raw product here is -190. Clamping the product, not the scalar, keeps the [-1, 1] scalar
    // range the decompiled fog update implies.
    CWorld::s_fogStart = CWorld::s_fogEnd * result.fogStartScalar;

    if (CWorld::s_fogStart < 0.0f) {
        CWorld::s_fogStart = 0.0f;
    }

    // The outdoor light direction is not a real sun arc and does not come from Light.dbc: the
    // reference (FUN_007eea90, reached from the DayNight update via FUN_007f3920) holds the
    // azimuth at a constant 225 degrees and wobbles only the zenith angle between 127 and 110
    // degrees twice a day, from a hard-coded four-key band over the day fraction. The vector it
    // stores points AWAY from the light; frozen's lighting dots the direction TO the light, so this
    // is the negation.
    {
        float day = CWorld::GetDayProgress();
        static const float s_thetaKeys[4] = { 0.0f, 0.25f, 0.5f, 0.75f };
        static const float s_thetaValues[4] = { 2.2165682f, 1.9198622f, 2.2165682f, 1.9198622f };

        int32_t k = 0;

        for (int32_t i = 0; i < 4; i++) {
            if (day >= s_thetaKeys[i]) {
                k = i;
            }
        }

        int32_t next = (k + 1) % 4;
        float span = (next == 0) ? (1.0f - s_thetaKeys[k]) : (s_thetaKeys[next] - s_thetaKeys[k]);
        float f = span > 0.0f ? (day - s_thetaKeys[k]) / span : 0.0f;

        if (f < 0.0f) f = 0.0f;
        if (f > 1.0f) f = 1.0f;

        float theta = s_thetaValues[k] + (s_thetaValues[next] - s_thetaValues[k]) * f;
        const float phi = 3.9269910f; // 225 degrees, constant everywhere
        float st = sinf(theta);

        CWorld::s_outdoorDirection.x = -(cosf(phi) * st);
        CWorld::s_outdoorDirection.y = -(sinf(phi) * st);
        CWorld::s_outdoorDirection.z = -cosf(theta);
    }

    // The active light may name a sky model (skybox) to draw instead of a plain gradient.
    ResolveSkybox(CWorld::s_outdoorParamsID);
}

void CWorld::SetCameraUnderLiquid(bool under) {
    CWorld::s_cameraUnderLiquid = under;
}

bool CWorld::IsCameraUnderLiquid() {
    return CWorld::s_cameraUnderLiquid;
}

const char* CWorld::GetSkyboxPath() {
    return s_skyboxPath[0] ? s_skyboxPath : nullptr;
}

float CWorld::GetDayProgress() {
    // Fraction of the day (0 at midnight .. 1). Skyboxes hold a full-day animation the client seeks
    // to this position rather than playing it in real time.
    int32_t minutes = g_clientGameTime.GetHourAndMinutes();

    if (minutes < 0) {
        minutes = 720;
    }

    return (minutes % 1440) / 1440.0f;
}

const C3Vector& CWorld::GetOutdoorAmbient() {
    return CWorld::s_outdoorAmbient;
}

const C3Vector& CWorld::GetOutdoorDiffuse() {
    return CWorld::s_outdoorDiffuse;
}

const C3Vector& CWorld::GetOutdoorDirection() {
    return CWorld::s_outdoorDirection;
}

const C3Vector& CWorld::GetFogColor() {
    return CWorld::s_fogColor;
}

float CWorld::GetSkyHighlight() {
    return CWorld::s_skyHighlight;
}

float CWorld::GetFogStart() {
    return CWorld::s_fogStart;
}

float CWorld::GetFogEnd() {
    return CWorld::s_fogEnd;
}

float CWorld::GetFogRate() {
    return CWorld::s_fogRate;
}

float CWorld::GetSidnScale() {
    return 1.0f;
}

const C3Vector& CWorld::GetSkyColor(int32_t index) {
    if (index < 0) {
        index = 0;
    } else if (index > 5) {
        index = 5;
    }

    return CWorld::s_skyColors[index];
}

namespace {

float AdjustFarClip(float farClip, int32_t mapID) {
    float minFarClip = 183.33333f;
    float maxFarClip = 1583.3334f;

    if (mapID < 530 || mapID == 575 || mapID == 543) {
        if (!CWorldParam::cvar_farClipOverride || CWorldParam::cvar_farClipOverride->GetInt() < 1) {
            maxFarClip = 791.66669f;
        }
    } else if (false /* TODO OsGetPhysicalMemory() <= 1073741824 */) {
        maxFarClip = 791.66669f;
    }

    return std::min(std::max(farClip, minFarClip), maxFarClip);
}

}

// ref: FUN_00781a10
HWORLDOBJECT CWorld::AddObject(CM2Model* model, void* handler, void* handlerParam, uint64_t param64, uint32_t param32, uint32_t objFlags) {
    auto entity = CMap::AllocEntity(objFlags & 0x8 ? true : false);

    entity->m_model = model;
    entity->m_param64 = param64;
    entity->m_param32 = param32;

    entity->m_dirLightScale = 1.0f;
    entity->m_dirLightScaleTarget = 1.0f;

    entity->m_type |= CMapBaseObj::Type_200;
    entity->m_handler = nullptr;

    // The entity's state word, rearranged out of the caller's flags. The reference writes it as
    // one expression over a preserved mask; spelled out, the bits move like this:
    //
    //   caller 0x01  ->  0x0002   the group list takes it at the head rather than the tail
    //   caller 0x02  ->  0x0800   INVERTED: clear here means the entity casts a blob shadow,
    //                             so an object says "no shadow" by setting its own bit 1
    //   caller 0x04  ->  0x0400   keep animating even when nothing can see it
    //   caller 0x08  ->  0x2000
    //   caller 0x10  ->  0x8000
    //
    // Everything else the word carries is left alone.
    uint32_t state = ((((objFlags & 0x10) << 3 | (objFlags & 0x4)) << 7) | (objFlags & 0x1)) << 1;

    state |= ~(objFlags << 10) & 0x800;
    state |= (objFlags >> 3 & 0x1) << 13;

    entity->m_flags7c = state | (entity->m_flags7c & 0xffff13fd);

    entity->m_flags = 0x0;

    if (objFlags & 0x20) {
        entity->m_flags = 0x20000;
    }

    // It starts at the sun's ambient, until the floor under it says otherwise.
    const C3Vector& amb = CMap::s_outdoorLight->m_light.m_ambColor;

    auto channel = [](float v) {
        if (!(0.0f < v)) {
            return 0.0f;
        }

        return v < 1.0f ? v * 255.0f + 0.5f : 255.0f;
    };

    CImVector start;
    start.b = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.z)));
    start.g = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.y)));
    start.r = static_cast<uint8_t>(static_cast<int32_t>(channel(amb.x)));
    start.a = 0xFF;
    entity->m_ambientTarget = start;
    entity->m_ambient = start;

    if (entity->m_model) {
        if (!SStrCmpI("InvisibleStalker.m2", entity->m_model->m_shared->m_filePath, STORM_MAX_STR)) {
            entity->m_flags7c |= 0x4000;
        }

        entity->m_model->m_lightingCallback = &CWorld::LightingCallback;
        entity->m_model->m_lightingArg = entity;
        entity->m_model->m_refCount++;
    }

    entity->m_handler = handler;
    entity->m_handlerParam = handlerParam;

    return reinterpret_cast<HWORLDOBJECT>(entity);
}

// ref: FUN_007826e0
void CWorld::RemoveObject(HWORLDOBJECT object) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    // FROZEN-ONLY guard: the reference rewrites the focus every update before anything reads it,
    // but frozen's world update can run without a camera target, and the plane must not read a
    // freed entity.
    if (CWorld::s_focusEntity == reinterpret_cast<CMapStaticEntity*>(entity)) {
        CWorld::s_focusEntity = nullptr;
    }

    for (auto link = entity->m_parentLinkList.Head(); link; ) {
        auto next = entity->m_parentLinkList.Next(link);
        CMap::FreeBaseObjLink(link);
        link = next;
    }

    if (entity->m_model && entity->m_model->m_attachParent) {
        entity->m_model->DetachFromParent();
    }

    CMap::FreeEntity(entity);
}

// ref: FUN_0077f2c0
void CWorld::SetObjectHandler(HWORLDOBJECT object, void* handler, void* handlerParam) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    entity->m_handler = handler;
    entity->m_handlerParam = handlerParam;
}

// ref: FUN_0077f1e0
int32_t CWorld::GetObjectFloor(HWORLDOBJECT object, uint32_t* fieldBC, float* height, uint32_t* a4) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (entity && (entity->m_flags7c & 0x20)) {
        *height = entity->m_liquidHeight;
        *fieldBC = entity->m_fieldBC;
        *a4 = 0;

        return 1;
    }

    return 0;
}

// ref: FUN_007815c0
void CWorld::UpdateWindowAndMap(const C3Vector& targetPos) {
    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CWorld::UpdateWindow(targetPos);
    CMap::Update(0);
}

uint32_t CWorld::GetCurTimeMs() {
    return CWorld::s_curTimeMs;
}

float CWorld::GetCurTimeSec() {
    return CWorld::s_curTimeSec;
}

float CWorld::GetFarClip() {
    return CWorld::s_farClip;
}

// How far the world itself is drawn, which is NOT farclip.
//
// `farclip` is where fog ends and where the client stops bothering with doodads. The terrain,
// water and buildings behind that go out to `farclip * horizonFarclipScale` -- the CVar is
// registered with a default of 4.0 and, until now, was never read by anything: its callback is a
// bare TODO. So the camera's far plane was farclip, and every piece of distant geometry was
// frustum-culled the moment it passed it: a hard, sudden edge that took terrain and water with it,
// which is exactly what the user described and is not something fog can explain.
float CWorld::GetHorizonFarClip() {
    float scale = CWorldParam::cvar_horizonFarClip
        ? CWorldParam::cvar_horizonFarClip->GetFloat()
        : 4.0f;

    // A scale below 1 would pull the world IN of the fog, which cannot be what was meant.
    if (scale < 1.0f) {
        scale = 1.0f;
    }

    return CWorld::s_farClip * scale;
}

uint32_t CWorld::GetFixedPrecisionTime(float timeSec) {
    return static_cast<uint32_t>(timeSec * 1024.0f);
}

uint32_t CWorld::GetGameTimeFixed() {
    return CWorld::s_gameTimeFixed;
}

float CWorld::GetGameTimeSec() {
    return CWorld::s_gameTimeSec;
}

CM2Scene* CWorld::GetM2Scene() {
    return CWorld::s_m2Scene;
}

float CWorld::GetNearClip() {
    return CWorld::s_nearClip;
}

uint32_t CWorld::GetTickTimeFixed() {
    return CWorld::s_tickTimeFixed;
}

uint32_t CWorld::GetTickTimeMs() {
    return CWorld::s_tickTimeMs;
}

float CWorld::GetTickTimeSec() {
    return CWorld::s_tickTimeSec;
}

uint32_t CWorld::s_maxLod = 3;
uint32_t CWorld::s_waterRipples;
uint32_t CWorld::s_detailDoodadAlpha = 0x80;
// DAT_00adeebc, 1.0 in the image: characters take the plain ambient until a zone or the console
// says otherwise. Left at zero it blacked out every character.
float CWorld::s_characterAmbient = 1.0f;
CMapStaticEntity* CWorld::s_focusEntity = nullptr;
uint32_t CWorld::s_characterAmbientActive;
uint32_t CWorld::s_showSimpleDoodads;

// ref: FUN_0077f500
// The scene's projection callback: a batch flagged for projection is drawn onto whatever lies
// under it rather than as geometry, when projected textures are on or the model insists.
void CWorld::ProjectionCallback(const CAaBox& bounds, const CImVector& color, uint32_t shaded, void* context, uint32_t force) {
    (void)context;

    if (force || (CWorld::s_enables2 & Enables2::Enable_ProjectedTextures)) {
        DecalDrawBoundReceivers(bounds, color, 0x200122, shaded ? 2 : 0, 0.4f);
    }
}

static int32_t WorldToggle(uint32_t bit, const char* enabled, const char* disabled) {
    if (CWorld::s_enables & bit) {
        ConsoleWrite(disabled, DEFAULT_COLOR);
        CWorld::s_enables &= ~bit;
    } else {
        ConsoleWrite(enabled, DEFAULT_COLOR);
        CWorld::s_enables |= bit;
    }

    return 1;
}

// ref: FUN_0077f5b0
static int32_t ConsoleShowDetailDoodads(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_DetailDoodads, "Detail doodads enabled.", "Detail doodads disabled.");
}

// ref: FUN_0077f600
static int32_t ConsoleMaxLod(const char* command, const char* arguments) {
    uint32_t lod = 0;
    sscanf(arguments, "%d", &lod);

    if (lod > 3) {
        CWorld::s_maxLod = 3;
        return 1;
    }

    CWorld::s_maxLod = lod < 2 ? 2 : lod;

    return 1;
}

// ref: FUN_0077f650
static int32_t ConsoleShowCull(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Culling, "Terrain culling enabled.", "Terrain culling disabled.");
}

// ref: FUN_0077f690
static int32_t ConsoleWaterRipples(const char* command, const char* arguments) {
    sscanf(arguments, "%d", &CWorld::s_waterRipples);

    return 1;
}

// ref: FUN_0077f6b0
static int32_t ConsoleWaterParticulates(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Particulates, "Particulates enabled", "Particulates disabled");
}

// ref: FUN_0077f700
static int32_t ConsoleDetailDoodadAlpha(const char* command, const char* arguments) {
    uint32_t alpha = 0;
    sscanf(arguments, "%d", &alpha);

    if (alpha > 0xFF) {
        ConsoleWrite("Alpha ref range 0 - 255.", DEFAULT_COLOR);
        return 1;
    }

    CWorld::s_detailDoodadAlpha = alpha;

    return 1;
}

// ref: FUN_0077f750
// The multiplier is stored as 3x the argument; it is in force unless the argument is exactly 1.
static int32_t ConsoleCharacterAmbient(const char* command, const char* arguments) {
    float ambient = 0.0f;
    sscanf(arguments, "%f", &ambient);

    if (ambient < 0.0f || ambient > 1.0f) {
        ConsoleWrite("Ambient multiply range 0.0 - 1.0.", DEFAULT_COLOR);
        return 1;
    }

    CWorld::s_characterAmbient = ambient + ambient + ambient;
    CWorld::s_characterAmbientActive = ambient != 1.0f ? 1 : 0;

    return 1;
}

// ref: FUN_0077f7e0
static int32_t ConsoleShowShadow(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_Shadow, "Terrain shadow enabled.", "Terrain shadow disabled.");
}

// ref: FUN_0077f820
static int32_t ConsoleShowLowDetail(const char* command, const char* arguments) {
    return WorldToggle(CWorld::Enable_LowDetail, "Terrain low detail enabled.", "Terrain low detail disabled.");
}

// ref: FUN_0077f870
static int32_t ConsoleShowSimpleDoodads(const char* command, const char* arguments) {
    if (CWorld::s_showSimpleDoodads) {
        ConsoleWrite("Simple doodads disabled.", DEFAULT_COLOR);
        CWorld::s_showSimpleDoodads = 0;
    } else {
        ConsoleWrite("Simple doodads enabled.", DEFAULT_COLOR);
        CWorld::s_showSimpleDoodads = 1;
    }

    return 1;
}

void CWorld::Initialize() {
    CWorld::s_enables |=
          Enables::Enable_1
        | Enables::Enable_2
        | Enables::Enable_10
        | Enables::Enable_Culling
        | Enables::Enable_Shadow
        | Enables::Enable_100
        | Enables::Enable_200
        | Enables::Enable_800
        | Enables::Enable_ObjectFade
        | Enables::Enable_DetailDoodads
        | Enables::Enable_Liquid
        | Enables::Enable_Particulates
        | Enables::Enable_LowDetail;

    CWorld::s_gameTimeFixed = 0;
    CWorld::s_gameTimeSec = 0.0f;

    if (GxCaps().m_shaderTargets[GxSh_Pixel] > GxShPS_none) {
        CWorld::s_enables |= Enables::Enable_PixelShader;
    }

    if (GxCaps().m_shaderTargets[GxSh_Vertex] > GxShVS_none) {
        CWorld::s_enables2 |= Enables2::Enable_VertexShader;
    }

    // All three M2 passes on. The reference ORs a literal 7 here (0x780fb4); M2PASS_COUNT is 3,
    // so this is the same value said in terms of the enum rather than as a magic number.
    CWorld::s_m2PassMask |= (1 << M2PASS_COUNT) - 1;

    // TODO

    CWorld::s_m2Scene = M2CreateScene();

    // TODO

    uint32_t m2Flags = M2GetCacheFlags();
    CShaderEffect::InitShaderSystem(
        (m2Flags & 0x8) != 0,
        (CWorld::s_enables2 & Enables2::Enable_HwPcf) != 0
    );

    // The named effects the world draws through, in the order the reference reads them.
    CShaderEffectManager::LoadEffectFile("MapObj.wfx");
    CShaderEffectManager::LoadEffectFile("MapObjU.wfx");
    CShaderEffectManager::LoadEffectFile("Model2.wfx");
    CShaderEffectManager::LoadEffectFile("Particle.wfx");
    CShaderEffectManager::LoadEffectFile("ShadowMap.wfx");

    // TODO

    for (int32_t i = 0; i < 8; i++) {
        CWorld::s_textureScroll[i][0] = 0.0f;
        CWorld::s_textureScroll[i][1] = 0.0f;
        CWorld::s_textureScroll[i][2] = 0.0f;
        CWorld::s_textureScroll[i][3] = 1.0f;
    }

    CWorld::s_terrainShadowLevel = CWorldParam::cvar_shadowLevel ? CWorldParam::cvar_shadowLevel->GetInt() : 1;

    MapShadowInitialize();

    CWorldScene::Initialize();

    CMap::Initialize();

    // TODO

    CWorld::s_weather = STORM_NEW(Weather);
    CWorld::s_particulates = STORM_NEW(Particulates)(0.02777777798473835f, 30.0f, "Textures\\WaterPoop02.blp");

    // The projected-texture callback (0x00781340). The particle ground query installed beside it
    // (FUN_0077f540, 0x00781351) waits on the map segment query FUN_007a3b70.
    CWorld::s_m2Scene->SetProjectionCallback(reinterpret_cast<void*>(&CWorld::ProjectionCallback), nullptr);

    ConsoleCommandRegister("showDetailDoodads", ConsoleShowDetailDoodads, GRAPHICS, nullptr);
    ConsoleCommandRegister("maxLOD", ConsoleMaxLod, GRAPHICS, nullptr);
    ConsoleCommandRegister("showCull", ConsoleShowCull, GRAPHICS, nullptr);
    // "setShadow" (FUN_00780e20) sets the terrain shadow colour through FUN_00780660, not ported.
    ConsoleCommandRegister("waterRipples", ConsoleWaterRipples, GRAPHICS, nullptr);
    ConsoleCommandRegister("waterParticulates", ConsoleWaterParticulates, GRAPHICS, nullptr);
    ConsoleCommandRegister("showShadow", ConsoleShowShadow, GRAPHICS, nullptr);
    ConsoleCommandRegister("showLowDetail", ConsoleShowLowDetail, GRAPHICS, nullptr);
    ConsoleCommandRegister("showSimpleDoodads", ConsoleShowSimpleDoodads, GRAPHICS, nullptr);
    ConsoleCommandRegister("detailDoodadAlpha", ConsoleDetailDoodadAlpha, GRAPHICS, nullptr);
    ConsoleCommandRegister("characterAmbient", ConsoleCharacterAmbient, GRAPHICS, nullptr);

    CWorld::InitializeLightZones();
}

int32_t CWorld::GetOutdoorParamsID() {
    return CWorld::s_outdoorParamsID;
}

// Reads the BLENDED value rather than the dominant light's record, which is what the reference
// does: the alphas live in its light block and go through the same two-block blend as every other
// light value, so crossing a light boundary used to step here and now ramps.
float CWorld::GetLiquidAlpha(int32_t oceanic, int32_t deep) {
    return CWorld::s_liquidAlpha[(oceanic ? 2 : 0) + (deep ? 1 : 0)];
}

const C3Vector& CWorld::GetLiquidShallow(int32_t oceanic) {
    // Band 14 (index 2) is ocean, band 16 (index 4) is river.
    return CWorld::s_lightBands12to17[oceanic ? 2 : 4];
}

const C3Vector& CWorld::GetLiquidDeep(int32_t oceanic) {
    return CWorld::s_lightBands12to17[oceanic ? 3 : 5];
}

void CWorld::LoadMap(const char* mapName, const C3Vector& position, int32_t mapID) {
    CWorld::s_farClip = AdjustFarClip(CWorldParam::cvar_farClip->GetFloat(), mapID);
    CWorld::s_nearClip = 0.2f;
    CWorld::s_prevFarClip = CWorld::s_farClip;

    CWorld::ComputeOutdoorLight(mapID);

    // TODO

    // The reference (FUN_00781430) takes the far clip from the map, remembers it as the value the
    // map was updated with, builds the chunk window from the spawn position and seeds the
    // previous window with it, and only then loads the map, so the load-time update streams the
    // tiles round the spawn. The rest of that function (the streaming-trial hook and the liquid
    // setup FUN_008a1720 / FUN_008a1730 / FUN_008a1f50) is not ported yet.
    CWorld::s_updateFarClip = CWorld::s_farClip;
    CWorld::UpdateWindow(position);
    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CMap::Load(mapName, mapID);

    // The liquid bank, after the map has settled what the device may do (FUN_00781430 tail): shader
    // materials need both the vertex and the pixel shader enable, specular water the map's
    // specular permission (0x00ce04a0, which CMap::LoadSettings derives from the same two words),
    // and the bank is emptied so the next lookup builds against the new settings.
    Liquid::SetShaderMaterials(
        (CWorld::s_enables2 & CWorld::Enables2::Enable_VertexShader)
        && (CWorld::s_enables & CWorld::Enables::Enable_PixelShader));
    Liquid::SetSpecular(
        (CWorld::s_enables & CWorld::Enables::Enable_8000000)
        && (CWorld::s_enables & CWorld::Enables::Enable_PixelShader));
    Liquid::ReleaseMaterials();

    // TODO the terrain, map objects, and doodads around the position are loaded here, and the
    // original reports their progress through the callback as they come in

    if (CWorld::s_loadProgressCallback) {
        CWorld::s_loadProgressCallback(1.0f);
    }
}

int32_t CWorld::OnTick(const EVENT_DATA_TICK* data, void* param) {
    CWorld::SetUpdateTime(data->tickTimeSec, data->curTimeMs);

    return 1;
}

void CWorld::SetFarClip(float farClip) {
    farClip = AdjustFarClip(farClip, CMap::s_mapID);

    if (CWorld::s_farClip == farClip) {
        return;
    }

    CWorld::s_prevFarClip = CWorld::s_farClip;
    CWorld::s_farClip = farClip;

    // TODO CMapRenderChunk::DirtyPools();

    CWorld::s_nearClip = 0.2f;

    CMapObj::s_farClipDirty = 1;
    CWorld::s_textureCacheDirty = 1;
}

// ref: FUN_00780cd0
// A model in the world takes the sun when nothing placed it; a placed one is fogged by the frame's
// fog and asks its map object for its lights and its side of the water.
void CWorld::LightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    lighting->m_flags |= 0x10;

    auto block = DayNightGetBlock();
    const float k = 1.0f / 255.0f;

    if (!arg) {
        C3Vector ambient = { block->ambient.r * k, block->ambient.g * k, block->ambient.b * k };
        lighting->AddAmbient(ambient);

        C3Vector diffuse = { block->diffuse.r * k, block->diffuse.g * k, block->diffuse.b * k };
        lighting->AddDiffuse(diffuse, block->direction);
    } else {
        C3Vector fog = { block->fogColor.r * k, block->fogColor.g * k, block->fogColor.b * k };
        lighting->SetFog(fog, block->fogStart, block->fogEnd, block->fogRate);

        auto obj = static_cast<CMapBaseObj*>(arg);
        obj->SelectLights(lighting);
        obj->SelectUnderwater(lighting);

        if (obj->m_flags & 0x2) {
            lighting->m_flags |= 0x8;
            return;
        }
    }

    lighting->m_flags &= ~0x8u;
}

// ref: FUN_0077f8f0
void CWorld::SetUpdateCallback(void (*callback)()) {
    CWorld::s_updateCallback = callback;
}

// The loading screen's progress, as CMap::Update reports it while a far-clip jump has the
// loading screen up: FUN_0040af40 takes the progress alone, and the map's callback slot passes
// an argument after it that the reference's cdecl call simply leaves on the stack.
static void ReportLoadProgress(float progress, void* arg) {
    LoadingScreenSetProgress3(progress);
}

void CWorld::SetLoadProgressCallback(void (*callback)(float)) {
    CWorld::s_loadProgressCallback = callback;
}

void CWorld::SetUpdateTime(float tickTimeSec, uint32_t curTimeMs) {
    auto tickTimeFixed = CWorld::GetFixedPrecisionTime(tickTimeSec);

    CWorld::s_curTimeMs = curTimeMs;
    CWorld::s_curTimeSec = static_cast<float>(curTimeMs) * 0.001f;

    CWorld::s_gameTimeFixed += tickTimeFixed;
    CWorld::s_gameTimeSec += tickTimeSec;

    CWorld::s_tickTimeFixed = tickTimeFixed;
    CWorld::s_tickTimeMs = static_cast<uint32_t>(tickTimeSec * 1000.0f);
    CWorld::s_tickTimeSec = tickTimeSec;
}

// ref: FUN_00780860
// Everything the map streams against, from the target position: the two update boxes, the inner
// chunk rectangle (the target's chunk, widened by one chunk per 33 yards of far clip, snapped to
// even chunks) and the loaded window two chunks beyond it, widened to at least eight chunks and
// clamped to the map. The reference also rebuilds the camera matrix and frustum corners here
// (FUN_006bf370 / FUN_006bf6d0 into the device's transform stack), which is not ported yet.
void CWorld::UpdateWindow(const C3Vector& targetPos) {
    CWorld::s_targetPos = targetPos;

    // TODO camera facing -> matrix (FUN_006bf370), frustum corners (FUN_006bf6d0), device
    // transform (FUN_00407f80 on g_theGxDevicePtr's stack)

    const float NEAR_EXTENT = 150.0f;   // DAT_009f989c
    CWorld::s_nearBox.b = { targetPos.x - NEAR_EXTENT, targetPos.y - NEAR_EXTENT, targetPos.z - NEAR_EXTENT };
    CWorld::s_nearBox.t = { targetPos.x + NEAR_EXTENT, targetPos.y + NEAR_EXTENT, targetPos.z + NEAR_EXTENT };

    float farClip = CWorld::s_farClip;
    CWorld::s_farBox.b = { targetPos.x - farClip, targetPos.y - farClip, targetPos.z - farClip };
    CWorld::s_farBox.t = { targetPos.x + farClip, targetPos.y + farClip, targetPos.z + farClip };

    // One chunk of margin per chunk of far clip (the multiply is by -0.03 and the result truncated)
    int32_t margin = 1 - static_cast<int32_t>(farClip * -0.029999999329447746f);

    const float MAP_HALF_EXTENT = 17066.666015625f;
    const float CHUNKS_PER_UNIT = 0.029999999329447746f;
    int32_t chunkY = static_cast<int32_t>(roundf(-(targetPos.x - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));
    int32_t chunkX = static_cast<int32_t>(roundf(-(targetPos.y - MAP_HALF_EXTENT) * CHUNKS_PER_UNIT - 0.5f));

    if (SFile::IsStreamingTrial()) {
        // TODO FUN_00420a50(chunkX, chunkY): streaming trial bookkeeping
    }

    int32_t hi = (chunkX + margin) & ~1;
    CMap::s_chunkInnerMinX = (chunkX - margin) & ~1;
    CMap::s_chunkInnerMaxX = hi + 1;
    CMap::s_chunkWindowMinX = CMap::s_chunkInnerMinX - 2;
    CMap::s_chunkWindowMaxX = hi + 3;

    hi = (chunkY + margin) & ~1;
    CMap::s_chunkInnerMinY = (chunkY - margin) & ~1;
    CMap::s_chunkInnerMaxY = hi + 1;
    CMap::s_chunkWindowMinY = CMap::s_chunkInnerMinY - 2;
    CMap::s_chunkWindowMaxY = hi + 3;

    if (CMap::s_chunkWindowMaxX - chunkX < 8) {
        int32_t widen = 8 - (CMap::s_chunkWindowMaxX - chunkX);
        CMap::s_chunkWindowMinY = (CMap::s_chunkWindowMinY - widen) & ~1;
        CMap::s_chunkWindowMinX = (CMap::s_chunkWindowMinX - widen) & ~1;
        CMap::s_chunkWindowMaxX = ((CMap::s_chunkWindowMaxX + widen) & ~1) + 1;
        CMap::s_chunkWindowMaxY = ((CMap::s_chunkWindowMaxY + widen) & ~1) + 1;
    }

    if (CMap::s_chunkInnerMinX < 0) CMap::s_chunkInnerMinX = 0;
    if (CMap::s_chunkInnerMaxX > 0x3FF) CMap::s_chunkInnerMaxX = 0x3FF;
    if (CMap::s_chunkInnerMinY < 0) CMap::s_chunkInnerMinY = 0;
    if (CMap::s_chunkInnerMaxY > 0x3FF) CMap::s_chunkInnerMaxY = 0x3FF;
    if (CMap::s_chunkWindowMinX < 0) CMap::s_chunkWindowMinX = 0;
    if (CMap::s_chunkWindowMaxX > 0x3FF) CMap::s_chunkWindowMaxX = 0x3FF;
    if (CMap::s_chunkWindowMinY < 0) CMap::s_chunkWindowMinY = 0;
    if (CMap::s_chunkWindowMaxY > 0x3FF) CMap::s_chunkWindowMaxY = 0x3FF;
}

// ref: FUN_0077f900
// When the base mip level or the far clip changed (s_textureCacheDirty) and gxTextureCacheSize leaves
// the choice to the client, size the device's texture cache from them: 64 MB, or 128 MB past a far
// clip of 727, at full detail; 16 MB, or 32 MB past 177, at the reduced base mip.
//
// DIVERGED in the one call that uses the size: the reference hands it to the device through its
// vtable slot 0xf4, which on the Direct3D 9 device is an empty `ret 4` (0x00632050). Frozen's D3D9
// device has nothing to receive it, so the choice is made and dropped as the reference's own device
// drops it.
void CWorld::UpdateTextureCacheSize() {
    if (!CWorld::s_textureCacheDirty || CWorld::s_textureCacheSize) {
        return;
    }

    CWorld::s_textureCacheDirty = 0;

    uint32_t baseMip = g_theGxDevicePtr->DeviceBaseMipLevel();
    int32_t farClip = static_cast<int32_t>(CWorld::s_farClip);

    int32_t megabytes;

    if (baseMip == 0) {
        megabytes = farClip <= 727 ? 64 : 128;
    } else {
        megabytes = farClip <= 177 ? 16 : 32;
    }

    // The size the reference passes to the device's slot 0xf4; see above.
    uint32_t size = static_cast<uint32_t>(megabytes) << 20;
    (void)size;
}

// ref: FUN_007831a0
// The reference's frame update, of which the map part is ported: the previous chunk window is
// kept, the new one built from the target, a window that no longer overlaps flags a full reload,
// a far-clip jump of more than ten yards goes behind a loading screen, and CMap::Update runs.
// The frame-time ring (FUN_0077f900), the scene camera (FUN_00795400), the day/night, underwater
// and weather updates and the zone-light selection at the end are not ported yet; the stand-in
// light handling below stays until they are.
void CWorld::Update(const C3Vector& cameraPos, const C3Vector& cameraTarget, const C3Vector& targetPos) {
    // The outdoor light is selected by the viewer's position (see UpdateOutdoorLight), so record the
    // camera each frame; the light and fog then reflect the zone the player is actually standing in.
    s_lightPos = cameraPos;

    C3Vector d = { cameraTarget.x - cameraPos.x, cameraTarget.y - cameraPos.y, cameraTarget.z - cameraPos.z };
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    if (len > 1e-4f) {
        CWorld::s_cameraDir = { d.x / len, d.y / len, d.z / len };
    }

    CWorld::UpdateTextureCacheSize();

    CWorld::s_frameTimes[CWorld::s_frameTimeIndex] = CWorld::s_tickTimeSec;
    CWorld::s_frameTimeIndex++;

    if (CWorld::s_frameTimeIndex == 30) {
        CWorld::s_frameTimeIndex = 0;
    }

    CWorld::s_prevWindowMinX = CMap::s_chunkWindowMinX;
    CWorld::s_prevWindowMinY = CMap::s_chunkWindowMinY;
    CWorld::s_prevWindowMaxY = CMap::s_chunkWindowMaxY;
    CWorld::s_prevWindowMaxX = CMap::s_chunkWindowMaxX;

    CWorld::UpdateWindow(targetPos);

    int32_t maxX = CWorld::s_prevWindowMaxX <= CMap::s_chunkWindowMaxX ? CWorld::s_prevWindowMaxX : CMap::s_chunkWindowMaxX;
    int32_t maxY = CWorld::s_prevWindowMaxY <= CMap::s_chunkWindowMaxY ? CWorld::s_prevWindowMaxY : CMap::s_chunkWindowMaxY;
    int32_t minX = CWorld::s_prevWindowMinX <= CMap::s_chunkWindowMinX ? CMap::s_chunkWindowMinX : CWorld::s_prevWindowMinX;
    int32_t minY = CWorld::s_prevWindowMinY <= CMap::s_chunkWindowMinY ? CMap::s_chunkWindowMinY : CWorld::s_prevWindowMinY;

    if (minY < maxY && minX < maxX) {
        CWorld::s_reloadMap = 0;
    } else {
        CWorld::s_reloadMap = 1;
        CWorld::s_mapDirty = 1;
    }

    // The shadow cascades were drawn over what is about to be unloaded (0x0078326d).
    g_shadowMapCascadesStale = CWorld::s_reloadMap;

    CWorld::s_updateCount++;

    if (CWorld::s_updateCallback) {
        CWorld::s_updateCallback();
    }

    // The texture scrolls step by the frame's tick (DAT_00cd76a0).
    float scrollStep = CWorld::s_tickTimeSec;

    for (int32_t i = 0; i < 8; i++) {
        float x = CWorld::s_textureScrollDir[i][0] * scrollStep + CWorld::s_textureScroll[i][0];
        CWorld::s_textureScroll[i][0] = x;
        float y = CWorld::s_textureScrollDir[i][1] * scrollStep + CWorld::s_textureScroll[i][1];
        CWorld::s_textureScroll[i][1] = y;

        if (64.0f <= x) {
            CWorld::s_textureScroll[i][0] = 0.0f;
        }
        if (64.0f <= y) {
            CWorld::s_textureScroll[i][1] = 0.0f;
        }
    }

    CWorldScene::UpdateCamera(cameraPos, cameraTarget);

    float farClipDelta = CWorld::s_farClip - CWorld::s_updateFarClip;
    bool smallChange = farClipDelta <= 10.0f;
    bool bigChange = 10.0f < farClipDelta;
    CWorld::s_updateFarClip = CWorld::s_farClip;

    if (bigChange) {
        CMap::s_loadProgressCallback = &ReportLoadProgress;
        CMap::s_loadProgressArg = nullptr;
        LoadingScreenStart(CMap::s_mapID, 1);
        CMap::s_loading = 1;
    }

    CMap::Update(smallChange);

    if (bigChange) {
        CMap::s_loading = 0;
        CMap::s_loadProgressCallback = nullptr;
        CMap::s_loadProgressArg = nullptr;
        AsyncFileReadSetProgressCallback(nullptr, nullptr);
        LoadingScreenFinish();
    }

    // Which building the camera is standing in, which is what CMap::Render's indoor branch runs
    // off. Safe to call now that that branch has somewhere to go: if the portal walk exposes
    // nothing it falls back to the outdoor traversal rather than to the unported teardown.
    CWorldScene::UpdateCameraDef();

    CWorld::UpdateDayNight(CWorld::s_forceDayNight | CWorld::s_reloadMap, &cameraPos);
    CWorld::s_frameFogEnd = DayNightGetBlock()->fogEnd;

    if (!g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        CWorld::s_frameFogEnd = 1e10f;
    }

    CWorld::s_forceDayNight = 0;

    if ((CWorld::s_enables & CWorld::Enable_Particulates) && CWorldScene::s_cameraLiquidType != 0) {
        CWorld::s_particulates->Update();
    }

    CWorld::s_weather->Update();

    // The characters' ambient multiplier follows the zone the player stands in (0x007833fa ..
    // 0x007834df): AreaTable's ambient multiplier m, taken from the parent zone unless the area
    // carries flag 0x2000, gives a target of 2m + 1, eased toward at the frame's tick and snapped
    // within a hundredth or after a second. The console's characterAmbient holds it while active.
    auto player = ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__);

    if (player && !CWorld::s_characterAmbientActive && player->m_worldObject) {
        uint32_t areaID = 0;
        CWorld::GetEntityAreaID(reinterpret_cast<CMapStaticEntity*>(player->m_worldObject), &areaID);

        auto area = g_areaTableDB.GetRecord(areaID);

        if (area && !(area->m_flags & 0x2000) && area->m_parentAreaID) {
            area = g_areaTableDB.GetRecord(area->m_parentAreaID);
        }

        if (area) {
            float target = area->m_ambientMultiplier * 2.0f + 1.0f;
            float delta = target - CWorld::s_characterAmbient;
            float dt = CWorld::s_tickTimeSec;

            if (std::fabs(delta) < 0.01f || dt >= 1.0f) {
                CWorld::s_characterAmbient = target;
            } else {
                CWorld::s_characterAmbient += delta * dt;
            }
        }
    }

    // The world scene learns which side of the liquid the camera is on (0x007834e5).
    CWorld::s_m2Scene->m_cameraLiquidType = CWorldScene::s_cameraLiquidType;
}

// ref: FUN_0077f030
// The weather's draw, which CGWorldFrame::OnWorldRender calls on each side of the liquid pass
void CWorld::RenderWeather() {
    CWorld::s_weather->Render();
}

float CWorld::GetCloudDensity() {
    return CWorld::s_cloudDensity;
}

const C3Vector& CWorld::GetBodyTint() {
    return CWorld::s_bodyTint;
}

const C3Vector& CWorld::GetCameraPos() {
    return s_lightPos;
}

const C3Vector& CWorld::GetCameraDir() {
    return CWorld::s_cameraDir;
}

// ref: FUN_0077f490
void CWorld::SetNearClip(float nearClip) {
    CWorld::s_nearClip = nearClip;
}

// ref: FUN_0077f4a0
void CWorld::SetHorizonFarClipScale(float scale) {
    CWorld::s_horizonFarClipScale = scale;
}

// ref: FUN_0077f4b0
void CWorld::SetHorizonNearClipScale(float scale) {
    CWorld::s_horizonNearClipScale = scale;
}

// The distance bands (DAT_00adf350, see WorldDetailBands)
static WorldDetailBands s_detailBands = {
    { 1.0f, 4.0f, 15.0f, 100.0f, 100000.0f },
    { 30.0f, 100.0f, 200.0f, 750.0f, 1250.0f },
    { 1.0f, 4.0f, 15.0f, 100.0f, 100000.0f },
    { 5.0f, 10.0f, 15.0f, 20.0f, 50.0f },
    { 30.0f, 100.0f, 200.0f, 750.0f, 1250.0f },
    { 900.0f, 10000.0f, 40000.0f, 562500.0f, 1562500.0f },
    { 25.0f, 90.0f, 185.0f, 730.0f, 1200.0f },
    { 625.0f, 8100.0f, 34225.0f, 532900.0f, 1440000.0f },
};

const WorldDetailBands& CWorld::GetDetailBands() {
    return s_detailBands;
}

// ref: FUN_00781610
// The fixed-function fog states from the current light: start and end only on a device
// without vertex shaders (the shaders fog for themselves), the colour always
void CWorld::SetupFogRenderStates() {
    if (GxCaps().m_shaderTargets[GxSh_Vertex] == 0) {
        g_theGxDevicePtr->RsSet(GxRs_FogStart, CWorld::GetFogStart());
        g_theGxDevicePtr->RsSet(GxRs_FogEnd, CWorld::GetFogEnd());
    }

    const C3Vector& fog = CWorld::GetFogColor();
    CImVector color;
    color.b = CM2Lighting::FogColorByte(fog.z);
    color.g = CM2Lighting::FogColorByte(fog.y);
    color.r = CM2Lighting::FogColorByte(fog.x);
    color.a = 0xFF;
    g_theGxDevicePtr->RsSet(GxRs_FogColor, color.value);
}

// ref: FUN_0078f570
void CWorld::SetEnvironmentDetail(float detail) {
    for (int32_t i = 0; i < 5; i++) {
        s_detailBands.nearDist[i] = s_detailBands.defaultNear[i];

        float farDist = s_detailBands.defaultFar[i];
        if (i != 0 && i != 4) {
            farDist *= detail;
        }

        s_detailBands.farDist[i] = farDist;
        s_detailBands.fadeStart[i] = farDist - s_detailBands.fadeWidth[i];
        s_detailBands.farDistSq[i] = farDist * farDist;
        s_detailBands.fadeStartSq[i] = s_detailBands.fadeStart[i] * s_detailBands.fadeStart[i];
    }
}

// ref: FUN_00780240
void CWorld::UpdateObject(HWORLDOBJECT object, const C44Matrix& matrix, const CAaBox& box,
                          const CAaSphere& sphere, const C3Vector& collisionCenter,
                          int32_t noRelink, uint32_t param) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    C3Vector position = { matrix.d0, matrix.d1, matrix.d2 };
    float scale = sqrtf(matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1 + matrix.a2 * matrix.a2);
    C3Vector collision = collisionCenter * matrix;

    CAaSphere placedSphere = { position, 0.0f };

    if (0.001f < sphere.r) {
        placedSphere.r = scale * sphere.r;
        placedSphere.c = sphere.c * matrix;
    }

    CAaBox placedBox = { position, position };

    if (box.b.x < box.t.x && box.b.y < box.t.y && box.b.z < box.t.z) {
        placedBox = TransformBox(box, matrix);
    }

    auto distSq = [](const C3Vector& a, const C3Vector& b) {
        float x = a.x - b.x;
        float y = a.y - b.y;
        float z = a.z - b.z;

        return z * z + y * y + x * x;
    };

    float moved = distSq(entity->m_position, position);
    float collisionMoved = distSq(entity->m_collisionCenter, collision);
    float bottomMoved = distSq(entity->m_bounds.b, placedBox.b);
    float topMoved = distSq(entity->m_bounds.t, placedBox.t);
    float sphereMoved = distSq(entity->m_sphere.c, placedSphere.c);
    float shrunk = entity->m_sphere.r - placedSphere.r;

    entity->m_position = position;
    entity->m_collisionCenter = collision;
    entity->m_scale = scale;
    entity->m_bounds = placedBox;
    entity->m_sphere = placedSphere;
    entity->m_param32 = param;

    if (noRelink) {
        return;
    }

    if (9.999999974752427e-07f < moved || 9.999999747378752e-05f < collisionMoved
        || 9.999999747378752e-05f < bottomMoved || 9.999999747378752e-05f < topMoved
        || 9.999999747378752e-05f < sphereMoved || 9.999999747378752e-05f < shrunk) {
        CMap::UpdateEntity(entity);
    }
}

// ref: FUN_00990560
// WMOAreaTable by (WMO id, name set, WMOGroupID). The reference binary-searches a copy sorted on
// those three; a linear walk of the table finds the same record.
static const WMOAreaTableRec* FindWMOArea(int32_t wmoID, int32_t nameSet, int32_t wmoGroupID) {
    for (uint32_t i = 0; i < g_wmoAreaTableDB.GetNumRecords(); i++) {
        const WMOAreaTableRec* rec = g_wmoAreaTableDB.GetRecordByIndex(i);

        if (rec && rec->m_wmoID == wmoID && rec->m_nameSetID == nameSet && rec->m_wmoGroupID == wmoGroupID) {
            return rec;
        }
    }

    return nullptr;
}

// ref: FUN_00782560
int32_t CWorld::GetEntityAreaID(CMapStaticEntity* entity, uint32_t* areaID) {
    // A global-WMO map is one area, Map.dbc's own.
    if (CMap::s_globalMapObj) {
        auto rec = g_mapDB.GetRecord(CMap::s_mapID);

        // Frozen-only null check: the reference reads the row unguarded.
        if (!rec || !rec->m_areaTableID) {
            return 0;
        }

        *areaID = static_cast<uint32_t>(rec->m_areaTableID);

        return 1;
    }

    if (!entity) {
        return 0;
    }

    auto link = entity->m_parentLinkList.Head();

    if (link) {
        if (link->ref && (link->ref->m_type & CMapBaseObj::Type_Chunk)) {
            *areaID = static_cast<CMapChunk*>(link->ref)->m_areaId;
            return 1;
        }

        CMapObjDef* def = nullptr;

        for (; link; link = entity->m_parentLinkList.Next(link)) {
            auto defGroupLink = link->ref ? link->ref->m_parentLinkList.Head() : nullptr;
            def = defGroupLink ? static_cast<CMapObjDef*>(defGroupLink->ref) : nullptr;

            if (def && !(def->m_flags & 0x400)) {
                break;
            }
        }

        if (link && def && def->m_mapObj && def->m_mapObj->m_mohd) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->ref);
            CMapObjGroup* group = def->m_mapObj->GetGroup(defGroup->m_groupIndex, 0);

            if (group) {
                const WMOAreaTableRec* rec = FindWMOArea(def->m_mapObj->m_mohd->wmoID, def->m_nameSet, group->m_groupID);

                if (rec && rec->m_areaTableID) {
                    *areaID = rec->m_areaTableID;
                    return 1;
                }
            }
        }
    }

    *areaID = CMap::GetChunkAreaID(entity->m_position);
    return 1;
}

// ref: FUN_009905c0
const LiquidTypeRec* CWorld::GetAreaLiquidType(uint32_t areaID, uint32_t liquidType) {
    if (liquidType == 0) {
        return nullptr;
    }

    if (areaID && liquidType < 0x15) {
        uint32_t index = (liquidType - 1) & 0x3;
        const AreaTableRec* area = g_areaTableDB.GetRecord(areaID);

        if (area) {
            if (area->m_liquidTypeID[index] == 0 && area->m_parentAreaID != 0) {
                area = g_areaTableDB.GetRecord(area->m_parentAreaID);
            }

            if (area && area->m_liquidTypeID[index]) {
                return g_liquidTypeDB.GetRecord(area->m_liquidTypeID[index]);
            }
        }
    }

    return g_liquidTypeDB.GetRecord(liquidType);
}

// ref: FUN_0077f310
int32_t WorldQuerySegment(const C3Vector& start, const C3Vector& end, C3Vector* hit, float* t, uint32_t flags, void* result) {
    return CMap::QuerySegment(start, end, hit, t, flags, result) ? 1 : 0;
}

// ref: FUN_00780710
// A new density asks the detail doodads to rebuild.
void CWorld::SetGroundEffectDensity(int32_t density) {
    if (CWorld::s_groundEffectDensity != density) {
        CWorld::s_groundEffectDensity = density;
        DetailDoodad::s_rebuild = 1;
    }
}

// ref: FUN_00780730
void CWorld::SetGroundEffectDist(float dist) {
    if (dist != DetailDoodad::s_fadeDistance) {
        DetailDoodad::s_fadeDistance = dist;
        DetailDoodad::s_rebuild = 1;
        CWorld::s_groundEffectDistSq = dist * dist;
    }
}

// ref: FUN_00782740
// The hit records are in each owner's own space; every face is taken out to the world by its
// record's placement, its normal turned by the placement's rotation and normalised (rsqrtss when
// CMap::s_useSse is set, which is the only difference between the reference's two copies of the
// loop).
void CWorld::AddHitFacets(CFacetList& list, uint32_t owner0, uint32_t owner1) {
    uint32_t first = list.facets.Count();

    for (uint32_t r = 0; r < CMapObjGroup::s_hitRecordCount; r++) {
        const CMapObjHitRecord& record = CMapObjGroup::s_hitRecords[r];
        const uint16_t* tri = record.indices;

        for (uint32_t f = 0; f < record.faceCount; f++, tri += 3) {
            M2CollisionTriangle* facet = list.facets.New();

            facet->plane.n = { 0.0f, 0.0f, 1.0f };
            facet->plane.d = 0.0f;

            const C3Vector& v0 = record.vertices[tri[0]];
            const C3Vector& v1 = record.vertices[tri[1]];
            const C3Vector& v2 = record.vertices[tri[2]];
            const C44Matrix& m = *record.placement;

            float nx = (v1.y - v0.y) * (v2.z - v0.z) - (v1.z - v0.z) * (v2.y - v0.y);
            float ny = (v1.z - v0.z) * (v2.x - v0.x) - (v2.z - v0.z) * (v1.x - v0.x);
            float nz = (v2.y - v0.y) * (v1.x - v0.x) - (v1.y - v0.y) * (v2.x - v0.x);

            facet->plane.n.x = m.a0 * nx + m.b0 * ny + m.c0 * nz;
            facet->plane.n.y = m.a1 * nx + m.b1 * ny + m.c1 * nz;
            facet->plane.n.z = m.c2 * nz + m.b2 * ny + m.a2 * nx;

            facet->vertices[0] = v0 * m;
            facet->vertices[1] = v1 * m;
            facet->vertices[2] = v2 * m;

            C3Vector& n = facet->plane.n;
            float lengthSq = n.x * n.x + n.y * n.y + n.z * n.z;

            if (lengthSq == 0.0f) {
                SysMsgPrintf(SYSMSG_ERROR, "Found degenerate triangle -- data needs to be fixed\n");
                n = { 0.0f, 0.0f, 1.0f };
            } else {
                float inv = CMap::s_useSse ? FacetRsqrt(lengthSq) : 1.0f / sqrtf(lengthSq);
                n = { inv * n.x, inv * n.y, inv * n.z };
            }

            const C3Vector& p0 = facet->vertices[0];
            facet->plane.d = -(p0.x * n.x + p0.y * n.y + p0.z * n.z);
        }
    }

    uint32_t count = list.facets.Count();

    if (count != first) {
        list.owners.SetCount(count);

        for (uint32_t i = first; i < count; i++) {
            list.owners[i] = { owner0, owner1 };
        }
    }
}

// ref: FUN_0077f330
int32_t WorldQueryFrustumFacets(const CWFrustum& frustum, CFacetList& list, uint32_t flags, uint32_t* hitFlags) {
    return CMap::QueryFrustumFacets(frustum, list, flags, hitFlags) ? 1 : 0;
}

// ref: FUN_0077f9d0
void CWorld::RenderParticulates() {
    if ((CWorld::s_enables & CWorld::Enable_Particulates) && CWorldScene::s_cameraLiquidType != 0) {
        CWorld::s_particulates->Render();
    }
}

// ref: FUN_0077f2e0
void CWorld::UpdateObjectLighting(HWORLDOBJECT object) {
    CMap::UpdateEntityLighting(reinterpret_cast<CMapEntity*>(object));
}

// ref: FUN_0077f9a0
void CWorld::SetBarrierPoint(const C3Vector& position) {
    CWorldScene::s_barriers.moverPos = position;
}

// ref: FUN_0077f980
void CWorld::RenderBarriers(float dt) {
    CWorldScene::RenderBarriers(CWorldScene::s_cameraPos, dt);
}

// ref: FUN_0077f400
void CWorld::AddRipple(const C3Vector& position, float angle, float radius, float alphaPeak, float life, float radiusRate, int32_t kind, int32_t reserved) {
    WaterRipples::Add(position, angle, radius, alphaPeak, life, radiusRate, kind, reserved);
}

static STORM_EXPLICIT_LIST(SWModelFadeout, m_link) s_fadeouts;      // DAT_00adf1a8
static STORM_EXPLICIT_LIST(SWModelFadeout, m_link) s_freeFadeouts;  // DAT_00adf1b4

// ref: FUN_00783100
static SWModelFadeout* AllocFadeout() {
    void* memory = SMemAlloc(sizeof(SWModelFadeout), ".?AUSWModelFadeout@@", -2, 0x8);

    return memory ? new (memory) SWModelFadeout() : nullptr;
}

// The handler a fading object's entity is given: the scene tells it whether it was seen, and it
// keeps the model drawing and animating only while it is.
// ref: FUN_007823d0
static int32_t FadeoutEntityHandler(void* param, int32_t event, uint32_t guidLow, uint32_t guidHigh, uint32_t param32) {
    auto entity = static_cast<CMapEntity*>(param);

    if (!entity) {
        return 0;
    }

    CM2Model* model = entity->m_model;

    if (!(event & 1)) {
        if (!model->m_attachParent) {
            model->m_flag8 = 0;
            model->m_flag10000 = 0;
        } else {
            model->m_flag80 = 0;
            model->m_flag20000 = 0;
        }

        model->SetAnimating(0);

        model = entity->m_model;

        if (!model->m_attachParent) {
            model->m_flag10000 = 0;
        } else {
            model->m_flag20000 = 0;
        }

        return 1;
    }

    model->SetAnimating(1);
    model = entity->m_model;

    if (model->m_attachParent) {
        model->m_flag80 = 1;
        model->m_flag20000 = 1;
    } else {
        model->m_flag8 = 1;
        model->m_flag10000 = 1;
    }

    return 1;
}

static void RetireFadeout(SWModelFadeout* fadeout) {
    CWorld::RemoveObject(reinterpret_cast<HWORLDOBJECT>(fadeout->entity));
    fadeout->entity = nullptr;
    fadeout->m_link.Unlink();
    s_freeFadeouts.LinkToTail(fadeout);
}

// ref: FUN_00783630
void CWorld::FadeOutObject(HWORLDOBJECT object, float alpha, WOWGUID transport) {
    auto entity = reinterpret_cast<CMapEntity*>(object);

    if (!entity) {
        return;
    }

    if (!entity->m_model || !entity->m_model->IsDrawable(0, 0) || alpha < 0.01f || (entity->m_flags7c & 0x4)) {
        CWorld::RemoveObject(object);
        return;
    }

    SWModelFadeout* fadeout = s_freeFadeouts.Head();

    if (!fadeout) {
        fadeout = AllocFadeout();
    }

    fadeout->entity = entity;
    fadeout->startMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
    fadeout->alpha = alpha;
    fadeout->transport = 0;

    if (transport) {
        // TODO the reference looks the transport up as a game object (ClntObjMgrObjectPtr, type
        // 0x20) and stores the model's matrix times the inverse of the transport's world matrix,
        // which it gets through game-object vtable slot +0xc4. That slot is not identified in
        // frozen, so a fade on a transport is held in world space, as it is when the transport
        // has already gone.
    }

    fadeout->m_link.Unlink();
    s_fadeouts.LinkToTail(fadeout);

    entity->m_handler = reinterpret_cast<void*>(&FadeoutEntityHandler);
    entity->m_handlerParam = entity;
}

// ref: FUN_00782f20
void CWorld::UpdateFadeouts() {
    int32_t now = static_cast<int32_t>(OsGetAsyncTimeMs());

    for (auto fadeout = s_fadeouts.Head(); fadeout; ) {
        auto next = s_fadeouts.Next(fadeout);
        int32_t elapsed = now - static_cast<int32_t>(fadeout->startMs);
        CM2Model* model = fadeout->entity->m_model;

        if (elapsed > 2000 || !model->IsLoadedWithTextures()) {
            RetireFadeout(fadeout);
            fadeout = next;
            continue;
        }

        if (fadeout->transport) {
            // TODO model->matrixB4 = fadeout->relative * (the transport's world matrix), with
            // m_flag8000 set; see FadeOutObject. Never reached while the transport is not stored.
        }

        float t = (1.0f - static_cast<float>(elapsed) * 0.0005f) * fadeout->alpha;

        if (t < 0.0f) {
            model->m_baseAlpha = 0.0f;
        } else if (t > 1.0f) {
            model->m_baseAlpha = 1.0f;
        } else {
            model->m_baseAlpha = (3.0f - (t + t)) * t * t;
        }

        fadeout = next;
    }
}

// ref: FUN_00782e40
void CWorld::ClearFadeouts() {
    for (auto fadeout = s_fadeouts.Head(); fadeout; ) {
        auto next = s_fadeouts.Next(fadeout);
        RetireFadeout(fadeout);
        fadeout = next;
    }
}

// ref: FUN_00783780
void CWorld::FreeFadeoutPool() {
    for (auto fadeout = s_freeFadeouts.Head(); fadeout; fadeout = s_freeFadeouts.Head()) {
        fadeout->m_link.Unlink();
        fadeout->~SWModelFadeout();
        SMemFree(fadeout, ".?AUSWModelFadeout@@", -2, 0);
    }
}

int32_t CWorld::s_forceDayNight;
float CWorld::s_frameFogEnd;

namespace {

const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554

// One light zone (0x30 bytes): an outline on a map that forces a light on near and inside it.
struct LightZone {
    int32_t mapID;          // +0x00
    int32_t unknown04;      // +0x04
    int32_t lightID;        // +0x08
    float* points;          // +0x0c, (x, y) pairs
    int32_t count;          // +0x10
    float offsetX;          // +0x14
    float offsetY;          // +0x18
    const char* path;       // +0x1c, an SVG path of L commands
    float minX;             // +0x20
    float minY;             // +0x24
    float maxX;             // +0x28
    float maxY;             // +0x2c
};

// The table as the reference's static constructor (0x009cdbc0) leaves it: every zone on map 571,
// in tile units shifted by the same origin, with its bounds emptied for the parser to grow.
LightZone s_lightZones[11] = {
    { 571, -1, 914, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[0], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 825, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[1], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 959, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[2], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 862, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[3], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1847, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[4], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1703, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[5], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1796, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[6], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1777, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[7], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1792, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[8], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1589, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[9], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
    { 571, -1, 1740, nullptr, 0, -1.6623375f, -145.7316f, s_lightZonePaths[10], 3.4028235e38f, 3.4028235e38f, -3.4028235e38f, -3.4028235e38f },
};

// The terrain shadow's colour, as an 8x8 texture of one colour (DAT_00cd7554..DAT_00cd7878).
HTEXTURE s_shadowModTexture;
uint32_t s_shadowModTexels[64];
uint32_t s_shadowModColor;

// ref: FUN_0077f4c0
void ShadowModCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Latch) {
        stride = w * 4;
        texels = userArg;
    }
}

// ref: FUN_007f9bf0
// The squared distance from `p` to the segment a..b.
float SegmentDistanceSq(const float* a, const float* b, const float* p) {
    float px = p[0] - a[0];
    float py = p[1] - a[1];
    float ex = b[0] - a[0];
    float ey = b[1] - a[1];
    float t = (ex * px + ey * py) / (ex * ex + ey * ey);

    if (t < 0.0f) {
        return py * py + px * px;
    }

    if (1.0f < t) {
        return (p[1] - b[1]) * (p[1] - b[1]) + (p[0] - b[0]) * (p[0] - b[0]);
    }

    float dx = p[0] - (ex * t + a[0]);
    float dy = p[1] - (t * ey + a[1]);

    return dy * dy + dx * dx;
}

// ref: FUN_007f9c90
// Whether `p` is inside the polygon, and its distance to the nearest edge.
bool PolygonContains(int32_t count, const float* points, const float* p, float* distance) {
    float best = 3.4028235e38f;
    bool inside = false;

    for (int32_t i = 0, j = count - 1; i < count; j = i++) {
        const float* a = &points[i * 2];
        const float* b = &points[j * 2];

        float d = SegmentDistanceSq(a, b, p);

        if (d < best) {
            best = d;
        }

        if (((a[1] < p[1] && p[1] <= b[1]) || (b[1] < p[1] && p[1] <= a[1]))
            && (b[0] - a[0]) * ((p[1] - a[1]) / (b[1] - a[1])) + a[0] < p[0]) {
            inside = !inside;
        }
    }

    *distance = sqrtf(best);

    return inside;
}

}

// ref: FUN_0077ed40
void CWorld::InitializeLightZones() {
    for (auto& zone : s_lightZones) {
        const char* p = zone.path;

        if (p) {
            for (const char* c = p; *c != 'z'; c++) {
                if (*c == 'M' || *c == 'L') {
                    zone.count++;
                }
            }

            float* out = static_cast<float*>(SMemAlloc(zone.count * 0xc, __FILE__, __LINE__, 0));
            zone.points = out;

            while (*p != 'z') {
                if (*p == 'M' || *p == 'L') {
                    const char* x = p + 2;

                    while (*p != ',') {
                        p++;
                    }

                    const char* y = p + 1;
                    p = y;

                    while (*p != ' ') {
                        p++;
                    }

                    out[0] = static_cast<float>(atof(x));
                    out[1] = static_cast<float>(atof(y));
                    out += 2;
                }

                p++;
            }
        }

        float* pt = zone.points;

        for (int32_t i = 0; i < zone.count; i++, pt += 2) {
            float x = zone.offsetX + pt[0];
            float y = (zone.offsetY + pt[1]) * 0.0009765625f;
            pt[0] = x * CHUNK_SIZE;
            pt[1] = y * 34133.332f;

            if (pt[0] < zone.minX) {
                zone.minX = pt[0];
            }

            if (pt[1] < zone.minY) {
                zone.minY = pt[1];
            }

            if (zone.maxX < pt[0]) {
                zone.maxX = pt[0];
            }

            if (zone.maxY < pt[1]) {
                zone.maxY = pt[1];
            }
        }

        zone.minX -= 50.0f;
        zone.minY -= 50.0f;
        zone.maxX += 50.0f;
        zone.maxY += 50.0f;
    }
}

// ref: FUN_00780660
void CWorld::SetShadowColor(const CImVector& color) {
    if (s_shadowModColor != color.value) {
        for (auto& texel : s_shadowModTexels) {
            texel = color.value;
        }

        if (!s_shadowModTexture) {
            CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1, 0, 0, 0);
            s_shadowModTexture = TextureCreate(static_cast<EGxTexTarget>(0), 8, 8, 0, static_cast<EGxTexFormat>(3), static_cast<EGxTexFormat>(2), flags, s_shadowModTexels, ShadowModCallback, "CWorld::shadowMod", 0);
        }

        auto gxTex = TextureGetGxTex(s_shadowModTexture, 1, nullptr);

        if (gxTex) {
            GxTexUpdate(gxTex, 0, 0, 8, 8, 1);
        }
    }

    s_shadowModColor = color.value;
}

// ref: FUN_0077eed0
void CWorld::UpdateLightZones(const C3Vector& cameraPos) {
    if (!DayNightGetBlock()) {
        return;
    }

    float p[2] = { -(cameraPos.y - 17066.666f), -(cameraPos.x - 17066.666f) };

    for (auto& zone : s_lightZones) {
        if (zone.mapID != CMap::s_mapID || !zone.points) {
            continue;
        }

        if (zone.minX <= p[0] && zone.minY <= p[1] && p[0] <= zone.maxX && p[1] <= zone.maxY) {
            float distance = 3.4028235e38f;

            if (PolygonContains(zone.count, zone.points, p, &distance)) {
                distance = -distance;
            }

            if (distance - 50.0f < 0.0f) {
                DayNightAddForcedLight(zone.lightID, -(distance - 50.0f));
            }
        }
    }
}

// ref: FUN_0077fb90
int32_t CWorld::QueryMapObjFog(SMOFog* fog, CMapObjDef** def, uint8_t* inside, TSGrowableArray<uint32_t>** groups, float* distance) {
    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (player && (player->Player()->flags & 0x10)) {
        return 0;
    }

    *distance = 3.4028235e38f;

    return CMap::QueryCameraFog(fog, def, inside, groups, distance);
}

// ref: FUN_007816f0
void CWorld::UpdateDayNight(int32_t force, const C3Vector* cameraPos) {
    auto block = DayNightGetBlock();
    float darken = 0.0f;

    int32_t fogMode = 1;
    int32_t vertexTarget = g_theGxDevicePtr->Caps().m_shaderTargets[GxSh_Vertex];

    if (vertexTarget == 0 || vertexTarget == 1) {
        fogMode = 0;
    }

    if (CMap::s_mapID < 0x212) {
        fogMode = 0;
    }

    DayNightSetFogMode(fogMode);

    for (int32_t i = 0; i < 5; i++) {
        block->forcedLight[i] = nullptr;
        block->forcedLightDepth[i] = 0.0f;
    }

    block->forcedLightCount = 0;

    if (cameraPos) {
        CWorld::UpdateLightZones(*cameraPos);
    }

    if (force) {
        if (cameraPos) {
            block->cameraPos = *cameraPos;
        }

        DayNightResetLightFade(1);
        DayNightClearFadeFlag(1);
    } else {
        darken = DayNightGetBodies()->sunGlare.darken * 0.35f;

        if (darken == 0.0f) {
            DayNightClearFadeFlag(1);
        }
    }

    DayNightUpdateCamera();
    DayNightUpdateClouds();
    DayNightUpdateStars();
    DayNightUpdateFog();

    CWorld::SetShadowColor(block->shadowColor);

    // A glare in view dims the outdoor colours.
    uint32_t keep = static_cast<uint32_t>(lrintf((1.0f - darken) * 255.0f)) & 0xFF;

    auto scale = [keep](CImVector& c) {
        uint32_t r = (c.r * keep + 0xFF) >> 8;
        uint32_t g = (c.g * keep + 0xFF) >> 8;
        uint32_t b = (c.b * keep + 0xFF) >> 8;
        c.value = (c.value & 0xFF000000) | ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
    };

    scale(block->ambient);
    scale(block->diffuse);

    if (CMap::s_outdoorLight) {
        CM2Light& light = CMap::s_outdoorLight->m_light;
        light.SetDirection(block->direction);
        light.m_ambColor = { block->ambient.r / 255.0f, block->ambient.g / 255.0f, block->ambient.b / 255.0f };
        light.m_dirColor = { block->diffuse.r / 255.0f, block->diffuse.g / 255.0f, block->diffuse.b / 255.0f };
        const CImVector& spec = block->info.color[9];
        light.m_specColor = { spec.r / 255.0f, spec.g / 255.0f, spec.b / 255.0f };
    }

    DayNightSetFogMode(0);

    CWorld::PublishDayNight();
}

// FROZEN-ONLY. The block, copied into the float-vector statics the stand-in light used to fill,
// because forty-odd readers (the terrain, liquid and sky passes) still take their light through
// CWorld's getters. Each one should read DayNightGetBlock() as its reference does; this goes when
// the last of them has been ported. The direction is the block's NEGATED, as the stand-in kept it:
// those readers dot it as the direction TO the light.
void CWorld::PublishDayNight() {
    auto block = DayNightGetBlock();
    const float k = 1.0f / 255.0f;

    auto rgb = [k](const CImVector& c) {
        return C3Vector { c.r * k, c.g * k, c.b * k };
    };

    CWorld::s_outdoorAmbient = rgb(block->ambient);
    CWorld::s_outdoorDiffuse = rgb(block->diffuse);
    CWorld::s_outdoorDirection = { -block->direction.x, -block->direction.y, -block->direction.z };
    CWorld::s_fogColor = rgb(block->finalFogColor);
    CWorld::s_fogStart = block->fogStart < 0.0f ? 0.0f : block->fogStart;
    CWorld::s_fogEnd = block->fogEnd;
    CWorld::s_fogRate = block->fogRate;

    const DNInfo& info = block->info;

    for (int32_t i = 0; i < 6; i++) {
        CWorld::s_skyColors[i] = rgb(info.color[DNInfo::Color_SkyTop + i]);
        CWorld::s_lightBands12to17[i] = rgb(info.color[12 + i]);
    }

    CWorld::s_bodyTint = rgb(info.color[9]);
    CWorld::s_cloudColor1 = rgb(info.color[10]);
    CWorld::s_cloudColor2 = rgb(info.color[11]);
    CWorld::s_floatBand2 = info.floatBand[0];
    CWorld::s_cloudDensity = info.floatBand[1];
    CWorld::s_floatBand4 = info.floatBand[2];
    CWorld::s_floatBand5 = info.floatBand[3];
    CWorld::s_skyHighlight = info.highlightSky;

    for (int32_t i = 0; i < 4; i++) {
        CWorld::s_liquidAlpha[i] = info.liquidAlpha[i];
    }

    auto skybox = info.skybox[0].id ? g_lightSkyboxDB.GetRecord(info.skybox[0].id) : nullptr;

    if (skybox && skybox->m_name) {
        SStrCopy(s_skyboxPath, skybox->m_name, sizeof(s_skyboxPath));
    } else {
        s_skyboxPath[0] = '\0';
    }
}

float CWorld::GetHorizonDistance() {
    return CWorld::s_horizonFarClipScale * CWorld::s_farClip;
}
