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
