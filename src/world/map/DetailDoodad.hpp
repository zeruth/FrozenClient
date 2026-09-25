#ifndef WORLD_MAP_DETAIL_DOODAD_HPP
#define WORLD_MAP_DETAIL_DOODAD_HPP

#include <storm/Array.hpp>
#include <cstdint>

class CGxBuf;
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

// Static variables
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

}

#endif
