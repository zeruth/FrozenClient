#ifndef WORLD_MAP_WATER_RIPPLES_HPP
#define WORLD_MAP_WATER_RIPPLES_HPP

#include "gx/Texture.hpp"
#include <storm/Array.hpp>
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxShader;

// One ripple on a water surface (reference Map.cpp; the allocation tag is the mangled
// ".?AVWater0Ripple@@", 0x50 bytes). It grows from `radius` at `radiusRate` a second, its alpha
// rises to `alphaPeak` over the first 40% of its life and falls back to nothing over the rest, and
// it draws over the water triangles inside the radius it will reach, gathered when it is spawned.
struct Water0Ripple {
    C3Vector position = { 0.0f, 0.0f, 0.0f };   // +0x00
    float angle = 0.0f;                          // +0x0c: turns the texture
    float radius = 0.0f;                         // +0x10
    float radiusRate = 0.0f;                     // +0x14
    float alpha = 0.0f;                          // +0x18
    float alphaPeak = 0.0f;                      // +0x1c
    float alphaRise = 0.0f;                      // +0x20: zero once the peak is passed
    float alphaFall = 0.0f;                      // +0x24
    float endTime = 0.0f;                        // +0x28: world seconds
    int32_t kind = 0;                            // +0x2c: 0 splash, 1 wake (picks the texture)
    TSLink<Water0Ripple> link;                   // +0x30: the live list
    TSLink<Water0Ripple> drawLink;               // +0x38: the frame's list for its kind
    TSGrowableArray<C3Vector> vertices;          // +0x40: water triangles, world space

    Water0Ripple() = default;
    Water0Ripple(const Water0Ripple& other);
    ~Water0Ripple();

    void Init(const C3Vector& position, float angle, float radius, float alphaPeak, float duration, float radiusRate, int32_t kind);
};

namespace WaterRipples {
    // The splash and wake textures (DAT_00cdf7c0), the 0x80-ripple pool (DAT_00cdffe0), the ripples
    // alive (0x00adfb58), the next pool slots (DAT_00cdf7c8 for splashes, 32..127, and DAT_00cdf7cc
    // for the first 32), and the archived WaterRipples shader pair (DAT_00cdffd4 / d8, usable when
    // both loaded: DAT_00cdffdc).
    extern HTEXTURE s_textures[2];
    extern TSGrowableArray<Water0Ripple> s_pool;
    extern STORM_EXPLICIT_LIST(Water0Ripple, link) s_live;
    extern uint32_t s_nextSlot;
    extern uint32_t s_nextReservedSlot;
    extern CGxShader* s_vertexShader;
    extern CGxShader* s_pixelShader;
    extern int32_t s_useShaders;

    void Initialize();
    void Destroy();
    void SetPoolSize(uint32_t count);
    // A ripple from the world's spawn call: on unless waterRipples is off or the water LOD is above
    // 0; `life` under a sixth of a second is stretched six times, longer lives last one second.
    // `reserved` draws from the first 32 slots.
    void Add(const C3Vector& position, float angle, float radius, float alphaPeak, float life, float radiusRate, int32_t kind, int32_t reserved);
    void Spawn(const C3Vector& position, float angle, float radius, float alphaPeak, float duration, float radiusRate, int32_t kind, int32_t reserved);
    // Advance every ripple, retire the finished, and draw the rest by kind.
    void Draw();
}

#endif
