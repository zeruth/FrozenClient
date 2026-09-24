#include "model/CM2Lighting.hpp"
#include "model/CM2Model.hpp"
#include "world/CWorld.hpp"
#include <tempest/ColorConvert.hpp>
#include "world/Terrain.hpp"
#include "gx/Gx.hpp"
#include "gx/Shader.hpp"
#include "model/Model2.hpp"
#include "world/CWorldParam.hpp"
#include "world/Map.hpp"
#include "world/Weather.hpp"
#include "db/Db.hpp"
#include "client/Client.hpp"
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <cstdio>
#include <cmath>
#include <vector>

uint32_t CWorld::s_curTimeMs;
float CWorld::s_curTimeSec;
uint32_t CWorld::s_enables;
uint32_t CWorld::s_enables2;
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
Weather* CWorld::s_weather;
C3Vector CWorld::s_outdoorAmbient = { 0.45f, 0.45f, 0.5f };
C3Vector CWorld::s_outdoorDiffuse = { 0.9f, 0.85f, 0.75f };
// Unit-length: CM2Lighting::AddDiffuse and the terrain/WMO bake dot this straight into N.L without
// renormalising, so a non-unit vector would scale every outdoor diffuse term. {-0.4,-0.3,0.86} has
// length 0.9948; normalised it keeps the same (still-placeholder) direction at exactly length 1.
C3Vector CWorld::s_outdoorDirection = { -0.402096f, -0.301572f, 0.864504f };
bool CWorld::s_cameraUnderLiquid = false;
C3Vector CWorld::s_cameraDir = { 1.0f, 0.0f, 0.0f };
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

        for (int32_t k = 0; k < 5; k++) {
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

HWORLDOBJECT CWorld::AddObject(CM2Model* model, void* handler, void* handlerParam, uint64_t param64, uint32_t param32, uint32_t objFlags) {
    auto entity = CMap::AllocEntity(objFlags & 0x8 ? true : false);

    entity->m_model = model;
    entity->m_param64 = param64;
    entity->m_param32 = param32;

    // TODO

    entity->m_dirLightScale = 1.0f;
    entity->m_dirLightScaleTarget = 1.0f;

    // TODO

    entity->m_type |= CMapBaseObj::Type_200;

    // TODO

    entity->m_flags = 0x0;

    if (objFlags & 0x20) {
        entity->m_flags = 0x20000;
    }

    // TODO

    return reinterpret_cast<HWORLDOBJECT>(entity);
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
        | Enables::Enable_1000000
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

    // TODO

    CWorld::s_m2Scene = M2CreateScene();

    // TODO

    uint32_t m2Flags = M2GetCacheFlags();
    CShaderEffect::InitShaderSystem(
        (m2Flags & 0x8) != 0,
        (CWorld::s_enables2 & Enables2::Enable_HwPcf) != 0
    );

    // TODO

    CMap::Initialize();

    // TODO

    CWorld::s_weather = STORM_NEW(Weather);

    // TODO
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

    CMap::Load(mapName, mapID);
    TerrainLoad(mapName, mapID);

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

    // TODO dword_D1C410 = 1;
    // TODO dword_ADEEE0 = 1;
}

// The reference's counterpart is FUN_004e3a20, identified 2026-09-23. CM2Model::SetupLighting
// invokes the callback through +0x2ac at 0x00831b70, passing (model, lighting, arg); five sites in
// the object code store 0x004e3a20 into that field, which is the same slot frozen fills from
// CGObject_C.
//
// **This body is a stand-in and the tag is NOT applied**, because the two do materially different
// things and claiming identity would say the port is worse than it is rather than that it is
// absent. What the reference does, from reading it -- 485 bytes, 11 branches -- so a real port has
// a starting point:
//
//   * indexes a global at 0x00ac436c into an array of 0x198-byte records at 0x00b6b240, bounds
//     checked against the count at 0x00b6b23c, and requires bit 0x2000 of that record's +0x170.
//   * gets a position from FUN_004e2790 on the model and calls CM2Lighting::Initialize with a
//     sphere centred there and a radius of ZERO -- so it RE-initialises the lighting that
//     SetupLighting already initialised and SelectLights already filled, down at least one of its
//     paths. That re-memset is the part to understand before porting: taken literally it discards
//     the scene lights the local-light chain now feeds in.
//   * builds temporary CM2Lights -- constructor, SetLightType, SetDirection, SetVisible -- and
//     hands them to CM2Lighting::AddLight twice.
//
// That last step is why this is worth recording now: every one of those is already ported and
// tagged. The machinery this callback drives exists; the driver does not.
//
// Unidentified callees it still needs: FUN_004e2790, FUN_0065c290, FUN_007ebf30 (the DayNight
// range), FUN_00982970, FUN_00834ab0, FUN_004e2730 and FUN_00834940.
//
// TODO the day/night cycle's light; until then every world model gets a fixed sun
void CWorld::LightingCallback(CM2Model* model, CM2Lighting* lighting, void* arg) {
    // Fog the model with the same data-driven distance fog the terrain and WMOs use. M2 materials
    // fog in the shader from the model's own lighting (the scene render turns the fixed-function fog
    // off), so it has to be set here or entities stay crisp against fogged terrain.
    if (CWorld::s_fogEnd > 1.0f && CWorld::s_fogEnd > CWorld::s_fogStart && CWorld::s_fogStart < CWorld::s_farClip) {
        lighting->SetFog(CWorld::s_fogColor, CWorld::s_fogStart, CWorld::s_fogEnd, CWorld::s_fogRate);
    }

    // A unit standing inside a WMO is lit by that building's interior lighting, not the outdoor sun,
    // exactly as the reference switches a model's lighting by the volume it occupies. The model's
    // world position is the translation column of its placement matrix.
    if (model) {
        C3Vector pos = { model->matrixB4.d0, model->matrixB4.d1, model->matrixB4.d2 };
        CImVector diffuse;
        CImVector ambient;

        // The reference's floor probe (CMapEntity::FloorLight): the MOCV under the model, split
        // into a diffuse and an ambient. The reference function that turns those two colours into
        // the model's lights has not been identified yet, so the diffuse is applied along the
        // outdoor sun direction here; the ambient is exact.
        if (TerrainWmoFloorLightAt(pos, &diffuse, &ambient)) {
            C3Vector amb;
            C3Vector dif;
            UnpackColor(amb, ambient);
            UnpackColor(dif, diffuse);
            lighting->AddAmbient(amb);
            lighting->AddDiffuse(dif, CWorld::s_outdoorDirection);
            return;
        }
    }

    // Outdoors: ambient and diffuse from Light.dbc (via ComputeOutdoorLight); the sun direction is
    // still fixed until the time-of-day arc is ported.
    lighting->AddAmbient(CWorld::s_outdoorAmbient);
    lighting->AddDiffuse(CWorld::s_outdoorDiffuse, CWorld::s_outdoorDirection);
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

void CWorld::Update(const C3Vector& cameraPos, const C3Vector& cameraTarget, const C3Vector& targetPos) {
    // The outdoor light is selected by the viewer's position (see UpdateOutdoorLight), so record the
    // camera each frame; the light and fog then reflect the zone the player is actually standing in.
    s_lightPos = cameraPos;

    C3Vector d = { cameraTarget.x - cameraPos.x, cameraTarget.y - cameraPos.y, cameraTarget.z - cameraPos.z };
    float len = sqrtf(d.x * d.x + d.y * d.y + d.z * d.z);

    if (len > 1e-4f) {
        CWorld::s_cameraDir = { d.x / len, d.y / len, d.z / len };
    }
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
