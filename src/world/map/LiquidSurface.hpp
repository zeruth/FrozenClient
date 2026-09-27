#ifndef WORLD_MAP_LIQUID_SURFACE_HPP
#define WORLD_MAP_LIQUID_SURFACE_HPP

#include <storm/Array.hpp>
#include <tempest/Matrix.hpp>
#include "gx/CGxBatch.hpp"
#include "gx/buffer/Types.hpp"
#include <tempest/Sphere.hpp>
#include <tempest/Plane.hpp>

#include <cstddef>
#include <cstdint>

class CChunkLiquid;
class CGxBuf;
class CM2Lighting;

// At GLOBAL scope on purpose: these live outside the Liquid namespace, and declaring them inside it
// invents Liquid::CMapObjGroup, which is not the class anything else means.
class CMapObj;
class CMapObjGroup;

namespace Liquid {

// The shared wave animator. Three waves drift past the camera and their parameters become shader
// constants, which is what makes a water surface move rather than sit still.
//
// A refcounted SINGLETON: the reference makes one for the first CChunkLiquid that needs it, keeps a
// use count beside the pointer, and every surface holds a reference. 0x00d2dd2c and 0x00d2dd30.
//
// The layout matters, because the material draw does not ask for fields -- it asks for a POINTER
// and a DWORD COUNT and walks them. The reference's vtable slot 3 is `return this + 8` and slot 4 is
// `return 42`, so the records are 42 contiguous dwords at +0x08: three of six floats and then three
// of eight. Keep m_wavesA and m_wavesB adjacent or the draw reads the wrong thing.
class CWaveManager {
    public:
        static const uint32_t WAVE_COUNT = 3;
        // What slot 4 answers. 42 dwords is exactly the 0xa8 bytes the constructor's first memset
        // clears, which is the independent check on this layout.
        static const uint32_t RECORD_DWORDS = 42;

        uint32_t m_refCount = 1;

        // The first three records. NOTHING fills these -- the constructor zeroes them and Update
        // never touches them -- so the draw's first three wave registers take zeroes even with a
        // manager present. Reproduced rather than repaired: that is what the reference does, and
        // whatever fed them in an earlier build is gone.
        float m_wavesA[WAVE_COUNT][6] = {};
        // The three that move: position, direction, two scalars, the faded amplitude, and a rate.
        float m_wavesB[WAVE_COUNT][8] = {};

        // Milliseconds since each wave respawned, and how long it lives.
        uint32_t m_elapsed[WAVE_COUNT] = {};
        uint32_t m_period[WAVE_COUNT] = {};

        // The unfaded copy Update works on; m_wavesB is this with the amplitude scaled by the fade.
        float m_working[WAVE_COUNT][8] = {};

        void AddRef();
        // Vtable slot 2, which is why CInstance releases this through a different slot than the
        // per-surface objects beside it.
        void Release();

        // Vtable slot 3 (FUN_008c6c80 is `return this + 8`), inline so it binds nothing.
        const float* Records() const { return &this->m_wavesA[0][0]; }
        // Vtable slot 4 (FUN_007d6200 is `return 42`), inline so it binds nothing.
        uint32_t RecordDwords() const { return RECORD_DWORDS; }

        // Age the three waves and respawn any whose time is up. Guarded to run once a frame however
        // many layers call it.
        void Update(const C3Vector& cameraPos);
};

// The singleton, made on first use.
CWaveManager* GetWaveManager();

class IMaterial;
class CMaterialSettings;

// The layers one surface is drawn from. The geometry is NOT built here: the factory only holds
// the list, and the vertices and indices are made when the surface is actually drawn. The name
// is the reference's own, off the RTTI on its allocation.
//
// THE BUILD CONTRACT, decoded from the reference so porting it is transcription. The draw reaches
// the geometry through this object's vtable (at 0x00a404c0), not through any named function:
//
//   slot 2 (FUN_007d4ab0, 1099 bytes)  Build(format, &vertexBuf, &indexBuf, &batch)
//   slot 4 (FUN_007d4790, 60 bytes)    a cheap prepare/test taking the matrix
//
// The format the draw passes is 0xb, which is GxVBF_PT2 -- position and TWO texture coordinate
// sets, no normal and no colour. Build asks GxVertexAttribOffset for attributes 0, 3, 4, 6 and 7
// and writes whichever the format actually carries, so it is written once for any format.
//
// Build works in two passes over the layers:
//
//   counting   xSpan = m_tileEndX - m_tileX, ySpan = m_tileEndY - m_tileY
//              vertices += (xSpan + 1) * (ySpan + 1)
//              indices  += xSpan * ySpan * 6
//
//   filling    for each layer, take the factory's own 4x4 at +0x34, translate it by
//              (layer->m_origin - layers[0]->m_origin) so every layer draws relative to the
//              FIRST one, and hand it to FUN_007ce390 (391 bytes) which writes that layer's
//              vertices and advances each attribute pointer.
//
// It caches: a holder at +0x1c keeps the buffer pair, and the whole build is skipped when that
// holder is present, both buffers still say they hold data, and the dirty flag at +8 is clear.
// The holder comes from a pool keyed on the EXACT byte sizes (FUN_007cf140 walks a free list
// looking for stride*vertices and indices*2, and FUN_007cefd0 makes one when nothing matches).
//
// The batch it fills says primType 4, start 0, minIndex 0 and maxIndex vertices - 1. RESOLVED
// 2026-09-27: it IS a strip, and this note used to say the two readings could not both be right and
// to check against a run. No run was needed. WriteLiquidIndices below (FUN_007a7920) starts each run
// with v0, v0, v1 -- a degenerate restart triangle -- adds only two indices per rendering tile, and
// repeats the last index to close a run. So three-per-vertex is an upper bound covering the
// restarts rather than six-per-quad, and a list could not fit the buffer at all: a 16x16 tile grid
// would want 6 * 256 = 1536 indices against a budget of 3 * 17 * 17 = 867.
// A liquid surface's geometry, whichever kind it is. The reference gives BOTH of its factories a
// four-slot vtable and stores either one in Liquid::CInstance's +0x08, so the pair is polymorphic
// there rather than two unrelated classes:
//
//   CChunkGeomFactory  vtable 0x00a404c0, build FUN_007d4ab0  -- terrain chunks
//   CMeshGeomFactory   vtable 0x00a404d4, build FUN_007d43f0  -- map object (WMO) groups
//
// Slot 1 is the release and slot 2 the build, which are the two the instance and the material
// actually call through, so those are the two this base makes virtual. The refcount sits at +0x04
// in both, right after the vtable, which is where the reference keeps it too.
class IGeomFactory {
    public:
        uint32_t m_refCount = 1;

        virtual ~IGeomFactory() = default;

        void AddRef();

        // The reference's vtable slot 1.
        virtual void Release() = 0;

        // The reference's vtable slot 2: build the surface's vertices and indices, or hand back
        // what was built last time.
        virtual int32_t Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf, CGxBuf** indexBuf,
                              CGxBatch* batch) = 0;
};

// What the outline's values are folded through. Both ends and the result are POINTERS to a vertex's
// attribute bytes -- the two folds below interpolate between two attribute blocks into a scratch
// slot and hand back its address. The float is the interpolation factor.
typedef void* (*MapObjPolyFold)(const void* a, const void* b, float t);

// ref: FUN_007a7f00
// Interpolate ONE byte. The fold for the vertex layout that carries a colour cursor.
void* MapObjPolyFoldByte(const void* a, const void* b, float t);

// ref: FUN_007a7e50
// Interpolate TWO uint16s. The fold for the layout without one.
void* MapObjPolyFoldShortPair(const void* a, const void* b, float t);

// Frozen-only: empty the fold scratch. The reference does this inline in the emitter (0x7a81e6).
void MapObjPolyFoldReset();
// The point-and-edge accumulator the map-object liquid geometry emitter builds its outline in, and
// the walker that reads it back. The reference keeps both in MapObjRead.cpp; their purpose was only
// established once FUN_007a7f60 -- the emitter -- was read, because nine functions operate on this
// and none of them says what it is for.
//
// Every offset below is confirmed against the reference, not inferred from spacing: the append
// writes a point at `base + count * 0x2c` with the count at +0x640, the link pass writes each
// point's +0x1c/+0x20/+0x24 and each edge's three fields at +0x580 + i * 0xc with that count at
// +0x644, and the walker seeds itself from +0x580/+0x584 indexed by the cursor at +0x64c. The two
// capacities fall out of those and are exact rather than guessed: 32 points fill 0x000..0x57f
// (32 * 0x2c = 0x580) and 16 edges fill 0x580..0x63f (16 * 0xc = 0xc0).
struct MapObjPolyPoint {
    C3Vector position;      // +0x00
    // +0x0c .. +0x18 is a LAZILY EVALUATED NODE, not four loose fields. FUN_007d91f0 evaluates
    // exactly this shape -- value at +0x00, two children at +0x04 and +0x08, an operand at +0x0c --
    // recursively and memoizes into the first slot, and MapObjPolyDeref does the same arithmetic one
    // level up at the point's +0x0c. A value of zero means "not computed yet".
    //
    // A 64-BIT PROBLEM LIVES HERE and it is why the two children are not pointers below. The
    // reference uses them as the receivers of its recursive calls, so they ARE pointers -- 32-bit
    // ones. Making them pointers here would take the node from 0x10 to 0x20 bytes and the point from
    // 0x2c to 0x40, which breaks the stride the append and the walk both compute by hand and which
    // the static_asserts below pin. So they stay 32-bit and opaque, and whoever ports the evaluator
    // has to decide between an index-based node and a side table. Recorded rather than guessed at.
    // +0x0c: a POINTER to this point's attribute bytes, memoised. Null means unevaluated. For a
    // point the emitter appended it aims into the group's m_liquidVerts; for one the clipper
    // interpolated it aims at a fold scratch slot.
    void* value;            // +0x0c
    // +0x10 and +0x14: the two points this one's value interpolates between, as INDICES.
    //
    // DIVERGENCE, and the reason the earlier note here about pointer width is now moot. The
    // reference stores addresses -- FUN_007d9470 writes `&points[from].value` and
    // `&points[to].value` into these slots at 0x7d95xx, reinterpreting a float field as a pointer --
    // and they always aim INSIDE THIS SAME ARRAY. So an index carries the identical information, at
    // 32 bits on any build, and the point keeps its 0x2c stride.
    //
    // Only read when `value` is zero, which is only true of points the clipper interpolated and so
    // wrote these for. A point from MapObjPolyAddPoint carries a non-zero value and never reaches
    // them -- which is what that function's fourth argument is for. The reference shares the hazard
    // that a leaf whose value is legitimately zero would read them uninitialised.
    int32_t childA;         // +0x10
    int32_t childB;         // +0x14
    // +0x18: the evaluator's third argument, and a FLOAT. Ghidra shows it as an int because it
    // reads the slot with an integer load; the reference uses flds and fstps on it (0x7d91fb and
    // 0x7d9206), so it goes to the callback as a float.
    float operand;          // +0x18
    int32_t inEdge;         // +0x1c: the edge arriving at this point
    int32_t outEdge;        // +0x20: the edge leaving it
    // +0x24 and +0x28 are a one-entry memo of this point's distance to a plane. FUN_007d9470
    // classifies every point of every unflagged edge against a plane, and rather than recompute a
    // point shared by two edges it stamps the generation it last did the work in. So the pair is
    // "the distance, and which pass it belongs to" -- and CloseOutline writing -1 into the stamp is
    // initialising it to a generation that can never match, not filling an unused slot.
    int32_t stamp;          // +0x24
    float planeDistance;    // +0x28
};

struct MapObjPolyEdge {
    int32_t from;           // +0x00, a POINT index
    int32_t to;             // +0x04, a point index
    uint8_t flag;           // +0x08
};

// IT IS A POLYGON CLIPPER. That only became clear from FUN_007d9470, which walks the unflagged edges
// classifying both of each edge's points against a plane and counting the crossings -- points,
// edges, cached signed distances and a boundary walk are the pieces of exactly that. For map-object
// water it is the liquid outline being clipped, which is why the emitter builds one per surface.
struct MapObjPolySet {
    MapObjPolyPoint points[32];   // +0x000
    MapObjPolyEdge edges[16];     // +0x580
    int32_t pointCount;           // +0x640
    int32_t edgeCount;            // +0x644
    // +0x648: the GENERATION counter. FUN_007d9470 bumps it on entry and stamps every point it
    // classifies with it, so a point touched twice in one pass is computed once.
    int32_t generation;           // +0x648
    int32_t cursor;               // +0x64c: which edge a walk starts from
};

// THE POINT'S LAYOUT DELIBERATELY DIVERGES NOW, and the asserts that pinned it are gone with a
// reason. Its +0x0c holds a POINTER -- the fold returns the address of an interpolated attribute
// block -- which is four bytes in the reference and eight here, so the struct cannot be 0x2c and
// everything after that field shifts. Nothing breaks: frozen reaches these by named field and array
// index throughout, nothing copies one out of file data, and the reference's own offsets are kept in
// the comments above for reading the disassembly against.
//
// The edge is still pointer-free, so its stride is still worth pinning -- it is the one number the
// reference's hand-written edge arithmetic depends on.
static_assert(sizeof(MapObjPolyEdge) == 0xc, "the edge array strides by 0xc");
// A walk outward from one edge. Both ends are POINT indices, seeded from that edge's two ends.
struct MapObjPolyWalk {
    MapObjPolySet* set;     // +0x00
    void* callback;         // +0x04, invoked by the dereference to fill a value lazily
    int32_t back;           // +0x08: the end stepping backwards
    int32_t forward;        // +0x0c: the end stepping forwards
    uint32_t steps;         // +0x10: its low bit alternates which end moves
    uint8_t done;           // +0x14
};

// ref: FUN_007d9740
// Empty the set: zero every point's position and reset the counts, the generation and the cursor.
void MapObjPolyReset(MapObjPolySet* set);

// ref: FUN_007d9270
// Turn the appended points into a CLOSED outline and return how many times the last edge wrapped.
int32_t MapObjPolyCloseOutline(MapObjPolySet* set);

// ref: FUN_007d92f0
// Park the cursor on the first edge whose flag is still zero.
void MapObjPolySeekUnflaggedEdge(MapObjPolySet* set);


// ref: FUN_007a7f60
// Emit the geometry for every liquid tile that carries its own, clipped to the group's portals.
// Returns how many vertices were written.
int32_t EmitLiquidTiles(CMapObj* mapObj, CMapObjGroup* group, const C44Matrix& matrix,
                        const uint32_t* color, int32_t uvFromBytes, uint32_t uv2First,
                        int32_t stride, uint8_t** positionOut, uint8_t** normalOut,
                        uint8_t** colorOut, uint8_t** uvOut, uint8_t** uv2Out,
                        uint16_t** indexOut, uint32_t baseVertex);

// ref: FUN_007a7b00
// Write one liquid vertex's attributes, each to its own cursor and only where that cursor exists.
void WriteLiquidVertex(CMapObjGroup* group, const C44Matrix& matrix, const C3Vector& position,
                       const uint8_t* vertexBytes, const uint32_t* color, int32_t uvFromBytes,
                       uint32_t uv2First, int32_t stride, uint8_t** positionOut,
                       uint8_t** normalOut, uint8_t** colorOut, uint8_t** uvOut,
                       uint8_t** uv2Out);

// ref: FUN_007d9470
// Clip the outline against one plane, in place. `side` flips which half-space is kept.
void MapObjPolyClipToPlane(MapObjPolySet* set, const C4Plane& plane, int32_t side);

// ref: FUN_007d91f0
// Evaluate one point's value, memoising into it and recursing through the two it interpolates.
void* MapObjPolyEval(MapObjPolySet* set, int32_t point, MapObjPolyFold fold);

// ref: FUN_007d9390
// Read the walk's current point: its position, and its value evaluated on demand.
void MapObjPolyDeref(const MapObjPolyWalk* walk, C3Vector* position, void** value);

// ref: FUN_007d9230
void MapObjPolyAddPoint(MapObjPolySet* set, const C3Vector& position, const void* value);

// ref: FUN_007d9330
void MapObjPolyBeginWalk(MapObjPolyWalk* walk, MapObjPolySet* set, void* callback);

// ref: FUN_007d9400
void MapObjPolyAdvance(MapObjPolyWalk* walk);

// ref: FUN_007d9460
uint8_t MapObjPolyAtEnd(const MapObjPolyWalk* walk);

// ref: FUN_0079b870
// Resolve a liquid type id to the shared block the vertex writer samples, or null when the type
// cannot be drawn this way. See the definition for what is and is not known about the block.
// 256 dwords, indexed by a byte out of the liquid vertex data -- the shape is settled by
// WriteLiquidVertex, whose second texcoord reads block[byte]. That matches the two 0x400-byte
// globals the reference selects between exactly.
const uint32_t* LiquidTypeBlock(int32_t liquidType);

// The duplicated-edge lists the liquid index writer is handed: which tile columns and which tile
// rows get emitted TWICE, so a seam can carry two sets of vertices. Two TSGrowableArrays back to
// back, which is how the reference lays it out -- the counts it reads at +0x04 and +0x14 and the
// data at +0x08 and +0x18 are those arrays' own count and data slots.
//
// BOTH ARE EMPTY FOR MAP-OBJECT WATER. The seam mechanism is shared-code generality; a WMO group
// emits each grid vertex exactly once, so every repeat count below collapses to one. The lists are
// still honoured rather than assumed away, because the writer is the reference's and the caller is
// what decides.
struct LiquidSeams {
    TSGrowableArray<uint8_t> dupColumns;
    TSGrowableArray<uint8_t> dupRows;
};

// ref: FUN_007a7920
// Write the liquid grid's indices as a TRIANGLE STRIP. See the definition -- this is what settles
// the strip-versus-list question this file's own notes left open.
void WriteLiquidIndices(CMapObjGroup* group, const LiquidSeams& seams, uint16_t** cursor,
                        uint32_t baseVertex);

// ref: FUN_007a7cc0
// Write one vertex per position of the MLIQ grid, honouring the seam lists. Returns the total
// written, duplicates included.
int32_t WriteLiquidGridVertices(CMapObjGroup* group, const C44Matrix& matrix,
                                const uint32_t* color, int32_t uvFromBytes, uint32_t uv2First,
                                const LiquidSeams& seams, int32_t stride,
                                uint8_t** positionOut, uint8_t** normalOut, uint8_t** colorOut,
                                uint8_t** uvOut, uint8_t** uv2Out);

// The map-object (WMO) half of the factory pair, vtable 0x00a404d4. Its sibling above covers
// terrain chunks; Liquid::CInstance holds either one polymorphically.
//
// Field map from the allocator's own fill (FUN_007d4920) rather than from a writer hunt -- it sets
// every slot it owns in one run, so the layout is read straight off it. Note it skips +0x18 and
// gives +0x14 the sentinel 0xffffffff while zeroing everything else through +0x44.
class CMeshGeomFactory : public IGeomFactory {
    public:
        CMapObj* m_mapObj = nullptr;            // +0x08
        CMapObjGroup* m_group = nullptr;        // +0x0c
        // +0x10: the cached buffer pair, which the build takes off the GROUP rather than owning.
        // Still null because the build is not ported -- see the note at Build.
        void* m_bufferHolder = nullptr;
        // +0x14: the texture, from m_materials[group->m_liquidMaterial]. The sentinel is what the
        // allocator writes, so "no texture yet" is -1 and not zero.
        uint32_t m_textureId = 0xFFFFFFFF;
        // +0x28: the seam lists BOTH grid writers are handed. The field map called this "a block the
        // two writers share" without knowing what it held; Build passes factory + 0x28 to
        // WriteLiquidGridVertices and WriteLiquidIndices as their seams argument, which names it.
        LiquidSeams m_seams;

        // +0x1c: whether the material's LVF is 1.
        int32_t m_lvf = 0;
        // +0x20: the fixed light the surface draws with -- 1.0 outdoors, 0.0 indoors. The caller
        // decides which, through the indoor test recorded in docs/ref/parity-liquid.md.
        float m_fixedLight = 0.0f;
        // +0x2c and +0x3c: extra grid extents the build ADDS to the group's vertex counts. Nothing
        // in the reference ever writes either, so both stay zero and the count reduces to the
        // group's own grid plus six per rendering tile. Kept because the build reads them.
        int32_t m_extraXVerts = 0;
        int32_t m_extraYVerts = 0;

        // ref: FUN_007d49b0
        static CMeshGeomFactory* Create(CMapObj* mapObj, CMapObjGroup* group);

        // ref: FUN_007d43b0 (the vtable's slot 1)
        void Release() override;

        int32_t Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf, CGxBuf** indexBuf,
                      CGxBatch* batch) override;

        // ref: FUN_007d43e0
        // Takes a POINTER and reads one dword through it, which is how the caller hands over the
        // material's texture field rather than a value.
        void SetTextureId(const uint32_t* textureId);

        // ref: FUN_007d4360
        void SetLvf(int32_t lvf);

        // ref: FUN_007d4370
        void SetFixedLight(float fixedLight);
};

class CChunkGeomFactory : public IGeomFactory {
    public:
        // TODO +0x08 the dirty flag, +0x0c
        TSGrowableArray<CChunkLiquid*> m_layers;   // +0x10 .. +0x1c
        // TODO +0x1c the cached buffer holder, +0x20 .. +0x2c the cached batch,
        // +0x34 the 4x4 every layer's own matrix is derived from

        void Release() override;

        // ref: FUN_007d4ab0 (the vtable's slot 2)
        int32_t Build(EGxVertexBufferFormat format, CGxBuf** vertexBuf, CGxBuf** indexBuf,
                      CGxBatch* batch) override;

        // The buffer pair built last time, and what it was built for.
        CGxBuf* m_vertexBuf = nullptr;
        CGxBuf* m_indexBuf = nullptr;
        CGxBatch m_batch;
        uint32_t m_builtFormat = 0xffffffff;
        int32_t m_dirty = 1;
        C44Matrix m_placement;
};

// What the material reads the frame's environment through. Holds nothing yet: the reference
// keeps a vtable, a null and the argument it was made with.
// What a surface asks for its lighting. It carries no lighting itself -- two switches and a
// virtual that fills a CM2Lighting on the caller's stack, which is the same shape the reference's
// per-terrain-chunk setup (FUN_007d04a0) uses. See docs/ref/parity-liquid.md.
//
// The reference gives it a vtable at 0x00a404e8 with four slots; slot 3 is SetupLighting below,
// slot 2 the m_indoor setter, slot 1 the release. See docs/ref/parity-liquid.md.
class CClientEnvironment {
    public:
        // Which half of the DayNight block the fog comes from: the outdoor set at +0x8c..+0x98 or
        // the indoor set at +0xa0..+0xac. Set through the reference's vtable slot 2, which is one
        // `*(this+4) = arg` folded with CMapLiquidData::SetBody. +0x04
        uint32_t m_indoor = 0;
        // Which light the surface is lit by: 0 takes the map light block's own CM2Light (at
        // 0x00ce04a8 + 0x58), anything else a fixed straight-down white directional light the
        // reference builds once and keeps. CreateSurface passes 0. +0x08
        uint32_t m_fixedLight = 0;

        void AddRef();
        void Release();

        // Fill a lighting block for this surface: the fog, one light, and then whatever the scene
        // finds nearby. The caller owns the block and has already constructed it over the
        // surface's sphere, which is what the reference's FUN_00790440 does one call earlier.
        // The reference's vtable slot 3. ref: FUN_007d4f40
        void SetupLighting(CM2Lighting* lighting);
};

// One drawable liquid surface: the material to draw it with, the layers it covers, and where it
// sits. A layer points at the instance it belongs to and the instance counts them, so a surface
// lives while any of its layers still wants it. 0x6c bytes in the reference.
class CInstance {
    public:
        IMaterial* m_material = nullptr;              // +0x00
        CMaterialSettings* m_settings = nullptr;      // +0x04
        IGeomFactory* m_geometry = nullptr;           // +0x08
        // +0x0c: the wave manager, a Liquid::CWaveManager -- identified from the RTTI name the
        // reference's allocator passes (".?AVCWaveManager@Liquid@@" at 0x00af16a0). It is a
        // refcounted SINGLETON at 0x00d2dd2c, made by the first CChunkLiquid to need it and shared
        // by every surface, which is why it is released through a different vtable slot than the
        // per-surface objects beside it.
        //
        // It supplies the wave records the material draw turns into shader constants: its vtable
        // slot 3 hands back a pointer and slot 4 a count, and the draw walks that range six dwords
        // at a time for three records and then eight dwords at a time for three more.
        //
        // Nothing frozen creates it yet, so the draw sees a null provider and writes the reference's
        // own zero-fill values into those registers. CWaveManager itself is FUN_007d6240 (the
        // constructor) and FUN_007d62a0 (the per-frame update).
        CWaveManager* m_waveManager = nullptr;
        CClientEnvironment* m_environment = nullptr;  // +0x10
        C44Matrix m_placement;                        // +0x14 .. +0x50
        CAaSphere m_sphere;                           // +0x54 .. +0x60
        // +0x64: cleared by the draw as it takes the instance off its bucket
        uint32_t m_queued = 0;
        // +0x68: one per layer pointing here, plus the one the constructor starts with, which
        // CreateSurface drops when it is done. A surface with no layers left frees itself.
        uint32_t m_refCount = 1;

        void AddRef();
        void Release();
};

// Build the surface a layer belongs to, and hand it to every layer it covers.
// ref: FUN_007cf200
void CreateSurface(CChunkLiquid* liquid);

// The frame's surfaces, in two buckets, each emptied by its own draw.
//
// BUCKET_COUNT is 2 and that is not a guess: the reference picks the bucket's comparator out of a
// two-entry function-pointer table at 0x00b23f6c, and its two call sites pass 0 and 1. Bucket 0 is
// drawn from CMap::Render (0x0079acf1, right after the blob shadows) and bucket 1 from the block at
// 0x00790a80, both gated on bit 24 of the render flags at 0x00cd774c and on the manager existing.
static const uint32_t BUCKET_COUNT = 2;

// Put a surface in this frame's queue. Nothing is drawn until the bucket is.
//
// The bucket is NOT the caller's choice: it is the settings' m_procedural flag, so procedural water
// goes to bucket 1 and everything else to bucket 0. That is the whole reason there are two -- the
// procedural bucket is drawn from a later block than CMap::Render. ref: FUN_008a20c0
void Add(CInstance* instance);

// Draw one bucket and empty it. The camera position goes to every material, which is what makes
// the wave and specular terms move with the viewer. ref: FUN_008a2240
void Draw(const C3Vector& cameraPos, uint32_t bucket);

// How many surfaces are waiting in a bucket.
uint32_t Queued(uint32_t bucket);

}

#endif
