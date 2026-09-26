#ifndef WORLD_SHADOW_HPP
#define WORLD_SHADOW_HPP

#include "gx/Texture.hpp"
#include <cstdint>

class CAaBox;
class CM2Model;
class CVar;

// Shadow.cpp: the blob shadow under a unit or a doodad. The reference's own module, recovered from
// the block at 0x007e2c40..0x007e4b40 -- twenty functions, about 7.8 KB, and the strings
// "Textures\ShadowBlob.blp", "ShadowAdd", "ShadowMod" and "Shadow LOD set to %d" name it.
//
// It is a PROJECTED DECAL: the caster's bounding box becomes a texture projection, and the
// receiver's own triangles are re-drawn with the blob texture on one stage and a fade ramp on the
// next. What is ported here is the module's state and its gate; the projector (FUN_007e4480), the
// identity-transform wrapper (FUN_007e4370) and the receiver walk (FUN_007e3e80 / FUN_007e3aa0 over
// FUN_007e2fd0, FUN_007e32f0, FUN_007e3580, FUN_007e35f0) are not, and ShadowDrawBlob says so.

// The blob texture, and the two generated fade ramps.
HTEXTURE ShadowBlobTexture();
// ref: FUN_007e2c40
CGxTex* ShadowModGxTex();
// ref: FUN_007e2c60
CGxTex* ShadowAddGxTex();

// The shadow LOD in effect (DAT_00af3e08). 1 draws blobs, 0 draws none.
extern int32_t g_shadowLOD;

// Create the blob texture, generate the two ramps and register the shadowLOD CVar.
// ref: FUN_007e4a40
void ShadowInit();

// Release all three textures. ref: FUN_007e2c80
void ShadowDestroy();

// Take a new LOD, regenerating the ramps when it turns blobs on. ref: FUN_007e3980
void ShadowSetLOD(int32_t lod);

// Draw one caster's blob, if this caster gets one at all. ref: FUN_007e49e0
void ShadowDrawBlob(CM2Model* model);

#endif
