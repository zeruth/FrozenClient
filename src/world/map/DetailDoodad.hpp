#ifndef WORLD_MAP_DETAIL_DOODAD_HPP
#define WORLD_MAP_DETAIL_DOODAD_HPP

#include "db/rec/GroundEffectDoodadRec.hpp"
#include <storm/Array.hpp>
#include "gx/Texture.hpp"
#include <tempest/Vector.hpp>
#include <tempest/Plane.hpp>
#include <cstdint>

class CGxBuf;
class CMapChunk;

class CM2Model;
class CGxPool;

// The grass and small scatter the terrain draws itself, rather than as placed models. A chunk's
// worth is generated from its ground-effect table and written into one of a fixed ring of GPU
// buffers, so the whole system costs two pools and nothing per chunk.
namespace DetailDoodad {

// How many buffers the ring holds -- vertex and index in pairs, so half that many chunks can be
// in flight at once.
static const uint32_t BUFFER_COUNT = 0x100;

// A vertex is position, normal, colour and one coordinate pair.
static const uint32_t VERTEX_STRIDE = 0x24;

// The most a chunk may scatter however high the density is set.
static const uint32_t MAX_PER_CHUNK = 0x1000;

// The tables the scatter builder works from, read out of the reference's data section rather
// than inferred. A chunk's eight-by-eight grid of cells is walked by these:
//
//   CELL_CORNER   the four corners of one cell, in the chunk's own space (DAT_00adfc64)
//   TRI_CORNER    which two corners each of a cell's four triangles takes (DAT_00a3fb50)
//   TRI_VERTEX    the matching pair of indices into the chunk's 145 heights, where 17 is one
//                 row of the nine-and-eight grid (DAT_00a3fb30)
//   TEXMAP_MASK   two bits a column of the low quality texture map, which says which of the
//                 four layers a cell takes, with TEXMAP_SHIFT to bring them down
//                 (DAT_00a3fb88, DAT_00a3fb98)
//   PREDTEX_MASK  one bit a column of the chunk's predTex byte, with PREDTEX_SHIFT likewise
//                 (DAT_00a3fbb8, DAT_00a3fbc8)
//
// CELL_STEP is negative because the map's axes count down as the indices count up.
static const float CELL_STEP = -4.166666507720947f;          // DAT_00a3fdd4

static const float CELL_CORNER[4][3] = {
    {  0.0f,       0.0f,      0.0f },
    {  0.0f,       CELL_STEP, 0.0f },
    {  CELL_STEP,  CELL_STEP, 0.0f },
    {  CELL_STEP,  0.0f,      0.0f }
};

static const int32_t TRI_CORNER[4][2] = { { 3, 0 }, { 0, 1 }, { 2, 3 }, { 1, 2 } };
static const int32_t TRI_VERTEX[4][2] = { { 17, 0 }, { 0, 1 }, { 18, 17 }, { 1, 18 } };

static const uint16_t TEXMAP_MASK[8] = { 0x3, 0xc, 0x30, 0xc0, 0x300, 0xc00, 0x3000, 0xc000 };
static const int32_t TEXMAP_SHIFT[8] = { 0, 2, 4, 6, 8, 10, 12, 14 };

static const uint16_t PREDTEX_MASK[8] = { 1, 2, 4, 8, 16, 32, 64, 128 };
static const int32_t PREDTEX_SHIFT[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

// The rest of the builder's numbers, also read rather than inferred.
static const float CELL_HALF = 2.0833332538604736f;   // DAT_00a40474: jitter half-extent in a cell
static const float TAU_HALF = 3.1415927410125732f;    // DAT_00a40478: rotation is (r + 1) * this
static const float SCALE_JITTER = 0.33000001f;        // DAT_00a14acc: scale is r * this + 1
static const float SHADOW_SCALE = 1.9199999570846558f;// DAT_00a4047c: yards to MCSH cells, 64/33.33
static const float MIN_NORMAL_Z = 0.4000000059604645f;// DAT_009f98d8: steeper than this, no grass
static const float COLOR_MAX = 255.0f;                // DAT_009e30c0
static const float ROUND_BIAS = 0.5f;                 // DAT_00af0abc
static const float CELL_SIZE_POS = 4.166666507720947f;
static const float CELL_MID = -2.0833332538604736f;   // DAT_00adfc94
static const float COLOR_DOUBLE = 2.0f;               // DAT_00a4040c

// One bit per 2x2 cell of the chunk's 4x4 hole grid. DAT_00a3faf0
static const uint16_t HOLE_MASK[16] = {
    0x0001, 0x0002, 0x0004, 0x0008,
    0x0010, 0x0020, 0x0040, 0x0080,
    0x0100, 0x0200, 0x0400, 0x0800,
    0x1000, 0x2000, 0x4000, 0x8000
};

// How the scatter works, decoded from FUN_007d3390 end to end. Everything below is established
// -- no table or constant in it is a guess -- so writing it is transcription.
//
// The chunk is seeded from its own indices, CRndSeed(indexX << 16 | indexY), so it scatters the
// same way every time it loads. Then, `groundEffectDensity` times over:
//
//  1. Pick a cell of the eight-by-eight grid at random, x and y each from three random bits.
//     A bitmask a row keeps a cell from being prepared twice, though the pick list still holds
//     the repeat and the scatter below runs for it again.
//  2. Preparing a cell means building its four triangles: each takes two of CELL_CORNER by
//     TRI_CORNER and the matching two heights by TRI_VERTEX, and PlaneFromPoints gives the
//     plane. Where the chunk has MCCV, the cell also keeps the corner colour and the two colour
//     deltas along the triangle's edges, all scaled by 2.
//
//     The plane must be NORMALIZED AND TURNED UPWARD. Frozen does have PlaneFromPoints (it is
//     in CWorldScene.cpp), but it takes three points and this builds the plane from a centre
//     and two corners it already has, so the normalize is written out here. The cross product through the cell's centre comes out pointing under the
//     ground and scaled by twice the triangle's area; CMapChunk::HeightAt can ignore both
//     because its division cancels them, but MIN_NORMAL_Z is a slope against a unit normal, so
//     left alone it rejects every placement on every chunk. It did: the first run scattered
//     nothing at all, on ground that was not steep anywhere.
//  3. The cell's ground effect comes from the low quality texture map, two bits a column by
//     TEXMAP_MASK/SHIFT, unless the chunk's predTex bit for that column (PREDTEX_MASK/SHIFT)
//     says otherwise; a hole in the chunk (s_holeMask) skips the cell outright.
//  4. That effect's four doodad kinds are dealt into a sixteen-slot bag by their weights,
//     stepping thirteen slots at a time so the kinds interleave rather than clump, and any
//     slots left over are filled round-robin from the four.
//  5. The effect's own count -- its DBC column after the weights, defaulting to eight -- says
//     how many to place. For each: two random numbers give a point in the cell, the kind comes
//     from the bag at (n + cell) & 15, and the triangle is chosen by which side of the cell's
//     two diagonals the point falls. A triangle whose normal leans past MIN_NORMAL_Z gets
//     nothing. The height comes from that triangle's plane, the colour from the interpolated
//     MCCV (or white, and darkened where MCSH shadows it), the rotation from (r + 1) * TAU_HALF
//     and the scale from r * SCALE_JITTER + 1.
//
// FUN_007b31e0, which takes all of that and fills one instance, is decoded too. The instance
// is 0xa4 bytes from the WDETAILDOODADINST heap and holds four batches of 0x24: a model key,
// running vertex and index totals, and a growable array of 0x2c-byte placements (cell, kind,
// position, rotation, scale, plane, colour). A placement joins the batch whose model matches
// and which still has room in one buffer of the ring, so a chunk draws at most four models.
//
// The picking and placing half of that is now ported, as Scatter below, and CHECKED AGAINST A
// RUN rather than read over: every placement's height was compared with CMapChunk::HeightAt,
// which is itself verified against the centre-vertex invariant and solves the same cell
// triangle by a separate path. 1875 placements over 40 chunks, none off by more than 0.00048
// yards, all inside their chunk, every kind drawn from that chunk's own ground effect table,
// and each chunk scattering identically twice over.
//
// WHAT STANDS BETWEEN HERE AND RETIRING THE STAND-IN is now only the wiring. Every function
// this module needs is ported: the scatter, the instance fill, the buffer acquire, both buffer
// fills and the draw. What is missing is the pass that walks the frame's chunks and calls Draw,
// and the deletion of Terrain.cpp's own scatter -- which have to happen together, because both
// running at once would draw the grass twice.
//
// Two things in here are ported but have NOT been seen working, and should be treated as
// suspect until they have:
//
//  - The GEOMETRY the vertex fill writes. Its counts are checked (20 batches filled with no
//    mismatch against the totals the instance fill accumulated and no overrun), but nothing has
//    looked at where the blades actually stand.
// THE PASS IS ALREADY DECODED, and it is FUN_007984a0 -- which reads like a general model pass
// and is not one. Its list nodes are these instances: it takes the chunk from +0x98, builds that
// chunk's matrix with FUN_00790440, asks the chunk for its lighting through its own vtable slot
// 1, translates by the chunk's position less the camera, and then either sets up fixed-function
// lights and fog or hands the matrix to the shader path (FUN_007b10e0) depending on what
// FUN_007b2d30 reports -- and only then calls Draw. Wiring it needs those two and nothing else
// from this module.
//
//  - The SLOPE-ALIGNED branch of the vertex fill, which stands a doodad up along the terrain
//    normal instead of merely turning it about Z. Only 30 of the 580 GroundEffectDoodad rows
//    set the flag that selects it, and none of them appear in the starting zone, so across 832
//    placements it ran exactly zero times.
//
//    REVIEWED TERM BY TERM against the reference on 2026-09-25, since it cannot be run here:
//    the perpendicular is (0, n.z, -n.y) normalized with the x component multiplied by a
//    constant that reads 0.0; row 0 is n x u, row 1 is u, row 2 is n; the Z rotation folds the
//    axis term away because the axis is +Z, leaving row 2 scaled by exactly 1; the vertex
//    transform multiplies by the frame in the same order; and the frame cache keys on
//    cell & 0xfc for the group with cell & 3 for the slot, starting from the same 0xffff
//    sentinel. All of it agrees. That is not the same as having seen it, but it is no longer
//    merely unexamined.

// One kind of detail doodad: the row that names it, and the model once something has asked for
// it. The model is not opened until a chunk that wants this kind comes into range.
class CDoodadModel {
    public:
        GroundEffectDoodadRec* m_rec = nullptr;  // +0x00
        CM2Model* m_model = nullptr;             // +0x04
        // +0x08: the model's first texture, resolved once the model lands. This is the key the
        // instance batches on, which is why two kinds sharing a texture share a draw.
        HTEXTURE m_texture = nullptr;
};

// Static variables
// Every kind the DBC carries, indexed by its own id, with gaps left null.
extern TSGrowableArray<CDoodadModel*> s_models; // DAT_00d1c4fc
extern uint32_t s_perChunk;                    // DAT_00d1c4d0
extern uint32_t s_vertexBytes;                 // DAT_00d1c4c8
extern uint32_t s_indexCount;                  // DAT_00d1c4c4
extern CGxPool* s_vertexPool;                  // DAT_00d1c4d8
extern CGxPool* s_indexPool;                   // DAT_00d1c4d4
extern TSGrowableArray<CGxBuf*> s_buffers;     // DAT_00d1c50c
extern int32_t s_rebuild;                      // DAT_00d1c4c0
// Whether the module's own vertex and pixel shaders were loaded. When they were, the shader
// applies the doodad's brightness ramp and the buffer keeps the raw colour; when they were not,
// the ramp is baked into the vertex colour on the way in.
extern int32_t s_useShaders;                   // DAT_00d1c4f0
// How far detail doodads are drawn, and the distance the fade is built around. The reference
// keeps it in a writable global seeded from the groundEffectDist CVar.
extern float s_fadeDistance;                   // DAT_00adeeb8, 70.0
// The 64x8 ramp the fixed-function path fades the grass out with.
extern HTEXTURE s_fadeTexture;                 // DAT_00d1c4e0

// One scattered doodad. 0x2c bytes, laid out as the reference's, which keeps these in a
// growable array on each of the instance's four batches. Only the triangle's NORMAL is kept,
// not the whole plane: the plane's job ended when it gave the doodad its height.
//
// The name is the reference's own, off the RTTI on the growable array's Reserve.
struct CDetailDoodadInstAdd {
    uint16_t unk0 = 0;      // +0x00: written by nothing the instance fill touches
    uint16_t cell = 0;      // +0x02: which of the chunk's 64 cells it stands on
    int32_t doodadId = 0;   // +0x04
    C3Vector position;      // +0x08: in the chunk's own space, as the terrain's vertices are
    float rotation = 0.0f;  // +0x14: 0 .. 2*pi
    float scale = 0.0f;     // +0x18: 0.67 .. 1.33
    C3Vector normal;        // +0x1c: the triangle's, normalized and pointing up
    uint32_t color = 0;     // +0x28: 0xAABBGGRR, the interpolated vertex colour
};

// One batch: every doodad on this chunk that shares a texture, so grass of several kinds still
// draws in one call. 0x24 bytes.
struct SBatch {
    HTEXTURE texture = nullptr;     // +0x00: the key -- batches are keyed by texture, not model
    uint32_t vertexTotal = 0;       // +0x04: running, against s_perChunk
    // +0x08: running, against s_indexCount -- which is the index buffer's CAPACITY (s_perChunk),
    // not the s_perChunk * 2 the reference also computes and never reads. See CreateBuffers.
    uint32_t indexTotal = 0;
    // The pair out of the ring this batch draws from, taken at draw time rather than at
    // scatter time, so a chunk that is scattered and never drawn costs no buffer.
    CGxBuf* vertexBuf = nullptr;    // +0x0c
    CGxBuf* indexBuf = nullptr;     // +0x10
    TSGrowableArray<CDetailDoodadInstAdd> placements;  // +0x14
};

// One chunk's worth of detail doodads. The reference takes it from the WDETAILDOODADINST heap,
// and this is its own name, off the RTTI on the array that holds them.
//
// The tail is read out of the pass, FUN_007984a0, which walks these: it clears +0x94, takes the
// chunk from +0x98 (and then reads that chunk's own vtable, matrix at +0x3c and position at
// +0x7c), and unlinks +0x9c from the chunk's list before relinking the instance elsewhere.
class CDetailDoodadData {
    public:
        uint32_t unk00 = 0;             // +0x00
        SBatch m_batches[4];            // +0x04: a chunk draws at most four textures' worth
        uint32_t m_queued = 0;          // +0x94: cleared by the pass as it takes the instance
        CMapChunk* m_chunk = nullptr;   // +0x98: the chunk it was scattered on
        TSLink<CDetailDoodadData> m_frameLink;  // +0x9c: this frame's list of instances to draw
};

// THE OFFSETS IN THE COMMENTS ABOVE ARE THE REFERENCE'S, AND FROZEN DOES NOT REPRODUCE THEM.
// Two reasons, both structural and neither worth working around:
//
//  - Frozen is a 64-bit build. Every pointer in these structs is eight bytes where the
//    reference's is four, so anything after the first one has moved.
//  - Frozen's TSBaseArray declares MemFileName and MemLineNo virtual, so every TSGrowableArray
//    carries a vtable pointer the reference's does not. The reference's batch array is sixteen
//    bytes of {capacity, count, data, chunk} at +0x14; frozen's is larger.
//
// The offsets are kept in the comments because they are what the decompilation reads and what
// the next person will be checking against -- not because the struct reproduces them. Nothing
// here depends on the layout: no allocation is shared with the reference and no field is
// reached by a raw offset. A static_assert on any of these numbers would be asserting something
// a 64-bit build cannot satisfy, which is why there is not one.

// Functions
// Build the two pools and the ring of buffers, sized from the ground effect density. Does
// nothing unless something has asked for a rebuild. ref: FUN_007b2a80
void CreateBuffers();

// Scatter one chunk's grass into `instance`, returning how many were placed. Part of
// FUN_007d3390: the picking and placing, without the allocation of the instance itself.
// Places nothing when the chunk has no layers or its models have not arrived.
uint32_t Scatter(CMapChunk* chunk, CDetailDoodadData* instance);

// The model has landed, so its first texture can be resolved and kept as the batching key.
// ref: FUN_007b1b10
void OnModelLoaded(CM2Model* model, void* param);

// File one placement into the instance: the batch already drawing this texture and still with
// room in one buffer of the ring, or a free batch, or nowhere if all four are taken.
// ref: FUN_007b31e0
void AddPlacement(CDetailDoodadData* instance, int32_t doodadId, const C3Vector& position,
                  float rotation, float scale, const C3Vector& normal, uint16_t cell,
                  uint32_t color);

// Take a vertex and index buffer for one batch: the last pair off the ring, or a streaming
// pair of its own when the ring has run dry. ref: FUN_007b2ca0
void AcquireBuffers(uint32_t vertexCount, CGxBuf** vertexBuf, uint32_t indexCount,
                    CGxBuf** indexBuf);

// Turn one detail doodad's model into vertices: each model vertex placed, oriented, coloured
// and handed its texture coordinates. ref: FUN_007b1b50
void FillVertexBuffer(SBatch* batch);

// Write the batch's indices: each placement's model contributes its whole index list, shifted
// by the vertices already written. ref: FUN_007b12b0
void FillIndexBuffer(SBatch* batch);

// Draw one batch, filling whichever of its two buffers is not filled yet. ref: FUN_007b3390
void DrawBatch(SBatch* batch);

// Draw a chunk's detail doodads, one call per texture it uses.
void Draw(CDetailDoodadData* instance);

// Put the device into the state the grass draws in, and say whether the shader path was taken.
// ref: FUN_007b2d30
int32_t SetupState();

// Take an instance for a chunk and scatter it. Null when the chunk scatters nothing.
CDetailDoodadData* CreateInstance(CMapChunk* chunk);

// Give one back. ref: FUN_007b3960
void ReleaseInstance(CDetailDoodadData* instance);

// Give the pools and every buffer back. ref: FUN_007b29b0
void ReleaseBuffers();

// Build the table of kinds out of GroundEffectDoodad.dbc. ref: FUN_007b2760
void Initialize();

// Open one kind's model if it is not open already. False only when the scene refuses it.
// ref: FUN_007b3050
bool EnsureModel(CDoodadModel* entry);

// Whether one kind is ready to draw. Asking starts the load. ref: FUN_007b3530
bool IsReady(int32_t doodadId);

}

#endif
