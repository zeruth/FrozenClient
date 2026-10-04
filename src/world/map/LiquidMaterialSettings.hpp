#ifndef WORLD_MAP_LIQUID_MATERIAL_SETTINGS_HPP
#define WORLD_MAP_LIQUID_MATERIAL_SETTINGS_HPP

#include "gx/Texture.hpp"
#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

namespace Liquid {

class CClientEnvironment;
class IGeomFactory;

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
        // Whether every frame of this slot has finished streaming, one a slot. Latched: it goes
        // to 1 the first time all of them resolve and is never cleared, which is what lets
        // GetFrame stop asking after that.
        int32_t m_resident[TEXTURE_SLOTS];                     // +0x364
        // Every frame of each slot's animation. A slot whose name carries no "%d" holds the one
        // still image instead.
        TSGrowableArray<HTEXTURE> m_frames[TEXTURE_SLOTS];     // +0x37c
        // The stand-in frames, shown while m_frames is still streaming. GetFrame picks out of this
        // set instead whenever any real frame still has pending data, and the moment they have all
        // arrived it closes every handle here and empties the array -- so this set exists only for
        // the first seconds of a map and is not a second quality level.
        //
        // It stays empty until the loader that fills it (FUN_004b8d70) is identified, which means
        // a slot that is still streaming currently shows nothing rather than a placeholder.
        TSGrowableArray<HTEXTURE> m_framesAlt[TEXTURE_SLOTS];  // +0x3dc

        // Member functions
        // ref: FUN_008a2100
        CMaterialSettings();

        // Give back the two sets of frame arrays. ref: FUN_008a1c90
        ~CMaterialSettings();

        // Fill the record from one LiquidType row and the material it names. False when the DBC
        // carries neither, which is what sends the bank to its fallback. ref: FUN_008a27c0
        bool LoadFromDbc(int32_t liquidType);

        // Open each slot's frames. A name carrying "%d" is an animation, numbered from one until
        // the files run out; anything else is a single still. ref: FUN_008a2450
        void LoadTextures();

        // Close every frame handle this record holds, in BOTH sets, and empty them.
        // ref: FUN_008a1d00
        void ReleaseFrames();

        // The frame one slot is showing now, over an animation `periodMs` long. Null while the
        // slot has nothing, or while its frames are still streaming and there is no stand-in.
        // ref: FUN_008a1d60
        CGxTex* GetFrame(uint32_t slot, uint32_t periodMs);

        // One of the four ints at +0x308. The draw uses these as the animation period in
        // milliseconds for the slots that do not take the default. ref: FUN_008a16c0
        int32_t GetInt(uint32_t index) const;

        // One of the eighteen stage floats at +0x318, by FLAT index -- which is how the draw asks
        // for them, walking 0..10 across the two stages rather than a stage at a time.
        // ref: FUN_008a16e0
        float GetStageFloat(uint32_t index) const;
};

// What actually draws a surface. There is one implementation a liquid material -- water, magma
// and procedural water -- in two flavours each, one written against the shaders and one against
// the fixed-function pipe, and the bank picks between them once from the device's caps.
//
// Each shader flavour loads its own vertex and pixel pair the first time one is constructed, and
// the permutation counts are the reference's: four vertex programs and one pixel program for
// every one of them except magma, which has one of each.
class IMaterial {
    public:
        virtual ~IMaterial() {}

        // THE DECLARATION ORDER HERE IS THE VTABLE ORDER, and it is the reference's: slot 0 is the
        // destructor, slot 1 EnsureShaders, slot 2 Draw. That was read off the three fixed-function
        // singletons, whose vtables sit at 0x00a594cc, 0x00a594d8 and 0x00a594e4 and each hold
        // [0] one folded destructor shared by all three, [1] the shader loader, [2] the draw. These
        // two used to be declared the other way round, which put Draw in slot 1 -- harmless while
        // nothing indexes the table numerically, and wrong the moment anything does.

        // Load this material's shader pair, once for the whole class. The fixed-function flavours
        // inherit the empty one, which is the whole point of them.
        virtual void EnsureShaders() {}

        // The argument list is read off the call in FUN_008a2240, which hands over seven values
        // from the instance: the environment, the geometry, the unknown at +0x0c, the camera
        // position, the placement matrix, the bounding sphere and the settings -- in that order.
        //
        // Each override carries its own reference body and its own tag, on the definition in the
        // .cpp rather than here: FUN_008a48f0 is CMaterialProcWater::Draw, FUN_008a5590 and
        // FUN_008a5900 share DrawWaterMaterial, and FUN_008a6090 is CMaterialMagma::Draw. The
        // three fixed-function draws are NOT ported and the reason is recorded against
        // 008a5c70, 008a5170 and 008a6350 in overrides.json -- the device implements no
        // fixed-function stage state at all, so a port of them would draw nothing.
        virtual void Draw(CClientEnvironment*, IGeomFactory*, void*, const C3Vector&,
                          const C44Matrix*, const CAaSphere*, CMaterialSettings*) {}
};

// LiquidMaterial 1, the shader path with specular. ref: FUN_008a3f70 / FUN_008a4790
class CMaterialWater : public IMaterial {
    public:
        // Drop this class's shader pair when the last instance goes.
        // ref: FUN_008a3fe0
        ~CMaterialWater() override;
        void EnsureShaders() override;
        void Draw(CClientEnvironment*, IGeomFactory*, void*, const C3Vector&,
                  const C44Matrix*, const CAaSphere*, CMaterialSettings*) override;
};

// LiquidMaterial 1 without specular. ref: FUN_008a4070 / FUN_008a47f0
class CMaterialWaterNoSpec : public IMaterial {
    public:
        // Drop this class's shader pair when the last instance goes.
        // ref: FUN_008a40e0
        ~CMaterialWaterNoSpec() override;
        void EnsureShaders() override;
        void Draw(CClientEnvironment*, IGeomFactory*, void*, const C3Vector&,
                  const C44Matrix*, const CAaSphere*, CMaterialSettings*) override;
};

// LiquidMaterial 2, magma and slime. ref: FUN_008a4190 / FUN_008a4870
class CMaterialMagma : public IMaterial {
    public:
        // Drop this class's shader pair when the last instance goes.
        ~CMaterialMagma() override;
        void EnsureShaders() override;
        void Draw(CClientEnvironment*, IGeomFactory*, void*, const C3Vector&,
                  const C44Matrix*, const CAaSphere*, CMaterialSettings*) override;
};

// LiquidMaterial 3, procedural water. Its shader names carry a suffix the reference is handed
// from outside the module. ref: FUN_008a3e00 / FUN_008a4710
class CMaterialProcWater : public IMaterial {
    public:
        // Drop this class's shader pair when the last instance goes.
        ~CMaterialProcWater() override;
        void EnsureShaders() override;
        void Draw(CClientEnvironment*, IGeomFactory*, void*, const C3Vector&,
                  const C44Matrix*, const CAaSphere*, CMaterialSettings*) override;
};

// The fixed-function flavours. They load no shaders, which is the whole point of them.
//
// NOT TAGGED, because these three are EMPTY and are meant to stay that way: their reference
// bodies (water FUN_008a4850, magma FUN_008a48d0, procedural water FUN_008a4770) draw through
// SetTextureStageState, and frozen's D3D backend does not have a single call to it. Their
// overrides record that as the reason. The line used to read `// ref:` and so claimed all three
// at once on whichever declaration the parser reached first, which is both false and a link the
// addresses' own `unlinked` entries were refusing.
class CMaterialWaterFFP : public IMaterial {};
class CMaterialMagmaFFP : public IMaterial {};
class CMaterialProcWaterFFP : public IMaterial {};

// The settings for one liquid type, built on first use. A type the DBC does not carry logs and
// comes back as water. ref: FUN_008a28f0
CMaterialSettings* GetMaterialSettings(int32_t liquidType);

// The material for one liquid type, built on first use. Keyed by the LiquidMaterial row the
// type names, not by the type, so every water shares one. ref: FUN_008a1fa0
IMaterial* GetMaterial(int32_t liquidType);

// Drop every material the bank holds. ref: FUN_008a1f50
// Configure the module: the liquid texture scale, the procedural shader suffix, and a hold on
// the settings bank. ref: FUN_008a1770
void Initialize(int32_t a1, float textureScale, int32_t a3, const char* procWaterSuffix);

// Give back the hold Initialize took, and tear the module down when the last one goes.
// ref: FUN_008a2400
void Shutdown();

void ReleaseMaterials();

// Whether the bank may hand out the shader materials (the map load sets it from the world's
// vertex and pixel shader enables). ref: FUN_008a1720
void SetShaderMaterials(int32_t enable);

// Whether water takes the specular material. ref: FUN_008a1730
void SetSpecular(int32_t enable);

// Initialize's first argument: the placement is made camera-relative before the transforms are
// built. ref: FUN_008a1740
int32_t GetCameraRelative();

// Clear the once-per-texture upload latches UpdateProceduralTextures sets. ref: FUN_008a2aa0
void ResetProceduralTextureLatches();

// Push each procedural texture's generated pixels to the device, once. Called once a frame from
// CMap::Render, where each of the three latches after its first upload. ref: FUN_008a2f00
void UpdateProceduralTextures();

// Close the procedural texture handles and let them be made again. ref: FUN_008a2980
void ReleaseProceduralTextures();

// Drop every settings record's texture frames and open them again. ref: FUN_008a2780
void ReloadAllLiquidTextures();

// Make the three generated liquid textures and register them under their names. Called once,
// from the liquid initialise. ref: FUN_008a2e20
void ProceduralLiquidTexture();

// Put the procedural textures back in the cache and reopen every liquid's frames, after a device
// reset has emptied the cache. ref: FUN_008a2a10
void RestoreLiquidTextures();

// Drop every record the bank holds. ref: part of FUN_008a2380
void ReleaseMaterialSettings();

// The liquid shaders' transforms for a surface at `placement`: the world-view and the device's
// NATIVE projection into the vertex block (c5..c8 and c0..c3, rows), and their product for the
// pixel program.
void SetupTransforms(const C3Vector& cameraPos, const C44Matrix& placement);

// The vertex program's constant block as the setup functions last left it, for inspection.
const C4Vector* VertexConstants();

}

#endif
