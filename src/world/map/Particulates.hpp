#ifndef WORLD_MAP_PARTICULATES_HPP
#define WORLD_MAP_PARTICULATES_HPP

#include "gx/Texture.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

// The motes that hang in the water around a submerged camera (reference: a 0xfa40-byte object
// at DAT_00cd7548, Map.cpp; the console command is waterParticulates). Four thousand points in a
// cube centred on the camera, each with its own size, carried by the liquid's current and wrapped
// round the cube's faces, then drawn as camera-facing quads cut from a 5x5 atlas.
class Particulates {
    public:
        // Types
        struct Mote {
            C3Vector position;      // relative to the camera
            float size;
        };

        // Member variables
        Mote m_motes[4000];         // +0x0000
        uint32_t m_count;           // +0xfa00
        C3Vector m_cameraPos;       // +0xfa04: where the camera was last update
        HTEXTURE m_texture;         // +0xfa10
        uint8_t m_active;           // +0xfa14: the liquid the camera is in takes motes
        float m_sizeScale;          // +0xfa18
        float m_boxSize;            // +0xfa1c: the cube's edge
        uint32_t m_liquidType;      // +0xfa24
        C3Vector m_currentDir;      // +0xfa28
        float m_currentRate;        // +0xfa34
        float m_currentPhase;       // +0xfa38
        float m_currentStrength;    // +0xfa3c

        // Member functions
        Particulates(float sizeScale, float boxSize, const char* texture);
        ~Particulates();
        void SetTexture(const char* texture);
        void SetSizeScale(float scale);
        // Scatter every mote through the cube again, for this liquid.
        void Respawn(uint32_t liquidType);
        // A new random current.
        void NewCurrent();
        // How far the water moves the motes this step.
        C3Vector Drift(float step);
        void Update();
        void Render();
};

#endif
