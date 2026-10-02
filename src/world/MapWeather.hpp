#ifndef WORLD_MAP_WEATHER_HPP
#define WORLD_MAP_WEATHER_HPP

#include "gx/Texture.hpp"
#include "net/Types.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CDataStore;
class CGxBuf;
class CGxPool;
class CGxShader;
class CVar;
class WeatherRec;

// The weather: rain, snow and sand falling through a box around the camera, the splashes rain
// leaves where it lands, and the mist sheets that drift through all three. The reference's
// MapWeather module (0x00783b90..0x0078d610, source file .\MapWeather.cpp), ported 2026-10-02.
//
// One Weather object lives on the world (reference 0x00cd7544, CWorld::Initialize creates it at
// 0x00781322); SMSG_WEATHER sets what it should show (SetWeather), CWorld::Update advances it
// (Update), and CGWorldFrame::OnWorldRender draws it twice through CWorld::RenderWeather
// (0x0077f030), once on each side of the liquid pass. Each effect advances its particles inside its
// own Render, so the particles step twice a frame, as they do in the reference.
//
// Each effect draws one of two ways, chosen when it is created: through the archived vertex
// shaders (Shaders\Vertex\rain, patter, snowpoint, sand), which move the particles on the GPU from
// a buffer built once per packet, or on the CPU into a stream buffer every frame.
//
// Offsets in the comments are the reference's (32-bit); the layout here is not byte-faithful.

// ---------------------------------------------------------------------------------------------
// The ground under the weather: 5x5 chunks of 32x32 cached heights around the camera. A cell is
// filled the first time something asks for it and kept until the window moves off its chunk.

struct WeatherGroundTile {
    float heights[1024];        // +0x0000, FLT_MAX until asked for
    int32_t tileX;              // +0x1000
    int32_t tileY;              // +0x1004

    float Height(const C3Vector& position);
};

class WeatherGroundQuery {
    public:
        // vtable 0x00a3e978 (pure)
        virtual void GetBounds(int32_t* rect) = 0;
        virtual void Update(const C3Vector& center) = 0;
        virtual bool Intersect(const C3Vector& start, const C3Vector& end, C3Vector& hit) = 0;
        virtual float HeightAt(const C3Vector& position) = 0;
        virtual void GetCellSize(float* size, float* inverse) = 0;
        virtual ~WeatherGroundQuery();
};

class WeatherGroundCache : public WeatherGroundQuery {
    public:
        // vtable 0x00a3e9f0
        WeatherGroundCache();
        void GetBounds(int32_t* rect) override;
        void Update(const C3Vector& center) override;
        bool Intersect(const C3Vector& start, const C3Vector& end, C3Vector& hit) override;
        float HeightAt(const C3Vector& position) override;
        void GetCellSize(float* size, float* inverse) override;
        ~WeatherGroundCache() override;

        float CellHeight(const int32_t* cell, float z);

        int32_t m_04 = 0;               // +0x04
        int32_t m_08 = 0;               // +0x08
        int32_t m_0c = 0;               // +0x0c
        int32_t m_centerTileX = 0;      // +0x10
        int32_t m_centerTileY = 0;      // +0x14
        int32_t m_originTileX = 0;      // +0x18
        int32_t m_originTileY = 0;      // +0x1c
        int32_t m_originCellX = 0;      // +0x20
        int32_t m_originCellY = 0;      // +0x24
        WeatherGroundTile* m_tiles[25]; // +0x28, row-major (y * 5 + x) from the origin
};

// ---------------------------------------------------------------------------------------------
// A packet is a batch of up to 0x1800 particles, all spawned within one window and retired
// together; with shaders it is uploaded once into a vertex buffer of its own and the vertex
// program moves each particle along its line by the packet's age. The reference names the
// template Packet<T, 6144, V>, V the vertices each particle takes.

template <class T, uint32_t N, uint32_t V>
struct Packet {
    TSLink<Packet> m_link;          // +0x00
    uint32_t m_capacity;            // +0x08
    T m_items[N];                   // +0x0c
    uint32_t m_count;               // used
    uint32_t m_baseTime;            // world time (1/1024 s) the packet's clock starts
    uint32_t m_lastTime;            // world time its last particle is done
    float m_buildTime;              // seconds of spawning folded in so far
    C3Vector m_anchor;              // the camera when the packet began (snow)
    CGxPool* m_pool;
    CGxBuf* m_buf;

    Packet();
    ~Packet();
};

// A free list and an active list of packets, with the reference's two counters.
template <class P>
class PacketPool {
    public:
        STORM_EXPLICIT_LIST(P, m_link) m_free;      // +0x00
        uint32_t m_used = 0;                        // +0x0c
        uint32_t m_freeCount = 0;                   // +0x10
        STORM_EXPLICIT_LIST(P, m_link) m_active;    // +0x14

        P* New();
        ~PacketPool();
};

// ---------------------------------------------------------------------------------------------
// Mist sheets: big soft quads that drift past the camera and follow the ground, 0x80 of them.

class Mists {
    public:
        struct Mist {
            uint32_t heightCount = 0x40;    // +0x000
            float heights[0x40];            // +0x004, ground heights along the path
            int32_t steps;                  // +0x104
            C3Vector position = { 0.0f, 0.0f, 0.0f };   // +0x108
            C3Vector velocity = { 0.0f, 0.0f, 0.0f };   // +0x114
            uint32_t startTime = 0;         // +0x120 (0 = idle)
            uint32_t endTime;               // +0x124
            float rise;                     // +0x128
        };

        // vtable 0x00a3ea40
        Mists(float size, const C3Vector& extents, const char* texture, float angle, float angleSpread,
              float speed, float speedSpread, uint32_t count);
        virtual void Spawn(Mist& mist, uint32_t time);
        ~Mists();

        void Init(Mist& mist, uint32_t time);
        void Render();

        C3Vector m_extents;             // +0x04
        C3Vector m_10;                  // +0x10
        float m_size;                   // +0x1c
        float m_spawnRate;              // +0x20
        float m_angle;                  // +0x24
        float m_angleSpread;            // +0x28
        float m_spawnAccum;             // +0x2c
        float m_fadeTime;               // +0x30
        float m_speed;                  // +0x34
        float m_speedSpread;            // +0x38
        uint8_t m_stopping;             // +0x3c
        HTEXTURE m_texture;             // +0x40
        TSGrowableArray<Mist> m_mists;  // +0x44
};

// ---------------------------------------------------------------------------------------------
// The three effects.

class Rain {
    public:
        struct Drop {
            C3Vector position;      // where it is at startTime
            C3Vector velocity;
            float unused18;
            float startTime;        // seconds since the packet's base time
            float endTime;          // when it hits the ground, or 0 if it never does
        };

        struct Patter {
            C3Vector position;      // where the drop landed
            float unusedC;
            float startTime;
            float endTime;
        };

        using DropPacket = Packet<Drop, 0x1800, 3>;
        using PatterPacket = Packet<Patter, 0x1800, 3>;

        Rain(float width, float height);
        ~Rain();

        void SetIntensity(float intensity);
        void Update();
        void Render();
        int32_t CountActive();

        void SpawnDrop(Drop& drop, float time);
        void FillPackets(DropPacket* drops, PatterPacket* patter, float count);
        void UploadDrops(DropPacket* packet);
        void UploadPatter(PatterPacket* packet);

        void RenderDrops();
        void RenderPatter();
        void RenderDropsShader();
        void RenderPatterShader();

        uint8_t m_useShaders;               // +0x00
        uint8_t m_stopping;                 // +0x01
        CAaBox m_bounds;                    // +0x04
        Mists m_mists;                      // +0x1c
        PacketPool<DropPacket> m_drops;     // +0x70
        PacketPool<PatterPacket> m_patter;  // +0x90
        DropPacket* m_dropPacket;           // +0xb0
        float m_maxBuild;                   // +0xb4
        PatterPacket* m_patterPacket;       // +0xb8
        HTEXTURE m_texture;                 // +0xbc
        CGxShader* m_shader;                // +0xc0
        HTEXTURE m_splashTexture;           // +0xc4
        CGxShader* m_patterShader;          // +0xc8
        float m_rate;                       // +0xcc
        float m_intensity;                  // +0xd0
};

class Snow {
    public:
        struct Flake {
            C3Vector position;
            C3Vector velocity;
            float startTime;
            float endTime;
        };

        using FlakePacket = Packet<Flake, 0x1800, 1>;

        Snow(float width, float height);
        ~Snow();

        void SetIntensity(float intensity);
        void Update();
        void Render();
        int32_t CountActive();

        void SpawnFlake(Flake& flake, float time, const C3Vector& anchor, const C3Vector& velocity);
        void FillPacket(FlakePacket* packet, float count, const C3Vector& anchor, const C3Vector& velocity);

        void RenderCpu();
        void RenderShader();

        uint8_t m_useShaders;               // +0x00
        uint8_t m_stopping;                 // +0x01
        CAaBox m_bounds;                    // +0x04
        Mists m_mists;                      // +0x1c
        PacketPool<FlakePacket> m_flakes;   // +0x70
        FlakePacket* m_packet;              // +0x90
        float m_maxBuild;                   // +0x94
        HTEXTURE m_texture;                 // +0x98
        CGxShader* m_shader;                // +0x9c
        float m_rate;                       // +0xa0
        float m_intensity;                  // +0xa4
};

class Sand {
    public:
        struct Grain {
            C3Vector position;
            C3Vector velocity;
            float startTime;
            float endTime;
        };

        using GrainPacket = Packet<Grain, 0x1800, 1>;

        Sand(float width, float height);
        ~Sand();

        void SetIntensity(float intensity);
        void Update();
        void Render();
        int32_t CountActive();
        void GetBounds(CAaBox& bounds) const;

        void SpawnGrain(Grain& grain, float time);
        void FillPacket(GrainPacket* packet, float count);

        void RenderCpu();
        void RenderShader();

        uint8_t m_useShaders;               // +0x00
        uint8_t m_stopping;                 // +0x01
        CAaBox m_bounds;                    // +0x04
        Mists m_mists;                      // +0x1c
        PacketPool<GrainPacket> m_grains;   // +0x70
        GrainPacket* m_packet;              // +0x90
        float m_maxBuild;                   // +0x94
        CGxShader* m_shader;                // +0x98
        float m_rate;                       // +0x9c
};

// ---------------------------------------------------------------------------------------------

struct PosDelta {
    TSLink<PosDelta> m_link;        // +0x00
    C3Vector delta;                 // +0x08
    int32_t time;                   // +0x14, ms
};

class Weather {
    public:
        Weather();
        ~Weather();

        void SetWeather(uint32_t type, float intensity, const WeatherRec* record, bool gradual, float density);
        void Clear();
        void Update();
        void Render();

        int32_t GetType() const;
        int32_t GetEffectBounds(CAaBox& bounds);
        void GetPlayerPosition(C3Vector& position) const;

        void SetDefaultTexture(uint32_t type);
        void UpdateIntensity(float from, float to);
        void UpdateMotion();
        void DestroyEffects();

        float m_intensity;                  // +0x00, the target, 0..1
        float m_prevIntensity;              // +0x04, where the ramp starts
        float m_curIntensity;               // +0x08
        float m_fogTarget;                  // +0x0c, intensity clamped to 0..0.25
        float m_fogFrom;                    // +0x10
        float m_fog;                        // +0x14
        uint32_t m_startTime;               // +0x18, ms
        uint32_t m_fogStartTime;            // +0x1c, ms
        int32_t m_pendingType;              // +0x20
        uint8_t m_dirty;                    // +0x24
        uint8_t m_gradual;                  // +0x25
        float m_density;                    // +0x28
        float m_densityFrom;                // +0x2c
        float m_densityTo;                  // +0x30
        CImVector m_color;                  // +0x34
        char m_texture[260];                // +0x38
        Rain* m_rain;                       // +0x13c
        Snow* m_snow;                       // +0x140
        Sand* m_sand;                       // +0x144
        WeatherGroundQuery* m_ground;       // +0x148
        STORM_EXPLICIT_LIST(PosDelta, m_link) m_playerDeltas;     // +0x14c
        STORM_EXPLICIT_LIST(PosDelta, m_link) m_transportDeltas;  // +0x158
        C3Vector m_playerVelocity;          // +0x164
        C3Vector m_transportVelocity;       // +0x170
        C3Vector m_playerPosition;          // +0x17c
        uint8_t m_playerFlag;               // +0x188, on a taxi
        float m_playerFacing;               // +0x18c
        float m_playerSpeed;                // +0x190
        uint8_t m_enabled;                  // +0x194

        static bool s_forced;               // 0x00cd8528, the force-weather-type-on registry key
        static CVar* s_useShadersCvar;      // 0x00cd852c
        static float s_density;             // 0x00adf1f8, weatherDensity
};

// FUN_004c8610: the ambience the weather asks the sound system for (-1 for none)
void WeatherSetAmbience(int32_t ambienceID);

// SMSG_WEATHER: Weather.dbc id, intensity 0..1, abrupt flag.
int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
