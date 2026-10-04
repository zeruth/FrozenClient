#include "world/DayNightLight.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "model/Model2.hpp"
#include "ui/game/CGCamera.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/map/CMapObj.hpp"
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/ColorConvert.hpp>
#include <cmath>
#include <cstring>
#include <new>

namespace {

DayNightBlock s_block;                      // 0x00d38b00
DNBodies s_bodies;

int32_t s_fogMode;                          // DAT_00d38acc: 1 when the device can do per-pixel fog
int32_t s_overrideActive;                   // DAT_00d38ad0
int32_t s_lightQueueByKey;                  // DAT_00d38ae0
uint32_t s_fadeFlags;                       // DAT_00d38184: bit 0 a timed fade, bit 1 a darkening
float s_fadeAmount;                         // DAT_00d38a88
float s_darken;                             // DAT_00d38a8c
float s_fadeDuration;                       // DAT_00d38a90
float s_fadeEnd;                            // DAT_00d38a98
float s_fadeStart;                          // DAT_00d38aa0
CImVector s_fadeTarget;                     // DAT_00d38d44

// A fog override pushed by FUN_007ed870 (spells and the like), and what it displaced.
float s_overrideFogRate;                    // DAT_00d38aa8
float s_overrideFogScalar;                  // DAT_00d38aac
float s_overrideFogEnd;                     // DAT_00d38ab0
int32_t s_savedDrawSky;                   // DAT_00d38ab4
float s_savedFogRate;                       // DAT_00d38ab8
float s_savedFogScalar;                     // DAT_00d38abc
float s_savedFogEnd;                        // DAT_00d38ac0
CImVector s_savedFogColor;                  // DAT_00d38d14
CImVector s_overrideFogColor;               // DAT_00d38d18

int32_t s_mapID;                            // DAT_00d38ac4
int32_t s_initialized;                      // DAT_00d38adc
int32_t s_skyReady;                         // DAT_00d38180
CM2Scene* s_skyScene;                       // DAT_00d38ad4
uint32_t s_skySceneTime;                    // DAT_00d38ad8
uint8_t s_lightFadeRestart;                 // DAT_00d38d9a

// The map's lights: slot 0 the map-wide one (no position), then the positional ones
// (DAT_00d39004, a TSFixedArray of LightRec pointers).
struct {
    uint32_t max;
    uint32_t count;
    LightRec** data;
    uint32_t grow;
} s_mapLights;

// Lights the server has switched on or off over an area (DAT_00d39014, entries 0x18 bytes).
struct AreaLightOverride {
    int32_t lightID;
    LightRec* light;
    float weight;
    int32_t state;                          // 1 fading in, 2 fading out
    uint32_t lastMs;
    int32_t durationMs;
};

struct {
    uint32_t max;
    uint32_t count;
    AreaLightOverride* data;
    uint32_t grow;
} s_areaLights;

TSHashTable<DNSkyModel, HASHKEY_STRI> s_skyModels;    // DAT_00d38d1c

DNStars s_stars;                            // DAT_00d38ae4
DNDome s_dome;                              // DAT_00d38d4c

// The reference's per-frame band tables, built on first use (the static guards at 0x00d39104 and
// 0x00d39208). Keys are (time of day, value) pairs.
const float s_sunThetaKeys[] = { 0.0f, 2.2165682f, 0.25f, 1.9198622f, 0.5f, 2.2165682f, 0.75f, 1.9198622f };
const float s_sunPhiKeys[] = { 0.0f, 3.926991f, 0.25f, 3.926991f, 0.5f, 3.926991f, 0.75f, 3.926991f };

const float s_starAlphaKeys[] = { 0.125f, 1.0f, 0.1875f, 0.0f, 0.9375f, 0.0f, 1.0f, 1.0f };     // 0x00af4c20
const float s_bodyBandAKeys[] = { 0.25f, 1.0f, 0.2916666567f, 0.0f, 0.8541666269f, 0.0f, 0.8958333134f, 1.0f };   // 0x00af4c80
const float s_bodyBandBKeys[] = { 0.0833333358f, 0.25f, 0.5f, 1.0f };    // 0x00af4ca0

// The bodies' bands (FUN_007eecc0's static tables): each body's elevation and azimuth over the
// day, and the sun's and the moons' sizes.
const float s_sunThetaBand[] = { 0.22916667f, 1.7453293f, 0.4965278f, 0.087266468f, 0.5f, 0.087266468f, 0.5034722f, 0.087266468f, 0.8958333f, 1.7453293f };
const float s_sunPhiBand[] = { 0.22916667f, 0.78539819f, 0.5f, 0.78539819f, 0.8958333f, 0.78539819f };
const float s_moonThetaBand[] = { 0.0f, 0.61086524f, 0.0034722222f, 0.61086524f, 0.16666667f, 1.7453293f, 0.91666669f, 1.7453293f, 0.99652779f, 0.61086524f };
const float s_moonPhiBand[] = { 0.0f, 0.78539819f, 0.16666667f, 0.78539819f, 0.91666669f, 0.78539819f };
const float s_moon2ThetaBand[] = { 0.0f, 0.61086524f, 0.0034722222f, 0.61086524f, 0.16666667f, 1.7453293f, 0.91666669f, 1.7453293f, 0.99652779f, 0.61086524f };
const float s_moon2PhiBand[] = { 0.0f, 2.3561945f, 0.16666667f, 2.6179938f, 0.91666669f, 2.8797934f };
const float s_sunSizeBand[] = { 0.25f, 2.0f, 0.28125f, 1.0f, 0.84375f, 1.0f, 0.875f, 2.0f };
const float s_moonSizeBand[] = { 0.041666672f, 1.0f, 0.16666667f, 1.5f, 0.91666669f, 1.5f, 0.99930561f, 1.0f };

int32_t TimeToHalfMinutes(float timeOfDay) {
    return static_cast<int32_t>(lrintf(timeOfDay * 2880.0f - 0.5f));
}

}

// ref: FUN_007ecef0
DayNightBlock* DayNightGetBlock() {
    return &s_block;
}

DNBodies* DayNightGetBodies() {
    return &s_bodies;
}

DNStars* DayNightGetStars() {
    return &s_stars;
}

DNDome* DayNightGetDome() {
    return &s_dome;
}

int32_t DayNightSkyReady() {
    return s_skyReady;
}

CM2Scene* DayNightGetSkyScene() {
    return s_skyScene;
}

uint32_t DayNightGetSkySceneTime() {
    return s_skySceneTime;
}

void DayNightSetSkySceneTime(uint32_t time) {
    s_skySceneTime = time;
}

int32_t DayNightTimedFadeActive() {
    return (s_fadeFlags & 1) != 0;
}

float DayNightTimedFadeAmount() {
    return s_fadeAmount;
}

// ref: FUN_007ece00
int32_t DayNightOverrideActive() {
    return s_overrideActive;
}

// ref: FUN_007ecc90
void DayNightSetFogMode(int32_t mode) {
    if (mode < 2) {
        s_fogMode = mode;
    }
}

// ref: FUN_007eccb0
static void DayNightClearForcedLights(DayNightBlock* block) {
    for (int32_t i = 0; i < 5; i++) {
        block->forcedLight[i] = nullptr;
        block->forcedLightDepth[i] = 0.0f;
    }

    block->forcedLightCount = 0;
}

// ref: FUN_007ecec0
void DayNightSetForcedParams(uint32_t index) {
    s_block.forcedParams = index;

    if (7 < index) {
        s_block.forcedParams = -1;
    }
}

// ref: FUN_007ecee0
void DayNightClearForcedParams() {
    s_block.forcedParams = -1;
}

// ref: FUN_007ecf00
void DayNightClearFadeFlag(uint8_t bit) {
    s_fadeFlags &= ~(1u << (bit & 0x1f));
}

// ref: FUN_007f1070
void DayNightResetLightFade(char now) {
    s_block.mapObjFogBlend = 0.0f;
    s_block.mapObjFogDef = 0;
    s_block.mapObjFogGroups.count = 0;

    if (now) {
        s_lightFadeRestart = 1;
    }
}

// ref: FUN_007eae70
// Which two keys of a day band bracket `time` (half-minutes, 0..2880), and how far between them.
// The band wraps across midnight, so a key below its predecessor closes the day.
static void FindBandSegment(int32_t count, const uint32_t* times, int32_t time, int32_t* cur, float* fraction, int32_t* next) {
    *next = 0;
    *fraction = 0.0f;
    *cur = 0;

    while (*cur < count) {
        int32_t t0 = static_cast<int32_t>(times[*cur]);
        *next = (*cur + 1) % count;
        int32_t t1 = static_cast<int32_t>(times[*next]);

        if (t0 < t1) {
            if (t0 <= time && time <= t1) {
                *fraction = static_cast<float>(time - t0) / static_cast<float>(t1 - t0);
                return;
            }
        } else if (time <= t1 || t0 <= time) {
            t1 += 2880;

            if (time < t0) {
                time += 2880;
            }

            *fraction = static_cast<float>(time - t0) / static_cast<float>(t1 - t0);
            return;
        }

        (*cur)++;
    }
}

// ref: FUN_007eb070
static CImVector InterpIntBand(const LightIntBandRec* band, int32_t time) {
    CImVector out;

    if (band->m_num == 0) {
        out.value = 0xFF000000;
        return out;
    }

    int32_t cur;
    int32_t next;
    float f;
    FindBandSegment(band->m_num, band->m_times, time, &cur, &f, &next);

    auto channel = [&](int32_t shift) {
        int32_t a = (band->m_values[cur] >> shift) & 0xFF;
        int32_t b = (band->m_values[next] >> shift) & 0xFF;
        return static_cast<uint32_t>(lrintf(static_cast<float>(b - a) * f + static_cast<float>(a))) & 0xFF;
    };

    uint32_t r = channel(16);
    uint32_t g = channel(8);
    uint32_t b = channel(0);
    out.value = ((r | 0xFFFFFF00) << 8 | g) << 8 | b;

    return out;
}

// ref: FUN_007eaef0
// Band 0 is the fog end, which the DBC keeps in inches: it alone is scaled by 1/36 into yards.
static float InterpFloatBandRec(const LightFloatBandRec* rec, int32_t time, int32_t band) {
    if (rec->m_num == 0) {
        return 0.0f;
    }

    int32_t cur;
    int32_t next;
    float f;
    FindBandSegment(rec->m_num, rec->m_times, time, &cur, &f, &next);

    if (band == 0) {
        float from = rec->m_values[cur] * 0.027777778f;

        return (rec->m_values[next] * 0.027777778f - from) * f + from;
    }

    return (rec->m_values[next] - rec->m_values[cur]) * f + rec->m_values[cur];
}

// ref: FUN_007ebf30
static CImVector DNInterpBandColor(int32_t time, const LightParamsRec* params, int32_t band) {
    auto rec = g_lightIntBandDB.GetRecord(params->m_ID * 18 + band - 17);

    if (!rec) {
        CImVector out;
        out.value = 0xFF000000;
        return out;
    }

    return InterpIntBand(rec, time);
}

// ref: FUN_007ebf90
static float DNInterpFloatBand(int32_t time, const LightParamsRec* params, int32_t band) {
    auto rec = g_lightFloatBandDB.GetRecord(params->m_ID * 6 + band - 5);

    return rec ? InterpFloatBandRec(rec, time, band) : 0.0f;
}

// ref: FUN_007ebff0
static void DNComputeInfo(int32_t time, DNInfo* out, const LightParamsRec* params) {
    out->color[DNInfo::Color_Diffuse] = DNInterpBandColor(time, params, 0);
    out->color[DNInfo::Color_Ambient] = DNInterpBandColor(time, params, 1);

    for (int32_t band = 2; band <= 7; band++) {
        out->color[band + 1] = DNInterpBandColor(time, params, band);
    }

    out->color[DNInfo::Color_Band8] = DNInterpBandColor(time, params, 8);

    for (int32_t band = 9; band < 18; band++) {
        out->color[band] = DNInterpBandColor(time, params, band);
    }

    out->fogEnd = DNInterpFloatBand(time, params, 0);
    float scalar = DNInterpFloatBand(time, params, 1);
    out->fogStartScalar = scalar;

    if (scalar < -1.0f) {
        out->fogStartScalar = -1.0f;
    } else if (1.0f < scalar) {
        out->fogStartScalar = 1.0f;
    }

    out->fogRate = 1.0f;

    for (int32_t i = 0; i < 4; i++) {
        out->floatBand[i] = DNInterpFloatBand(time, params, i + 2);
    }

    out->highlightSky = static_cast<float>(params->m_highlightSky);
    out->glow = params->m_glow;
    out->liquidAlpha[0] = params->m_waterShallowAlpha;
    out->liquidAlpha[1] = params->m_waterDeepAlpha;
    out->liquidAlpha[2] = params->m_oceanShallowAlpha;
    out->liquidAlpha[3] = params->m_oceanDeepAlpha;
    out->skybox[0].id = params->m_lightSkyboxID;
    out->skybox[0].weight = 1.0f;
    out->cloudTypeID = params->m_cloudTypeID;
    out->weight = 1.0f;
}

// ref: FUN_007ecd00
// With per-pixel fog, a fog that ends short of the view distance is thickened toward the far
// clip: 1.5 at the far end, rising by up to 5.5 as the fog closes in.
static float DNFogRateFor(float start, float end) {
    float range = s_block.farClip;

    if (700.0f < range) {
        range = 700.0f;
    }

    if (end - start <= range - 200.0f) {
        return (1.0f - (end - start) / (range - 200.0f)) * 5.5f + 1.5f;
    }

    return 1.5f;
}

// ref: FUN_007ecd80
static void DNComputeInfoClamped(int32_t time, DNInfo* out, const LightParamsRec* params) {
    DNComputeInfo(time, out, params);

    if (out->fogEnd < 10.0f) {
        out->fogEnd = 10.0f;
    }

    if (s_fogMode == 1) {
        if (27.777779f <= out->fogEnd) {
            out->fogRate = DNFogRateFor(out->fogStartScalar * out->fogEnd, out->fogEnd);
            out->fogEnd = s_block.farClip;
        }

        if (out->fogStartScalar < 0.0f) {
            out->fogStartScalar = 0.0f;
        }
    }
}

// ref: FUN_007ee360
static void DNClearInfo(DNInfo* info) {
    memset(info, 0, sizeof(DNInfo));
}

// ref: FUN_007eb180
static const LightParamsRec* DNLightParams(const LightRec* light, int32_t slot) {
    return g_lightParamsDB.GetRecord(light->m_params[slot]);
}

// ref: FUN_007ec220
// Pull `info` toward `target` by `t`: the colours through LerpColor, the scalars linearly.
static void DNMixInfo(DNInfo* info, const DNInfo* target, float t) {
    float f = 0.0f;

    if (0.0f <= t) {
        f = 1.0f <= t ? 1.0f : t;
    }

    uint32_t alpha = static_cast<uint32_t>(lrintf(f * 255.0f)) & 0xFF;

    static const int32_t s_order[] = { 1, 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17 };

    for (int32_t slot : s_order) {
        if (alpha) {
            LerpColor(info->color[slot], alpha, target->color[slot]);
        }
    }

    info->fogEnd = (target->fogEnd - info->fogEnd) * f + info->fogEnd;
    info->fogStartScalar = (target->fogStartScalar - info->fogStartScalar) * f + info->fogStartScalar;
    info->fogRate = (target->fogRate - info->fogRate) * f + info->fogRate;

    if (alpha) {
        LerpColor(info->color[DNInfo::Color_Band8], alpha, target->color[DNInfo::Color_Band8]);
    }

    info->floatBand[1] = (target->floatBand[1] - info->floatBand[1]) * f + info->floatBand[1];
    info->glow = (target->glow - info->glow) * f + info->glow;

    for (int32_t i = 0; i < 4; i++) {
        info->liquidAlpha[i] = (target->liquidAlpha[i] - info->liquidAlpha[i]) * f + info->liquidAlpha[i];
    }
}

// ref: FUN_007ed2d0
CImVector SkyLerp(const CImVector& a, const CImVector& b, float t) {
    auto channel = [t](uint8_t from, uint8_t to) {
        float fa = static_cast<float>(from);
        float fb = static_cast<float>(to);
        float v = fb < fa ? fa - (fa - fb) * t : (fb - fa) * t + fa;
        return static_cast<uint8_t>(lrintf(v - 0.5f));
    };

    CImVector out;
    out.b = channel(a.b, b.b);
    out.g = channel(a.g, b.g);
    out.r = channel(a.r, b.r);
    out.a = 0xFF;

    return out;
}

static float DNLerp(float from, float to, float t) {
    return to < from ? from - (from - to) * t : (to - from) * t + from;
}

// ref: FUN_007ed4c0
// Blend `src` into `dst` by `t`, and record which skyboxes it brings with it: a skybox already
// present gains `t`, a new one takes the first free slot.
static void DNBlendInfo(DNInfo* dst, const DNInfo* src, float t) {
    dst->color[0] = SkyLerp(dst->color[0], src->color[0], t);
    dst->color[1] = SkyLerp(dst->color[1], src->color[1], t);
    dst->color[2] = SkyLerp(dst->color[2], src->color[2], t);

    for (int32_t i = 3; i < 9; i++) {
        dst->color[i] = SkyLerp(dst->color[i], src->color[i], t);
    }

    dst->fogEnd = DNLerp(dst->fogEnd, src->fogEnd, t);
    dst->fogStartScalar = DNLerp(dst->fogStartScalar, src->fogStartScalar, t);
    dst->fogRate = DNLerp(dst->fogRate, src->fogRate, t);
    dst->highlightSky = DNLerp(dst->highlightSky, src->highlightSky, t);

    for (int32_t i = 9; i < 14; i++) {
        dst->color[i] = SkyLerp(dst->color[i], src->color[i], t);
    }

    for (int32_t i = 14; i < 18; i++) {
        dst->color[i] = SkyLerp(dst->color[i], src->color[i], t);
    }

    dst->floatBand[1] = DNLerp(dst->floatBand[1], src->floatBand[1], t);
    dst->glow = DNLerp(dst->glow, src->glow, t);

    for (int32_t i = 0; i < 4; i++) {
        dst->liquidAlpha[i] = DNLerp(dst->liquidAlpha[i], src->liquidAlpha[i], t);
    }

    int32_t skybox = src->skybox[0].id;

    if (skybox && 0.0f < t) {
        for (int32_t i = 0; i < 3; i++) {
            if (dst->skybox[i].id == skybox) {
                dst->skybox[i].weight = t + dst->skybox[i].weight;

                if (1.0f < dst->skybox[i].weight) {
                    dst->skybox[i].weight = 1.0f;
                }

                break;
            }

            if (dst->skybox[i].id == 0) {
                dst->skybox[i].weight = t;
                dst->skybox[i].id = skybox;
                break;
            }
        }
    }

    dst->cloudTypeID = src->cloudTypeID;
    dst->weight = t;
}

// ref: FUN_007ed790
CImVector DayNightScaleSaturation(uint32_t color, float scale) {
    C3Vector rgb = {
        static_cast<float>((color >> 16) & 0xFF) * (1.0f / 255.0f),
        static_cast<float>((color >> 8) & 0xFF) * (1.0f / 255.0f),
        static_cast<float>(color & 0xFF) * (1.0f / 255.0f)
    };
    C3Vector hsv = { 0.0f, 0.0f, 0.0f };

    RgbToHsv(rgb, hsv);
    hsv.z = hsv.z * scale;
    HsvToRgb(hsv, rgb);

    CImVector out;
    PackColor(out, rgb);

    return out;
}

// ref: FUN_007ee510
// One light's record at the current time, with the storm variant of its params mixed in by the
// storm amount. Slot 0/1 are the clear params above and under water; 2/3 their storm versions.
static void DNComputeLightInfo(DNInfo* out, const LightRec* light, int32_t underwater) {
    int32_t time = TimeToHalfMinutes(s_block.timeOfDay);
    auto params = DNLightParams(light, underwater != 0);

    if (params) {
        DNComputeInfoClamped(time, out, params);
    }

    if (0.0f < s_block.storm) {
        DNInfo storm;
        DNClearInfo(&storm);
        time = TimeToHalfMinutes(s_block.timeOfDay);
        auto stormParams = DNLightParams(light, (underwater != 0) + 2);

        if (stormParams) {
            DNComputeInfoClamped(time, &storm, stormParams);
        }

        DNMixInfo(out, &storm, s_block.storm);
    }
}

// ref: FUN_007ee5d0
// A positional light, faded out across its falloff and blended in at no more than `weight`.
static void DNAddLight(DNInfo* dst, const LightRec* light, int32_t underwater, float weight) {
    DNInfo info;
    DNClearInfo(&info);
    DNComputeLightInfo(&info, light, underwater);

    float dx = s_block.queryPos.x - light->m_x;
    float dy = s_block.queryPos.y - light->m_y;
    float dz = s_block.queryPos.z - light->m_z;
    float dist = sqrtf(dy * dy + dz * dz + dx * dx);

    float f = 1.0f;

    if (light->m_falloffStart < dist) {
        f = 0.0f;
        float g = 1.0f - (dist - light->m_falloffStart) / (light->m_falloffEnd - light->m_falloffStart);

        if (0.0f <= g) {
            f = g;
        }
    }

    DNBlendInfo(dst, &info, f < weight ? f : weight);
}

// ref: FUN_007ee6b0
// A light the camera's liquid forces on, stronger the deeper the camera is.
static void DNAddForcedLight(DNInfo* dst, const LightRec* light, int32_t underwater, float weight, float depth) {
    DNInfo info;
    DNClearInfo(&info);
    DNComputeLightInfo(&info, light, underwater);

    float g = (100.0f - depth) * 0.01f;
    float f;

    if (g <= 0.0f) {
        f = 1.0f;
    } else {
        f = 1.0f - g;

        if (f < 0.0f) {
            f = 0.0f;
        }
    }

    DNBlendInfo(dst, &info, f < weight ? f : weight);
}

namespace {

// The reference's LightQE queue (FUN_007f0d40 and friends): a 1-based binary heap of (squared
// distance, light) pairs. Popping it hands back the FARTHEST light first, so the nearest one is
// blended in last and dominates.
struct LightQE {
    float key;
    LightRec** light;
};

// ref: FUN_007ed0a0
bool LightQEOrder(const LightQE& a, const LightQE& b) {
    if (s_lightQueueByKey) {
        return !(b.key < a.key);
    }

    const LightRec* la = *a.light;
    const LightRec* lb = *b.light;
    float dx = la->m_x - lb->m_x;
    float dy = la->m_y - lb->m_y;
    float dz = la->m_z - lb->m_z;

    if (0.33333334f < sqrtf(dx * dx + dy * dy + dz * dz)) {
        return b.key <= a.key;
    }

    return lb->m_falloffStart <= la->m_falloffStart;
}

struct LightQueue {
    LightQE* data = nullptr;
    uint32_t capacity = 0;
    uint32_t count = 1;

    ~LightQueue() {
        if (this->data) {
            SMemFree(this->data, ".?AVLightQE@@", -2, 0);
        }
    }

    // ref: FUN_007f0dc0
    void Push(float key, LightRec** light) {
        if (this->capacity < this->count + 1) {
            uint32_t grow = this->capacity ? this->capacity : 0x20;
            this->data = static_cast<LightQE*>(SMemReAlloc(this->data, (this->capacity + grow) * sizeof(LightQE), ".?AVLightQE@@", -2, 0));
            this->capacity += grow;
        }

        this->count++;

        LightQE entry = { key, light };
        uint32_t i = this->count - 1;

        while (1 < i) {
            uint32_t parent = i >> 1;

            if (!LightQEOrder(entry, this->data[parent])) {
                break;
            }

            this->data[i] = this->data[parent];
            i = parent;
        }

        this->data[i] = entry;
    }

    // ref: FUN_007f1280
    LightQE Pop() {
        LightQE top = this->data[1];
        LightQE last = this->data[this->count - 1];

        if (this->count) {
            this->count--;
        }

        if (1 < this->count) {
            uint32_t n = this->count - 1;
            uint32_t half = n >> 1;
            uint32_t i = 1;

            while (i <= half) {
                uint32_t child = i * 2;

                if (child < n && LightQEOrder(this->data[child + 1], this->data[child])) {
                    child++;
                }

                if (LightQEOrder(last, this->data[child])) {
                    break;
                }

                this->data[i] = this->data[child];
                i = child;
            }

            this->data[i] = last;
        }

        return top;
    }
};

}

// ref: FUN_007ecff0
static void ReserveAreaLights(uint32_t count) {
    s_areaLights.max = count;
    s_areaLights.data = static_cast<AreaLightOverride*>(SMemReAlloc(s_areaLights.data, count * sizeof(AreaLightOverride), ".?AUAreaLightOverride@@", -2, 0));
}

// ref: FUN_007f0cc0
static AreaLightOverride* NewAreaLight() {
    uint32_t want = s_areaLights.count + 1;

    if (s_areaLights.max < want) {
        uint32_t grow = s_areaLights.grow ? s_areaLights.grow : (want < 0x20 ? 0x20 : want);

        if (want % grow) {
            want = grow - want % grow + want;
        }

        ReserveAreaLights(want);
    }

    return &s_areaLights.data[s_areaLights.count++];
}

// ref: FUN_007f1360
// The positional half of the light: forced liquid lights, then the map's lights in reach of the
// query point by distance, then the area overrides at their current fade.
static void DNGatherLights(DNInfo* dst, int32_t underwater) {
    LightQueue queue;

    if (s_block.forcedLightCount) {
        for (int32_t i = 0; i < s_block.forcedLightCount; i++) {
            DNAddForcedLight(dst, s_block.forcedLight[i], underwater, 1.0f, s_block.forcedLightDepth[i]);
        }

        for (int32_t i = 0; i < 5; i++) {
            s_block.forcedLight[i] = nullptr;
            s_block.forcedLightDepth[i] = 0.0f;
        }

        s_block.forcedLightCount = 0;
    }

    for (uint32_t i = 1; i < s_mapLights.count; i++) {
        LightRec** slot = &s_mapLights.data[i];
        const LightRec* light = *slot;

        float dx = s_block.queryPos.x - light->m_x;
        float dy = s_block.queryPos.y - light->m_y;
        float dz = s_block.queryPos.z - light->m_z;
        float distSq = dy * dy + dz * dz + dx * dx;

        if (distSq < light->m_falloffEnd * light->m_falloffEnd) {
            queue.Push(distSq, slot);
        }
    }

    while (1 < queue.count) {
        LightQE entry = queue.Pop();
        DNAddLight(dst, *entry.light, underwater, 1.0f);
    }

    uint32_t count = s_areaLights.count;

    for (uint32_t i = 0; i < count; i++) {
        AreaLightOverride* area = &s_areaLights.data[i];
        uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
        uint32_t elapsed = now - area->lastMs;
        area->lastMs = now;

        if (area->state == 1) {
            float w = static_cast<float>(elapsed) / static_cast<float>(static_cast<uint32_t>(area->durationMs)) + area->weight;
            area->weight = w <= 1.0f ? w : 1.0f;
        } else if (area->state == 2) {
            float w = area->weight - static_cast<float>(elapsed) / static_cast<float>(static_cast<uint32_t>(area->durationMs));
            area->weight = 0.0f <= w ? w : 0.0f;
        }

        const LightRec* light = area->light;
        float dx = s_block.queryPos.x - light->m_x;
        float dy = s_block.queryPos.y - light->m_y;
        float dz = s_block.queryPos.z - light->m_z;

        if (0.0f < area->weight && dz * dz + dy * dy + dx * dx < light->m_falloffEnd * light->m_falloffEnd) {
            DNAddLight(dst, light, underwater, area->weight);
        }

        if (area->state == 2 && area->weight == 0.0f) {
            SMemFree(area->light, ".?AVLightRec@@", -2, 0);

            count--;
            *area = s_areaLights.data[count];

            if (s_areaLights.count < count && s_areaLights.max < count) {
                ReserveAreaLights(count);
            }

            s_areaLights.count = count;
            i--;
        }
    }
}

// ref: FUN_007f10a0
// The server has lit an area: `lightID` takes `sourceID`'s values at its own place, fading in.
void DayNightAddAreaLight(int32_t lightID, int32_t sourceID, int32_t durationMs) {
    auto light = g_lightDB.GetRecord(lightID);
    auto source = g_lightDB.GetRecord(sourceID);

    if (!light || !source) {
        return;
    }

    void* mem = SMemAlloc(sizeof(LightRec), ".?AVLightRec@@", -2, 0);

    if (!mem) {
        return;
    }

    auto copy = new (mem) LightRec(*source);
    copy->m_x = light->m_x;
    copy->m_y = light->m_y;
    copy->m_z = light->m_z;
    copy->m_falloffStart = light->m_falloffStart;
    copy->m_falloffEnd = light->m_falloffEnd;

    auto area = NewAreaLight();

    if (!area) {
        return;
    }

    area->light = copy;
    area->weight = 0.0f;
    area->lightID = lightID;
    area->state = 1;
    area->lastMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
    area->durationMs = durationMs;

    DayNightResetLightFade(1);
}

// ref: FUN_007f11a0
void DayNightRemoveAreaLight(int32_t lightID, int32_t durationMs) {
    for (uint32_t i = 0; i < s_areaLights.count; i++) {
        AreaLightOverride* area = &s_areaLights.data[i];

        if (area->lightID == lightID) {
            area->state = 2;
            area->lastMs = static_cast<uint32_t>(OsGetAsyncTimeMs());
            area->durationMs = durationMs;
            DayNightResetLightFade(1);
            return;
        }
    }
}

// ref: FUN_007ed150
// Queue a light the camera's liquid forces on, at the camera's depth below its surface.
void DayNightAddForcedLight(int32_t lightID, float depth) {
    if (5 <= s_block.forcedLightCount) {
        return;
    }

    auto light = g_lightDB.GetRecord(lightID);

    if (!light) {
        return;
    }

    s_block.forcedLight[s_block.forcedLightCount] = light;
    s_block.forcedLightDepth[s_block.forcedLightCount] = depth;
    s_block.forcedLightCount++;
}

// ref: FUN_007ed870
void DayNightBeginFogOverride(float startScalar, float end, CImVector color, int32_t drawSky) {
    if (!s_overrideActive) {
        s_savedFogEnd = s_block.info.fogEnd;
        s_savedFogColor = s_block.info.color[DNInfo::Color_Fog];
        s_savedFogScalar = s_block.info.fogStartScalar;
        s_savedDrawSky = s_block.drawSky;
        s_savedFogRate = s_block.info.fogRate;
    }

    s_overrideFogEnd = end;
    s_overrideFogScalar = startScalar;
    s_overrideFogRate = DNFogRateFor(end * startScalar, end);
    s_overrideFogColor = color;
    s_block.drawSky = drawSky;
    s_overrideActive = 1;
}

// ref: FUN_007ed820
void DayNightEndFogOverride() {
    if (s_overrideActive) {
        s_block.info.fogEnd = s_savedFogEnd;
        s_block.info.color[DNInfo::Color_Fog] = s_savedFogColor;
        s_block.info.fogStartScalar = s_savedFogScalar;
        s_block.drawSky = s_savedDrawSky;
        s_overrideActive = 0;
        s_block.info.fogRate = s_savedFogRate;
    }
}

// The half-way blend FUN_007ee750 writes twice, per channel: a + (b - a) / 2 in byte arithmetic.
static uint32_t HalfwayPacked(uint32_t from, uint32_t to) {
    auto channel = [](uint32_t a, uint32_t b) {
        return static_cast<uint8_t>(a + static_cast<uint8_t>(static_cast<uint32_t>(static_cast<int32_t>(b - a) << 7) >> 8));
    };

    uint32_t r = channel((from >> 16) & 0xFF, (to >> 16) & 0xFF);
    uint32_t g = channel((from >> 8) & 0xFF, (to >> 8) & 0xFF);
    uint32_t b = channel(from & 0xFF, to & 0xFF);

    return (r << 8 | g) << 8 | b;
}

// ref: FUN_007ee750
// The outdoor colours the models light with, out of the blended record.
static void DNUpdateColors() {
    uint32_t ambient = s_block.info.color[DNInfo::Color_Ambient].value;
    uint32_t diffuse = s_block.info.color[DNInfo::Color_Diffuse].value;

    s_block.diffuse.value = diffuse;

    // The ambient pulled halfway to the diffuse, then lifted by 16 per channel with a packed
    // saturating add.
    uint32_t half = HalfwayPacked(ambient, diffuse);
    uint32_t sum = half - 0xEFEFF0;
    uint32_t carry = ((half ^ sum) ^ 0xFFFEFEFF) & 0x1010100;
    uint32_t ambientHalf = (carry - (carry >> 8)) | (sum - carry);

    uint32_t diffuseHalf = HalfwayPacked(diffuse, ambient);

    s_block.lightDir = s_block.direction;
    s_block.ambient.value = ambient;
    s_block.ambientHalf.value = ambientHalf;
    s_block.diffuseHalf.value = diffuseHalf;
    s_block.lightDiffuseHalf.value = diffuseHalf;
    s_block.lightSpecular[0] = s_block.specular[0];
    s_block.lightSpecular[1] = s_block.specular[1];
    s_block.lightSpecular[2] = s_block.specular[2];
    s_block.lightSpecular[3] = s_block.specular[3];
    s_block.lightDiffuse.value = diffuse;
    s_block.lightAmbient.value = ambient;
    s_block.lightAmbientHalf.value = ambientHalf;

    uint32_t r = (ambient >> 16) & 0xFF;
    uint32_t g = (ambient >> 8) & 0xFF;
    uint32_t b = ambient & 0xFF;

    uint32_t shadow = ((((r + 3) * 0x55) >> 8) & 0xFF) << 16
                    | ((((g + 3) * 0x55) >> 8) & 0xFF) << 8
                    | ((((b + 3) * 0x55) >> 8) & 0xFF);
    shadow |= ambient & 0xFF000000;
    s_block.shadowColor.value = shadow;
    s_block.shadowColor.a = s_block.info.color[DNInfo::Color_Band8].r;

    C3Vector rgb = {
        static_cast<float>(r) * (1.0f / 255.0f),
        static_cast<float>(g) * (1.0f / 255.0f),
        static_cast<float>(b) * (1.0f / 255.0f)
    };
    C3Vector hsv = { 0.0f, 0.0f, 0.0f };

    RgbToHsv(rgb, hsv);
    hsv.y = hsv.y * 0.33f;
    hsv.z = hsv.z * 1.25f;
    HsvToRgb(hsv, rgb);

    s_block.specular[0] = rgb.x;
    s_block.specular[1] = rgb.y;
    s_block.specular[2] = rgb.z;
    s_block.specular[3] = 1.0f;
}

// The client's polynomial cosine of x * pi, the one the camera uses too.
static float DNCosPi(float x) {
    float fraction;
    int32_t whole;
    CameraSplitFloor(x, &fraction, &whole);

    float v = 1.0f - (6.0f - 4.0f * fraction) * fraction * fraction;

    return (whole & 1) ? -v : v;
}

// ref: FUN_007eea90
// The outdoor light's direction: azimuth pinned at 225 degrees, elevation wobbling twice a day
// between 37 and 20 degrees above the horizon. It points AWAY from the light.
void DNUpdateDirection() {
    float theta = InterpBodyBand(s_sunThetaKeys, 4, s_block.timeOfDay);
    float phi = InterpBodyBand(s_sunPhiKeys, 4, s_block.timeOfDay);

    float a = theta * 0.31830987f;
    float sinTheta = DNCosPi(a - 0.5f);
    float cosTheta = DNCosPi(a);

    float b = phi * 0.31830987f;
    float sinPhi = DNCosPi(b - 0.5f);
    float cosPhi = DNCosPi(b);

    s_block.direction.x = cosPhi * sinTheta;
    s_block.direction.y = sinTheta * sinPhi;
    s_block.direction.z = cosTheta;
}

// ref: FUN_007ed3b0
float InterpBodyBand(const float* keys, int32_t count, float t) {
    float f = t;

    if (f < 0.0f) {
        f = 0.0f;
    } else if (1.0f <= f) {
        f = 1.0f;
    }

    int32_t i = 0;

    while (i < count && !(f < keys[i * 2])) {
        i++;
    }

    int32_t prev;

    if (i == count) {
        i = 0;
        prev = count - 1;
    } else if (i == 0) {
        prev = count - 1;
    } else {
        prev = i - 1;
    }

    float span = keys[i * 2] - keys[prev * 2];

    if (fabsf(span) < 0.001f) {
        return keys[prev * 2 + 1];
    }

    if (span < 0.0f) {
        span += 1.0f;
    }

    float into = f - keys[prev * 2];

    if (into < 0.0f) {
        into += 1.0f;
    }

    float from = keys[prev * 2 + 1];
    float to = keys[i * 2 + 1];

    if (to < from) {
        return from - (from - to) * (into / span);
    }

    return from + (to - from) * (into / span);
}

// ref: FUN_007f30c0
DNSkyModel* DayNightGetSkyModel(const char* name, uint32_t flags) {
    if (!name || !*name) {
        return nullptr;
    }

    auto sky = s_skyModels.Ptr(name);

    if (sky) {
        return sky;
    }

    sky = s_skyModels.New(name, 0, 0);

    if (!s_skyScene) {
        s_skyScene = M2CreateScene();
        s_skySceneTime = static_cast<uint32_t>(OsGetAsyncTimeMs());
    }

    sky->m_model = s_skyScene->CreateModel(name, 0);
    sky->m_flags = flags;

    return sky;
}


// The highlight's strength over the day (0x00af4b7c) and its fall-off around the sky from the
// direction the camera faces (0x00af4bac).
static const float s_skyHighlightBand[12] = { 0.125f, 0.0f, 0.27083334f, 1.0f, 0.29166669f, 0.0f, 0.85416663f, 0.0f, 0.89583331f, 1.0f, 0.99930555f, 0.0f };
static const float s_skyAzimuthBand[12] = { 0.125f, 1.0f, 0.375f, 0.0f, 0.5f, -0.5f, 0.625f, -0.7f, 0.75f, -0.5f, 0.875f, 0.0f };

// ref: FUN_007f0530
// The dome's colours: the zenith from sky band 3, rings 1 to 4 from bands 4 to 7 pulled toward a
// highlight that follows the camera's heading (and that only a light with highlightSky has), the
// horizon ring and the nadir in the fog colour, each faded by the death fade.
static void DNUpdateDomeColors(DNDome* dome) {
    float highlight = InterpBodyBand(s_skyHighlightBand, 6, s_block.timeOfDay) * s_block.info.highlightSky;

    if (s_fadeFlags & 1) {
        for (int32_t slot = 3; slot < 9; slot++) {
            uint32_t alpha = static_cast<uint32_t>(lrintf((1.0f - s_fadeAmount) * 255.0f)) & 0xFF;

            if (alpha) {
                LerpColor(s_block.info.color[slot], alpha, s_fadeTarget);
            }
        }
    }

    CImVector toward[6] = {};

    for (int32_t i = 0; i < 5; i++) {
        toward[i + 1] = SkyLerp(s_block.info.color[4 + i], s_block.info.color[4], highlight);
    }

    CImVector* out = dome->colors;
    *out++ = DayNightScaleSaturation(s_block.info.color[3].value, 1.0f);

    float step = -1.0f / static_cast<float>(dome->segments);

    for (int32_t ring = 1; ring <= 4; ring++) {
        float u = s_block.cameraYaw * 0.15915494f + 0.25f;

        if (1.0f < u) {
            u = u - 1.0f;
        }

        for (int32_t j = 0; j < dome->segments; j++) {
            if (u < 0.0f) {
                u = u + 1.0f;
            }

            float w = InterpBodyBand(s_skyAzimuthBand, 6, u);
            CImVector c;

            if (0.0f <= w) {
                c = SkyLerp(s_block.info.color[ring + 3], toward[ring], (1.0f - w) * highlight);
            } else {
                CImVector up = SkyLerp(toward[ring], s_block.info.color[3], highlight * 0.7f);
                c = SkyLerp(toward[ring], up, -w * highlight);
            }

            if (s_block.fadeAlpha) {
                LerpColor(c, s_block.fadeAlpha, s_block.fadeColor);
            }

            *out++ = c;
            u = u + step;
        }
    }

    CImVector fog = s_block.info.color[DNInfo::Color_Fog];

    if (s_block.fadeAlpha) {
        LerpColor(fog, s_block.fadeAlpha, s_block.fadeColor);
    }

    for (int32_t j = 0; j < dome->segments; j++) {
        *out++ = fog;
    }

    fog = s_block.info.color[DNInfo::Color_Fog];

    if (s_block.fadeAlpha) {
        LerpColor(fog, s_block.fadeAlpha, s_block.fadeColor);
    }

    *out = fog;
}

// ref: FUN_007f3230
// The frame's light: the record the camera stands in, the outdoor colours from it, and the
// skyboxes it calls for.
static void DNUpdateLight() {
    float depth = 0.0f;
    uint32_t liquidType = CWorldScene::s_cameraLiquidType;
    depth = CWorldScene::s_cameraLiquidDepth;

    auto liquid = liquidType ? g_liquidTypeDB.GetRecord(liquidType) : nullptr;
    bool liquidLight = liquid && liquid->m_lightID != 0;

    for (int32_t i = 0; i < 3; i++) {
        s_block.skyWeight[i] = 0.0f;
        s_block.sky[i] = nullptr;
        s_block.skyFlag[i] = 0;
    }

    s_block.overrideSkyWeight = 0.0f;
    s_block.overrideSky = nullptr;

    if (s_mapLights.count == 0) {
        memset(&s_block.info, 0xFF, sizeof(DNInfo));
        s_block.info.fogEnd = 1e10f;
        s_block.info.color[DNInfo::Color_Ambient].value = 0xFF404040;
        s_block.info.fogStartScalar = 0.5f;
        s_block.info.cloudTypeID = 0;
        s_block.info.skybox[0].id = 0;
        s_block.info.fogRate = 4.0f;
        s_block.info.skybox[1].id = 0;
        s_block.info.skybox[2].id = 0;
        s_block.info.highlightSky = 0.0f;
        s_block.info.floatBand[0] = 0.0f;
        s_block.info.floatBand[1] = 0.0f;
        s_block.info.floatBand[2] = 0.0f;
        s_block.info.floatBand[3] = 0.0f;
        s_block.info.glow = 0.5f;
        s_block.info.liquidAlpha[2] = 0.75f;
        s_block.info.liquidAlpha[3] = 1.0f;
        s_block.info.liquidAlpha[1] = 1.0f;
        s_block.info.liquidAlpha[0] = 0.5f;
    } else if (!liquidType || !liquidLight) {
        DNInfo info;
        DNClearInfo(&info);
        const LightRec* global = s_mapLights.data[0];
        int32_t time = TimeToHalfMinutes(s_block.timeOfDay);

        auto params = DNLightParams(global, liquidType != 0);

        if (params) {
            DNComputeInfoClamped(time, &info, params);
        }

        if (0.0f < s_block.storm) {
            DNInfo storm;
            DNClearInfo(&storm);
            time = TimeToHalfMinutes(s_block.timeOfDay);
            auto stormParams = DNLightParams(global, (liquidType != 0) + 2);

            if (stormParams) {
                DNComputeInfoClamped(time, &storm, stormParams);
            }

            DNMixInfo(&info, &storm, s_block.storm);
        }

        DNGatherLights(&info, liquidType);

        const LightParamsRec* forced = s_block.forcedParams == -1 ? nullptr : DNLightParams(global, s_block.forcedParams);

        if (!forced) {
            s_block.info = info;
        } else {
            time = TimeToHalfMinutes(s_block.timeOfDay);
            DNComputeInfoClamped(time, &s_block.info, forced);

            // The forced params keep the gathered skyboxes, glow and liquid alphas.
            s_block.info.glow = info.glow;
            s_block.info.skybox[1].weight = info.skybox[1].weight;
            s_block.info.liquidAlpha[0] = info.liquidAlpha[0];
            s_block.info.cloudTypeID = info.cloudTypeID;
            s_block.info.skybox[0].id = info.skybox[0].id;
            s_block.info.liquidAlpha[1] = info.liquidAlpha[1];
            s_block.info.skybox[0].weight = info.skybox[0].weight;
            s_block.info.liquidAlpha[2] = info.liquidAlpha[2];
            s_block.info.skybox[1].id = info.skybox[1].id;
            s_block.info.liquidAlpha[3] = info.liquidAlpha[3];
            s_block.info.skybox[2].id = info.skybox[2].id;
            s_block.info.skybox[2].weight = info.skybox[2].weight;
            s_block.info.weight = info.weight;

            auto skybox = g_lightSkyboxDB.GetRecord(forced->m_lightSkyboxID);

            if (skybox) {
                s_block.overrideSky = DayNightGetSkyModel(skybox->m_name, skybox->m_flags);
                s_block.overrideSkyWeight = 1.0f;
            }
        }
    } else if (liquid) {
        auto params = g_lightParamsDB.GetRecord(liquid->m_lightID);
        int32_t time = TimeToHalfMinutes(s_block.timeOfDay);

        if (params) {
            DNComputeInfoClamped(time, &s_block.info, params);
        }
    }

    DNUpdateColors();

    // Under liquid that darkens, the fog, the ambient and the diffuse lose saturation with depth.
    if (liquidType && liquid && 0.0f < liquid->m_maxDarkenDepth) {
        float maxDepth = -liquid->m_maxDarkenDepth;
        float surface = 0.0f < depth ? 0.0f : depth;
        float clamped = maxDepth < surface ? surface : maxDepth;
        float k = (1.0f - clamped / maxDepth) - 1.0f;

        s_block.fogColor = DayNightScaleSaturation(s_block.fogColor.value, k * liquid->m_fogDarkenIntensity + 1.0f);
        s_block.ambient = DayNightScaleSaturation(s_block.ambient.value, k * liquid->m_ambDarkenIntensity + 1.0f);
        s_block.diffuse = DayNightScaleSaturation(s_block.diffuse.value, k * liquid->m_dirDarkenIntensity + 1.0f);
    }

    DNUpdateDomeColors(&s_dome);

    // The bodies and their glares take band 9's tint.
    CImVector tint = s_block.info.color[9];
    s_bodies.sun.color = tint;
    s_bodies.sunGlare.color = tint;
    s_bodies.moon.color = tint;
    s_bodies.moonGlare.color = tint;

    // The fade (+0x50 a strength, +0x51 a colour) pulls the outdoor colours and the bodies toward
    // its colour.
    if (s_block.fadeAlpha != 0) {
        CImVector target = s_block.fadeColor;

        LerpColor(s_block.fogColor, s_block.fadeAlpha, target);
        LerpColor(s_block.ambient, s_block.fadeAlpha, target);
        LerpColor(s_block.diffuse, s_block.fadeAlpha, target);
        LerpColor(s_bodies.sun.color, s_block.fadeAlpha, target);
        LerpColor(s_bodies.moon.color, s_block.fadeAlpha, target);
    }

    // A storm dims the bodies.
    if (s_block.storm != 0.0f) {
        uint8_t alpha = static_cast<uint8_t>(lrintf((1.0f - s_block.storm) * 255.0f));
        s_bodies.sun.color.a = alpha;
        s_bodies.sunGlare.color.a = alpha;
        s_bodies.moon.color.a = alpha;
        s_bodies.moonGlare.color.a = alpha;
        s_bodies.moon2.color.a = alpha;
    }

    // The skyboxes the record carries: an opaque one at full weight displaces any already chosen.
    int32_t n = 0;

    for (int32_t i = 0; i < 3 && n < 3; i++) {
        int32_t id = s_block.info.skybox[i].id;
        float weight = s_block.info.skybox[i].weight;

        if (!id || !(0.0f < weight)) {
            continue;
        }

        auto skybox = g_lightSkyboxDB.GetRecord(id);

        if (!skybox) {
            continue;
        }

        auto model = DayNightGetSkyModel(skybox->m_name, skybox->m_flags);
        uint32_t flag = skybox->m_flags & 2;

        if (0.99f < weight && n != 0 && flag == 0) {
            n = 0;
        }

        s_block.sky[n] = model;
        s_block.skyWeight[n] = weight;
        s_block.skyFlag[n] = flag;
        n++;
    }

    if (n < 3) {
        s_block.skyWeight[n] = 0.0f;
    }

    s_block.bodyBandA = InterpBodyBand(s_bodyBandAKeys, 4, s_block.timeOfDay);
    s_block.bodyBandB = InterpBodyBand(s_bodyBandBKeys, 2, s_block.timeOfDay);
}

// A body's place on the sky at twelve yards from the camera, from its elevation and azimuth.
static C3Vector DNBodyPosition(float theta, float phi) {
    float a = theta * 0.31830987f;
    float sinTheta = DNCosPi(a - 0.5f);
    float cosTheta = DNCosPi(a);

    float b = phi * 0.31830987f;
    float sinPhi = DNCosPi(b - 0.5f);
    float cosPhi = DNCosPi(b);

    float x = cosPhi * sinTheta;
    float y = sinTheta * sinPhi;
    float scale = 12.0f / sqrtf(y * y + cosTheta * cosTheta + x * x);

    return {
        s_block.cameraPos.x + x * scale,
        y * scale + s_block.cameraPos.y,
        scale * cosTheta + s_block.cameraPos.z
    };
}

// ref: FUN_007eecc0
// The sun, the moon and the second moon over the day, and how deep into dawn or dusk it is.
static void DNUpdateBodies() {
    float t = s_block.timeOfDay;

    s_bodies.sun.pos = DNBodyPosition(InterpBodyBand(s_sunThetaBand, 5, t), InterpBodyBand(s_sunPhiBand, 3, t));
    s_bodies.sun.size = InterpBodyBand(s_sunSizeBand, 4, t) * s_bodies.sun.scale;
    s_bodies.sunGlare.pos = s_bodies.sun.pos;

    s_bodies.moon.pos = DNBodyPosition(InterpBodyBand(s_moonThetaBand, 5, t), InterpBodyBand(s_moonPhiBand, 3, t));
    float moonSize = InterpBodyBand(s_moonSizeBand, 4, t) * s_bodies.moon.scale;
    s_bodies.moon.size = moonSize;
    s_bodies.moonGlare.sizeMin = moonSize;
    s_bodies.moonGlare.sizeMax = moonSize;
    s_bodies.moonGlare.pos = s_bodies.moon.pos;

    // The second moon runs on its own period, in 1/65536ths of a day.
    float period = s_bodies.moon2.scale2;
    uint32_t days = static_cast<uint32_t>(lrintf(s_block.dayCount * 65536.0f - 0.5f));
    uint32_t now = static_cast<uint32_t>(lrintf(s_block.timeOfDay * 65536.0f - 0.5f)) + days;
    uint32_t cycle = static_cast<uint32_t>(lrintf(static_cast<float>(floor((s_block.dayCount + s_block.timeOfDay) / period)) * period * 65536.0f - 0.5f));

    if (now < cycle) {
        cycle = now;
    }

    float t2 = static_cast<float>(now - cycle) * 1.5258789e-05f / period;

    s_bodies.moon2.pos = DNBodyPosition(InterpBodyBand(s_moon2ThetaBand, 5, t2), InterpBodyBand(s_moon2PhiBand, 3, t2));
    s_bodies.moon2.size = InterpBodyBand(s_moonSizeBand, 4, t2) * s_bodies.moon2.scale;

    if (0.22916667f <= t && t < 0.5f) {
        s_block.twilight = (t - 0.22916667f) * 3.6923077f;
    } else if (0.5f <= t && t < 0.8958333f) {
        s_block.twilight = 1.0f - (t - 0.5f) * 2.5263159f;
    } else if (0.91666669f < t && t < 1.0f) {
        s_block.twilight = (t - 0.91666669f) * 12.000003f;
    } else if (0.0f < t && t < 0.16666667f) {
        s_block.twilight = 1.0f - t * 6.0f;
    } else {
        s_block.twilight = 0.0f;
    }
}

// ref: FUN_007ee0d0
static void DNUpdateStarSky() {
    s_stars.pos = s_block.cameraPos;

    float band = InterpBodyBand(s_starAlphaKeys, 4, s_block.timeOfDay);
    s_stars.color.a = static_cast<uint8_t>(static_cast<int32_t>(band * 254.0f + 1.0f));
}

// ref: FUN_007eea80
void DayNightUpdateStars() {
    DNUpdateStarSky();
}

// ref: FUN_007f3920
void DayNightUpdateCamera() {
    s_block.cameraYaw = atan2f(s_block.cameraDir.y, s_block.cameraDir.x);

    if (s_block.cameraYaw < 0.0f) {
        s_block.cameraYaw += 6.2831855f;
    }

    s_block.storm = s_block.stormInput * 4.0f;

    if (1.0f < s_block.storm) {
        s_block.storm = 1.0f;
    }

    DNUpdateLight();
    DNUpdateDirection();
    DNUpdateBodies();
}

// ref: FUN_007ecb30
// Every Light.dbc row on the map: the one with no position goes first, the rest after it. A map
// with none borrows row 1's light.
static void DNLoadMapLights(int32_t mapID) {
    uint32_t count = 1;
    int32_t numRecords = g_lightDB.GetNumRecords();

    for (int32_t i = 0; i < numRecords; i++) {
        auto light = g_lightDB.GetRecordByIndex(i);

        if (light && light->m_mapID == mapID && (light->m_x != 0.0f || light->m_y != 0.0f || light->m_z != 0.0f)) {
            count++;
        }
    }

    if (s_mapLights.count < count && s_mapLights.max < count) {
        uint32_t grow = s_mapLights.grow ? s_mapLights.grow : count;
        uint32_t want = count % grow ? grow - count % grow + count : count;
        s_mapLights.data = static_cast<LightRec**>(SMemReAlloc(s_mapLights.data, want * sizeof(LightRec*), ".?AULightRef@@", -2, 0));
        s_mapLights.max = want;
    }

    s_mapLights.count = count;

    bool haveGlobal = false;
    uint32_t next = 1;

    for (int32_t i = 0; i < numRecords; i++) {
        auto light = g_lightDB.GetRecordByIndex(i);

        if (!light || light->m_mapID != mapID) {
            continue;
        }

        if (light->m_x == 0.0f && light->m_y == 0.0f && light->m_z == 0.0f) {
            haveGlobal = true;
            s_mapLights.data[0] = light;
        } else {
            s_mapLights.data[next++] = light;
        }
    }

    if (haveGlobal) {
        return;
    }

    s_mapLights.data[0] = g_lightDB.GetRecord(1);
}

// ref: FUN_007f2790
void DayNightInitialize(int32_t mapID) {
    // FROZEN-ONLY: the reference shuts the last map's DayNight down from CMap's unload
    // (FUN_007c3830 -> FUN_007f1d30), which frozen does not have yet; without this a second map
    // would make every sky resource again on top of the first's.
    if (s_initialized) {
        DayNightShutdown();
    }

    s_mapID = mapID;
    DNLoadMapLights(mapID);

    s_block.timeOfDay = 0.0f;
    s_block.dayCount = 0.0f;
    s_block.minutes = 0;
    s_block.playerPos = { 0.0f, 0.0f, 0.0f };
    s_block.cameraPos = { 0.0f, 0.0f, 0.0f };
    s_block.cameraYaw = 0.0f;
    s_block.queryPos = { 0.0f, 0.0f, 0.0f };
    s_block.farClip = 0.0f;
    s_block.timeSec = 0.0f;
    s_block.frameDelta = 0.0f;
    s_block.stormInput = 0.0f;
    s_block.overrideSkyWeight = 0.0f;
    s_block.cameraDir = { 0.0f, 0.0f, 0.0f };
    s_block.fadeAlpha = 0;
    s_block.fadeColor.value = 0;
    s_block.forcedParams = -1;
    s_block.overrideSky = nullptr;

    for (int32_t i = 0; i < 3; i++) {
        s_block.sky[i] = nullptr;
        s_block.skyWeight[i] = 0.0f;
        s_block.skyFlag[i] = 0;
    }

    s_block.mapObjFogBlend = 0.0f;
    s_block.mapObjFogDef = 0;
    s_block.mapObjFogGroups.count = 0;

    DayNightSkyInitialize();

    s_bodies.sun.scale = 1.0f;
    s_bodies.sun.scale2 = 1.0f;
    s_bodies.moon.scale = 1.75f;
    s_bodies.moon.scale2 = 1.0f;
    s_bodies.moon2.scale = 1.0f;
    s_bodies.moon2.scale2 = 1.7f;

    s_skyReady = 1;
    s_block.drawSky = 1;
    s_block.fadeAlpha = 0;
    s_fadeFlags = 0;

    s_initialized = 1;
}

// ref: FUN_007f1d30
void DayNightShutdown() {
    for (auto sky = s_skyModels.Head(); sky; ) {
        auto next = s_skyModels.Next(sky);

        if (sky->m_model) {
            sky->m_model->Release();
        }

        s_skyModels.Delete(sky);
        sky = next;
    }

    if (s_skyScene) {
        s_skyScene->Release();
    }

    s_skyScene = nullptr;
    s_block.overrideSky = nullptr;

    for (int32_t i = 0; i < 3; i++) {
        s_block.sky[i] = nullptr;
    }

    for (uint32_t i = 0; i < s_areaLights.count; i++) {
        SMemFree(s_areaLights.data[i].light, ".?AVLightRec@@", -2, 0);
    }

    s_areaLights.count = 0;

    DayNightSkyShutdown();
    s_skyReady = 0;

    s_initialized = 0;
}

// ref: FUN_007ed1b0
// The building fog the camera is in, set up the way the outdoor fog is.
static void DNSetupMapObjFog(float end, float startScalar, CImVector color) {
    s_block.mapObjFogEnd = s_block.farClip;

    if (end < s_block.farClip) {
        s_block.mapObjFogEnd = end;
    }

    s_block.mapObjFogStart = startScalar * s_block.mapObjFogEnd;
    s_block.mapObjFogColor = color;
    s_block.mapObjFogRate = 1.0f;

    if (s_block.mapObjFogEnd < 30.0f) {
        s_block.mapObjFogEnd = 30.0f;
    }

    if (s_fogMode == 1) {
        s_block.mapObjFogRate = DNFogRateFor(s_block.mapObjFogStart, s_block.mapObjFogEnd);
        s_block.mapObjFogEnd = s_block.farClip;

        if (s_block.mapObjFogStart < 0.0f) {
            s_block.mapObjFogStart = 0.0f;
        }
    }
}

// ref: FUN_007eea10
static void DNDarkenColor(CImVector* color) {
    uint32_t keep = static_cast<uint32_t>(lrintf((1.0f - s_darken) * 255.0f)) & 0xFF;
    uint32_t r = ((color->r * keep + 0xFF) >> 8) & 0xFF;
    uint32_t g = ((color->g * keep + 0xFF) >> 8) & 0xFF;
    uint32_t b = ((color->b * keep + 0xFF) >> 8) & 0xFF;

    color->value = (color->value & 0xFF000000) | r << 16 | g << 8 | b;
}

// ref: FUN_007ee9b0
// Pull a fog (colour, start, end, rate) in toward the camera and toward the fade colour.
static void DNFadeFog(CImVector* color, float* start, float* end, float* rate) {
    *start = s_fadeAmount * *start;
    *end = *end * s_fadeAmount;
    *rate = *rate * s_fadeAmount;

    uint32_t alpha = static_cast<uint32_t>(lrintf((1.0f - s_fadeAmount) * 255.0f)) & 0xFF;

    if (alpha) {
        LerpColor(*color, alpha, s_fadeTarget);
    }
}

// ref: FUN_007f16f0
// The frame's fog: the outdoor fog from the light (or an override), the building fog the camera is
// in when it is in one, blended by how far the camera is from the way out, and the timed fade.
void DayNightUpdateFog() {
    float end = s_block.farClip;

    if (!s_overrideActive) {
        if (s_block.info.fogEnd <= end) {
            end = s_block.info.fogEnd;
        }

        s_block.fogEnd = end;
        s_block.fogStart = end * s_block.info.fogStartScalar;
        s_block.fogColor = s_block.info.color[DNInfo::Color_Fog];
        s_block.fogRate = s_block.info.fogRate;
    } else {
        if (s_overrideFogEnd <= end) {
            end = s_overrideFogEnd;
        }

        s_block.fogEnd = end;
        s_block.fogStart = end * s_overrideFogScalar;
        s_block.fogColor = s_overrideFogColor;
        s_block.fogRate = s_overrideFogRate;
    }

    SMOFog fog;
    memset(&fog, 0, sizeof(fog));
    CMapObjDef* def = nullptr;
    uint8_t inside = 0;
    TSGrowableArray<uint32_t>* groups = nullptr;
    float distance = 0.0f;

    bool active = CWorld::QueryMapObjFog(&fog, &def, &inside, &groups, &distance) == 1;

    if (!active) {
        distance = 0.0f;
    }

    uint32_t liquidType = CWorldScene::s_cameraLiquidType;

    if (active) {
        auto liquid = liquidType ? g_liquidTypeDB.GetRecord(liquidType) : nullptr;
        int32_t under = 0;
        bool keepOutdoor = false;

        if (liquidType) {
            uint32_t flags = liquid ? liquid->m_flags : 0;
            under = 1;

            if ((flags & 0x20) && !(fog.flags & 0x100)) {
                under = 0;
            }

            if ((flags & 0x100) && !(fog.flags & 0x10)) {
                keepOutdoor = true;
            } else if (!under) {
                keepOutdoor = true;
            }
        }

        if (keepOutdoor) {
            s_block.mapObjFogStart = s_block.fogStart;
            s_block.mapObjFogColor = s_block.fogColor;
            s_block.mapObjFogEnd = s_block.fogEnd;
            s_block.mapObjFogRate = s_block.fogRate;
        } else {
            DNSetupMapObjFog(fog.fog[under].end, fog.fog[under].startScalar, fog.fog[under].color);

            if (under && liquid && (liquid->m_flags & 0x40)) {
                s_block.fogEnd = s_block.mapObjFogEnd;
                s_block.fogColor = s_block.mapObjFogColor;
                s_block.fogStart = s_block.mapObjFogStart;
                s_block.fogRate = s_block.mapObjFogRate;
            }
        }

        if (inside) {
            s_block.mapObjFogDef = 1;

            if (groups && groups->Count()) {
                uint32_t count = groups->Count();

                if (s_block.mapObjFogGroups.max < count) {
                    s_block.mapObjFogGroups.data = static_cast<uint32_t*>(SMemReAlloc(s_block.mapObjFogGroups.data, count * sizeof(uint32_t), ".I", -2, 0));
                    s_block.mapObjFogGroups.max = count;
                }

                for (uint32_t i = 0; i < count; i++) {
                    s_block.mapObjFogGroups.data[i] = (*groups)[i];
                }

                s_block.mapObjFogGroups.count = count;
            }
        }
    }

    float blend = distance * 0.04f;
    s_block.mapObjFogBlend = 0.0f;

    if (0.0f <= blend) {
        s_block.mapObjFogBlend = 1.0f <= blend ? 1.0f : blend;
    }

    s_block.mapObjFogDef = 0;
    s_block.mapObjFogGroups.count = 0;

    if (active && inside) {
        float t = s_block.mapObjFogBlend;
        s_block.finalFogEnd = (s_block.mapObjFogEnd - s_block.fogEnd) * t + s_block.fogEnd;
        s_block.finalFogStart = (s_block.mapObjFogStart - s_block.fogStart) * t + s_block.fogStart;
        s_block.finalFogRate = (s_block.mapObjFogRate - s_block.fogRate) * t + s_block.fogRate;

        CImVector color = s_block.fogColor;
        uint32_t alpha = static_cast<uint32_t>(lrintf(t * 255.0f)) & 0xFF;

        if (alpha) {
            LerpColor(color, alpha, s_block.mapObjFogColor);
        }

        s_block.finalFogColor = color;
    } else {
        s_block.finalFogColor = s_block.fogColor;
        s_block.finalFogStart = s_block.fogStart;
        s_block.finalFogEnd = s_block.fogEnd;
        s_block.finalFogRate = s_block.fogRate;
    }

    s_block.fogEnd = s_block.finalFogEnd;
    s_block.fogStart = s_block.finalFogStart;
    s_block.fogRate = s_block.finalFogRate;

    if (s_fogMode == 1 && liquidType) {
        s_block.fogRate = s_block.finalFogRate * 2.0f;
        s_block.finalFogRate = s_block.finalFogRate * 2.0f;
    }

    if (s_fadeFlags & 2) {
        DNDarkenColor(&s_block.fogColor);
        DNDarkenColor(&s_block.finalFogColor);
    }

    if (s_fadeFlags & 1) {
        if (s_fadeEnd < s_block.timeSec) {
            s_fadeFlags &= ~1u;
            return;
        }

        float x = static_cast<float>(exp2(static_cast<double>((s_block.timeSec - s_fadeStart) / s_fadeDuration * 7.2134752f)));
        float v = 1.0f / x;

        if (v < 0.0f) {
            v = 0.0f;
        } else if (1.0f <= v) {
            v = 1.0f;
        }

        s_fadeAmount = 1.0f - v;

        DNFadeFog(&s_block.fogColor, &s_block.fogStart, &s_block.fogEnd, &s_block.fogRate);
        DNFadeFog(&s_block.finalFogColor, &s_block.finalFogStart, &s_block.finalFogEnd, &s_block.finalFogRate);
    }
}

