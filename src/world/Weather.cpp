#include "world/Weather.hpp"
#include "world/Terrain.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include <common/DataStore.hpp>
#include <cstdio>

static CVar* s_useShadersCvar;

bool WeatherDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t weatherID = 0;
    float intensity = 0.0f;
    uint8_t abrupt = 0;

    msg->Get(weatherID);
    msg->Get(intensity);
    msg->Get(abrupt);

    // The reference logs "Weather changed to %d, intensity %f" (FUN_00526530) and applies the
    // Weather.dbc row: effectType 1 rain, 2 snow, 3 sand/mist, 0 or unknown = clear.
    auto rec = g_weatherDB.GetRecord(static_cast<int32_t>(weatherID));
    int32_t effectType = rec ? rec->m_effectType : 0;
    const float* color = rec ? rec->m_effectColor : nullptr;
    const char* texture = (rec && rec->m_effectTexture && *rec->m_effectTexture) ? rec->m_effectTexture : nullptr;

    fprintf(stderr, "Weather changed to %u, intensity %f\n", weatherID, intensity);

    TerrainSetWeather(effectType, intensity, color, texture, abrupt != 0);

    return 1;
}

Weather::Weather() {
    // TODO

    CVar::Register(
        "weatherDensity",
        nullptr,
        0x0,
        "2",
        &WeatherDensityCallback,
        DEFAULT
    );

    s_useShadersCvar = CVar::Register(
        "useWeatherShaders",
        nullptr,
        0x0,
        "1",
        nullptr,
        DEFAULT
    );
}

#include "world/Weather.hpp"
#include "world/Terrain.hpp"
#include "world/DayNight.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/ParticleFx.hpp"
#include "model/CM2Scene.hpp"
#include "model/CM2Model.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/Transform.hpp"
#include "gx/shader/CGxShader.hpp"
#include "util/CStatus.hpp"
#include <storm/String.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <cmath>
#include <cstdint>

namespace {

// ------------------------------------------------------------------------------------------------
// What this code used from Terrain.cpp, kept here so the move changes nothing it computes. The
// camera position is pushed in by WeatherSetCameraPos from where the terrain already tracks it; the
// view-projection and fog flag come through the terrain's own public accessors; and the UI shader
// pair stands in for the file-local pair plus EnsureShaders().
C3Vector s_cameraPos = { 0.0f, 0.0f, 0.0f };

CGxShader* s_uiVertexShader[1] = { nullptr };
CGxShader* s_uiPixelShader = nullptr;

void EnsureShaders() {
    TerrainUiShaders(s_uiVertexShader[0], s_uiPixelShader);
}

} // namespace

namespace {


const int32_t WEATHER_MAX_PARTICLES = 1024;
const float WEATHER_RADIUS = 28.0f; // half-size of the box around the camera, yards
const float WEATHER_TOP = 24.0f;
const float WEATHER_BOTTOM = -12.0f;

struct WeatherParticle {
    C3Vector pos;
    float speed;
    float phase;
};

int32_t s_weatherType = 0;          // 0 clear, 1 rain, 2 snow, 3 sand/mist
int32_t s_weatherTargetType = 0;
float s_weatherIntensity = 0.0f;    // current, eased toward the target
float s_weatherTarget = 0.0f;
C3Vector s_weatherColor = { 1.0f, 1.0f, 1.0f };
char s_weatherTexturePath[260] = { 0 };
HTEXTURE s_weatherTexture = nullptr;
char s_weatherTextureLoaded[260] = { 0 };
WeatherParticle s_weatherParticles[WEATHER_MAX_PARTICLES];
int32_t s_weatherAlive = 0;
uint32_t s_weatherRand = 0x12345678;
uint32_t s_weatherLastTime = 0;

C3Vector s_weatherPos[WEATHER_MAX_PARTICLES * 4];
C2Vector s_weatherUv[WEATHER_MAX_PARTICLES * 4];
CImVector s_weatherCol[WEATHER_MAX_PARTICLES * 4];
uint16_t s_weatherIdx[WEATHER_MAX_PARTICLES * 6];
bool s_weatherIdxBuilt = false;

float WeatherRand() {
    s_weatherRand = s_weatherRand * 1664525u + 1013904223u;
    return static_cast<float>((s_weatherRand >> 8) & 0xFFFF) / 65535.0f;
}

void WeatherSeed(WeatherParticle& p, const C3Vector& cam, bool atTop) {
    p.pos.x = cam.x + (WeatherRand() * 2.0f - 1.0f) * WEATHER_RADIUS;
    p.pos.y = cam.y + (WeatherRand() * 2.0f - 1.0f) * WEATHER_RADIUS;
    p.pos.z = cam.z + (atTop ? WEATHER_TOP : (WEATHER_BOTTOM + WeatherRand() * (WEATHER_TOP - WEATHER_BOTTOM)));
    p.phase = WeatherRand() * 6.2831853f;

    switch (s_weatherType) {
        case 1: p.speed = 22.0f + WeatherRand() * 8.0f; break;  // rain: fast, near-vertical streaks
        case 2: p.speed = 1.2f + WeatherRand() * 1.2f; break;   // snow: slow drifting flakes
        default: p.speed = 0.4f + WeatherRand() * 0.6f; break;  // mist: barely sinking sheets
    }
}

const char* WeatherDefaultTexture(int32_t type) {
    switch (type) {
        case 1: return "textures\\Weather\\RainDrop01.blp";
        case 2: return "textures\\Weather\\SnowMist01.blp";
        case 3: return "textures\\Weather\\WeatherMistGrainy01.blp";
        default: return nullptr;
    }
}


void WeatherUpdate(const C3Vector& cam, float dt) {
    // Ease toward the target; a type change waits for the old weather to fade out
    if (s_weatherType != s_weatherTargetType) {
        s_weatherIntensity -= dt * 0.5f;

        if (s_weatherIntensity <= 0.0f) {
            s_weatherIntensity = 0.0f;
            s_weatherType = s_weatherTargetType;
            s_weatherAlive = 0;
        }
    } else if (s_weatherIntensity < s_weatherTarget) {
        s_weatherIntensity = s_weatherIntensity + dt * 0.5f > s_weatherTarget ? s_weatherTarget : s_weatherIntensity + dt * 0.5f;
    } else if (s_weatherIntensity > s_weatherTarget) {
        s_weatherIntensity = s_weatherIntensity - dt * 0.5f < s_weatherTarget ? s_weatherTarget : s_weatherIntensity - dt * 0.5f;
    }

    if (!s_weatherType || s_weatherIntensity <= 0.0f) {
        s_weatherAlive = 0;
        return;
    }

    // Density: the weatherDensity cvar (0..3, default 2) scales the particle budget
    float density = 1.0f;
    CVar* dv = CVar::Lookup("weatherDensity");

    if (dv) {
        density = dv->GetFloat() * 0.5f;
        if (density < 0.0f) density = 0.0f;
        if (density > 1.5f) density = 1.5f;
    }

    int32_t budget = s_weatherType == 3 ? 160 : (s_weatherType == 2 ? 600 : 900);
    int32_t want = static_cast<int32_t>(budget * s_weatherIntensity * density);

    if (want > WEATHER_MAX_PARTICLES) want = WEATHER_MAX_PARTICLES;

    while (s_weatherAlive < want) {
        WeatherSeed(s_weatherParticles[s_weatherAlive++], cam, false);
    }

    if (s_weatherAlive > want) {
        s_weatherAlive = want;
    }

    for (int32_t i = 0; i < s_weatherAlive; i++) {
        WeatherParticle& p = s_weatherParticles[i];
        p.pos.z -= p.speed * dt;

        if (s_weatherType != 1) {
            p.phase += dt;
            p.pos.x += sinf(p.phase) * dt * 0.8f;
            p.pos.y += cosf(p.phase * 0.7f) * dt * 0.8f;
        }

        bool outside = p.pos.x < cam.x - WEATHER_RADIUS || p.pos.x > cam.x + WEATHER_RADIUS
            || p.pos.y < cam.y - WEATHER_RADIUS || p.pos.y > cam.y + WEATHER_RADIUS
            || p.pos.z > cam.z + WEATHER_TOP + 1.0f;

        if (p.pos.z < cam.z + WEATHER_BOTTOM) {
            WeatherSeed(p, cam, true);
        } else if (outside) {
            WeatherSeed(p, cam, false);
        }
    }
}



// Animated surface textures per LiquidType ("XTextures\\river\\lake_a.%d.blp", frames from 1)
struct LiquidTextures {
    int32_t liquidType = -1;
    HTEXTURE frames[32] = { nullptr };
    uint32_t frameCount = 0;
};

const int32_t MAX_LIQUID_TEXTURE_SETS = 16;
LiquidTextures s_liquidTextures[MAX_LIQUID_TEXTURE_SETS];

LiquidTextures* GetLiquidTextures(int32_t liquidType) {
    for (int32_t i = 0; i < MAX_LIQUID_TEXTURE_SETS; i++) {
        if (s_liquidTextures[i].liquidType == liquidType) {
            return &s_liquidTextures[i];
        }
    }

    for (int32_t i = 0; i < MAX_LIQUID_TEXTURE_SETS; i++) {
        LiquidTextures& set = s_liquidTextures[i];

        if (set.liquidType != -1) {
            continue;
        }

        set.liquidType = liquidType;
        auto rec = g_liquidTypeDB.GetRecord(liquidType);

        if (rec && rec->m_texture[0] && *rec->m_texture[0]) {
            // Stop at the first frame that does not exist. These sets have 30 frames
            // ("XTextures\river\lake_a.1.blp" .. ".30.blp"); probing past the end and relying on
            // TextureCreate to fail does not work, because a missing file still yields a
            // placeholder texture -- which showed up as the water flashing green once per cycle.
            for (uint32_t f = 1; f <= 32; f++) {
                char path[260];
                SStrPrintf(path, sizeof(path), rec->m_texture[0], f);

                if (!SFile::FileExists(path)) {
                    break;
                }

                CStatus status;
                HTEXTURE tex = TextureCreate(path, CGxTexFlags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1), &status, 0);

                if (!tex) {
                    break;
                }

                set.frames[set.frameCount++] = tex;
            }
        }

        return &set;
    }

    return nullptr;
}

} // namespace

void TerrainSetWeather(int32_t effectType, float intensity, const float* color, const char* texture, bool abrupt) {
    if (effectType < 1 || effectType > 3) {
        effectType = 0;
        intensity = 0.0f;
    }

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;

    s_weatherTargetType = effectType;
    s_weatherTarget = intensity;

    if (color) {
        s_weatherColor = { color[0], color[1], color[2] };
    } else {
        s_weatherColor = { 1.0f, 1.0f, 1.0f };
    }

    const char* path = texture ? texture : WeatherDefaultTexture(effectType);
    SStrCopy(s_weatherTexturePath, path ? path : "", sizeof(s_weatherTexturePath));

    if (abrupt || s_weatherType == 0) {
        s_weatherType = effectType;
        s_weatherIntensity = intensity;
        s_weatherAlive = 0;
    }
}

void WeatherRender() {
    uint32_t now = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
    float dt = s_weatherLastTime ? static_cast<float>(now - s_weatherLastTime) * 0.001f : 0.0f;
    s_weatherLastTime = now;

    if (dt > 0.1f) dt = 0.1f;

    WeatherUpdate(s_cameraPos, dt);

    if (!s_weatherAlive || CWorld::IsCameraUnderLiquid()) {
        return;
    }

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    if (SStrCmpI(s_weatherTexturePath, s_weatherTextureLoaded, 0x7FFFFFFF) != 0) {
        if (s_weatherTexture) {
            HandleClose(s_weatherTexture);
            s_weatherTexture = nullptr;
        }

        if (s_weatherTexturePath[0]) {
            CStatus status;
            s_weatherTexture = TextureCreate(s_weatherTexturePath, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 0);
        }

        SStrCopy(s_weatherTextureLoaded, s_weatherTexturePath, sizeof(s_weatherTextureLoaded));
    }

    if (!s_weatherTexture) {
        return;
    }

    if (!s_weatherIdxBuilt) {
        for (int32_t i = 0; i < WEATHER_MAX_PARTICLES; i++) {
            uint16_t b = static_cast<uint16_t>(i * 4);
            s_weatherIdx[i * 6 + 0] = b; s_weatherIdx[i * 6 + 1] = b + 1; s_weatherIdx[i * 6 + 2] = b + 2;
            s_weatherIdx[i * 6 + 3] = b; s_weatherIdx[i * 6 + 4] = b + 2; s_weatherIdx[i * 6 + 5] = b + 3;
        }

        s_weatherIdxBuilt = true;
    }

    // Camera basis for the billboards: right is horizontal, up follows the world for rain streaks
    // (so they hang vertically) and the camera for flakes and mist
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);

    if (rl < 1e-4f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right.x /= rl; right.y /= rl;
    }

    C3Vector up = { 0.0f, 0.0f, 1.0f };

    if (s_weatherType != 1) {
        up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };
    }

    float halfW, halfH, alphaScale;

    switch (s_weatherType) {
        case 1: halfW = 0.035f; halfH = 0.9f; alphaScale = 0.55f; break;
        case 2: halfW = 0.16f; halfH = 0.16f; alphaScale = 0.9f; break;
        default: halfW = 3.0f; halfH = 2.0f; alphaScale = 0.35f; break;
    }

    float a = s_weatherIntensity * alphaScale;
    CImVector col;
    col.b = static_cast<uint8_t>(s_weatherColor.z * 255.0f);
    col.g = static_cast<uint8_t>(s_weatherColor.y * 255.0f);
    col.r = static_cast<uint8_t>(s_weatherColor.x * 255.0f);
    col.a = static_cast<uint8_t>((a > 1.0f ? 1.0f : a) * 255.0f);

    for (int32_t i = 0; i < s_weatherAlive; i++) {
        const C3Vector& p = s_weatherParticles[i].pos;
        C3Vector rx = { right.x * halfW, right.y * halfW, right.z * halfW };
        C3Vector uy = { up.x * halfH, up.y * halfH, up.z * halfH };
        int32_t b = i * 4;
        s_weatherPos[b + 0] = { p.x - rx.x + uy.x, p.y - rx.y + uy.y, p.z - rx.z + uy.z };
        s_weatherPos[b + 1] = { p.x + rx.x + uy.x, p.y + rx.y + uy.y, p.z + rx.z + uy.z };
        s_weatherPos[b + 2] = { p.x + rx.x - uy.x, p.y + rx.y - uy.y, p.z + rx.z - uy.z };
        s_weatherPos[b + 3] = { p.x - rx.x - uy.x, p.y - rx.y - uy.y, p.z - rx.z - uy.z };
        s_weatherUv[b + 0] = { 0.0f, 0.0f };
        s_weatherUv[b + 1] = { 1.0f, 0.0f };
        s_weatherUv[b + 2] = { 1.0f, 1.0f };
        s_weatherUv[b + 3] = { 0.0f, 1.0f };
        s_weatherCol[b + 0] = col; s_weatherCol[b + 1] = col; s_weatherCol[b + 2] = col; s_weatherCol[b + 3] = col;
    }

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, TerrainFogActive() ? 1 : 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&TerrainViewProjT()), 4);
    GxRsSet(GxRs_Texture0, TextureGetGxTex(s_weatherTexture, 0, nullptr));

    GxPrimLockVertexPtrs(
        s_weatherAlive * 4,
        s_weatherPos, sizeof(C3Vector),
        nullptr, 0,
        s_weatherCol, sizeof(CImVector),
        nullptr, 0,
        s_weatherUv, sizeof(C2Vector),
        nullptr, 0
    );
    GxDrawLockedElements(GxPrim_Triangles, s_weatherAlive * 6, s_weatherIdx);
    GxPrimUnlockVertexPtrs();

    GxRsPop();
}

void UnderwaterOverlayRender() {
    // Both the gate and the liquid kind come from the reference camera-liquid query now
    // (CWorldScene::UpdateCameraLiquid), which records the LiquidType row the camera is submerged
    // in. 0 means not submerged.
    int32_t cameraLiquidType = static_cast<int32_t>(CWorldScene::s_cameraLiquidType);

    if (!cameraLiquidType) {
        return;
    }

    if (!s_uiVertexShader[0] || !s_uiVertexShader[0]->Valid() || !s_uiPixelShader || !s_uiPixelShader->Valid()) {
        return;
    }

    // The surface texture of the liquid the camera is in, scrolled slowly for the caustic look.
    // LiquidAt recorded the exact type when it decided the camera was submerged, so there is no
    // need to walk every tile and chunk again looking for a surface of the same kind.
    LiquidTextures* set = GetLiquidTextures(cameraLiquidType);
    uint32_t now = CWorld::GetM2Scene() ? CWorld::GetM2Scene()->m_time : 0;
    HTEXTURE tex = (set && set->frameCount) ? set->frames[(now / 50) % set->frameCount] : nullptr;

    // A quad one yard in front of the eye, wide enough to cover any field of view
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);
    if (rl < 1e-4f) { right = { 1.0f, 0.0f, 0.0f }; } else { right.x /= rl; right.y /= rl; }
    C3Vector up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };
    C3Vector c = { s_cameraPos.x + fwd.x, s_cameraPos.y + fwd.y, s_cameraPos.z + fwd.z };
    const float H = 3.0f;

    C3Vector pos[4] = {
        { c.x - right.x * H + up.x * H, c.y - right.y * H + up.y * H, c.z - right.z * H + up.z * H },
        { c.x + right.x * H + up.x * H, c.y + right.y * H + up.y * H, c.z + right.z * H + up.z * H },
        { c.x + right.x * H - up.x * H, c.y + right.y * H - up.y * H, c.z + right.z * H - up.z * H },
        { c.x - right.x * H - up.x * H, c.y - right.y * H - up.y * H, c.z - right.z * H - up.z * H },
    };
    float scroll = static_cast<float>(now % 20000) / 20000.0f;
    C2Vector uv[4] = { { scroll, scroll }, { scroll + 2.0f, scroll }, { scroll + 2.0f, scroll + 1.5f }, { scroll, scroll + 1.5f } };

    const C3Vector& fog = CWorld::GetFogColor();
    CImVector col[4];
    for (int32_t i = 0; i < 4; i++) {
        col[i].b = static_cast<uint8_t>(fog.z * 255.0f);
        col[i].g = static_cast<uint8_t>(fog.y * 255.0f);
        col[i].r = static_cast<uint8_t>(fog.x * 255.0f);
        col[i].a = 0x50;
    }
    static const uint16_t idx[6] = { 0, 1, 2, 0, 2, 3 };

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_VertexShader, s_uiVertexShader[0]);
    GxRsSet(GxRs_PixelShader, s_uiPixelShader);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&TerrainViewProjT()), 4);
    GxRsSet(GxRs_Texture0, tex ? TextureGetGxTex(tex, 0, nullptr) : (SkyWhiteTexture() ? TextureGetGxTex(SkyWhiteTexture(), 0, nullptr) : nullptr));
    GxPrimLockVertexPtrs(4, pos, sizeof(C3Vector), nullptr, 0, col, sizeof(CImVector), nullptr, 0, uv, sizeof(C2Vector), nullptr, 0);
    GxDrawLockedElements(GxPrim_Triangles, 6, idx);
    GxPrimUnlockVertexPtrs();
    GxRsPop();
}

void WeatherSetCameraPos(const C3Vector& cameraPos) {
    s_cameraPos = cameraPos;
}
