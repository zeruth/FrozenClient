#ifndef WORLD_MAP_LIQUID_MATERIAL_SETTINGS_HPP
#define WORLD_MAP_LIQUID_MATERIAL_SETTINGS_HPP

#include "gx/Texture.hpp"
#include <storm/Array.hpp>
#include <cstdint>

namespace Liquid {

// Everything one kind of liquid needs to draw, flattened out of LiquidType.dbc and the
// LiquidMaterial row it names. One of these is built the first time a liquid type is asked for
// and kept for the life of the map; a type the DBC does not carry falls back to water.
//
// The six texture slots are the animation frames the surface cycles through. The two blocks of
// nine floats are LiquidType's eighteen, split down the middle -- one block per texture stage.
class CMaterialSettings {
    public:
        // Static constants
        static const uint32_t TEXTURE_SLOTS = 6;
        static const uint32_t TEXTURE_NAME_SIZE = 0x80;
        static const uint32_t STAGE_COUNT = 2;
        static const uint32_t STAGE_FLOATS = 9;

        // Member variables
        // The animation frames, as name patterns; the loader expands the %d in each.
        char m_textureName[TEXTURE_SLOTS][TEXTURE_NAME_SIZE];  // +0x000
        uint32_t m_color[2];                                   // +0x300
        int32_t m_int[4];                                      // +0x308
        float m_stage[STAGE_COUNT][STAGE_FLOATS];              // +0x318
        // Whether the water is drawn by the procedural shaders rather than the plain ones;
        // LiquidMaterial's own flag bit 0.
        uint8_t m_procedural;                                  // +0x360
        // The frame the surface is showing right now, one a slot. Nothing here fills it; the
        // draw picks it out of m_frames as the clock moves.
        HTEXTURE m_current[TEXTURE_SLOTS];                     // +0x364
        // Every frame of each slot's animation. A slot whose name carries no "%d" holds the one
        // still image instead.
        TSGrowableArray<HTEXTURE> m_frames[TEXTURE_SLOTS];     // +0x37c
        // A second set the reference loads beside the first and keeps only when it came out the
        // same length. What distinguishes the two is the loader it uses, which frozen has not
        // identified, so this stays empty.
        TSGrowableArray<HTEXTURE> m_framesAlt[TEXTURE_SLOTS];  // +0x3dc

        // Member functions
        // Fill the record from one LiquidType row and the material it names. False when the DBC
        // carries neither, which is what sends the bank to its fallback. ref: FUN_008a27c0
        bool LoadFromDbc(int32_t liquidType);

        // Open each slot's frames. A name carrying "%d" is an animation, numbered from one until
        // the files run out; anything else is a single still. ref: FUN_008a2450
        void LoadTextures();
};

// The settings for one liquid type, built on first use. A type the DBC does not carry logs and
// comes back as water. ref: FUN_008a28f0
CMaterialSettings* GetMaterialSettings(int32_t liquidType);

// Drop every record the bank holds. ref: part of FUN_008a2380
void ReleaseMaterialSettings();

}

#endif
