#ifndef WORLD_LIGHTNING_HPP
#define WORLD_LIGHTNING_HPP

#include "gx/Texture.hpp"
#include <storm/Array.hpp>
#include <tempest/Box.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class SpellChainEffectsRec;

// The reference's Common\Lightning.cpp: chain-lightning bolts, each a ribbon of quads strung along a
// jittered line from a start point to an end point and drawn camera-facing. A bolt's look and motion
// all come from its SpellChainEffects.dbc row.
//
// The system (CLightningSystem, 0x20 bytes at 0x00d39750) owns the bolts by index; SpellVisuals'
// LightningObject holds the indices of the bolts it made.

// One point of a bolt's spine, and how it is wandering (RTTI LightningJoint, 0x28 bytes).
struct LightningJoint {
    C3Vector m_offset;          // +0x00, where the joint is pushed off the straight line
    // +0x0c: the joint's smoothed position, -1 in x until it has one (the reference tests the
    // float's bits against -NaN, which is what 0xffffffff reads as).
    C3Vector m_smoothed;
    C3Vector m_velocity;        // +0x18
    float m_moveTimer;          // +0x24, seconds until the joint picks a new direction
};

// One bolt (RTTI CLightning, 0xb4 bytes).
class CLightning {
    public:
        // The callback a bolt's owner may install to move its two ends each frame.
        typedef void (*UPDATEFN)(void* param, float elapsed, C3Vector* start, C3Vector* end);

        C3Vector m_start;                                   // +0x00
        C3Vector m_end;                                     // +0x0c
        CImVector m_color;                                  // +0x18
        const SpellChainEffectsRec* m_rec = nullptr;        // +0x1c
        TSGrowableArray<LightningJoint> m_joints;           // +0x20
        TSGrowableArray<C3Vector> m_points;                 // +0x2c
        TSGrowableArray<C3Vector> m_vertices;               // +0x38
        TSGrowableArray<C2Vector> m_texCoords;              // +0x44
        TSGrowableArray<CImVector> m_colors;                // +0x50
        TSGrowableArray<uint16_t> m_indices;                // +0x5c
        CAaBox m_bounds;                                    // +0x68
        float m_texOffset = 0.0f;                           // +0x80
        HTEXTURE m_texture = nullptr;                       // +0x84
        UPDATEFN m_update = nullptr;                        // +0x88
        void* m_updateParam = nullptr;                      // +0x8c
        // +0x90: 0x1 joints built, 0x2 visible, 0x4 flickered off, 0x8 pulse running backwards,
        // 0x10 nothing lit before the pulse, 0x20 the pulse reached the end.
        uint32_t m_flags = 0;
        float m_waveAngle = 0.0f;                           // +0x94
        float m_waveSpin = 0.0f;                            // +0x98
        float m_wavePhase = 0.0f;                           // +0x9c
        float m_arcAngle = 0.0f;                            // +0xa0
        float m_arcSpin = 0.0f;                             // +0xa4
        float m_flickerTimer = 0.0f;                        // +0xa8
        float m_pulse = 0.0f;                               // +0xac
        float m_randomTexOffset = 0.0f;                     // +0xb0

        // ref: FUN_009a97b0
        CLightning();
        // ref: FUN_009a9860
        ~CLightning();

        // ref: FUN_009a8cb0
        void SetTexture(HTEXTURE texture);
        // ref: FUN_009a8ec0
        void Restart();
        // ref: FUN_009a90c0
        void NewJoint(LightningJoint& joint, float radius);
        // ref: FUN_009a9350
        void PlaceJoint(C3Vector& out, const C3Vector& base, float t, LightningJoint& joint, float scale);
        // ref: FUN_009a9490
        void BuildPulse(int32_t* first, int32_t* count);
        // ref: FUN_009a9900
        void SetRecord(const SpellChainEffectsRec* rec);
        // ref: FUN_009a9980
        void ComputeArcAndWave();
        // ref: FUN_009a9b30
        void ApplyWave(C3Vector& point, float t, float length);
        // ref: FUN_009a9ca0
        void BuildSpine();
        // ref: FUN_009a9dc0
        void BuildJointedSpine();
        // ref: FUN_009aa210
        void Render(float elapsed, const C3Vector& cameraPos);
        // ref: FUN_009aaf10
        void SetJointCount(uint32_t count);
        // ref: FUN_009ab2e0
        void BuildJoints(float length, float radius);
        // ref: FUN_009ab3b0
        void Update(float elapsed);
};

class CLightningSystem {
    public:
        // ref: FUN_009aae80
        CLightningSystem();
        // ref: FUN_009aaea0
        ~CLightningSystem();

        // ref: FUN_009aafb0
        // A bolt for `rec`, reusing a freed slot when there is one; the index names it.
        int32_t Create(CLightning::UPDATEFN update, void* param, const SpellChainEffectsRec* rec);
        // ref: FUN_009ab2b0
        void Destroy(int32_t index);
        // ref: FUN_009a96e0
        CLightning* Get(int32_t index);
        // ref: FUN_009a9710
        void SetEnds(int32_t index, const C3Vector* start, const C3Vector* end);
        // ref: FUN_009a9770
        void SetVisible(int32_t index, int32_t visible);
        // ref: FUN_009ab730
        void Update(float elapsed);
        // ref: FUN_009ab070
        // Every live bolt, in four passes by render layer, in the camera's space.
        void Draw(const C3Vector& cameraPos);

    private:
        // The bolts. The low bit of a slot marks it free, as the reference stores it.
        TSGrowableArray<uintptr_t> m_slots;     // +0x00
        TSGrowableArray<int32_t> m_free;        // +0x10
};

// The system (0x00d39750), made by SpellVisualsInitialize.
extern CLightningSystem* g_lightningSystem;

#endif
