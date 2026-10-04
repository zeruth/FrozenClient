#ifndef WORLD_DAY_NIGHT_LIGHT_HPP
#define WORLD_DAY_NIGHT_LIGHT_HPP

#include "gx/Texture.hpp"
#include <storm/Hash.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CM2Model;
class CM2Scene;
class CRect;
struct CGxQuery;
class LightRec;
class LightParamsRec;

// The reference's DayNight module, light half: the outdoor light the world draws under. Every
// frame CWorld::UpdateDayNight (FUN_007816f0) feeds the camera and the clock into one global block
// (DayNightGetBlock, FUN_007ecef0 -> 0x00d38b00), the Light.dbc lights around the camera are
// blended into one record (DNInfo, at block +0xd4), and the fog, the outdoor colours and the sun
// direction are derived from it. Fifty-one reference functions read the block; this is where it
// is written. The sky half -- the dome, the bodies, the glare and the clouds -- is DayNight.cpp.

// One light's interpolated values (0x9c bytes, the reference's "DNInfo"). FUN_007ebff0 fills it from
// a LightParams row; FUN_007ed4c0 blends one into another. The colour slots are NOT in LightIntBand
// order: slot 0 is band 1 (ambient), slot 1 band 0 (diffuse), slot 2 band 8, slots 3..8 bands 2..7
// (the sky, zenith first, fog last), and slots 9..17 bands 9..17.
struct DNInfo {
    enum {
        Color_Ambient       = 0,
        Color_Diffuse       = 1,
        Color_Band8         = 2,
        Color_SkyTop        = 3,
        Color_Fog           = 8,
        Color_Count         = 18,
    };

    CImVector color[Color_Count];   // +0x00
    float fogEnd;                   // +0x48, LightFloatBand 0
    float fogStartScalar;           // +0x4c, LightFloatBand 1, clamped to [-1, 1]
    float fogRate;                  // +0x50, 1.0 on load
    float highlightSky;             // +0x54, LightParams column 1, as a float
    float glow;                     // +0x58, LightParams +0x10
    float floatBand[4];             // +0x5c, LightFloatBand 2..5
    float liquidAlpha[4];           // +0x6c, LightParams +0x14..+0x20
    struct {
        int32_t id;                 // LightSkybox id
        float weight;
    } skybox[3];                    // +0x7c
    int32_t cloudTypeID;            // +0x94, LightParams +0xc
    float weight;                   // +0x98
};

// A sky model named by a LightSkybox row, created on first use and kept for the map (the
// reference's hash table at 0x00d38d1c, entries 0x28 bytes).
struct DNSkyModel : TSHashObject<DNSkyModel, HASHKEY_STRI> {
    CM2Model* m_model = nullptr;    // +0x18
    uint32_t m_duration = 0;        // +0x1c, the model's day animation length
    uint32_t m_lastTime = 0;        // +0x20
    uint32_t m_flags = 0;           // +0x24, LightSkybox flags
};

// The block at 0x00d38b00. Offsets are the reference's.
struct DayNightBlock;

// One of the block's byte colours as a 0..1 vector, for the readers that work in floats.
inline C3Vector DNColorVector(const CImVector& c) {
    const float k = 1.0f / 255.0f;

    return C3Vector { c.r * k, c.g * k, c.b * k };
}

struct DayNightBlock {
    int32_t minutes;                // +0x00, the game clock in minutes of the day
    float timeOfDay;                // +0x04, 0..1
    float dayCount;                 // +0x08, days since the epoch
    C3Vector playerPos;             // +0x0c
    C3Vector cameraPos;             // +0x18
    C3Vector queryPos;              // +0x24, what the camera looks at; the light query point
    C3Vector cameraDir;             // +0x30, normalised camera forward -- NOT the sun
    float cameraYaw;                // +0x3c
    float farClip;                  // +0x40
    float timeSec;                  // +0x44
    float frameDelta;               // +0x48
    float stormInput;               // +0x4c, the weather's density times its fog
    uint8_t fadeAlpha;              // +0x50
    CImVector fadeColor;            // +0x51, unaligned in the reference
    int32_t forcedParams;           // +0x58, a Light.dbc params slot to force, or -1
    DNSkyModel* overrideSky;        // +0x5c
    float overrideSkyWeight;        // +0x60
    DNSkyModel* sky[3];             // +0x64
    float skyWeight[3];             // +0x70
    uint32_t skyFlag[3];            // +0x7c, LightSkybox flag 0x2
    float storm;                    // +0x88, stormInput * 4, at most 1
    CImVector fogColor;             // +0x8c
    float fogStart;                 // +0x90
    float fogEnd;                   // +0x94
    float fogRate;                  // +0x98
    float mapObjFogBlend;           // +0x9c
    CImVector finalFogColor;        // +0xa0
    float finalFogStart;            // +0xa4
    float finalFogEnd;              // +0xa8
    float finalFogRate;             // +0xac
    CImVector mapObjFogColor;       // +0xb0
    float mapObjFogStart;           // +0xb4
    float mapObjFogEnd;             // +0xb8
    float mapObjFogRate;            // +0xbc
    int32_t mapObjFogDef;           // +0xc0
    struct {
        uint32_t max;
        uint32_t count;
        uint32_t* data;
    } mapObjFogGroups;              // +0xc4
    int32_t mapObjFogFlags;         // +0xd0
    DNInfo info;                    // +0xd4
    C3Vector lightDir;              // +0x170, a copy of direction for the shader block
    CImVector lightDiffuse;         // +0x17c
    CImVector lightAmbient;         // +0x180
    CImVector lightDiffuseHalf;     // +0x184, ambient pulled halfway to diffuse
    CImVector lightAmbientHalf;     // +0x188, diffuse pulled halfway to ambient
    float lightSpecular[4];         // +0x18c
    C3Vector direction;             // +0x19c, points AWAY from the light
    CImVector diffuse;              // +0x1a8
    CImVector ambient;              // +0x1ac
    CImVector diffuseHalf;          // +0x1b0
    CImVector ambientHalf;          // +0x1b4
    float specular[4];              // +0x1b8, the ambient desaturated and brightened, alpha 1
    CImVector shadowColor;          // +0x1c8, the ambient a third of the way to white
    int32_t drawSky;                // +0x1cc, whether the sky draws at all (names.json had it as s_drawGlare)
    int32_t pad1d0;                 // +0x1d0
    int32_t pad1d4;                 // +0x1d4
    float twilight;                 // +0x1d8, 1 at dawn and dusk, 0 at noon and midnight
    float bodyBandA;                // +0x1dc
    float bodyBandB;                // +0x1e0
    int32_t forcedLightCount;       // +0x1e4
    int32_t pad1e8;                 // +0x1e8
    LightRec* forcedLight[5];       // +0x1ec
    float forcedLightDepth[5];      // +0x200
};

// A sky body (the reference's objects at 0x00d38e28 the sun, 0x00d38e48 the moon and 0x00d38e68
// the second moon): where FUN_007eecc0 places it, at twelve yards from the camera, and the tint
// FUN_007f3230 gives it.
struct DNBody {
    C3Vector pos;                   // +0x00
    CImVector color;                // +0x0c
    HTEXTURE texture;               // +0x10
    float size;                     // +0x14, the day band times scale
    float scale;                    // +0x18
    float scale2;                   // +0x1c, the second moon's orbit period
};

// A body's glare (the reference's objects at 0x00d38ea8 the sun's and 0x00d38f58 the moon's; vtables
// 0x00a41c18 and 0x00a41c2c, which differ only in how cloud cover dims them).
struct DNGlare {
    enum {
        Kind_Sun = 0,
        Kind_Moon = 1,
    };

    int32_t kind = Kind_Sun;
    CGxQuery* query = nullptr;      // +0x04, the occlusion query
    int32_t enabled = 0;            // +0x08
    C3Vector pos;                   // +0x0c
    CImVector color;                // +0x18
    HTEXTURE texture = nullptr;     // +0x1c
    float sizeBase = 0.0f;          // +0x20
    float size = 0.0f;              // +0x24
    float rise = 0.0f;              // +0x28
    float fall = 0.0f;              // +0x2c
    float current = 0.0f;           // +0x30
    float target = 0.0f;            // +0x34
    int32_t skipQuery = 0;          // +0x38, the first frame has no query result yet
    uint32_t lastPixels = 0;        // +0x6c
    float band[8] = {};             // +0x70, strength over the day
    float sizeMin = 0.0f;           // +0x90
    float sizeMax = 0.0f;           // +0x94
    float dotMin = 0.0f;            // +0x98
    float alphaMin = 0.0f;          // +0x9c
    float alphaMax = 0.0f;          // +0xa0
    float darken = 0.0f;            // +0xa4, how much it dims the scene
    DNBody* body = nullptr;         // +0xa8
};

struct DNBodies {
    DNBody sun;
    DNBody moon;
    DNBody moon2;
    DNGlare sunGlare;
    DNGlare moonGlare;
};

// The cloud sheet (0x00d38d90): value noise generated a few rows a frame into one of two textures,
// drawn on a twelve-ring dome.
struct DNClouds {
    float exponent = 0.0f;          // +0x00, the coverage-to-alpha curve
    float density = 0.0f;           // +0x04, a fixed cover, or 0 to follow the light
    uint8_t threshold = 0;          // +0x08
    uint8_t lod = 0;                // +0x09
    uint8_t restart = 0;            // +0x0a
    uint8_t current = 0;            // +0x0b
    float speed = 2.0f;             // +0x0c
    uint32_t rows = 8;              // +0x10
    uint32_t rowStart = 0;          // +0x14
    uint32_t format = 2;            // +0x18
    uint32_t size = 0;              // +0x1c
    uint32_t sizeShift = 0;         // +0x20
    uint32_t sizeMask = 0;          // +0x24
    uint32_t octaves = 4;           // +0x28
    int32_t enabled = 0;            // +0x2c
    CImVector* texels = nullptr;    // +0x38
    uint8_t* coverage = nullptr;    // +0x44
    float* rowValues = nullptr;     // +0x50
    C3Vector verts[192];            // +0x5c
    C2Vector uvs[192];              // +0x68
    CImVector colors[192];          // +0x74
    uint16_t indices[374];          // +0x80
    uint16_t indexCount = 0;        // +0x84
    uint16_t vertexCount = 0;       // +0x86
    uint16_t time = 0;              // +0x88
    float clock = 0.0f;             // +0x8c
    HTEXTURE textures[2] = {};      // +0x90
    float lightU = 0.0f;            // 0x00d38cd0
    float lightV = 0.0f;            // 0x00d38cd4
};

// The star sky (0x00d38ae4): its own scene and stars.mdl, drawn at the camera with an alpha from
// the day band (FUN_007ee0d0).
struct DNStars {
    CM2Scene* scene = nullptr;      // +0x00
    CM2Model* model = nullptr;      // +0x04
    CImVector color;                // +0x08, alpha at +0x0b
    C3Vector pos;                   // +0x0c
    uint32_t time = 0;              // +0x18
};

// The sky dome (0x00d38d4c): seven rings of a sphere sunk so its 45-degree ring is the horizon,
// coloured each frame from the light's sky bands (FUN_007f0530).
struct DNDome {
    C3Vector verts[168];
    CImVector colors[168];
    uint16_t indices[300];
    int32_t segments = 0;           // +0x24
    uint16_t vertexCount = 0;       // +0x28
    uint16_t indexCount = 0;        // +0x2a
};

DayNightBlock* DayNightGetBlock();
DNBodies* DayNightGetBodies();
DNStars* DayNightGetStars();
DNDome* DayNightGetDome();
DNClouds* DayNightGetClouds();
// ref: FUN_007eb060
// Bring every positional Light.dbc row into world space, once, as the client database finishes
// loading. With `convert` false nothing changes.
void DayNightConvertLights(int32_t convert);

int32_t DayNightSkyReady();
CM2Scene* DayNightGetSkyScene();
uint32_t DayNightGetSkySceneTime();
void DayNightSetSkySceneTime(uint32_t time);
int32_t DayNightTimedFadeActive();
float DayNightTimedFadeAmount();

// The light state CMap::Render and the lighting callbacks read.
int32_t DayNightOverrideActive();
void DayNightSetFogMode(int32_t mode);
void DayNightSetForcedParams(uint32_t index);
void DayNightClearForcedParams();
void DayNightClearFadeFlag(uint8_t bit);
void DayNightResetLightFade(char now);
void DayNightAddForcedLight(int32_t lightID, float depth);
void DayNightBeginFogOverride(float startScalar, float end, CImVector color, int32_t drawSky);
void DayNightEndFogOverride();
void DayNightAddAreaLight(int32_t lightID, int32_t sourceID, int32_t durationMs);
void DayNightRemoveAreaLight(int32_t lightID, int32_t durationMs);

void DayNightInitialize(int32_t mapID);
void DayNightShutdown();

// The per-frame steps CWorld::UpdateDayNight runs, in its order.
void DayNightUpdateCamera();
void DayNightUpdateClouds();
void DayNightUpdateStars();
void DayNightUpdateFog();

// The interpolation primitives the sky shares.
float InterpBodyBand(const float* keys, int32_t count, float t);
// The outdoor light's direction for the block's time of day, into the block (FUN_007eea90).
void DNUpdateDirection();
// The gradient dome's seven rings of vertices and its strip indices (FUN_007f2470).
void DomeBuild(DNDome* dome, float radius);
CImVector SkyLerp(const CImVector& a, const CImVector& b, float t);
CImVector DayNightScaleSaturation(uint32_t color, float scale);

DNSkyModel* DayNightGetSkyModel(const char* name, uint32_t flags);
DNSkyModel* DayNightSetSkyModel(uint32_t slot, int32_t enable, const char* name, uint32_t flags, float weight);

// The sky, drawn by CMap::Render into the window it is given (FUN_007f09b0).
void DayNightSkyRender(const CRect& window);
// The sky's own resources, made at map load and freed at shutdown.
void DayNightSkyInitialize();
void DayNightSkyShutdown();
// The world frame's glare pass (FUN_007f0870).
void DayNightGlareRender();

#endif
