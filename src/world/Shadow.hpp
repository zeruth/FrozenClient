#ifndef WORLD_SHADOW_HPP
#define WORLD_SHADOW_HPP

#include "gx/Texture.hpp"
#include <cstdint>

class C44Matrix;
class CAaBox;
class CImVector;
class CM2Model;
class CVar;

// Shadow.cpp: the blob shadow under a unit or a doodad. The reference's own module, recovered from
// the block at 0x007e2c40..0x007e4b40 -- twenty functions, about 7.8 KB, and the strings
// "Textures\ShadowBlob.blp", "ShadowAdd", "ShadowMod" and "Shadow LOD set to %d" name it.
//
// It is a PROJECTED DECAL: the caster's bounding box becomes a texture projection, and the
// receiver's own triangles are re-drawn with the blob texture on one stage and a fade ramp on the
// next. The module is ported end to end as of 2026-09-26 -- state and gate, the projector
// (FUN_007e4480), the identity-transform wrapper (FUN_007e4370), the receiver walk (FUN_007e3e80)
// with all three stream builders, the collection setup (FUN_007e35f0) and, in CMapObjGroup.cpp, the
// world query that fills the receiver list (MapQueryBox, FUN_007a6af0).
//
// Two things still stand between that and a blob on screen, and neither is in this module. The
// query's MAP-OBJECT half is not ported, so a blob does not reach a building floor through it
// (MapQueryBox carries the note). And nothing here has been SEEN RUNNING: a blob needs
// `shadowLOD` 1 and `extShadowQuality` 0, and this whole path has never been on screen.

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

// The two texture transforms a projected decal draws through: the footprint on stage 0 and the
// fade ramp along the projection axis on stage 1. Shared by several reference decal kinds.
// ref: FUN_007e2d60
void DecalBuildTransforms(C44Matrix& stage0, C44Matrix& stage1, const CAaBox& box, const C44Matrix* extra, float bias, int32_t absolute);

// Draw every hit record a query left behind, as a decal receiver. ref: FUN_007e3e80
void DecalDrawReceivers(const CAaBox& casterBox, const CImVector& color, uint32_t queryMask, uint32_t flags, float strength);

// Collect the receivers under a caster into the hit-record pools. ref: FUN_007e35f0
int32_t DecalCollectReceivers(const CAaBox& casterBox, uint32_t queryMask, uint32_t flags, uint32_t* wantM2, uint32_t* wantHits);

// The wrapper every projected decal shares: transforms, the two texture matrices, the receiver
// walk. ref: FUN_007e4370
void DecalDrawProjected(const CAaBox& bounds, const CImVector& color, const C44Matrix& texMatrix, float bias, uint32_t queryMask, uint32_t flags, float strength);

// Turn a caster's box into a projection volume and its texture matrix, set the decal state, draw.
// ref: FUN_007e4480
void ShadowProjectBlob(const CAaBox& casterBox, CM2Model* model, float strength);

// Draw one caster's blob, if this caster gets one at all. ref: FUN_007e49e0
void ShadowDrawBlob(const CAaBox& casterBox, CM2Model* model);

#endif
