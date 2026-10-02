#include "world/MapWeather.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include "world/ShadowMap.hpp"
#include "console/Console.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/shader/CGxShader.hpp"
#include "gx/texture/CGxTex.hpp"
#include "model/CM2Lighting.hpp"
#include "object/Types.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "util/CStatus.hpp"
#include "util/Random.hpp"
#include <common/DataStore.hpp>
#include <common/Time.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Math.hpp>
#include <tempest/Matrix.hpp>
#include <cfloat>
#include <cmath>
#include <cstring>

static const float CHUNK_SIZE = 33.33333206176758f;       // DAT_00a3e554

bool Weather::s_forced;
CVar* Weather::s_useShadersCvar;
float Weather::s_density = 0.66f;

// ---------------------------------------------------------------------------------------------
// Helpers for what the reference does inline, and wrappers for what lives outside this module.

namespace {

// The reference's `fistp(x - 0.5)` under round-to-nearest: an integer at or just below x
int32_t RoundDown(float x) {
    return static_cast<int32_t>(std::nearbyint(x - 0.5f));
}

// FUN_0088ce30 (floor) then FUN_0088b9c0 (ftol)
int32_t FloorToInt(float x) {
    return static_cast<int32_t>(std::floor(static_cast<double>(x)));
}

// A CRandom draw as a float in [1, 2): the mantissa bits under a fixed exponent
float RandBits(uint32_t bits) {
    uint32_t value = (bits & 0x7FFFFF) | 0x3F800000;
    float result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

float UintToFloat(uint32_t value) {
    return static_cast<float>(value);
}

float Clamp01(float value) {
    float result = 0.0f;
    if (0.0f <= value && (result = value, 1.0f <= value)) {
        result = 1.0f;
    }
    return result;
}

// The reference's DAT_00cd7694 / DAT_00cd7698 / DAT_00cd76a0: world time and the frame step in
// 1/1024 seconds, and the frame step in seconds (CWorld::SetUpdateTime, 0x0077ecb0)
uint32_t WorldTime() {
    return CWorld::GetGameTimeFixed();
}

uint32_t WorldTickFixed() {
    return CWorld::GetTickTimeFixed();
}

float WorldTickSec() {
    return CWorld::GetTickTimeSec();
}

// FUN_00780640: the world camera position (DAT_00cd8f5c)
const C3Vector& WorldCameraPos() {
    return CWorld::GetCameraPos();
}

// FUN_00984c90: a packed colour as four shader-constant floats
void ImVectorToFloats(float* out, const CImVector& color) {
    float scale = 1.0f / 255.0f;
    out[0] = color.r * scale;
    out[1] = color.g * scale;
    out[2] = color.b * scale;
    out[3] = scale * color.a;
}

// FUN_006c42f0: a colour in the device's byte order (red and blue trade places on rgba devices)
void ImVectorToDevice(CImVector& color) {
    if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
        uint8_t r = color.r;
        color.r = color.b;
        color.b = r;
    }
}

// The DayNight block's fog colour (DAT_00d38b8c). frozen keeps no DayNight block; the fog colour
// CWorld derives from Light.dbc is the value the block would hold.
CImVector DayNightFogColor() {
    const C3Vector& fog = CWorld::GetFogColor();
    CImVector color;
    color.b = CM2Lighting::FogColorByte(fog.z);
    color.g = CM2Lighting::FogColorByte(fog.y);
    color.r = CM2Lighting::FogColorByte(fog.x);
    color.a = 0xFF;
    return color;
}

// DayNight +0x4c, the weather's darkening of the light, and FUN_007f1070(0), which marks the
// DayNight block for recomputation. Not ported: frozen keeps no DayNight block, so the value is
// kept here for whoever ports the reader (FUN_004f8410).
float s_dayNightWeatherScale = 1.0f;

void DayNightSetWeatherScale(float scale) {
    s_dayNightWeatherScale = scale;
}

void DayNightInvalidate(int32_t force) {
    (void)force;
}

// FUN_007ade10: the highest surface (terrain, map objects, models) within `range` below the point.
// Not ported (it walks CMap::s_areaGrid chunks, map object groups and the M2 collision list); the
// empty answer is "nothing there".
int32_t WorldGroundHeight(const C3Vector& position, float range, float* height) {
    (void)position;
    (void)range;
    (void)height;
    return 0;
}

// FUN_00770840: a value from the registry. frozen reads none; the key is absent.
int32_t RegistryReadInt(const char* section, const char* key, uint32_t* value) {
    (void)section;
    (void)key;
    (void)value;
    return 0;
}

// The device's ShaderDestroy (vtable +0x114), as the effect destructors call it.
void GxShaderRelease(CGxShader*& shader) {
    g_theGxDevicePtr->ShaderDestroy(&shader);
}

// CGxCaps +0x104, +0x108 and +0x10c, which frozen's CGxCaps does not carry (see the note there):
// whether the sand shader may run, the largest point size, and point sprite support. The empty
// answer turns the point-sprite paths off, and snow and sand draw on the CPU.
int32_t CapsSandShader() {
    return 0;
}

float CapsMaxPointSize() {
    return 0.0f;
}

int32_t CapsPointSprites() {
    return 0;
}

// The active player's movement (CMovement_C +0x64 direction, +0x8c speed): CMovementShared does
// not expose them in frozen yet. The empty answer is standing still.
C3Vector UnitMoveDirection(CGUnit_C* unit) {
    (void)unit;
    return { 0.0f, 0.0f, 0.0f };
}

float UnitMoveSpeed(CGUnit_C* unit) {
    (void)unit;
    return 0.0f;
}

// The spline a unit's vehicle follows (CGUnit_C +0x844, FUN_00717870) and its point count
// (+0x144). frozen has no vehicles; the answer is none.
void* UnitVehiclePath(CGUnit_C* unit) {
    (void)unit;
    return nullptr;
}

// FUN_004c2210: a point through the 4x4, each output a row dotted with (point, 1)
C3Vector TransformPoint(const C44Matrix& m, const C3Vector& v) {
    return {
        m.a0 * v.x + m.a1 * v.y + m.a2 * v.z + m.a3,
        m.b1 * v.y + m.b0 * v.x + m.b2 * v.z + m.b3,
        m.c1 * v.y + m.c0 * v.x + m.c2 * v.z + m.c3
    };
}

} // namespace

// ref: FUN_004c3460
// A 4x4 rotation by `angle` about `axis` (normalised first unless `unit` says it already is)
static C44Matrix RotationAroundAxis4(float angle, const C3Vector& axis, bool unit) {
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    if (!unit) {
        float inv = 1.0f / sqrtf(x * x + y * y + z * z);
        x = inv * x;
        y = y * inv;
        z = inv * z;
    }

    float c = cosf(angle);
    float s = sinf(angle);
    float t = 1.0f - c;

    C44Matrix m;
    m.a0 = x * x * t + c;
    float xy = y * x * t;
    m.a1 = xy + s * z;
    float xz = z * x * t;
    m.a2 = xz - y * s;
    m.a3 = 0.0f;
    m.b0 = xy - s * z;
    m.b1 = y * y * t + c;
    float yz = t * z * y;
    m.b2 = yz + x * s;
    m.b3 = 0.0f;
    m.c0 = xz + y * s;
    m.c1 = yz - x * s;
    m.c2 = z * z * t + c;
    m.c3 = 0.0f;
    m.d0 = 0.0f;
    m.d1 = 0.0f;
    m.d2 = 0.0f;
    m.d3 = 1.0f;
    return m;
}

// ---------------------------------------------------------------------------------------------

// ref: FUN_00783b60
// Which effect is up: 1 rain, 2 snow, 3 sand, 0 none
int32_t Weather::GetType() const {
    int32_t type = 0;

    if (this->m_rain) {
        return 1;
    }

    if (this->m_snow) {
        return 2;
    }

    if (this->m_sand) {
        type = 3;
    }

    return type;
}

// ref: FUN_00783b90
void Weather::SetDefaultTexture(uint32_t type) {
    if (type == 1) {
        SStrCopy(this->m_texture, "textures\\Weather\\RainDrop01.blp", 0x7FFFFFFF);
        return;
    }

    if (type == 2) {
        SStrCopy(this->m_texture, "textures\\Weather\\SnowFlake01.blp", 0x7FFFFFFF);
    }
}

// ref: FUN_00783bd0
// The ground straight below a point, from the world query, or 200 yards down when there is none
static float WeatherGroundBelow(const C3Vector& position) {
    float height;

    if (WorldGroundHeight(position, 200.0f, &height)) {
        return height;
    }

    return position.z - 200.0f;
}

// ref: FUN_00783c10
float WeatherGroundTile::Height(const C3Vector& position) {
    float x = position.x - static_cast<float>(this->tileX) * CHUNK_SIZE;
    float y = position.y - CHUNK_SIZE * static_cast<float>(this->tileY);

    if (0.0f <= x && 0.0f <= y) {
        int32_t cellY = RoundDown(y * 0.96f);
        int32_t cellX = RoundDown(x * 0.96f);

        if (-1 < cellX && -1 < cellY && cellX < 0x20 && cellY < 0x20) {
            float* height = &this->heights[cellY * 0x20 + cellX];

            if (FLT_MAX == *height) {
                *height = WeatherGroundBelow(position);
            }

            return *height;
        }
    }

    return FLT_MAX;
}

// ref: FUN_00783ce0
// The height of one cell of the window, `cell` counted from the window's origin. A cell not yet
// asked for is queried at its centre, from `z`.
float WeatherGroundCache::CellHeight(const int32_t* cell, float z) {
    int32_t x = cell[0];
    int32_t y = cell[1];
    int32_t index = (y & 0x1F) * 0x20 + (x & 0x1F);
    WeatherGroundTile* tile = this->m_tiles[(x >> 5) + (y >> 5) * 5];

    C3Vector center = {
        static_cast<float>(this->m_originTileX) * CHUNK_SIZE + static_cast<float>(x) * 1.0416666f + 0.5208333f,
        CHUNK_SIZE * static_cast<float>(this->m_originTileY) + 1.0416666f * static_cast<float>(y) + 0.5208333f,
        z
    };

    if (FLT_MAX != tile->heights[index]) {
        return tile->heights[index];
    }

    float height;

    if (WorldGroundHeight(center, 200.0f, &height)) {
        tile->heights[index] = height;
        return tile->heights[index];
    }

    tile->heights[index] = center.z - 200.0f;
    return tile->heights[index];
}

// ref: FUN_00783dc0
// The chunk rectangle the cached tiles cover: rect = { minY, minX, maxY, maxX }
void WeatherGroundCache::GetBounds(int32_t* rect) {
    rect[0] = 0xFFFF;
    rect[2] = -0xFFFF;
    rect[1] = 0xFFFF;
    rect[3] = -0xFFFF;

    for (int32_t i = 0; i < 25; i++) {
        WeatherGroundTile* tile = this->m_tiles[i];

        if (tile) {
            if (tile->tileX < rect[1]) {
                rect[1] = tile->tileX;
            }
            if (rect[3] < tile->tileX) {
                rect[3] = tile->tileX;
            }
            if (tile->tileY < rect[0]) {
                rect[0] = tile->tileY;
            }
            if (rect[2] < tile->tileY) {
                rect[2] = tile->tileY;
            }
        }
    }
}

// ref: FUN_00783fb0
WeatherGroundQuery::~WeatherGroundQuery() {
}

// ref: FUN_00783fe0
void Sand::GetBounds(CAaBox& bounds) const {
    bounds = this->m_bounds;
}

// ref: FUN_00784010
void Weather::GetPlayerPosition(C3Vector& position) const {
    position = this->m_playerPosition;
}

// ref: FUN_00784040
static bool WeatherDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    switch (SStrToInt(value)) {
        case 0:
            Weather::s_density = 0.1f;
            break;

        case 1:
            Weather::s_density = 0.33f;
            break;

        case 2:
            Weather::s_density = 0.66f;
            break;

        case 3:
            Weather::s_density = 1.0f;
            break;

        default:
            ConsoleWriteA("Value out of range (%d - %d)\n", DEFAULT_COLOR, 0, 3);
            return false;
    }

    if (CWorld::s_weather) {
        CWorld::s_weather->m_dirty = 1;
    }

    return true;
}

// ref: FUN_007840b0
void Rain::SetIntensity(float intensity) {
    this->m_intensity = intensity;

    float density = Weather::s_density;
    float rate = this->m_useShaders ? 35000.0f : 6500.0f;

    this->m_rate = Weather::s_density * rate * intensity;
    this->m_maxBuild = rate * 0.00016276042f;

    float mist = this->m_useShaders ? 38.0f : 18.0f;
    intensity = intensity - 0.5f;

    if (0.0f <= intensity) {
        this->m_mists.m_spawnRate = (intensity + intensity) * density * mist;
        return;
    }

    this->m_mists.m_spawnRate = density * 0.0f * mist;
}

// ref: FUN_00784140
// Tilts the rain by the player's ground speed: up to 45 degrees at 30 yards a second, leaning
// against the direction of travel
static void WeatherRainTilt(C33Matrix& matrix) {
    Weather* weather = CWorld::s_weather;
    float vy = weather->m_playerVelocity.y;
    float vx = weather->m_playerVelocity.x;
    float lenSq = vx * vx + vy * vy;

    float amount = Clamp01(sqrtf(lenSq) / 30.0f) * 45.0f;

    if (fabsf(lenSq) < 0.001f) {
        return;
    }

    float inv = 1.0f / sqrtf(lenSq);
    float zero = inv * 0.0f * 0.0f;
    C3Vector axis;
    axis.x = vy * inv - zero;
    axis.y = zero - inv * vx;
    axis.z = inv * vx * 0.0f - vy * inv * 0.0f;

    if (fabsf(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z) < 0.001f) {
        return;
    }

    float invAxis = 1.0f / sqrtf(axis.z * axis.z + axis.y * axis.y + axis.x * axis.x);
    axis.x = axis.x * invAxis;
    axis.y = axis.y * invAxis;
    axis.z = invAxis * axis.z;

    // FUN_004c5940: the matrix is rotated in place, the new rotation on the left
    matrix = C33Matrix::RotationAroundAxis(amount * 0.017453292f, axis, true) * matrix;
}

// ref: FUN_00784270
// How far behind a drop its tail reaches: two seconds of fall while the player stands still
static float WeatherRainTail() {
    Weather* weather = CWorld::s_weather;
    float tail = 2.0f;

    if (0.001f <= fabsf(
        weather->m_playerVelocity.x * weather->m_playerVelocity.x
        + weather->m_playerVelocity.y * weather->m_playerVelocity.y
        + weather->m_playerVelocity.z * weather->m_playerVelocity.z
    )) {
        tail = 0.0f;
    }

    return tail;
}

// ref: FUN_007842d0
// Writes a drop packet into its vertex buffer: three vertices per drop, its position and velocity,
// the corner number in the colour's alpha, and its start and end times
void Rain::UploadDrops(DropPacket* packet) {
    auto vertex = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(packet->m_buf));

    for (uint32_t i = 0; i < packet->m_count; i++) {
        Drop& drop = packet->m_items[i];

        for (uint32_t corner = 0; corner < 3; corner++) {
            std::memcpy(&vertex[0], &drop.position, sizeof(C3Vector));
            std::memcpy(&vertex[3], &drop.velocity, sizeof(C3Vector));

            uint32_t color = corner << 24;
            uint32_t deviceColor;

            if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
                deviceColor = corner << 24;
            } else {
                deviceColor = color;
            }

            vertex[6] = deviceColor;
            std::memcpy(&vertex[7], &drop.startTime, sizeof(float));
            std::memcpy(&vertex[8], &drop.endTime, sizeof(float));
            vertex += 9;
        }
    }

    CGxBuf* buf = packet->m_buf;
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;
}

// ref: FUN_007843e0
// Writes a patter packet: three vertices per splash, its position, the corner number, and its
// start and end times
void Rain::UploadPatter(PatterPacket* packet) {
    auto vertex = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(packet->m_buf));

    for (uint32_t i = 0; i < packet->m_count; i++) {
        Patter& patter = packet->m_items[i];

        for (uint32_t corner = 0; corner < 3; corner++) {
            std::memcpy(&vertex[0], &patter.position, sizeof(C3Vector));

            uint32_t color = corner << 24;
            uint32_t deviceColor;

            if (g_theGxDevicePtr->Caps().m_colorFormat == GxCF_rgba) {
                deviceColor = corner << 24;
            } else {
                deviceColor = color;
            }

            vertex[3] = deviceColor;
            std::memcpy(&vertex[4], &patter.startTime, sizeof(float));
            std::memcpy(&vertex[5], &patter.endTime, sizeof(float));
            vertex += 6;
        }
    }

    CGxBuf* buf = packet->m_buf;
    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;
}

// ref: FUN_007844f0
void Snow::SetIntensity(float intensity) {
    this->m_intensity = intensity;

    float density = Weather::s_density;
    float rate = this->m_useShaders ? 14000.0f : 1300.0f;

    this->m_rate = Weather::s_density * rate * intensity;
    this->m_maxBuild = rate * 0.00016276042f;

    float mist = this->m_useShaders ? 48.0f : 24.0f;
    intensity = intensity - 0.5f;

    if (0.0f <= intensity) {
        this->m_mists.m_spawnRate = (intensity + intensity) * density * mist;
        return;
    }

    this->m_mists.m_spawnRate = density * 0.0f * mist;
}

// ref: FUN_00784580
void Sand::SetIntensity(float intensity) {
    float rate = this->m_useShaders ? 32000.0f : 6000.0f;

    this->m_maxBuild = 0.00016276042f * rate;

    float density = Weather::s_density;
    this->m_rate = Weather::s_density * rate * intensity;

    if (!this->m_useShaders) {
        this->m_mists.m_spawnRate = 32.0f * density * intensity;
        return;
    }

    this->m_mists.m_spawnRate = 64.0f * density * intensity;
}

// ref: FUN_007845f0
// Writes a snow or sand packet: one point per particle, its position, velocity and times
static void WeatherUploadFlakes(uint32_t count, const void* items, CGxBuf* buf) {
    auto vertex = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(buf));
    auto item = reinterpret_cast<const uint32_t*>(items);

    for (uint32_t i = 0; i < count; i++) {
        vertex[0] = item[0];
        vertex[1] = item[1];
        vertex[2] = item[2];
        vertex[3] = item[3];
        vertex[4] = item[4];
        vertex[5] = item[5];
        vertex[6] = item[6];
        vertex[7] = item[7];
        item += 8;
        vertex += 8;
    }

    g_theGxDevicePtr->BufUnlock(buf, 0);
    buf->unk1C = 1;
}

// ref: FUN_007846a0
// What SMSG_WEATHER asks for: the effect type (0 clear, 1 rain, 2 snow, 3 sand), its intensity,
// the Weather.dbc row for its colour and texture, whether to change gradually, and the sky density
// to ramp to. A new type is only queued here; Update builds it.
void Weather::SetWeather(uint32_t type, float intensity, const WeatherRec* record, bool gradual, float density) {
    if (Weather::s_forced || 3 < type) {
        return;
    }

    this->m_gradual = gradual;

    bool same = true;

    if (this->GetType() != static_cast<int32_t>(type)) {
        this->m_texture[0] = '\0';
        same = false;
    }

    bool textureChanged = false;

    if (record) {
        this->m_color.Set(1.0f, record->m_effectColor[0], record->m_effectColor[1], record->m_effectColor[2]);

        if (SStrLen(record->m_effectTexture)) {
            if (SStrCmpI(this->m_texture, record->m_effectTexture, 0x7FFFFFFF)) {
                same = false;
                SStrCopy(this->m_texture, record->m_effectTexture, 0x7FFFFFFF);
                textureChanged = true;
            }
        }
    }

    if (!SStrLen(this->m_texture) && !textureChanged) {
        this->SetDefaultTexture(type);
        same = false;
    }

    if (this->GetType() != static_cast<int32_t>(type) || !same) {
        this->m_pendingType = type;
    }

    float clamped;

    if (intensity < 0.0f) {
        clamped = 0.0f;
    } else if (1.0f > intensity) {
        clamped = intensity;
    } else {
        clamped = 1.0f;
    }

    this->m_prevIntensity = this->m_intensity;
    this->m_intensity = clamped;

    if (!this->GetType()) {
        this->m_densityTo = density;
    } else if (type == 0) {
        density = this->m_densityTo;
    }

    this->m_densityFrom = this->m_densityTo;
    this->m_densityTo = density;

    if (!gradual) {
        this->m_dirty = 1;
        this->m_prevIntensity = clamped;
        this->m_curIntensity = clamped;
        this->m_densityFrom = density;
        this->m_density = density;
    }

    if (clamped < 0.0f) {
        this->m_fogTarget = 0.0f;
    } else if (0.25f > clamped) {
        this->m_fogTarget = clamped;
    } else {
        this->m_fogTarget = 0.25f;
    }

    float prev = this->m_prevIntensity;

    if (prev < 0.0f) {
        this->m_fogFrom = 0.0f;
    } else if (0.25f > prev) {
        this->m_fogFrom = prev;
    } else {
        this->m_fogFrom = 0.25f;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    this->m_startTime = now;
    this->m_fogStartTime = now;
}

// ref: FUN_00784850
// Ramps the effects from `from` to `to` over ten seconds per unit of change, and the sky's
// darkening and density with them; an effect only shows above a quarter intensity
void Weather::UpdateIntensity(float from, float to) {
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    float elapsed = UintToFloat(now - this->m_startTime);
    float t = Clamp01((elapsed * 0.001f) / (fabsf((to - from) + 0.001f) * 10.0f));

    float cur = t * (to - from) + from;
    this->m_curIntensity = cur;

    float effect = (cur - 0.25f) * 1.3333334f;

    if (effect < 0.0f) {
        effect = 0.0f;
    }

    if (this->m_rain) {
        this->m_rain->SetIntensity(effect);
    }

    if (this->m_snow) {
        this->m_snow->SetIntensity(effect);
    }

    if (this->m_sand) {
        this->m_sand->SetIntensity(effect);
    }

    // The reference reads `now` again from EDX here, which the three calls above leave intact
    float fogElapsed = UintToFloat(now - this->m_fogStartTime);

    float fogT = Clamp01(
        (fogElapsed * 0.001f) / (fabsf((this->m_fogTarget - this->m_fogFrom) * 4.0f + 0.001f) * 10.0f)
    );
    this->m_fog = (this->m_fogTarget - this->m_fogFrom) * fogT + this->m_fogFrom;

    float densityT = Clamp01(
        (fogElapsed * 0.001f) / (fabsf((this->m_densityTo - this->m_densityFrom) * 4.0f + 0.001f) * 5.0f)
    );
    this->m_density = this->m_densityFrom + (this->m_densityTo - this->m_densityFrom) * densityT;

    DayNightSetWeatherScale(this->m_fog * this->m_density);
    DayNightInvalidate(0);

    this->m_dirty = 0;
}

// ref: FUN_00784a30
// The box the falling particles fill, around the camera, or 0 when the sky is clear
int32_t Weather::GetEffectBounds(CAaBox& bounds) {
    if (this->m_rain) {
        bounds = this->m_rain->m_bounds;
        return 1;
    }

    if (this->m_snow || this->m_sand) {
        if (this->m_snow) {
            bounds = this->m_snow->m_bounds;
            return 1;
        }

        if (this->m_sand) {
            this->m_sand->GetBounds(bounds);
            return 1;
        }
    }

    return 0;
}

// ref: FUN_00784ab0
WeatherGroundCache::WeatherGroundCache() {
    for (int32_t y = 0; y < 5; y++) {
        for (int32_t x = 0; x < 5; x++) {
            auto tile = static_cast<WeatherGroundTile*>(SMemAlloc(sizeof(WeatherGroundTile), __FILE__, __LINE__, 0x0));

            if (tile) {
                tile->tileX = -9999;
                tile->tileY = -9999;

                for (int32_t i = 0; i < 0x400; i++) {
                    tile->heights[i] = FLT_MAX;
                }
            }

            this->m_tiles[y * 5 + x] = tile;
        }
    }
}

// ref: FUN_00784b60
void WeatherGroundCache::GetCellSize(float* size, float* inverse) {
    *size = 1.0416666f;
    *inverse = 0.96f;
}

// ref: FUN_00784b80
WeatherGroundCache::~WeatherGroundCache() {
    for (int32_t i = 0; i < 25; i++) {
        if (this->m_tiles[i]) {
            SMemFree(this->m_tiles[i], __FILE__, __LINE__, 0x0);
        }
    }
}

// ref: FUN_00784be0
float WeatherGroundCache::HeightAt(const C3Vector& position) {
    int32_t tileX = FloorToInt(position.x * 0.03f);
    int32_t tileY = FloorToInt(position.y * 0.03f);

    uint32_t x = static_cast<uint32_t>(tileX - this->m_originTileX);
    uint32_t y = static_cast<uint32_t>(tileY - this->m_originTileY);

    if (x < 5 && y < 5) {
        return this->m_tiles[y * 5 + x]->Height(position);
    }

    return FLT_MAX;
}

// ref: FUN_00784c60
// Walks the cells under the segment from `start` to `end` (a Bresenham line over the window),
// sampling the segment's height at each and stopping at the first cell whose ground is above it
bool WeatherGroundCache::Intersect(const C3Vector& start, const C3Vector& end, C3Vector& hit) {
    hit = start;

    int32_t x0 = FloorToInt(start.x * 0.96f);
    int32_t y0 = FloorToInt(start.y * 0.96f);
    y0 = y0 - this->m_originCellY;
    x0 = x0 - this->m_originCellX;

    int32_t x1 = FloorToInt(end.x * 0.96f);
    int32_t y1 = FloorToInt(end.y * 0.96f);
    y1 = y1 - this->m_originCellY;
    x1 = x1 - this->m_originCellX;

    C3Vector delta = { end.x - start.x, end.y - start.y, end.z - start.z };

    int32_t d[2];
    d[0] = std::abs(x1 - x0);
    d[1] = std::abs(y1 - y0);

    // FUN_00984c70: the line is y-major when |dx| <= |dy|
    bool yMajor = std::abs(d[0]) <= std::abs(d[1]);

    int32_t count;
    int32_t error;
    int32_t incStraight;
    int32_t incDiagonal;
    int32_t straightX;
    int32_t straightY;
    int32_t diagonalX;
    int32_t diagonalY;

    if (!yMajor) {
        count = d[0];
        incStraight = d[1] * 2;
        error = incStraight - d[0];
        incDiagonal = (d[1] - d[0]) * 2;
        straightX = 1;
        diagonalX = 1;
        straightY = 0;
        diagonalY = 1;
    } else {
        count = d[1];
        incStraight = d[0] * 2;
        error = incStraight - d[1];
        incDiagonal = (d[0] - d[1]) * 2;
        straightX = 0;
        diagonalX = 1;
        straightY = 1;
        diagonalY = 1;
    }

    if (end.x < start.x) {
        straightX = -straightX;
        diagonalX = -1;
    }

    if (end.y < start.y) {
        straightY = -straightY;
        diagonalY = -1;
    }

    if (count <= 0) {
        count = 1;
    }

    float inv = 1.0f / static_cast<float>(count);
    int32_t cell[2] = { x0, y0 };

    for (int32_t i = 0; i <= count; i++) {
        if (cell[0] < 0 || cell[1] < 0 || 0xA0 <= cell[0] || 0xA0 <= cell[1]) {
            hit = start;
            return false;
        }

        float t = static_cast<float>(i) * inv;
        hit.x = start.x + delta.x * t;
        hit.y = delta.y * t + start.y;
        hit.z = delta.z * t + start.z;

        float z = hit.z;
        float height = this->CellHeight(cell, hit.z);

        if (height > z) {
            hit.z = height;
            return true;
        }

        if (error > 0) {
            error += incDiagonal;
            cell[0] += diagonalX;
            cell[1] += diagonalY;
        } else {
            error += incStraight;
            cell[0] += straightX;
            cell[1] += straightY;
        }

        if (i == count) {
            hit = end;
        }
    }

    return false;
}

// ref: FUN_00784f20
// Moves the 5x5 window to centre on the chunk under `center`. Tiles still inside it keep their
// heights and move to their new slots; the rest are reused, cleared, for the new chunks.
void WeatherGroundCache::Update(const C3Vector& center) {
    int32_t tileX = FloorToInt(center.x * 0.03f);
    int32_t tileY = FloorToInt(center.y * 0.03f);

    if (tileX == this->m_centerTileX && tileY == this->m_centerTileY) {
        return;
    }

    this->m_centerTileY = tileY;
    this->m_originTileY = tileY - 2;
    this->m_originTileX = tileX - 2;
    this->m_centerTileX = tileX;
    this->m_originCellX = (tileX - 2) * 0x20;
    this->m_originCellY = this->m_originTileY << 5;

    WeatherGroundTile* old[25];

    for (int32_t i = 0; i < 25; i++) {
        old[i] = this->m_tiles[i];
        this->m_tiles[i] = nullptr;
    }

    for (int32_t y = 0; y < 5; y++) {
        for (int32_t x = 0; x < 5; x++) {
            for (int32_t i = 0; i < 25; i++) {
                WeatherGroundTile* tile = old[i];

                if (tile && tile->tileX == this->m_originTileX + x && tile->tileY == this->m_originTileY + y) {
                    this->m_tiles[y * 5 + x] = old[i];
                    old[i] = nullptr;
                    break;
                }
            }
        }
    }

    for (int32_t y = 0; y < 5; y++) {
        for (int32_t x = 0; x < 5; x++) {
            if (this->m_tiles[y * 5 + x]) {
                continue;
            }

            for (int32_t i = 0; i < 25; i++) {
                if (!old[i]) {
                    continue;
                }

                this->m_tiles[y * 5 + x] = old[i];
                int32_t originY = this->m_originTileY;
                old[i] = nullptr;

                WeatherGroundTile* tile = this->m_tiles[y * 5 + x];
                tile->tileX = this->m_originTileX + x;
                tile->tileY = originY + y;

                for (int32_t h = 0; h < 0x400; h++) {
                    tile->heights[h] = FLT_MAX;
                }

                break;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Packets

// ref: FUN_00786920
// (FUN_007850c0, the drop array's constructor, is inlined here)
template <>
Packet<Rain::Drop, 0x1800, 3>::Packet() {
    this->m_capacity = 0x1800;

    for (int32_t i = 0; i < 0x1800; i++) {
        Rain::Drop& drop = this->m_items[i];
        drop.position = { 0.0f, 0.0f, 0.0f };
        drop.velocity = { 0.0f, 0.0f, 0.0f };
        drop.unused18 = 0.0f;
    }

    this->m_anchor = { 0.0f, 0.0f, 0.0f };
    this->m_pool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, 0xA2000, GxPoolHintBit_Unk2, "WeatherPacket_vtx");
    this->m_buf = g_theGxDevicePtr->BufCreate(this->m_pool, 0x24, 0x4800, 0);
}

// ref: FUN_00786990
template <>
Packet<Rain::Patter, 0x1800, 3>::Packet() {
    this->m_capacity = 0x1800;

    for (int32_t i = 0; i < 0x1800; i++) {
        Rain::Patter& patter = this->m_items[i];
        patter.position = { 0.0f, 0.0f, 0.0f };
        patter.unusedC = 0.0f;
    }

    this->m_anchor = { 0.0f, 0.0f, 0.0f };
    this->m_pool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, 0x6C000, GxPoolHintBit_Unk2, "WeatherPacket_vtx");
    this->m_buf = g_theGxDevicePtr->BufCreate(this->m_pool, 0x18, 0x4800, 0);
}

// ref: FUN_00786a20
// (FUN_00785100, the flake array's constructor, is inlined here)
template <>
Packet<Snow::Flake, 0x1800, 1>::Packet() {
    this->m_capacity = 0x1800;

    for (int32_t i = 0; i < 0x1800; i++) {
        Snow::Flake& flake = this->m_items[i];
        flake.position = { 0.0f, 0.0f, 0.0f };
        flake.velocity = { 0.0f, 0.0f, 0.0f };
    }

    this->m_anchor = { 0.0f, 0.0f, 0.0f };
    this->m_pool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, 0x30000, GxPoolHintBit_Unk2, "WeatherPacket_vtx");
    this->m_buf = g_theGxDevicePtr->BufCreate(this->m_pool, 0x20, 0x1800, 0);
}

// The reference folds the grain packet's constructor into the flake one (FUN_00786a20)
template <>
Packet<Sand::Grain, 0x1800, 1>::Packet() {
    this->m_capacity = 0x1800;

    for (int32_t i = 0; i < 0x1800; i++) {
        Sand::Grain& grain = this->m_items[i];
        grain.position = { 0.0f, 0.0f, 0.0f };
        grain.velocity = { 0.0f, 0.0f, 0.0f };
    }

    this->m_anchor = { 0.0f, 0.0f, 0.0f };
    this->m_pool = g_theGxDevicePtr->PoolCreate(GxPoolTarget_Vertex, GxPoolUsage_Dynamic, 0x30000, GxPoolHintBit_Unk2, "WeatherPacket_vtx");
    this->m_buf = g_theGxDevicePtr->BufCreate(this->m_pool, 0x20, 0x1800, 0);
}

// ref: FUN_00786c30
template <>
Packet<Rain::Drop, 0x1800, 3>::~Packet() {
    if (this->m_buf) {
        GxBufDestroy(this->m_buf);
    }

    if (this->m_pool) {
        GxPoolDestroy(this->m_pool);
    }

    this->m_link.Unlink();
}

// ref: FUN_00786cd0
template <>
Packet<Rain::Patter, 0x1800, 3>::~Packet() {
    if (this->m_buf) {
        GxBufDestroy(this->m_buf);
    }

    if (this->m_pool) {
        GxPoolDestroy(this->m_pool);
    }

    this->m_link.Unlink();
}

// ref: FUN_00786d70
template <>
Packet<Snow::Flake, 0x1800, 1>::~Packet() {
    if (this->m_buf) {
        GxBufDestroy(this->m_buf);
    }

    if (this->m_pool) {
        GxPoolDestroy(this->m_pool);
    }

    this->m_link.Unlink();
}

// The reference folds the grain packet's destructor into the flake one (FUN_00786d70)
template <>
Packet<Sand::Grain, 0x1800, 1>::~Packet() {
    if (this->m_buf) {
        GxBufDestroy(this->m_buf);
    }

    if (this->m_pool) {
        GxPoolDestroy(this->m_pool);
    }

    this->m_link.Unlink();
}

// ---------------------------------------------------------------------------------------------
// Spawning

// ref: FUN_00785140
// A raindrop somewhere in the box above the camera, falling at a slant that grows with intensity,
// tilted against the player's travel and carried along with it; then traced down to the ground
// so it knows when it lands
void Rain::SpawnDrop(Drop& drop, float time) {
    C3Vector size = {
        this->m_bounds.t.x - this->m_bounds.b.x,
        this->m_bounds.t.y - this->m_bounds.b.y,
        this->m_bounds.t.z - this->m_bounds.b.z
    };

    float ry = RandBits(CRandom::uint32(g_rndSeed));
    float rx = RandBits(CRandom::uint32(g_rndSeed));

    C3Vector top;
    top.x = ((rx - 1.0f) - 0.5f) * size.x;
    top.y = ((ry - 1.0f) - 0.5f) * size.y;
    top.z = size.z * 0.0f;

    float angle = (this->m_intensity * 0.20943952f + 0.05235988f)
        * ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f)
        - 1.57f;
    float c = cosf(angle);
    float s = sinf(angle);

    float speedBase = this->m_intensity * 9.49f + 0.01f;
    float spread = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * this->m_intensity;
    float speed = spread + spread + speedBase;

    float fallBase = -28.0f - this->m_intensity * 4.0f;
    float fall = RandBits(CRandom::uint32(g_rndSeed)) - 1.0f;

    drop.velocity.x = s * speed;
    drop.velocity.y = speed * c;
    drop.velocity.z = fall * this->m_intensity * -2.0f + fallBase;

    float back = -((size.z * 0.5f) / drop.velocity.z);
    drop.position.x = top.x - drop.velocity.x * back;
    drop.position.y = top.y - drop.velocity.y * back;
    drop.position.z = top.z - drop.velocity.z * back;

    Weather* weather = CWorld::s_weather;
    C3Vector axis = { 0.0f, 0.0f, 0.0f };
    CMath::SinCos(weather->m_playerFacing - 1.5707964f, axis.y, axis.x);

    float lean = Clamp01(weather->m_playerSpeed * 0.055555556f);
    C44Matrix rotation = RotationAroundAxis4(lean * 1.134464f, axis, false);
    drop.position = TransformPoint(rotation, drop.position);

    drop.position.x = drop.position.x + weather->m_playerVelocity.x * 1.75f;
    drop.position.y = drop.position.y + weather->m_playerVelocity.y * 1.75f;
    drop.position.z = drop.position.z + weather->m_playerVelocity.z * 1.75f;

    const C3Vector& camera = WorldCameraPos();
    drop.position.x = drop.position.x + camera.x;
    drop.position.y = drop.position.y + camera.y;
    drop.position.z = drop.position.z + camera.z;

    C3Vector bottom = {
        drop.position.x + drop.velocity.x * back,
        drop.position.y + drop.velocity.y * back,
        drop.velocity.z * back + drop.position.z
    };

    if (weather->m_ground->HeightAt(drop.position) < drop.position.z) {
        float scale = 2.0f;

        if (weather->m_playerFlag
            || 2.0f < weather->m_transportVelocity.x * weather->m_transportVelocity.x
                + weather->m_transportVelocity.y * weather->m_transportVelocity.y
                + weather->m_transportVelocity.z * weather->m_transportVelocity.z
        ) {
            scale = 1.0f;
        }

        C3Vector end = {
            drop.velocity.x * back * scale + bottom.x,
            drop.velocity.y * back * scale + bottom.y,
            drop.velocity.z * back * scale + bottom.z
        };
        C3Vector hit = { 0.0f, 0.0f, 0.0f };

        bool landed = weather->m_playerFlag
            || 2.0f < weather->m_transportVelocity.x * weather->m_transportVelocity.x
                + weather->m_transportVelocity.y * weather->m_transportVelocity.y
                + weather->m_transportVelocity.z * weather->m_transportVelocity.z;

        if (!landed) {
            landed = weather->m_ground->Intersect(drop.position, end, hit);
            end = hit;
        }

        if (landed) {
            hit = end;

            float invSpeed = 1.0f / sqrtf(
                drop.velocity.x * drop.velocity.x
                + drop.velocity.y * drop.velocity.y
                + drop.velocity.z * drop.velocity.z
            );
            float lead = sqrtf(
                (drop.position.x - hit.x) * (drop.position.x - hit.x)
                + (drop.position.z - hit.z) * (drop.position.z - hit.z)
                + (drop.position.y - hit.y) * (drop.position.y - hit.y)
            ) * invSpeed;

            drop.position.x = hit.x - drop.velocity.x * lead;
            drop.position.y = hit.y - drop.velocity.y * lead;
            drop.position.z = hit.z - drop.velocity.z * lead;

            drop.startTime = time;
            drop.endTime = time + sqrtf(
                (drop.position.y - hit.y) * (drop.position.y - hit.y)
                + (drop.position.x - hit.x) * (drop.position.x - hit.x)
                + (drop.position.z - hit.z) * (drop.position.z - hit.z)
            ) * invSpeed;
            return;
        }
    }

    drop.startTime = 0.0f;
    drop.endTime = 0.0f;
}

// ref: FUN_00785640
// Spawns `count` drops this frame (as many as fit), each at a random moment within it, and a
// splash for each drop that lands where nothing is moving under the player
void Rain::FillPackets(DropPacket* drops, PatterPacket* patter, float count) {
    float tick = WorldTickSec();

    float room = UintToFloat(0x1800 - drops->m_count);

    if (room < count) {
        count = room;
    }

    int32_t spawn = RoundDown(count);

    for (int32_t i = 0; i < spawn; i++) {
        uint32_t bits = CRandom::uint32(g_rndSeed);
        Drop& drop = drops->m_items[drops->m_count];

        this->SpawnDrop(drop, (RandBits(bits) - 1.0f) * tick + drops->m_buildTime);

        float life = (-2.0f / drop.velocity.z + drop.endTime) * 1024.0f;
        uint32_t lastTime = drops->m_lastTime;

        if (lastTime < drops->m_baseTime + RoundDown(life)) {
            lastTime = drops->m_baseTime + RoundDown(life);
        }

        drops->m_lastTime = lastTime;

        Weather* weather = CWorld::s_weather;

        if (drop.endTime != drop.startTime
            && !weather->m_playerFlag
            && weather->m_transportVelocity.x * weather->m_transportVelocity.x
                + weather->m_transportVelocity.y * weather->m_transportVelocity.y
                + weather->m_transportVelocity.z * weather->m_transportVelocity.z <= 2.0f
        ) {
            float fall = drop.endTime - drop.startTime;
            Patter& splash = patter->m_items[patter->m_count];
            patter->m_count++;

            splash.position.x = drop.position.x + drop.velocity.x * fall;
            splash.startTime = drop.endTime;
            splash.position.y = drop.velocity.y * fall + drop.position.y;
            splash.position.z = drop.velocity.z * fall + drop.position.z;
            splash.endTime = drop.endTime + 0.25f;

            uint32_t patterLast = patter->m_lastTime;

            if (patterLast < patter->m_baseTime + RoundDown(splash.endTime * 1024.0f)) {
                patterLast = patter->m_baseTime + RoundDown(splash.endTime * 1024.0f);
            }

            patter->m_lastTime = patterLast;
        }

        drops->m_count++;
    }

    drops->m_buildTime = drops->m_buildTime + tick;
}

// ref: FUN_00785880
// A snowflake in the box above `anchor`, drifting in a random direction that widens with
// intensity, tilted against the player's travel; traced to the ground so it knows when it lands
void Snow::SpawnFlake(Flake& flake, float time, const C3Vector& anchor, const C3Vector& velocity) {
    C3Vector size = {
        this->m_bounds.t.x - this->m_bounds.b.x,
        this->m_bounds.t.y - this->m_bounds.b.y,
        this->m_bounds.t.z - this->m_bounds.b.z
    };

    float ry = RandBits(CRandom::uint32(g_rndSeed));
    float rx = RandBits(CRandom::uint32(g_rndSeed));

    C3Vector top;
    top.x = ((rx - 1.0f) - 0.5f) * size.x;
    top.y = ((ry - 1.0f) - 0.5f) * size.y;
    top.z = size.z * 0.0f;

    float angle = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f)
        * (6.2831855f - this->m_intensity * 5.9341197f)
        - 1.57f;
    float c = cosf(angle);
    float s = sinf(angle);

    float speedBase = this->m_intensity * 5.985f + 0.015f;
    float speed = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * this->m_intensity + speedBase;

    float fallBase = -2.0f - this->m_intensity * 3.5f;
    float fall = RandBits(CRandom::uint32(g_rndSeed)) - 1.0f;

    flake.velocity.x = s * speed;
    flake.velocity.y = speed * c;
    flake.velocity.z = fall * this->m_intensity * -1.0f + fallBase;

    float back = -((size.z * 0.5f) / flake.velocity.z);
    flake.position.x = top.x - flake.velocity.x * back;
    flake.position.y = top.y - flake.velocity.y * back;
    flake.position.z = top.z - flake.velocity.z * back;

    Weather* weather = CWorld::s_weather;
    C3Vector axis = { 0.0f, 0.0f, 0.0f };
    CMath::SinCos(weather->m_playerFacing - 1.5707964f, axis.y, axis.x);

    float lean = Clamp01(weather->m_playerSpeed * 0.055555556f);
    C44Matrix rotation = RotationAroundAxis4(lean * 1.134464f, axis, false);
    flake.position = TransformPoint(rotation, flake.position);

    flake.position.x = flake.position.x + velocity.x * 1.75f;
    flake.position.y = flake.position.y + velocity.y * 1.75f;
    flake.position.z = flake.position.z + velocity.z * 1.75f;

    flake.position.x = flake.position.x + anchor.x;
    flake.position.y = flake.position.y + anchor.y;
    flake.position.z = flake.position.z + anchor.z;

    C3Vector bottom = {
        flake.position.x + flake.velocity.x * back,
        flake.position.y + flake.velocity.y * back,
        flake.velocity.z * back + flake.position.z
    };

    if (weather->m_ground->HeightAt(flake.position) < flake.position.z) {
        float scale = 2.0f;

        if (weather->m_playerFlag
            || 2.0f < weather->m_transportVelocity.x * weather->m_transportVelocity.x
                + weather->m_transportVelocity.y * weather->m_transportVelocity.y
                + weather->m_transportVelocity.z * weather->m_transportVelocity.z
        ) {
            scale = 1.0f;
        }

        C3Vector end = {
            flake.velocity.x * back * scale + bottom.x,
            flake.velocity.y * back * scale + bottom.y,
            flake.velocity.z * back * scale + bottom.z
        };
        C3Vector hit = { 0.0f, 0.0f, 0.0f };

        bool landed = weather->m_playerFlag
            || 2.0f < weather->m_transportVelocity.x * weather->m_transportVelocity.x
                + weather->m_transportVelocity.y * weather->m_transportVelocity.y
                + weather->m_transportVelocity.z * weather->m_transportVelocity.z;

        if (!landed) {
            landed = weather->m_ground->Intersect(flake.position, end, hit);
            end = hit;
        }

        if (landed) {
            hit = end;

            float invSpeed = 1.0f / sqrtf(
                flake.velocity.x * flake.velocity.x
                + flake.velocity.y * flake.velocity.y
                + flake.velocity.z * flake.velocity.z
            );
            float lead = sqrtf(
                (flake.position.z - hit.z) * (flake.position.z - hit.z)
                + (flake.position.y - hit.y) * (flake.position.y - hit.y)
                + (flake.position.x - hit.x) * (flake.position.x - hit.x)
            ) * invSpeed;

            flake.position.x = hit.x - flake.velocity.x * lead;
            flake.position.y = hit.y - flake.velocity.y * lead;
            flake.position.z = hit.z - flake.velocity.z * lead;

            flake.startTime = time;
            flake.endTime = time + sqrtf(
                (flake.position.x - hit.x) * (flake.position.x - hit.x)
                + (flake.position.z - hit.z) * (flake.position.z - hit.z)
                + (flake.position.y - hit.y) * (flake.position.y - hit.y)
            ) * invSpeed;
            return;
        }
    }

    flake.startTime = 0.0f;
    flake.endTime = -0.25f;
}

// ref: FUN_00785d60
void Snow::FillPacket(FlakePacket* packet, float count, const C3Vector& anchor, const C3Vector& velocity) {
    float tick = WorldTickSec();

    float room = UintToFloat(0x1800 - packet->m_count);

    if (room < count) {
        count = room;
    }

    int32_t spawn = RoundDown(count);

    for (int32_t i = 0; i < spawn; i++) {
        uint32_t bits = CRandom::uint32(g_rndSeed);
        Flake& flake = packet->m_items[packet->m_count];

        this->SpawnFlake(flake, (RandBits(bits) - 1.0f) * tick + packet->m_buildTime, anchor, velocity);

        float end = 0.0f;

        if (0.0f < flake.endTime) {
            end = flake.endTime;
        }

        float life = (end + 0.25f) * 1024.0f;
        uint32_t lastTime = packet->m_lastTime;

        if (lastTime < packet->m_baseTime + RoundDown(life)) {
            lastTime = packet->m_baseTime + RoundDown(life);
        }

        packet->m_lastTime = lastTime;
        packet->m_count++;
    }

    packet->m_buildTime = packet->m_buildTime + tick;
}

// ref: FUN_00785ea0
// A sand grain blown across the box from behind the camera's facing, nearly level, carried with
// the player; traced to the ground so it knows when it stops
void Sand::SpawnGrain(Grain& grain, float time) {
    C3Vector size = {
        this->m_bounds.t.x - this->m_bounds.b.x,
        this->m_bounds.t.y - this->m_bounds.b.y,
        this->m_bounds.t.z - this->m_bounds.b.z
    };

    float rz = RandBits(CRandom::uint32(g_rndSeed));
    float ry = RandBits(CRandom::uint32(g_rndSeed));
    float rx = RandBits(CRandom::uint32(g_rndSeed));

    grain.position.x = ((rx - 1.0f) * 0.15f + 0.85f) * size.x;
    grain.position.y = ((ry - 1.0f) - 0.5f) * size.y;
    grain.position.z = ((rz - 1.0f) - 0.5f) * size.z;

    float angle = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * 0.34906587f - 1.57f;
    float c = cosf(angle);
    float s = sinf(angle);

    float speed = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * 0.6666667f + 18.666666f;
    float rise = RandBits(CRandom::uint32(g_rndSeed));

    grain.velocity.x = s * speed;
    grain.velocity.y = speed * c;
    grain.velocity.z = ((rise - 1.0f) - 0.5f) * 0.16666667f + 0.8333333f;

    Weather* weather = CWorld::s_weather;
    C44Matrix facing = C44Matrix::RotationAroundZ(-weather->m_playerFacing);
    grain.velocity = TransformPoint(facing, grain.velocity);
    grain.position = TransformPoint(facing, grain.position);

    C3Vector playerVelocity = weather->m_playerVelocity;
    float vy = playerVelocity.y * 1.75f;
    float vz = playerVelocity.z * 1.75f;
    float px = grain.position.x + playerVelocity.x * 1.75f;
    grain.position.x = px;
    float py = vy + grain.position.y;
    grain.position.y = py;
    float pz = vz + grain.position.z;
    grain.position.z = pz;

    const C3Vector& camera = WorldCameraPos();
    grain.position.x = px + camera.x;
    grain.position.y = py + camera.y;
    grain.position.z = pz + camera.z;

    float reach = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * 0.3f + 3.2f;

    if (grain.position.z <= weather->m_ground->HeightAt(grain.position)) {
        grain.startTime = 0.0f;
        grain.endTime = 0.0f;
        return;
    }

    C3Vector end = {
        grain.position.x + reach * grain.velocity.x,
        grain.velocity.y * reach + grain.position.y,
        grain.velocity.z * reach + grain.position.z
    };
    C3Vector hit = { 0.0f, 0.0f, 0.0f };

    weather->m_ground->Intersect(grain.position, end, hit);

    grain.startTime = time;
    grain.endTime = sqrtf(
        (grain.position.z - hit.z) * (grain.position.z - hit.z)
        + (grain.position.y - hit.y) * (grain.position.y - hit.y)
        + (grain.position.x - hit.x) * (grain.position.x - hit.x)
    ) / sqrtf(
        grain.velocity.x * grain.velocity.x
        + grain.velocity.y * grain.velocity.y
        + grain.velocity.z * grain.velocity.z
    ) + time;
}

// ref: FUN_00786210
void Sand::FillPacket(GrainPacket* packet, float count) {
    float tick = WorldTickSec();

    float room = UintToFloat(0x1800 - packet->m_count);

    if (room < count) {
        count = room;
    }

    int32_t spawn = RoundDown(count);

    for (int32_t i = 0; i < spawn; i++) {
        uint32_t bits = CRandom::uint32(g_rndSeed);
        Grain& grain = packet->m_items[packet->m_count];

        this->SpawnGrain(grain, (RandBits(bits) - 1.0f) * tick + packet->m_buildTime);

        uint32_t lastTime = packet->m_lastTime;

        if (lastTime < packet->m_baseTime + RoundDown(grain.endTime * 1024.0f)) {
            lastTime = packet->m_baseTime + RoundDown(grain.endTime * 1024.0f);
        }

        packet->m_lastTime = lastTime;
        packet->m_count++;
    }

    packet->m_buildTime = packet->m_buildTime + tick;
}

// ---------------------------------------------------------------------------------------------
// Mists

// ref: FUN_00786330
// Starts a mist sheet at `time`: Spawn places it, then the ground along its path is sampled once
// per cell it crosses, and the path is cut short where the ground climbs too steeply to follow
void Mists::Init(Mist& mist, uint32_t time) {
    this->Spawn(mist, time);

    float duration = UintToFloat(mist.endTime - mist.startTime) * 0.0009765625f;

    C3Vector point = mist.position;

    float cellSize;
    float cellInverse;
    CWorld::s_weather->m_ground->GetCellSize(&cellSize, &cellInverse);

    float speed = sqrtf(mist.velocity.y * mist.velocity.y + mist.velocity.x * mist.velocity.x);
    float inv = 1.0f / speed;
    float stepX = inv * mist.velocity.x * cellSize;
    float stepY = inv * mist.velocity.y * cellSize;
    float stepZ = inv * 0.0f * cellSize;

    int32_t steps = static_cast<int32_t>(std::nearbyint((speed * duration) / sqrtf(stepX * stepX + stepY * stepY)));
    mist.steps = steps;

    if (0x3F < steps) {
        steps = 0x40;
    }

    mist.steps = steps;

    for (int32_t i = 0; i < mist.steps; i++) {
        mist.heights[i] = CWorld::s_weather->m_ground->HeightAt(point);
        point.x = point.x + stepX;
        point.y = point.y + stepY;
        point.z = point.z + stepZ;
    }

    if (mist.steps != 3 && -1 < mist.steps - 3) {
        for (int32_t i = 0; i < mist.steps - 3; i++) {
            if (1.0f < mist.heights[i + 3] - mist.heights[i]
                || 0.75f < mist.heights[i + 2] - mist.heights[i]
                || 0.5f < mist.heights[i + 1] - mist.heights[i]
            ) {
                float end = (static_cast<float>(i + 1) / static_cast<float>(mist.steps)) * duration * 1024.0f - 0.5f;
                mist.steps = i;
                mist.endTime = mist.startTime + static_cast<int32_t>(std::nearbyint(end));

                if (i != 0) {
                    return;
                }

                mist.endTime = 0;
                mist.startTime = 0;
                return;
            }
        }
    }
}

// ref: FUN_00786560
// Places a mist sheet: a random point in the box around the camera, drifting along the mist's
// angle (turned with the camera's facing) and set back a second and a half upwind, lifted to half
// its size above the ground, with a random rise and a life of 2.55..2.85 seconds
void Mists::Spawn(Mist& mist, uint32_t time) {
    mist.startTime = time;

    float angle = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * this->m_angleSpread + this->m_angle;
    float c = cosf(angle);
    float s = sinf(angle);

    float speed = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * this->m_speedSpread + this->m_speed;
    float rise = RandBits(CRandom::uint32(g_rndSeed));

    mist.velocity.x = s * speed;
    mist.velocity.y = speed * c;
    mist.velocity.z = ((rise - 1.0f) - 0.5f) * 0.033333335f + 0.33333334f;

    Weather* weather = CWorld::s_weather;
    C44Matrix facing = C44Matrix::RotationAroundZ(-weather->m_playerFacing);
    mist.velocity = TransformPoint(facing, mist.velocity);

    float rz = RandBits(CRandom::uint32(g_rndSeed));
    float ry = RandBits(CRandom::uint32(g_rndSeed));
    float rx = RandBits(CRandom::uint32(g_rndSeed));

    mist.position.x = ((rx - 1.0f) - 0.5f) * this->m_extents.x;
    mist.position.y = ((ry - 1.0f) - 0.5f) * this->m_extents.y;
    mist.position.z = ((rz - 1.0f) - 0.5f) * this->m_extents.z;
    mist.position = TransformPoint(facing, mist.position);

    const C3Vector& camera = WorldCameraPos();
    float y = camera.y - mist.velocity.y * 1.5f;
    float z = camera.z - mist.velocity.z * 1.5f;
    mist.position.x = mist.position.x + (camera.x - mist.velocity.x * 1.5f);
    mist.position.y = mist.position.y + y;
    mist.position.z = z + mist.position.z;

    float ground = weather->m_ground->HeightAt(mist.position);

    if (ground < mist.position.z) {
        ground = mist.position.z;
    }

    mist.position.z = this->m_size * 0.5f + ground;

    mist.rise = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * 3.3333333f;

    float life = ((RandBits(CRandom::uint32(g_rndSeed)) - 1.0f) - 0.5f) * 0.3f + 2.7f;
    mist.endTime = mist.startTime + RoundDown(life * 1024.0f);
}

// ref: FUN_00786a90
// Particles still to come: active drop packets, and mist sheets alive now
int32_t Rain::CountActive() {
    uint32_t now = WorldTime();
    int32_t packets = 0;

    for (auto packet = this->m_drops.m_active.Head(); packet; packet = this->m_drops.m_active.Next(packet)) {
        if (now < packet->m_lastTime) {
            packets++;
        }
    }

    int32_t mists = 0;

    for (uint32_t i = 0; i < this->m_mists.m_mists.Count(); i++) {
        Mists::Mist& mist = this->m_mists.m_mists[i];

        if (mist.startTime && mist.startTime < now && now <= mist.endTime) {
            mists++;
        }
    }

    return mists + packets;
}

// ref: FUN_00786b10
int32_t Snow::CountActive() {
    uint32_t now = WorldTime();
    int32_t packets = 0;

    for (auto packet = this->m_flakes.m_active.Head(); packet; packet = this->m_flakes.m_active.Next(packet)) {
        if (now < packet->m_lastTime) {
            packets++;
        }
    }

    int32_t mists = 0;

    for (uint32_t i = 0; i < this->m_mists.m_mists.Count(); i++) {
        Mists::Mist& mist = this->m_mists.m_mists[i];

        if (mist.startTime && mist.startTime < now && now <= mist.endTime) {
            mists++;
        }
    }

    return mists + packets;
}

// The reference folds sand's count into snow's (FUN_00786b10); the layouts match
int32_t Sand::CountActive() {
    uint32_t now = WorldTime();
    int32_t packets = 0;

    for (auto packet = this->m_grains.m_active.Head(); packet; packet = this->m_grains.m_active.Next(packet)) {
        if (now < packet->m_lastTime) {
            packets++;
        }
    }

    int32_t mists = 0;

    for (uint32_t i = 0; i < this->m_mists.m_mists.Count(); i++) {
        Mists::Mist& mist = this->m_mists.m_mists[i];

        if (mist.startTime && mist.startTime < now && now <= mist.endTime) {
            mists++;
        }
    }

    return mists + packets;
}

// ref: FUN_00786e10
// Advances and draws the mist sheets: new ones start while the spawn budget lasts, each drifts,
// rises over the ground it was sampled against, and fades in and out and near the camera. One
// camera-facing quad each, alpha blended, unlit and unfogged.
void Mists::Render() {
    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    float half = this->m_size * 0.5f;

    C44Matrix view;
    view.a0 = 1.0f;
    view.a1 = 0.0f;
    view.b0 = 0.0f;
    view.c0 = 0.0f;
    view.c1 = 0.0f;
    view.b1 = 1.0f;
    GxXformView(view);

    C3Vector right = { view.a0 * half, view.b0 * half, view.c0 * half };
    C3Vector up = { view.a1 * half, view.b1 * half, view.c1 * half };

    C3Vector corners[4];
    corners[0] = { -right.x + up.x, -right.y + up.y, -right.z + up.z };
    corners[1] = { up.x + right.x, up.y + right.y, up.z + right.z };
    corners[2] = { -up.x + right.x, -up.y + right.y, -up.z + right.z };
    corners[3] = { -right.x + -up.x, -right.y + -up.y, -up.z + -right.z };

    static const float uvs[8] = { 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f };

    uint32_t now = WorldTime();
    uint32_t count = this->m_mists.Count();
    float dt = WorldTickSec();
    float dtSq = WorldTickSec() * WorldTickSec();
    uint32_t prev = WorldTime() - WorldTickFixed();
    uint32_t tick = WorldTickFixed();

    float accum = WorldTickSec() * this->m_spawnRate + this->m_spawnAccum;
    this->m_spawnAccum = accum;

    float capacity = UintToFloat(count);

    if (capacity < accum) {
        accum = capacity;
    }

    this->m_spawnAccum = accum;

    CGxBuf* vertexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, count * 4);
    auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(vertexBuf));
    CGxBuf* indexBuf = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, this->m_mists.Count() * 6);
    auto index = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(indexBuf));

    uint32_t vertexCount = 0;
    const C3Vector& camera = WorldCameraPos();

    for (uint32_t i = 0; i < this->m_mists.Count(); i++) {
        Mist& mist = this->m_mists[i];

        if (mist.startTime && mist.endTime < now) {
            mist.startTime = 0;
        }

        if (this->m_stopping && now <= mist.startTime) {
            mist.startTime = 0;
        }

        if (!mist.startTime && 1.0f <= this->m_spawnAccum && !this->m_stopping) {
            this->m_spawnAccum = this->m_spawnAccum - 1.0f;

            uint32_t bits = CRandom::uint32(g_rndSeed);
            float offset = UintToFloat(tick) * (RandBits(bits) - 1.0f);

            this->Init(mist, RoundDown(offset) + prev);
        }

        if (!mist.startTime) {
            continue;
        }

        float duration = UintToFloat(mist.endTime - mist.startTime) * 0.0009765625f;
        float fall = dtSq * mist.rise * 0.5f;

        mist.position.x = mist.position.x + mist.velocity.x * dt + fall;
        mist.position.y = mist.velocity.y * dt + fall + mist.position.y;
        mist.position.z = fall + mist.velocity.z * dt + mist.position.z;

        uint32_t start = mist.startTime;
        float before;

        if (start < prev) {
            before = UintToFloat(prev - start) * 0.0009765625f;
        } else {
            before = 0.0f;
        }

        before = before * (1.0f / duration);

        float age = UintToFloat(now - start) * (1.0f / duration) * 0.0009765625f;
        float progress = 1.0f;

        if (age < 1.0f) {
            progress = age;
        }

        int32_t from = RoundDown(before * static_cast<float>(mist.steps));
        int32_t to = RoundDown(static_cast<float>(mist.steps) * progress);

        if (to == from) {
            to = from + 1;
        }

        if (mist.steps - 1 <= to) {
            to = mist.steps - 1;
        }

        float h0 = mist.heights[from];
        float ground = this->m_size * 0.5f
            + (static_cast<float>(mist.steps) * before - static_cast<float>(from)) * (mist.heights[to] - h0)
            + h0;

        if (mist.position.z < ground) {
            mist.rise = mist.rise + 1.6666666f;

            float climb = ground - mist.position.z;
            float limit = this->m_size * 0.25f;

            if (limit <= climb) {
                mist.position.z = limit + mist.position.z;
            } else {
                mist.position.z = climb + mist.position.z;
            }
        }

        float elapsed = UintToFloat(now - mist.startTime);
        CImVector fog = DayNightFogColor();

        float fadeScale = 1.0f / this->m_fadeTime;
        float fadeIn = Clamp01(fadeScale * -1.0f * elapsed * 0.0009765625f + fadeScale * this->m_fadeTime);

        float outScale = 1.0f / (duration - (duration - this->m_fadeTime));
        float fadeOut = Clamp01(outScale * -1.0f * elapsed * 0.0009765625f + outScale * duration);

        float alpha = fadeOut * (1.0f - fadeIn);

        for (int32_t corner = 0; corner < 4; corner++) {
            C3Vector p = {
                (corners[corner].x + mist.position.x) - camera.x,
                (corners[corner].y + mist.position.y) - camera.y,
                (mist.position.z + corners[corner].z) - camera.z
            };

            float closeness = Clamp01(1.5f - sqrtf(p.x * p.x + p.z * p.z + p.y * p.y) * 0.083333336f);
            int32_t a = static_cast<int32_t>(std::nearbyint((1.0f - closeness) * alpha * 255.0f));

            CImVector color = fog;
            color.a = static_cast<uint8_t>(a);
            ImVectorToDevice(color);

            vertex[0] = p.x;
            vertex[1] = p.y;
            vertex[2] = p.z;
            std::memcpy(&vertex[3], &color, sizeof(uint32_t));
            vertex[4] = uvs[corner * 2];
            vertex[5] = uvs[corner * 2 + 1];
            vertex += 6;
        }

        uint16_t base = static_cast<uint16_t>(vertexCount);
        index[1] = base + 1;
        index[0] = base;
        index[3] = base;
        index[2] = base + 2;
        index[4] = base + 2;
        index[5] = base + 3;
        vertexCount += 4;
        index += 6;
    }

    g_theGxDevicePtr->BufUnlock(indexBuf, 0);
    indexBuf->unk1C = 1;
    g_theGxDevicePtr->BufUnlock(vertexBuf, 0);
    vertexBuf->unk1C = 1;

    if (vertexCount == 0) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    CGxBatch batch;
    batch.m_primType = GxPrim_Triangles;
    batch.m_start = 0;
    batch.m_count = (vertexCount * 6) >> 2;
    batch.m_minIndex = 0;
    batch.m_maxIndex = static_cast<uint16_t>(vertexCount - 1);
    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();
}

// ref: FUN_00787780
Weather::Weather() {
    this->m_intensity = 0.0f;
    this->m_pendingType = -1;
    this->m_prevIntensity = 0.0f;
    this->m_curIntensity = 0.0f;
    this->m_startTime = 0;
    this->m_fogTarget = 0.0f;
    this->m_fogStartTime = 0;
    this->m_fogFrom = 0.0f;
    this->m_dirty = 0;
    this->m_fog = 0.0f;
    this->m_gradual = 0;
    this->m_density = 1.0f;
    this->m_densityFrom = 1.0f;
    this->m_densityTo = 1.0f;
    this->m_color.b = 0xFF;
    this->m_color.g = 0xFF;
    this->m_color.r = 0xFF;
    this->m_color.a = 0xFF;
    this->m_rain = nullptr;
    this->m_snow = nullptr;
    this->m_sand = nullptr;
    this->m_ground = nullptr;
    this->m_playerVelocity = { 0.0f, 0.0f, 0.0f };
    this->m_transportVelocity = { 0.0f, 0.0f, 0.0f };
    this->m_playerPosition = { 0.0f, 0.0f, 0.0f };
    this->m_playerFlag = 0;
    this->m_playerFacing = 0.0f;
    this->m_enabled = 0;
    this->m_playerSpeed = 0.0f;

    this->m_ground = STORM_NEW(WeatherGroundCache)();

    CVar::Register("weatherDensity", nullptr, 0x0, "2", &WeatherDensityCallback, DEFAULT);
    Weather::s_useShadersCvar = CVar::Register("useWeatherShaders", nullptr, 0x0, "1", nullptr, DEFAULT);

    uint32_t forced = 0;

    if (RegistryReadInt("Internal", "force-weather-type-on", &forced)
        && 0 < static_cast<int32_t>(forced)
        && static_cast<int32_t>(forced) < 4
    ) {
        this->SetWeather(forced, 1.0f, nullptr, false, 1.0f);
        Weather::s_forced = true;
    }

    this->m_texture[0] = '\0';
}

// ---------------------------------------------------------------------------------------------
// Packet pools

// ref: FUN_00787920
template <>
Rain::DropPacket* PacketPool<Rain::DropPacket>::New() {
    Rain::DropPacket* packet = this->m_free.Head();

    if (!packet) {
        void* memory = SMemAlloc(sizeof(Rain::DropPacket), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        packet = memory ? new (memory) Rain::DropPacket() : nullptr;

        this->m_free.LinkToTail(packet);
        this->m_freeCount++;
    }

    packet->m_buildTime = 0.0f;
    packet->m_count = 0;
    packet->m_baseTime = 0;
    packet->m_anchor.x = 0.0f;
    packet->m_lastTime = 0;
    packet->m_anchor.y = 0.0f;
    packet->m_anchor.z = 0.0f;

    this->m_active.LinkToTail(packet);
    this->m_used++;
    this->m_freeCount--;

    return packet;
}

// ref: FUN_00787a10
template <>
Rain::PatterPacket* PacketPool<Rain::PatterPacket>::New() {
    Rain::PatterPacket* packet = this->m_free.Head();

    if (!packet) {
        void* memory = SMemAlloc(sizeof(Rain::PatterPacket), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        packet = memory ? new (memory) Rain::PatterPacket() : nullptr;

        this->m_free.LinkToTail(packet);
        this->m_freeCount++;
    }

    packet->m_buildTime = 0.0f;
    packet->m_count = 0;
    packet->m_baseTime = 0;
    packet->m_anchor.x = 0.0f;
    packet->m_lastTime = 0;
    packet->m_anchor.y = 0.0f;
    packet->m_anchor.z = 0.0f;

    this->m_active.LinkToTail(packet);
    this->m_used++;
    this->m_freeCount--;

    return packet;
}

// ref: FUN_00787b00
template <>
Snow::FlakePacket* PacketPool<Snow::FlakePacket>::New() {
    Snow::FlakePacket* packet = this->m_free.Head();

    if (!packet) {
        void* memory = SMemAlloc(sizeof(Snow::FlakePacket), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        packet = memory ? new (memory) Snow::FlakePacket() : nullptr;

        this->m_free.LinkToTail(packet);
        this->m_freeCount++;
    }

    packet->m_buildTime = 0.0f;
    packet->m_count = 0;
    packet->m_baseTime = 0;
    packet->m_anchor.x = 0.0f;
    packet->m_lastTime = 0;
    packet->m_anchor.y = 0.0f;
    packet->m_anchor.z = 0.0f;

    this->m_active.LinkToTail(packet);
    this->m_used++;
    this->m_freeCount--;

    return packet;
}

// ref: FUN_00787bf0
template <>
Sand::GrainPacket* PacketPool<Sand::GrainPacket>::New() {
    Sand::GrainPacket* packet = this->m_free.Head();

    if (!packet) {
        void* memory = SMemAlloc(sizeof(Sand::GrainPacket), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        packet = memory ? new (memory) Sand::GrainPacket() : nullptr;

        this->m_free.LinkToTail(packet);
        this->m_freeCount++;
    }

    packet->m_buildTime = 0.0f;
    packet->m_count = 0;
    packet->m_baseTime = 0;
    packet->m_anchor.x = 0.0f;
    packet->m_lastTime = 0;
    packet->m_anchor.y = 0.0f;
    packet->m_anchor.z = 0.0f;

    this->m_active.LinkToTail(packet);
    this->m_used++;
    this->m_freeCount--;

    return packet;
}

// ---------------------------------------------------------------------------------------------
// Updates

// ref: FUN_00787ce0
// Retires finished packets (and, while stopping, ones not yet started), uploads any whose buffer
// went stale, then spawns this frame's drops into the open packet, closing it when it is full,
// has built for long enough, or its clock has started
void Rain::Update() {
    if (this->m_stopping && this->m_dropPacket) {
        this->m_drops.m_used--;
        this->m_drops.m_freeCount++;
        this->m_drops.m_free.LinkToTail(this->m_dropPacket);
        this->m_dropPacket = nullptr;
    }

    uint32_t now = WorldTime();

    for (auto packet = this->m_drops.m_active.Head(); packet; ) {
        auto next = this->m_drops.m_active.Next(packet);
        uint32_t lastTime = packet->m_lastTime;

        if (lastTime < now) {
            this->m_drops.m_used--;
            this->m_drops.m_freeCount++;
            this->m_drops.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (this->m_stopping && !(0.25f < CWorld::s_weather->m_fog) && now <= packet->m_baseTime) {
            this->m_drops.m_used--;
            this->m_drops.m_freeCount++;
            this->m_drops.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (60.0f <= UintToFloat(lastTime - packet->m_baseTime) * 0.0009765625f) {
            // FUN_005eeb70 (a no-op in this build): "Rain packet lifetime was too long. curTime: %g,
            // baseTime: %g, lastTime: %g, buildTime: %g"
            this->m_drops.m_used--;
            this->m_drops.m_freeCount++;
            this->m_drops.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (!packet->m_buf->unk1C || !packet->m_buf->unk1D) {
            this->UploadDrops(packet);
        }

        packet = next;
    }

    for (auto packet = this->m_patter.m_active.Head(); packet; ) {
        auto next = this->m_patter.m_active.Next(packet);

        if (packet->m_lastTime < now) {
            this->m_patter.m_used--;
            this->m_patter.m_freeCount++;
            this->m_patter.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (!packet->m_buf->unk1C || !packet->m_buf->unk1D) {
            this->UploadPatter(packet);
        }

        packet = next;
    }

    if (this->m_stopping) {
        return;
    }

    float step = 0.016667f;

    if (WorldTickSec() < 0.016667f) {
        step = WorldTickSec();
    }

    float count = step * this->m_rate;

    if (!(1.0f < count)) {
        return;
    }

    if (!this->m_dropPacket) {
        this->m_dropPacket = this->m_drops.New();
        float build = 6144.0f / this->m_rate;
        this->m_dropPacket->m_baseTime = RoundDown(build * 1024.0f) + now;

        this->m_patterPacket = this->m_patter.New();
        this->m_patterPacket->m_baseTime = this->m_dropPacket->m_baseTime;
    }

    this->FillPackets(this->m_dropPacket, this->m_patterPacket, count);

    DropPacket* packet = this->m_dropPacket;

    if (0x17FF < packet->m_count || this->m_maxBuild <= packet->m_buildTime || packet->m_baseTime < now) {
        this->UploadDrops(packet);
        this->m_dropPacket = nullptr;
        this->UploadPatter(this->m_patterPacket);
        this->m_patterPacket = nullptr;
    }
}

// ref: FUN_00788090
// As Rain::Update, for flakes. A packet is anchored where the camera was when it opened, and is
// dropped early once the camera has moved 200 yards from that anchor. Riding a vehicle along a
// path, the anchor is lowered by how steeply the path is falling.
void Snow::Update() {
    if (this->m_stopping && this->m_packet) {
        this->m_flakes.m_used--;
        this->m_flakes.m_freeCount++;
        this->m_flakes.m_free.LinkToTail(this->m_packet);
        this->m_packet = nullptr;
    }

    uint32_t now = WorldTime();

    for (auto packet = this->m_flakes.m_active.Head(); packet; ) {
        auto next = this->m_flakes.m_active.Next(packet);

        if (packet->m_lastTime < now) {
            this->m_flakes.m_used--;
            this->m_flakes.m_freeCount++;
            this->m_flakes.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        bool retire = false;

        if (!this->m_stopping || 0.25f < CWorld::s_weather->m_fog || packet->m_baseTime < now) {
            const C3Vector& camera = WorldCameraPos();
            float dist = sqrtf(
                (packet->m_anchor.z - camera.z) * (packet->m_anchor.z - camera.z)
                + (packet->m_anchor.y - camera.y) * (packet->m_anchor.y - camera.y)
                + (packet->m_anchor.x - camera.x) * (packet->m_anchor.x - camera.x)
            );

            if (200.0f < dist) {
                retire = true;
            }
        } else {
            retire = true;
        }

        if (!retire && 60.0f <= UintToFloat(packet->m_lastTime - packet->m_baseTime) * 0.0009765625f) {
            // FUN_005eeb70 (a no-op in this build): "Snow packet lifetime was too long. ..."
            retire = true;
        }

        if (retire) {
            this->m_flakes.m_used--;
            this->m_flakes.m_freeCount++;
            this->m_flakes.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (!packet->m_buf->unk1C || !packet->m_buf->unk1D) {
            WeatherUploadFlakes(packet->m_count, packet->m_items, packet->m_buf);
        }

        packet = next;
    }

    if (this->m_stopping) {
        return;
    }

    Weather* weather = CWorld::s_weather;
    C3Vector anchor = WorldCameraPos();
    C3Vector velocity = weather->m_playerVelocity;

    if (weather->m_playerFlag) {
        WOWGUID guid = ClntObjMgrGetActivePlayer();
        CGObject_C* object = guid ? ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__) : nullptr;
        CGUnit_C* unit = static_cast<CGUnit_C*>(object);
        void* path = unit ? UnitVehiclePath(unit) : nullptr;

        if (path) {
            // The vehicle's path is sampled ahead of the player, its slope taken, and the anchor
            // lowered by up to the box's height as the fall steepens past 10 yards a second. Not
            // reachable until vehicles are ported (UnitVehiclePath answers none).
        }
    }

    float step = 0.016667f;

    if (WorldTickSec() < 0.016667f) {
        step = WorldTickSec();
    }

    float count = step * this->m_rate;

    if (!(1.0f < count)) {
        return;
    }

    if (!this->m_packet) {
        this->m_packet = this->m_flakes.New();
        float build = 6144.0f / this->m_rate;
        build = build * 1024.0f;
        this->m_packet->m_baseTime = RoundDown(build) + now;
        this->m_packet->m_anchor = anchor;
    }

    this->FillPacket(this->m_packet, count, anchor, velocity);

    FlakePacket* packet = this->m_packet;

    if (0x17FF < packet->m_count || this->m_maxBuild <= packet->m_buildTime || packet->m_baseTime < now) {
        WeatherUploadFlakes(packet->m_count, packet->m_items, packet->m_buf);
        this->m_packet = nullptr;
    }
}

// ref: FUN_00788660
void Sand::Update() {
    if (this->m_stopping && this->m_packet) {
        this->m_grains.m_used--;
        this->m_grains.m_freeCount++;
        this->m_grains.m_free.LinkToTail(this->m_packet);
        this->m_packet = nullptr;
    }

    uint32_t now = WorldTime();

    for (auto packet = this->m_grains.m_active.Head(); packet; ) {
        auto next = this->m_grains.m_active.Next(packet);
        uint32_t lastTime = packet->m_lastTime;

        if (lastTime < now) {
            this->m_grains.m_used--;
            this->m_grains.m_freeCount++;
            this->m_grains.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (this->m_stopping && !(0.25f < CWorld::s_weather->m_fog) && now <= packet->m_baseTime) {
            this->m_grains.m_used--;
            this->m_grains.m_freeCount++;
            this->m_grains.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (60.0f <= UintToFloat(lastTime - packet->m_baseTime) * 0.0009765625f) {
            // FUN_005eeb70 (a no-op in this build): "Sand packet lifetime was too long. ..."
            this->m_grains.m_used--;
            this->m_grains.m_freeCount++;
            this->m_grains.m_free.LinkToTail(packet);
            packet = next;
            continue;
        }

        if (!packet->m_buf->unk1C || !packet->m_buf->unk1D) {
            WeatherUploadFlakes(packet->m_count, packet->m_items, packet->m_buf);
        }

        packet = next;
    }

    if (this->m_stopping) {
        return;
    }

    float step = 0.016667f;

    if (WorldTickSec() < 0.016667f) {
        step = WorldTickSec();
    }

    float count = step * this->m_rate;

    if (!(1.0f < count)) {
        return;
    }

    if (!this->m_packet) {
        this->m_packet = this->m_grains.New();
        float build = 6144.0f / this->m_rate;
        this->m_packet->m_baseTime = RoundDown(build * 1024.0f) + now;
    }

    this->FillPacket(this->m_packet, count);

    GrainPacket* packet = this->m_packet;

    if (0x17FF < packet->m_count || this->m_maxBuild <= packet->m_buildTime || packet->m_baseTime < now) {
        WeatherUploadFlakes(packet->m_count, packet->m_items, packet->m_buf);
        this->m_packet = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
// Pool teardown

// ref: FUN_00788c10
template <>
PacketPool<Rain::DropPacket>::~PacketPool() {
    for (auto packet = this->m_active.Head(); packet; ) {
        auto next = this->m_active.Next(packet);
        this->m_used--;
        this->m_freeCount++;
        this->m_free.LinkToTail(packet);
        packet = next;
    }

    for (auto packet = this->m_free.Head(); packet; ) {
        auto next = this->m_free.Next(packet);
        packet->~Packet();
        SMemFree(packet, __FILE__, __LINE__, 0x0);
        packet = next;
    }
}

// ref: FUN_00788d50
template <>
PacketPool<Rain::PatterPacket>::~PacketPool() {
    for (auto packet = this->m_active.Head(); packet; ) {
        auto next = this->m_active.Next(packet);
        this->m_used--;
        this->m_freeCount++;
        this->m_free.LinkToTail(packet);
        packet = next;
    }

    for (auto packet = this->m_free.Head(); packet; ) {
        auto next = this->m_free.Next(packet);
        packet->~Packet();
        SMemFree(packet, __FILE__, __LINE__, 0x0);
        packet = next;
    }
}

// ref: FUN_00788e90
template <>
PacketPool<Snow::FlakePacket>::~PacketPool() {
    for (auto packet = this->m_active.Head(); packet; ) {
        auto next = this->m_active.Next(packet);
        this->m_used--;
        this->m_freeCount++;
        this->m_free.LinkToTail(packet);
        packet = next;
    }

    for (auto packet = this->m_free.Head(); packet; ) {
        auto next = this->m_free.Next(packet);
        packet->~Packet();
        SMemFree(packet, __FILE__, __LINE__, 0x0);
        packet = next;
    }
}

// ref: FUN_00788fd0
template <>
PacketPool<Sand::GrainPacket>::~PacketPool() {
    for (auto packet = this->m_active.Head(); packet; ) {
        auto next = this->m_active.Next(packet);
        this->m_used--;
        this->m_freeCount++;
        this->m_free.LinkToTail(packet);
        packet = next;
    }

    for (auto packet = this->m_free.Head(); packet; ) {
        auto next = this->m_free.Next(packet);
        packet->~Packet();
        SMemFree(packet, __FILE__, __LINE__, 0x0);
        packet = next;
    }
}

// ---------------------------------------------------------------------------------------------
// Rain

// ref: FUN_00789110
Rain::~Rain() {
    HandleClose(this->m_texture);
    HandleClose(this->m_splashTexture);

    if (this->m_shader) {
        GxShaderRelease(this->m_shader);
    }

    if (this->m_patterShader) {
        GxShaderRelease(this->m_patterShader);
    }

    // m_patter, m_drops and m_mists are destroyed after this body, in that order
}

// ref: FUN_007891b0
// The splashes on the CPU: a small camera-facing triangle each, its corner raised by how steeply
// the camera looks down, picking one of 4x4 frames by age and view angle from the splash texture
void Rain::RenderPatter() {
    if (!this->m_patter.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_splashTexture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Mod2x);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_MatDiffuse, CWorld::s_weather->m_color.value);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 1);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_FogStart, 70.0f);
    GxRsSet(GxRs_FogEnd, 75.0f);
    GxRsSet(GxRs_FogColor, 0x80808080u);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

    C44Matrix view;
    view.a0 = 1.0f;
    view.a1 = 0.0f;
    view.b0 = 0.0f;
    view.c0 = 0.0f;
    view.c1 = 0.0f;
    view.b1 = 1.0f;
    GxXformView(view);

    C3Vector right = { view.a0 * 0.083333336f, view.b0 * 0.083333336f, view.c0 * 0.083333336f };
    C3Vector up = { view.a1 * 0.16666667f, view.b1 * 0.16666667f, view.c1 * 0.16666667f };

    const C3Vector& camera = WorldCameraPos();
    uint32_t now = WorldTime();

    for (auto packet = this->m_patter.m_active.Head(); packet; packet = this->m_patter.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age = UintToFloat(now - packet->m_baseTime) * 0.0009765625f;

        CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x14, packet->m_count * 3);
        auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));
        uint32_t vertexCount = 0;

        for (uint32_t i = 0; i < packet->m_count; i++) {
            Patter& patter = packet->m_items[i];

            if (!(patter.startTime <= age && age <= patter.endTime)) {
                continue;
            }

            float dx = patter.position.x - camera.x;
            float dy = patter.position.y - camera.y;
            float dz = patter.position.z - camera.z;
            float inv = 1.0f / sqrtf(dy * dy + dz * dz + dx * dx);

            float down = 0.0f;
            float slope = (-(dy * inv) + -(dx * inv)) * 0.0f + -(dz * inv);

            if (0.0f < slope) {
                down = slope;
            }

            float lift = 0.5f * down;
            dx = dx - up.x * lift;
            dy = dy - up.y * lift;
            dz = down * 0.083333336f + (dz - up.z * lift);

            float frame = static_cast<float>(std::floor(static_cast<double>((age - patter.startTime) * 4.0f * 3.99f)));
            float row = static_cast<float>(std::floor(static_cast<double>((1.0f - down) * 3.99f)));

            float u = frame * 0.25f;
            float v = row * 0.25f + 0.25f;

            vertex[0] = -right.x + dx;
            vertex[1] = -right.y + dy;
            vertex[2] = -right.z + dz;
            vertex[3] = u;
            vertex[4] = v;

            vertex[5] = dx + up.x;
            vertex[6] = dy + up.y;
            vertex[7] = up.z + dz;
            vertex[8] = u + 0.125f;
            vertex[9] = row * 0.25f + 0.04296875f;

            vertex[10] = dx + right.x;
            vertex[11] = dy + right.y;
            vertex[12] = right.z + dz;
            vertex[13] = 0.25f + u;
            vertex[14] = v;

            vertexCount += 3;
            vertex += 15;
        }

        g_theGxDevicePtr->BufUnlock(buf, 0);
        buf->unk1C = 1;

        if (vertexCount) {
            GxPrimVertexPtr(buf, GxVBF_PT);

            CGxBatch batch;
            batch.m_primType = GxPrim_Triangles;
            batch.m_start = 0;
            batch.m_count = vertexCount;
            batch.m_minIndex = 0;
            batch.m_maxIndex = 0;
            g_theGxDevicePtr->Draw(&batch, 0);
        }
    }

    GxRsPop();
}

// ref: FUN_007898a0
// The drops on the CPU: a long thin triangle each, from where the drop is now back along its fall
// (two seconds of it when the player stands still), its base turned to face the camera, the tail
// tilted with the player's travel
void Rain::RenderDrops() {
    if (!this->m_drops.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Mod2x);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_MatDiffuse, CWorld::s_weather->m_color.value);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 1);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_FogStart, 70.0f);
    GxRsSet(GxRs_FogEnd, 75.0f);
    GxRsSet(GxRs_FogColor, 0x80808080u);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

    C3Vector camera = WorldCameraPos();
    uint32_t now = WorldTime();

    C33Matrix tilt(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    WeatherRainTilt(tilt);
    float tail = WeatherRainTail();

    for (auto packet = this->m_drops.m_active.Head(); packet; packet = this->m_drops.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age = UintToFloat(now - packet->m_baseTime) * 0.0009765625f;

        CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x14, packet->m_count * 3);
        auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));
        uint32_t vertexCount = 0;

        for (uint32_t i = 0; i < packet->m_count; i++) {
            Drop& drop = packet->m_items[i];

            float start = drop.startTime;
            float end = drop.endTime;
            float tailEnd = -(tail / drop.velocity.z) + end;

            if (!(start <= age && age <= tailEnd)) {
                continue;
            }

            if (age < end) {
                end = age;
            }

            float headT = end - start;
            float tailT = tailEnd;

            if (age < tailEnd) {
                tailT = age;
            }

            tailT = tailT - start;

            C3Vector head = {
                drop.velocity.x * headT + (drop.position.x - camera.x),
                drop.velocity.y * headT + (drop.position.y - camera.y),
                drop.velocity.z * headT + (drop.position.z - camera.z)
            };
            C3Vector back = {
                drop.velocity.x * tailT + (drop.position.x - camera.x),
                drop.velocity.y * tailT + (drop.position.y - camera.y),
                (drop.position.z - camera.z) + drop.velocity.z * tailT
            };

            C3Vector toCamera = { -head.x, -head.y, -head.z };
            float invTo = 1.0f / sqrtf(toCamera.x * toCamera.x + toCamera.y * toCamera.y + toCamera.z * toCamera.z);
            toCamera.x = toCamera.x * invTo;
            toCamera.y = toCamera.y * invTo;
            toCamera.z = toCamera.z * invTo;

            C3Vector fallDir = { -drop.velocity.x, -drop.velocity.y, -drop.velocity.z };
            float invFall = 1.0f / sqrtf(fallDir.x * fallDir.x + fallDir.y * fallDir.y + fallDir.z * fallDir.z);
            fallDir.x = fallDir.x * invFall;
            fallDir.y = fallDir.y * invFall;
            fallDir.z = invFall * fallDir.z;

            C3Vector side = {
                fallDir.z * toCamera.y - fallDir.y * toCamera.z,
                toCamera.z * fallDir.x - fallDir.z * toCamera.x,
                fallDir.y * toCamera.x - toCamera.y * fallDir.x
            };
            float invSide = 1.0f / sqrtf(side.x * side.x + side.z * side.z + side.y * side.y);
            side.y = side.y * invSide;
            side.z = invSide * side.z;

            vertex[0] = side.x * invSide * -0.05f + head.x;
            vertex[1] = side.y * -0.05f + head.y;
            vertex[2] = -0.05f * side.z + head.z;
            vertex[3] = 0.0f;
            vertex[4] = 1.0f;

            vertex[5] = side.x * invSide * 0.05f + head.x;
            vertex[6] = side.y * 0.05f + head.y;
            vertex[7] = 0.05f * side.z + head.z;
            vertex[8] = 1.0f;
            vertex[9] = 1.0f;

            C3Vector streak = { fallDir.x * 2.0f, fallDir.y * 2.0f, 2.0f * fallDir.z };
            C3Vector turned = {
                streak.y * tilt.a1 + streak.z * tilt.a2 + tilt.a0 * streak.x,
                streak.y * tilt.b1 + streak.z * tilt.b2 + tilt.b0 * streak.x,
                streak.y * tilt.c1 + streak.z * tilt.c2 + tilt.c0 * streak.x
            };

            vertex[10] = back.x + turned.x;
            vertex[11] = turned.y + back.y;
            vertex[12] = turned.z + back.z;
            vertex[13] = 0.5f;
            vertex[14] = 0.0f;

            vertexCount += 3;
            vertex += 15;
        }

        g_theGxDevicePtr->BufUnlock(buf, 0);
        buf->unk1C = 1;

        if (vertexCount) {
            GxPrimVertexPtr(buf, GxVBF_PT);

            CGxBatch batch;
            batch.m_primType = GxPrim_Triangles;
            batch.m_start = 0;
            batch.m_count = vertexCount;
            batch.m_minIndex = 0;
            batch.m_maxIndex = 0;
            g_theGxDevicePtr->Draw(&batch, 0);
        }
    }

    GxRsPop();
}

// The view-projection the weather shaders take (c2..c5 or c4..c7), transposed for the vertex
// program, built the way the reference builds it inline: the view off the transform stack, the
// native projection (FUN_00407f80 on +0xfc8, its z row negated on OpenGL), multiplied and turned
static C44Matrix WeatherViewProjection() {
    C44Matrix view;
    GxXformView(view);

    C44Matrix proj;
    GxXformProjNative(proj);

    return (view * proj).Transpose();
}

// ref: FUN_0078a030
// The splashes through Shaders\Vertex\patter: each packet's static buffer drawn whole, the
// program placing and animating each splash from its age
void Rain::RenderPatterShader() {
    if (!this->m_patter.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_splashTexture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Mod2x);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 1);
    GxRsSet(GxRs_Culling, 0);

    float fog[4] = { g_shadowMapFogScale * -0.2f, 15.0f, 1.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, fog, 1);

    float color[4];
    ImVectorToFloats(color, CWorld::s_weather->m_color);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 7, color, 1);

    GxRsSet(GxRs_FogColor, 0x80808080u);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, this->m_patterShader);

    C44Matrix view;
    GxXformView(view);
    C44Matrix viewProj = WeatherViewProjection();

    const C3Vector& camera = WorldCameraPos();
    float eye[4] = { camera.x, camera.y, camera.z, 1.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 1, eye, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 2, &viewProj.a0, 4);

    C3Vector right = { view.a0 * 0.083333336f, view.b0 * 0.083333336f, view.c0 * 0.083333336f };
    C3Vector up = { view.a1 * 0.16666667f, view.b1 * 0.16666667f, view.c1 * 0.16666667f };

    // The w of each is whatever follows it on the reference's stack
    float left[4] = { -right.x, -right.y, -right.z, right.x };
    float upward[4] = { up.x, up.y, up.z, 0.0f };
    float rightward[4] = { right.x, right.y, right.z, up.x };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 8, left, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 9, upward, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 10, rightward, 1);

    float frames[4] = { 0.0f, 0.25f, 0.0f, 0.0f };
    float frameSize[4] = { 0.125f, 0.04296875f, 0.0f, 0.0f };
    float frameStep[4] = { 0.25f, 0.25f, 0.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 11, frames, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 12, frameSize, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 13, frameStep, 1);

    uint32_t now = WorldTime();

    for (auto packet = this->m_patter.m_active.Head(); packet; packet = this->m_patter.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age[4] = { UintToFloat(now - packet->m_baseTime) * 0.0009765625f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 6, age, 1);

        GxPrimVertexPtr(packet->m_buf, GxVBF_PCT);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = packet->m_count * 3;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 0;
        g_theGxDevicePtr->Draw(&batch, 0);
    }

    GxRsPop();
}

// ref: FUN_0078a640
// The drops through Shaders\Vertex\rain: each packet's static buffer drawn whole, the program
// moving each drop by its age, the player's tilt in c8..c10 and the tail length in c11
void Rain::RenderDropsShader() {
    if (!this->m_drops.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Mod2x);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 1);
    GxRsSet(GxRs_Culling, 0);

    float fog[4] = { g_shadowMapFogScale * -0.2f, 15.0f, 1.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, fog, 1);

    float color[4];
    ImVectorToFloats(color, CWorld::s_weather->m_color);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 7, color, 1);

    GxRsSet(GxRs_FogColor, 0x80808080u);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, this->m_shader);

    C44Matrix viewProj = WeatherViewProjection();

    const C3Vector& camera = WorldCameraPos();
    float eye[4] = { camera.x, camera.y, camera.z, 1.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 1, eye, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 2, &viewProj.a0, 4);

    C33Matrix tilt(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f);
    WeatherRainTilt(tilt);

    float column0[4] = { tilt.a0, tilt.b0, tilt.c0, 0.0f };
    float column1[4] = { tilt.a1, tilt.b1, tilt.c1, 0.0f };
    float column2[4] = { tilt.a2, tilt.b2, tilt.c2, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 8, column0, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 9, column1, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 10, column2, 1);

    float tail[4] = { WeatherRainTail(), 0.0f, 0.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 11, tail, 1);

    float c12[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    float c13[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
    float c14[4] = { 0.5f, 0.0f, 0.0f, 0.0f };
    float c15[4] = { -0.05f, -0.05f, -0.05f, 0.0f };
    float c16[4] = { 0.05f, 0.05f, 0.05f, 0.0f };
    float c17[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float c18[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float c19[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float c20[4] = { 2.0f, 2.0f, 2.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 12, c12, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 13, c13, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 14, c14, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 15, c15, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 16, c16, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 17, c17, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 18, c18, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 19, c19, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 20, c20, 1);

    uint32_t now = WorldTime();

    for (auto packet = this->m_drops.m_active.Head(); packet; packet = this->m_drops.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age[4] = { UintToFloat(now - packet->m_baseTime) * 0.0009765625f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 6, age, 1);

        GxPrimVertexPtr(packet->m_buf, GxVBF_PNCT);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = packet->m_count * 3;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 0;
        g_theGxDevicePtr->Draw(&batch, 0);
    }

    GxRsPop();
}

// ref: FUN_0078ae20
void Rain::Render() {
    this->Update();

    if (CWorldScene::s_window.depth < 0.0f) {
        return;
    }

    if (this->m_useShaders) {
        this->RenderDropsShader();
        this->RenderPatterShader();
        this->m_mists.Render();
        return;
    }

    this->RenderDrops();
    this->RenderPatter();
    this->m_mists.Render();
}

// ---------------------------------------------------------------------------------------------
// Snow

// ref: FUN_0078ae70
Snow::~Snow() {
    HandleClose(this->m_texture);

    if (this->m_shader) {
        GxShaderRelease(this->m_shader);
    }

    // m_flakes and m_mists are destroyed after this body
}

// ref: FUN_0078aee0
// The flakes through Shaders\Vertex\snowpoint: textured point sprites, alpha blended
void Snow::RenderShader() {
    if (!this->m_flakes.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);

    float maxSize = 24.0f;

    if (CapsMaxPointSize() < 24.0f) {
        maxSize = CapsMaxPointSize();
    }

    GxRsSet(GxRs_PointScaleMax, maxSize);
    GxRsSet(GxRs_PointSprite, 1);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

    float color[4];
    ImVectorToFloats(color, CWorld::s_weather->m_color);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 8, color, 1);

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, this->m_shader);

    C44Matrix viewProj = WeatherViewProjection();

    const C3Vector& camera = WorldCameraPos();
    float eye[4] = { camera.x, camera.y, camera.z, 1.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 1, eye, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 2, &viewProj.a0, 4);

    float size[4] = { -0.02f, 1.0f, 14.0f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 7, size, 1);

    uint32_t now = WorldTime();

    for (auto packet = this->m_flakes.m_active.Head(); packet; packet = this->m_flakes.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age[4] = { UintToFloat(now - packet->m_baseTime) * 0.0009765625f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 6, age, 1);

        GxPrimVertexPtr(packet->m_buf, GxVBF_PNT);

        CGxBatch batch;
        batch.m_primType = GxPrim_Points;
        batch.m_start = 0;
        batch.m_count = packet->m_count;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 0;
        g_theGxDevicePtr->Draw(&batch, 0);
    }

    GxRsPop();
}

// ref: FUN_0078b370
// The grains on the CPU. The reference reads its texture from +0x98, which in Sand is the vertex
// shader slot (a reference bug: sand has no texture of its own); frozen treats that as no texture
// and draws nothing rather than handing a shader to the texture code. Recorded as diverged.
void Sand::RenderCpu() {
    if (!this->m_grains.m_active.Head()) {
        return;
    }

    CGxTex* gxTex = nullptr;

    if (!gxTex) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 0);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);

    C44Matrix view;
    GxXformView(view);

    const float k = 0.083333336f;
    C3Vector offsets[3] = {
        { k * 0.0f, 1.0f * 1.0f * k, 0.0f * 1.0f * k },
        { k * 0.70710677f, k * -0.70710677f, k * 0.0f },
        { k * -0.70710677f, k * -0.70710677f, k * -0.0f }
    };

    static const float uvs[6] = { 0.5f, -0.75f, -0.5f, 1.5f, 1.5f, 1.5f };

    C3Vector camera = WorldCameraPos();
    uint32_t now = WorldTime();

    CImVector baseColor = CWorld::s_weather->m_color;
    ImVectorToDevice(baseColor);

    for (auto packet = this->m_grains.m_active.Head(); packet; packet = this->m_grains.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age = UintToFloat(now - packet->m_baseTime) * 0.0009765625f;

        CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, packet->m_count * 3);
        auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));
        uint32_t vertexCount = 0;

        for (uint32_t i = 0; i < packet->m_count; i++) {
            Grain& grain = packet->m_items[i];
            float end = grain.endTime;

            if (!(grain.startTime <= age && age <= end + 0.25f)) {
                continue;
            }

            float t = end;

            if (age < end) {
                t = age;
            }

            t = t - grain.startTime;

            float after = age - end;

            if (after < 0.0f) {
                after = 0.0f;
            }

            C3Vector p = {
                grain.position.x + grain.velocity.x * t,
                grain.velocity.y * t + grain.position.y,
                grain.velocity.z * t + grain.position.z
            };

            float fade = Clamp01(1.0f - after * 4.0f);

            if (1.0f < t) {
                t = 1.0f;
            }

            if (fade < 1.0f) {
                t = fade;
            }

            CImVector color = baseColor;
            color.a = static_cast<uint8_t>(static_cast<int32_t>(std::nearbyint(t * 255.0f)));
            uint32_t packed;
            std::memcpy(&packed, &color, sizeof(packed));

            C3Vector rel = { p.x - camera.x, p.y - camera.y, p.z - camera.z };

            for (int32_t corner = 0; corner < 3; corner++) {
                vertex[0] = rel.x + offsets[corner].x;
                vertex[1] = rel.y + offsets[corner].y;
                vertex[2] = offsets[corner].z + rel.z;
                std::memcpy(&vertex[3], &packed, sizeof(packed));
                vertex[4] = uvs[corner * 2];
                vertex[5] = uvs[corner * 2 + 1];
                vertex += 6;
            }

            vertexCount += 3;
        }

        g_theGxDevicePtr->BufUnlock(buf, 0);
        buf->unk1C = 1;

        if (vertexCount) {
            GxPrimVertexPtr(buf, GxVBF_PCT);

            CGxBatch batch;
            batch.m_primType = GxPrim_Triangles;
            batch.m_start = 0;
            batch.m_count = vertexCount;
            batch.m_minIndex = 0;
            batch.m_maxIndex = 0;
            g_theGxDevicePtr->Draw(&batch, 0);
        }
    }

    GxRsPop();
}

// ref: FUN_0078ba60
void Snow::Render() {
    this->Update();

    if (CWorldScene::s_window.depth < 0.0f) {
        return;
    }

    if (this->m_useShaders) {
        this->RenderShader();
        this->m_mists.Render();
        return;
    }

    this->RenderCpu();
    this->m_mists.Render();
}

// ref: FUN_0078baa0
Sand::~Sand() {
    if (this->m_shader) {
        GxShaderRelease(this->m_shader);
    }

    // m_grains and m_mists are destroyed after this body
}

// ref: FUN_0078bb00
// The flakes on the CPU: one point each in the fog colour, fading in over its first fifth of a
// second and out over its last
void Snow::RenderCpu() {
    if (!this->m_flakes.m_active.Head()) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);

    C3Vector camera = WorldCameraPos();
    uint32_t now = WorldTime();
    CImVector fog = DayNightFogColor();

    for (auto packet = this->m_flakes.m_active.Head(); packet; packet = this->m_flakes.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age = UintToFloat(now - packet->m_baseTime) * 0.0009765625f;

        CGxBuf* buf = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x10, packet->m_count);
        auto vertex = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(buf));
        uint32_t vertexCount = 0;

        for (uint32_t i = 0; i < packet->m_count; i++) {
            Flake& flake = packet->m_items[i];
            float start = flake.startTime;

            if (!(start <= age && age <= flake.endTime)) {
                continue;
            }

            float duration = flake.endTime - start;
            float since = age - start;
            float t = 0.0f;

            if (0.0f <= since && (t = since, duration <= since)) {
                t = duration;
            }

            float fadeIn = Clamp01(1.0f - 5.0f * t);

            float outScale = 1.0f / (duration - (duration - 0.2f));
            float fadeOut = Clamp01(-1.0f * outScale * t + outScale * duration);

            CImVector color = fog;
            color.a = static_cast<uint8_t>(static_cast<int32_t>(std::nearbyint(fadeOut * (1.0f - fadeIn) * 255.0f)));
            ImVectorToDevice(color);

            vertex[0] = (flake.position.x + flake.velocity.x * t) - camera.x;
            vertex[1] = (flake.position.y + flake.velocity.y * t) - camera.y;
            vertex[2] = (flake.velocity.z * t + flake.position.z) - camera.z;
            std::memcpy(&vertex[3], &color, sizeof(uint32_t));
            vertex += 4;
            vertexCount++;
        }

        g_theGxDevicePtr->BufUnlock(buf, 0);
        buf->unk1C = 1;

        if (vertexCount) {
            GxPrimVertexPtr(buf, GxVBF_PC);

            CGxBatch batch;
            batch.m_primType = GxPrim_Points;
            batch.m_start = 0;
            batch.m_count = vertexCount;
            batch.m_minIndex = 0;
            batch.m_maxIndex = 0;
            g_theGxDevicePtr->Draw(&batch, 0);
        }
    }

    GxRsPop();
}

// ref: FUN_0078bee0
// The grains through Shaders\Vertex\sand: untextured point sprites in the fog colour, sized by the
// window height
void Sand::RenderShader() {
    if (!this->m_grains.m_active.Head()) {
        return;
    }

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSetAlphaRef();
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_PointSprite, 1);
    GxRsSet(GxRs_PointScaleMin, 1.0f);

    float maxSize = 24.0f;

    if (CapsMaxPointSize() < 24.0f) {
        maxSize = CapsMaxPointSize();
    }

    GxRsSet(GxRs_PointScaleMax, maxSize);
    g_theGxDevicePtr->RsSet(GxRs_VertexShader, this->m_shader);

    C44Matrix viewProj = WeatherViewProjection();

    const C3Vector& camera = WorldCameraPos();
    float eye[4] = { camera.x, camera.y, camera.z, 1.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 3, eye, 1);
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 4, &viewProj.a0, 4);

    CRect window;
    g_theGxDevicePtr->CapsWindowSize(window);

    float size[4] = { -0.05f, 1.5f, (window.maxY - window.minY) * 0.0025f, 0.0f };
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 9, size, 1);

    float color[4];
    ImVectorToFloats(color, DayNightFogColor());
    g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 0, color, 1);

    uint32_t now = WorldTime();

    for (auto packet = this->m_grains.m_active.Head(); packet; packet = this->m_grains.m_active.Next(packet)) {
        if (!(packet->m_baseTime < now)) {
            continue;
        }

        float age[4] = { UintToFloat(now - packet->m_baseTime) * 0.0009765625f, 0.0f, 0.0f, 0.0f };
        g_theGxDevicePtr->ShaderConstantsSet(GxSh_Vertex, 8, age, 1);

        GxPrimVertexPtr(packet->m_buf, GxVBF_PNT);

        CGxBatch batch;
        batch.m_primType = GxPrim_Points;
        batch.m_start = 0;
        batch.m_count = packet->m_count;
        batch.m_minIndex = 0;
        batch.m_maxIndex = 0;
        g_theGxDevicePtr->Draw(&batch, 0);
    }

    GxRsPop();
}

// ref: FUN_0078c3e0
void Sand::Render() {
    this->Update();

    if (CWorldScene::s_window.depth < 0.0f) {
        return;
    }

    if (this->m_useShaders) {
        this->RenderShader();
        this->m_mists.Render();
        return;
    }

    this->RenderCpu();
    this->m_mists.Render();
}

// ref: FUN_0078c420
Mists::Mists(float size, const C3Vector& extents, const char* texture, float angle, float angleSpread,
             float speed, float speedSpread, uint32_t count) {
    this->m_extents = extents;
    this->m_10 = { 0.0f, 0.0f, 0.0f };
    this->m_stopping = 0;
    this->m_size = size;
    this->m_texture = nullptr;
    this->m_spawnRate = 0.0f;
    this->m_angle = angle;
    this->m_angleSpread = angleSpread;
    this->m_spawnAccum = 0.0f;
    this->m_speed = speed;
    this->m_speedSpread = speedSpread;
    this->m_fadeTime = 0.4f;

    this->m_mists.SetCount(count);

    CStatus status;
    this->m_texture = TextureCreate(texture, CGxTexFlags(GxTex_LinearMipLinear, 0, 0, 0, 0, 0, 1), &status, 1);
}

// The reference inlines this into each effect's destructor (FUN_00789110, FUN_0078ae70,
// FUN_0078baa0): the texture closed, then the array freed
Mists::~Mists() {
    HandleClose(this->m_texture);
}

// ref: FUN_0078c500
// Tracks the player's motion: per-update deltas of the player's and the transport's positions are
// kept for 150 ms, and their averages are the velocities the weather leans and carries with.
// Standing still, the player's is zeroed.
void Weather::UpdateMotion() {
    uint32_t total = 0;

    for (auto delta = this->m_playerDeltas.Head(); delta; ) {
        total += delta->time;
        delta = this->m_playerDeltas.Next(delta);

        if (0x95 < total) {
            this->m_playerDeltas.DeleteNode(this->m_playerDeltas.Head());
        }
    }

    total = 0;

    for (auto delta = this->m_transportDeltas.Head(); delta; ) {
        total += delta->time;
        delta = this->m_transportDeltas.Next(delta);

        if (0x95 < total) {
            this->m_transportDeltas.DeleteNode(this->m_transportDeltas.Head());
        }
    }

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    C3Vector moveVelocity = { 0.0f, 0.0f, 0.0f };
    C3Vector transport = { 0.0f, 0.0f, 0.0f };

    WOWGUID guid = ClntObjMgrGetActivePlayer();
    CGObject_C* player = guid ? ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__) : nullptr;

    if (player) {
        position = player->GetPosition();

        CGUnit_C* unit = static_cast<CGUnit_C*>(player);
        float speed = UnitMoveSpeed(unit);
        C3Vector direction = UnitMoveDirection(unit);
        moveVelocity = { direction.x * speed, direction.y * speed, direction.z * speed };

        WOWGUID transportGuid = player->GetTransportGUID();

        if (transportGuid) {
            CGObject_C* object = ClntObjMgrObjectPtr(player->GetTransportGUID(), TYPE_OBJECT, __FILE__, __LINE__);

            if (object) {
                transport = object->GetPosition();
            }
        }
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    static C3Vector s_prevTransport = transport;
    static uint32_t s_prevTime = now;

    int32_t elapsed;

    if (now == s_prevTime) {
        elapsed = 0;
    } else {
        void* memory = SMemAlloc(sizeof(PosDelta), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        PosDelta* delta = memory ? new (memory) PosDelta() : nullptr;

        this->m_transportDeltas.LinkToTail(delta);
        delta->delta = { transport.x - s_prevTransport.x, transport.y - s_prevTransport.y, transport.z - s_prevTransport.z };
        elapsed = now - s_prevTime;
        delta->time = elapsed;
    }

    s_prevTransport = transport;

    if (elapsed) {
        void* memory = SMemAlloc(sizeof(PosDelta), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY);
        PosDelta* delta = memory ? new (memory) PosDelta() : nullptr;

        this->m_playerDeltas.LinkToTail(delta);
        delta->delta = {
            position.x - this->m_playerPosition.x,
            position.y - this->m_playerPosition.y,
            position.z - this->m_playerPosition.z
        };
        delta->time = now - s_prevTime;
    }

    int32_t time = 0;
    C3Vector sum = { 0.0f, 0.0f, 0.0f };

    for (auto delta = this->m_playerDeltas.Head(); delta; delta = this->m_playerDeltas.Next(delta)) {
        time += delta->time;
        sum.x = delta->delta.x + sum.x;
        sum.y = delta->delta.y + sum.y;
        sum.z = sum.z + delta->delta.z;
    }

    float inv = 1.0f / (UintToFloat(static_cast<uint32_t>(time + 1)) * 0.001f);
    s_prevTime = now;
    this->m_playerVelocity = { sum.x * inv, sum.y * inv, inv * sum.z };

    time = 0;
    sum = { 0.0f, 0.0f, 0.0f };

    for (auto delta = this->m_transportDeltas.Head(); delta; delta = this->m_transportDeltas.Next(delta)) {
        sum.x = sum.x + delta->delta.x;
        time += delta->time;
        sum.y = delta->delta.y + sum.y;
        sum.z = delta->delta.z + sum.z;
    }

    float invTransport = 1.0f / (UintToFloat(static_cast<uint32_t>(time + 1)) * 0.001f);
    this->m_transportVelocity = { sum.x * invTransport, sum.y * invTransport, invTransport * sum.z };

    if (fabsf(moveVelocity.x) < 2.3841858e-07f
        && fabsf(moveVelocity.y) < 2.3841858e-07f
        && fabsf(moveVelocity.z) < 2.3841858e-07f
        && this->m_transportVelocity.x * this->m_transportVelocity.x
            + this->m_transportVelocity.y * this->m_transportVelocity.y
            + this->m_transportVelocity.z * this->m_transportVelocity.z <= 2.0f
    ) {
        this->m_playerVelocity = { 0.0f, 0.0f, 0.0f };
    }
}

// ref: FUN_0078ca50
void Weather::Render() {
    if (!this->m_enabled) {
        return;
    }

    if (this->m_rain) {
        this->m_rain->Render();
    }

    if (this->m_snow) {
        this->m_snow->Render();
    }

    if (this->m_sand) {
        this->m_sand->Render();
    }
}

// ref: FUN_0078ca90
Rain::Rain(float width, float height)
    : m_mists(12.0f, C3Vector(44.0f, 44.0f, 25.0f), "textures\\Weather\\SnowMist01.blp", -1.57f, 0.34906587f, 5.0f, 1.2f, 0x80) {
    this->m_useShaders = 1;
    this->m_stopping = 0;
    this->m_bounds = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    this->m_dropPacket = nullptr;
    this->m_patterPacket = nullptr;
    this->m_texture = nullptr;
    this->m_shader = nullptr;
    this->m_splashTexture = nullptr;
    this->m_patterShader = nullptr;
    this->m_maxBuild = 0.0f;
    this->m_rate = 0.0f;
    this->m_intensity = 0.0f;

    this->SetIntensity(0.0f);

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    this->m_bounds.b.x = -halfWidth;
    this->m_bounds.b.y = -halfWidth;
    this->m_bounds.b.z = -halfHeight;
    this->m_bounds.t.x = halfWidth;
    this->m_bounds.t.y = halfWidth;
    this->m_bounds.t.z = halfHeight;

    g_theGxDevicePtr->ShaderCreate(&this->m_shader, GxSh_Vertex, "Shaders\\Vertex", "rain", 1);

    CStatus status;
    this->m_texture = TextureCreate(CWorld::s_weather->m_texture, CGxTexFlags(GxTex_LinearMipLinear, 0, 0, 0, 0, 0, 1), &status, 1);

    g_theGxDevicePtr->ShaderCreate(&this->m_patterShader, GxSh_Vertex, "Shaders\\Vertex", "patter", 1);
    this->m_splashTexture = TextureCreate("textures\\Weather\\RainDropSplash01.blp", CGxTexFlags(GxTex_LinearMipLinear, 0, 0, 0, 0, 0, 1), &status, 1);

    if (!this->m_shader || !this->m_shader->Valid() || !this->m_patterShader || !this->m_patterShader->Valid()) {
        this->m_useShaders = 0;
    }

    if (!Weather::s_useShadersCvar->m_intValue) {
        this->m_useShaders = 0;
    }
}

// ref: FUN_0078cd30
Snow::Snow(float width, float height)
    : m_mists(12.0f, C3Vector(44.0f, 44.0f, 25.0f), "textures\\Weather\\SnowMist01.blp", -1.57f, 0.34906587f, 9.0f, 3.0f, 0x80) {
    this->m_useShaders = 1;
    this->m_stopping = 0;
    this->m_bounds = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    this->m_packet = nullptr;
    this->m_texture = nullptr;
    this->m_shader = nullptr;
    this->m_maxBuild = 0.0f;
    this->m_rate = 0.0f;
    this->m_intensity = 0.0f;

    this->SetIntensity(0.0f);

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    this->m_bounds.b.x = -halfWidth;
    this->m_bounds.b.y = -halfWidth;
    this->m_bounds.b.z = -halfHeight;
    this->m_bounds.t.x = halfWidth;
    this->m_bounds.t.y = halfWidth;
    this->m_bounds.t.z = halfHeight;

    g_theGxDevicePtr->ShaderCreate(&this->m_shader, GxSh_Vertex, "Shaders\\Vertex", "snowpoint", 1);

    CStatus status;
    this->m_texture = TextureCreate(CWorld::s_weather->m_texture, CGxTexFlags(GxTex_LinearMipLinear, 0, 0, 0, 0, 0, 1), &status, 1);

    if (!this->m_shader || !this->m_shader->Valid() || !CapsPointSprites()) {
        this->m_useShaders = 0;
    }

    if (!Weather::s_useShadersCvar->m_intValue) {
        this->m_useShaders = 0;
    }
}

// ref: FUN_0078cf20
Sand::Sand(float width, float height)
    : m_mists(12.0f, C3Vector(44.0f, 44.0f, 25.0f), "textures\\Weather\\WeatherMistGrainy01.blp", -1.57f, 0.34906587f, 15.0f, 4.5f, 0x80) {
    this->m_useShaders = 1;
    this->m_stopping = 0;
    this->m_bounds = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };

    this->m_packet = nullptr;
    this->m_shader = nullptr;
    this->m_maxBuild = 0.0f;
    this->m_rate = 0.0f;

    this->SetIntensity(0.0f);

    float halfWidth = width * 0.5f;
    float halfHeight = height * 0.5f;
    this->m_bounds.b.x = -halfWidth;
    this->m_bounds.b.y = -halfWidth;
    this->m_bounds.b.z = -halfHeight;
    this->m_bounds.t.x = halfWidth;
    this->m_bounds.t.y = halfWidth;
    this->m_bounds.t.z = halfHeight;

    g_theGxDevicePtr->ShaderCreate(&this->m_shader, GxSh_Vertex, "Shaders\\Vertex", "sand", 1);

    if (!this->m_shader || !this->m_shader->Valid() || !CapsSandShader()) {
        this->m_useShaders = 0;
    }

    if (!Weather::s_useShadersCvar->m_intValue) {
        this->m_useShaders = 0;
    }
}

// ref: FUN_0078d0b0
void Weather::DestroyEffects() {
    Rain* rain = this->m_rain;

    if (rain) {
        rain->~Rain();
        SMemFree(rain, __FILE__, __LINE__, 0x0);
    }

    Snow* snow = this->m_snow;
    this->m_rain = nullptr;

    if (snow) {
        snow->~Snow();
        SMemFree(snow, __FILE__, __LINE__, 0x0);
    }

    Sand* sand = this->m_sand;
    this->m_snow = nullptr;

    if (sand) {
        sand->~Sand();
        SMemFree(sand, __FILE__, __LINE__, 0x0);
    }

    this->m_sand = nullptr;
}

// ref: FUN_0078d130
// Leaving the world: the effects go and the state returns to a clear sky
void Weather::Clear() {
    if (Weather::s_forced) {
        return;
    }

    this->DestroyEffects();

    this->m_prevIntensity = 0.0f;
    this->m_pendingType = -1;
    this->m_intensity = 0.0f;
    this->m_texture[0] = '\0';
    this->m_curIntensity = 0.0f;
    this->m_fogFrom = 0.0f;
    this->m_fogTarget = 0.0f;
    this->m_fog = 0.0f;
    this->m_densityTo = 1.0f;
    this->m_densityFrom = 1.0f;
    this->m_density = 1.0f;
}

// ref: FUN_0078d170
// The weather's per-update step: the player's motion and heading, then a queued effect type is
// built once the old effect has drained (or at once when the change is abrupt) while the old one
// is told to stop; the ground window follows the camera and the intensity ramps
void Weather::Update() {
    this->UpdateMotion();

    WOWGUID guid = ClntObjMgrGetActivePlayer();
    CGObject_C* player = guid ? ClntObjMgrObjectPtr(guid, TYPE_PLAYER, __FILE__, __LINE__) : nullptr;

    if (player) {
        CGUnit_C* unit = static_cast<CGUnit_C*>(player);

        C3Vector heading = { this->m_playerVelocity.x, this->m_playerVelocity.y, 0.0f };

        if (sqrtf(heading.x * heading.x + heading.y * heading.y) < 1.0f
            || ((unit->Unit()->flags >> 20) & 1) != 0
        ) {
            this->m_playerFacing = player->GetFacing();
        } else {
            float inv = 1.0f / sqrtf(heading.x * heading.x + heading.y * heading.y + heading.z * heading.z);
            heading.x = heading.x * inv;
            heading.y = heading.y * inv;
            heading.z = inv * heading.z;

            float facing = atan2f(heading.y, heading.x);
            this->m_playerFacing = facing;

            if (facing < 0.0f) {
                this->m_playerFacing = facing + 6.2831855f;
            }
        }

        this->m_playerFlag = (unit->Unit()->flags >> 20) & 1;
        this->m_playerPosition = player->GetPosition();

        if (this->m_transportVelocity.x * this->m_transportVelocity.x
            + this->m_transportVelocity.y * this->m_transportVelocity.y
            + this->m_transportVelocity.z * this->m_transportVelocity.z <= 2.0f
        ) {
            this->m_playerSpeed = UnitMoveSpeed(unit);
        } else {
            this->m_playerSpeed = sqrtf(
                this->m_transportVelocity.y * this->m_transportVelocity.y
                + this->m_transportVelocity.x * this->m_transportVelocity.x
            );
        }
    }

    bool draining = false;

    if (this->m_pendingType != -1) {
        int32_t active = 0;

        if (this->m_rain) {
            active = this->m_rain->CountActive();
        }

        if (this->m_snow) {
            active = active + this->m_snow->CountActive();
        }

        if (this->m_sand) {
            active = active + this->m_sand->CountActive();
        }

        if (active < 1 || !this->m_gradual) {
            this->DestroyEffects();

            if (this->m_pendingType == 1) {
                void* memory = SMemAlloc(sizeof(Rain), __FILE__, __LINE__, 0x0);
                this->m_rain = memory ? new (memory) Rain(130.0f, 75.0f) : nullptr;
            } else if (this->m_pendingType == 2) {
                void* memory = SMemAlloc(sizeof(Snow), __FILE__, __LINE__, 0x0);
                this->m_snow = memory ? new (memory) Snow(90.0f, 60.0f) : nullptr;
            } else if (this->m_pendingType == 3) {
                void* memory = SMemAlloc(sizeof(Sand), __FILE__, __LINE__, 0x0);
                this->m_sand = memory ? new (memory) Sand(40.0f, 25.0f) : nullptr;
            }

            uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
            this->m_prevIntensity = this->m_curIntensity;
            this->m_startTime = now;
            this->m_pendingType = -1;
        } else {
            float to = 0.25f;

            if (this->m_intensity < 0.25f) {
                to = this->m_intensity;
            }

            this->UpdateIntensity(this->m_prevIntensity, to);
            draining = true;

            if (this->m_snow) {
                this->m_snow->m_stopping = 1;
                this->m_snow->m_mists.m_stopping = 1;
            }

            if (this->m_rain) {
                this->m_rain->m_stopping = 1;
                this->m_rain->m_mists.m_stopping = 1;
            }

            if (this->m_sand) {
                this->m_sand->m_stopping = 1;
                this->m_sand->m_mists.m_stopping = 1;
            }
        }
    }

    if (this->m_rain || this->m_snow || this->m_sand) {
        this->m_ground->Update(WorldCameraPos());
    }

    if (((2.3841858e-07f <= fabsf(this->m_curIntensity - this->m_intensity)
            || 2.3841858e-07f <= fabsf(this->m_fog - this->m_fogTarget)
            || 2.3841858e-07f <= fabsf(this->m_density - this->m_densityTo))
            && !draining)
        || this->m_dirty
    ) {
        this->UpdateIntensity(this->m_prevIntensity, this->m_intensity);
    }

    this->m_enabled = 1;
}

// ref: FUN_0078d540
Weather::~Weather() {
    this->DestroyEffects();

    if (this->m_ground) {
        this->m_ground->~WeatherGroundQuery();
        SMemFree(this->m_ground, __FILE__, __LINE__, 0x0);
    }

    this->m_ground = nullptr;

    for (auto delta = this->m_playerDeltas.Head(); delta; delta = this->m_playerDeltas.Head()) {
        this->m_playerDeltas.DeleteNode(delta);
    }

    for (auto delta = this->m_transportDeltas.Head(); delta; delta = this->m_transportDeltas.Head()) {
        this->m_transportDeltas.DeleteNode(delta);
    }
}

// ---------------------------------------------------------------------------------------------

// FUN_004c8610: the weather's ambience (two copies of one value the sound system reads)
static int32_t s_weatherAmbienceID = -1;
static int32_t s_weatherAmbienceIDCopy = -1;

// ref: FUN_004c8610
void WeatherSetAmbience(int32_t ambienceID) {
    s_weatherAmbienceID = ambienceID;
    s_weatherAmbienceIDCopy = ambienceID;
}

// SMSG_WEATHER, one arm of the reference's world-state message handler (around 0x00526a69): the
// Weather.dbc row gives the effect type, sky density and ambience, the weather is set (gradually
// unless the server says abrupt), the ambience follows, and the change is logged.
int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t weatherID = 0;
    float intensity = 0.0f;
    uint8_t abrupt = 0;

    msg->Get(weatherID);
    msg->Get(intensity);
    msg->Get(abrupt);

    int32_t effectType = 0;
    int32_t ambienceID = 0;
    float density = 1.0f;

    auto record = g_weatherDB.GetRecord(static_cast<int32_t>(weatherID));

    if (record) {
        effectType = record->m_effectType;
        density = record->m_transitionSkybox;
        ambienceID = record->m_ambienceID;
    }

    if (CWorld::s_weather) {
        CWorld::s_weather->SetWeather(effectType, intensity, record, abrupt == 0, density);
    }

    if (ambienceID <= 0) {
        ambienceID = -1;
    }

    WeatherSetAmbience(ambienceID);

    ConsoleWriteA("Weather changed to %d, intensity %f\n", DEFAULT_COLOR, effectType, static_cast<double>(intensity));

    return 1;
}
