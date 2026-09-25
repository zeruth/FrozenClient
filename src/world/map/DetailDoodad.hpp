#ifndef WORLD_MAP_DETAIL_DOODAD_HPP
#define WORLD_MAP_DETAIL_DOODAD_HPP

#include "db/rec/GroundEffectDoodadRec.hpp"
#include <storm/Array.hpp>
#include <cstdint>

class CGxBuf;
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

// One kind of detail doodad: the row that names it, and the model once something has asked for
// it. The model is not opened until a chunk that wants this kind comes into range.
class CDoodadModel {
    public:
        GroundEffectDoodadRec* m_rec = nullptr;  // +0x00
        CM2Model* m_model = nullptr;             // +0x04
        // TODO +0x08
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

// Functions
// Build the two pools and the ring of buffers, sized from the ground effect density. Does
// nothing unless something has asked for a rebuild. ref: FUN_007b2a80
void CreateBuffers();

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
