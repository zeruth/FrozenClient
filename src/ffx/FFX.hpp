#ifndef FFX_FFX_HPP
#define FFX_FFX_HPP

#include "gx/Texture.hpp"
#include <storm/Array.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class C44Matrix;
class CGxBatch;
class CGxShader;
class CGxBuf;
class CVar;

// The full-screen effects (the reference's FFX namespace, whose one recovered RTTI name is
// .?AVPass@FFX@@). An effect is a list of passes; each pass draws a screen-aligned quad from one
// or more input targets into an output target through its own pixel shader. The world is drawn
// into the back buffer as usual, copied into the scene target, and the active effect runs its
// passes from there back onto the screen.
// The switch callback every full-screen effect's console variable shares: it reports the new
// state and accepts it. ref: FUN_008c02a0
bool FFXDeathCallback(CVar* var, const char* oldValue, const char* value, void* arg);

namespace FFX {

// One render target (0x1c bytes in the reference). The texture is created at a size the device
// can take -- a power of two unless rectangle or non-power-of-two targets are allowed -- and the
// part of it in use is m_width by m_height.
struct Target {
    HTEXTURE m_texture = nullptr;   // +0x00; null for the screen itself
    int32_t m_texWidth = 0;         // +0x04
    int32_t m_texHeight = 0;        // +0x08
    int32_t m_width = 0;            // +0x0c
    int32_t m_height = 0;           // +0x10
    float m_invTexWidth = 0.0f;     // +0x14
    float m_invTexHeight = 0.0f;    // +0x18
};

// The shared targets, in the reference's order (DAT_00d45784 .. DAT_00d4582c).
enum {
    Target_Screen = 0,          // DAT_00d45784: the back buffer
    Target_Scene,               // DAT_00d457a0: the world, copied out of the back buffer
    Target_Full,                // DAT_00d457bc
    Target_Half0,               // DAT_00d457d8
    Target_Half1,               // DAT_00d457f4
    Target_Quarter0,            // DAT_00d45810
    Target_Quarter1,            // DAT_00d4582c
    Targets_Last
};

extern Target s_targets[Targets_Last];

// FUN_008c10b0 == 2: targets are rectangle textures, addressed in texels rather than 0..1.
extern bool s_useRectangle;                 // DAT_00d45768
extern CVar* s_ffxCvar;                     // DAT_00d45774: "ffx", all full-screen effects
extern CVar* s_ffxRectangleCvar;            // DAT_00d45770: "ffxRectangle"
extern CVar* s_gxMultisampleCvar;           // DAT_00d4576c
extern uint16_t s_quadIndices[4];           // DAT_00d45778: { 0, 3, 1, 2 }
// The corners every pass builds its quad in, or the 36 points of a six-by-six grid
// (DAT_00d45848, DAT_00d459f8: 0x1b0 and 0x120 bytes).
extern C3Vector s_quadPositions[36];
extern C2Vector s_quadTexCoords[36];

class Effect;

// A pass: what it reads, where it draws, how it blends, and whether its quad is flipped.
class Pass {
    public:
        // Member variables
        Target* m_inputs[8] = {};       // +0x04
        Target* m_target = nullptr;     // +0x24
        uint32_t m_blend = 0;           // +0x28, EGxBlend
        bool m_flip = false;            // +0x2c

        // Virtual member functions
        virtual ~Pass();
        virtual bool IsValid() = 0;
        virtual void Render() = 0;

        // Member functions
        Pass(Target* const* inputs, uint32_t inputCount, Target* target, uint32_t blend, bool flip);
};

// An effect: its switch, and the passes it runs in order.
class Effect {
    public:
        // Member variables
        CVar* m_cvar = nullptr;                 // +0x04
        TSGrowableArray<Pass*> m_passes;        // +0x08
        bool m_defaultOn = true;                // +0x18: the switch's default, "1" or "0"

        // Virtual member functions
        virtual ~Effect();
        virtual bool IsEnabled();
        virtual void Render();
        virtual void Deactivate();
        virtual void SetParams(uint32_t count, const uint32_t* params);

        // Member functions
        Effect();
};

extern Effect* s_activeEffect;              // DAT_00d45780

// Bring the system up: the switches, the targets cleared, the quad indices, and whether
// rectangle targets are in use. ref: FUN_008c12f0
void Init();
// Release every target's texture and clear them. ref: FUN_008c0360
void ReleaseTargets();
// Which texture target the effect targets use: 0 (2D), 2 (rectangle) or 3 (non-power-of-two).
// ref: FUN_008c10b0
EGxTexTarget TargetKind();
// (Re)create a target's texture for a size, keeping it if the texture size is unchanged.
// ref: FUN_008c1100
void CreateTarget(Target* target, const int32_t* size, bool bit8, bool renderTarget);
// Size every target to the window: full, half and quarter. ref: FUN_008c15f0
void UpdateTargets();
// Make an effect the active one; the one it replaces is deactivated. ref: FUN_008c02e0
void SetEffect(Effect* effect);
// Before the world draws: keep the targets the window's size, and keep the world within the part
// of the back buffer the scene target can take. ref: FUN_008c1770
void BeginScene();
// After the world draws: copy it out of the back buffer and run the active effect over it.
// ref: FUN_008c1010
void EndScene();
// Set up one pass: an orthographic projection over the target in pixels, no view or world
// transform, no culling, lighting, depth or fog, and the target bound. ref: FUN_008c1890
void BeginPass(Target* target);
// Put back what BeginPass changed. ref: FUN_008c1520
void EndPass();
// The view BeginPass last set aside. ref: FUN_008c0290
const C44Matrix& SavedView();
// The quad covering a target and its texture coordinates into a source, with the half-texel
// shift each API needs. ref: FUN_008c0590
void QuadCoords(const int32_t* targetSize, const int32_t* sourceSize, const int32_t* sourceTexSize,
                C3Vector* positions, C2Vector* texCoords, bool flip);
// Stream the quad with four sets of texture coordinates, each the quad's own shifted by one of
// four texel offsets, and bind it. ref: FUN_008c0c90
void StreamQuad(const C3Vector* positions, uint32_t color, const C2Vector* texCoords,
                const float* offsets, const float* invTexSize);
// Bind the quad's four indices. ref: FUN_008c0ec0
void QuadIndex();
// The quad's batch: a four-index strip (DAT_00b24a80).
CGxBatch* QuadBatch();
// An n by n grid over a target in clip space, with its texture coordinates into a source (texels
// for rectangle targets). ref: FUN_008c0740
void GridCoords(const int32_t* targetSize, const int32_t* sourceSize, const int32_t* sourceTexSize,
                C3Vector* positions, C2Vector* texCoords, int32_t n, bool flip);
// Stream points with one texture coordinate and a grey taken from a value in -1..1, and bind
// them. ref: FUN_008c0de0
void StreamColored(uint32_t count, const C3Vector* positions, const C2Vector* texCoords, const float* values);
// Bind the triangles of a columns-by-rows grid of points. ref: FUN_008c0f00
void GridIndex(int32_t columns, int32_t rows);
// The six-by-six grid's batch: 150 indices over 36 points (DAT_00b24a90).
CGxBatch* GridBatch();

}

#endif
