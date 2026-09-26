# Plan of attack: replace the stand-ins, map modules first

Written 2026-09-24. Goal: the scene renders the way the reference renders it, by porting the
reference's map modules function for function and deleting `src/world/Terrain.cpp` and the other
stand-ins as each stage's ownership moves. This file is the plan and the tooling that makes it
fast. `docs/client-inventory.md` is the inventory it starts from; `docs/recomp/REPORT.md` is the
measurement.

## Why five functions a session is the current speed

Each function today costs: a Ghidra headless run (1-3 minutes, serialized), reading a
decompilation in which every struct field is `*(int *)(param_1 + 0x1c0)` and every global is
`DAT_00cd8794`, re-deriving the same struct layout the previous session already derived, then
finding where in a 6,400-line stand-in the equivalent behaviour lives. None of that is porting.
The plan below removes each of those costs once, so a session spends its time writing C++.

## The job, sized

Reference functions by address range (from `data/ref-functions.jsonl` and `data/map.json`,
2026-09-24):

| region | functions | code | linked |
|---|---:|---:|---:|
| world / map (`0x77e000`-`0x7d7000`: CWorld, CMap, WorldScene, MapChunk, MapObj, portals, MapMem, DetailDoodad) | 1,185 | 345k | 67 |
| shadow / DayNight / sky / world text (`0x7d7000`-`0x7f4000`) | 519 | 112k | 36 |
| liquid (`0x8a2000`-`0x8a6000`) and sky draw helpers (`0x9ab000`-`0x9ad000`) | 93 | 22k | 0 |
| M2 (`0x81c000`-`0x839000`) | 313 | 111k | 104 |
| Gx device and state (`0x680000`-`0x6c0000`) | 1,023 | 239k | 86 |

The first three rows are the map job: about 1,800 functions, 480k bytes, 103 linked. The average
map function is 290 bytes, so most are small. The frozen side to be replaced is 11,361 lines in
`src/world` (6,425 of them in `Terrain.cpp`, which carries one `// ref:` tag).

## The single biggest lever: linker order is source order

MSVC emits a translation unit's functions contiguously and in definition order. The report's
module anchoring already uses this. It means a full decompilation of `0x7b6b00`-`0x7c4000`, laid
out in address order, **is `Map.cpp`** as its author wrote it: same function order, same helpers
next to their callers, same static tables between them. Porting a module top to bottom from that
file is a transcription job with a known destination file name, not a search. Every tool below
exists to make that file readable and to keep it readable as names are recovered.

## Tooling, in build order

Each tool is one Python script or Ghidra script, built in one session or less, and pays back on
the first module. No agent fan-out: the parallelism is Ghidra's decompiler across the 20 cores of
this machine, run once.

### T1. The corpus: decompile everything once (`tools/recomp/corpus.py` + `ghidra/ExportDecompAll.java`)

- One headless run using Ghidra's parallel decompiler over all 27k functions, writing
  `tools/recomp/corpus/<addr>.c` (raw, committed as a compressed archive or kept local and
  regenerated; 27k files at ~1.5k each is about 40 MB).
- `corpus.py --module Map.cpp` assembles a module file in address order into
  `docs/recomp/modules/Map.cpp.c`, with a header table (address, size, callers, callees, strings,
  linked frozen name and status) and a banner per function.
- Cost: **built 2026-09-24; the full export takes 131 seconds** (27,597 functions, 36 MB), so
  re-exporting after a Ghidra rename is routine rather than a one-off. Every read is instant.
  Replaces `decomp.sh`.
- Done: `corpus.py --module Map.cpp` renders 227 functions from the corpus with no Ghidra call.

### T2. Names that stick (`tools/recomp/names.json` + `corpus.py --render`)

- A single committed registry: reference address to name for functions, `DAT_` globals and
  `PTR_` tables; and per class, field offset to name and type. Sources: `overrides.json` and
  `// ref:` tags (function names come for free from the map), `docs/world-render-inventory.md`
  and `docs/ref/parity-*.md` (already hold dozens of recovered names and offsets), and every
  future session's discoveries.
- The renderer rewrites the corpus text on output: `FUN_007b6b00` becomes `CMap::Update`,
  `DAT_00cd8794` becomes `g_cameraLiquidId`, `*(float *)(param_1 + 0x90)` becomes
  `this->fogStart` when the function's `this` type is known. Text substitution, no Ghidra.
- **Built 2026-09-24** with the corpus renderer; `names.json` also carries the curated module
  ranges from T6 (World, MapWeather, WorldScene, Map, MapObj, DetailDoodad, MapShadow), edges
  provisional. Payback: the second module read in a session is written in the vocabulary of the
  first.

### T3. Layout recovery without Ghidra (`tools/recomp/layout.py <class>`)

- Reads every corpus function whose `this` (or first parameter) is known to be a given class,
  collects each `param + 0xNN` access with its width and the type it is cast to, and prints a
  field table: offset, size, inferred type, which functions touch it, and the frozen field that
  already sits at that offset if the class is declared in `src/`.
- This is the struct-layout step done once per class instead of once per function, and it feeds
  T2 directly. `ExportStructRefs.java` does this for globals today; this is the `this`-relative
  version and needs only the text.
- **Built 2026-09-24**, with a second mode, `--globals` / `--globals-of`, that lists every
  `DAT_` a set of functions touches with read/write counts: CMap in 3.3.5a is a set of statics,
  so that mode is the layout step for the map modules.

### T4. Push names back into Ghidra (`ghidra/ApplyNames.java`, run when convenient)

- Reads `names.json` and the map, sets function names and signatures (from frozen's libclang
  inventory, which knows every linked function's prototype) and applies struct types to `this`
  parameters. Then T1 re-runs for the affected region.
- After this, Ghidra's own decompiler propagates the types through callers and callees, which the
  text renderer cannot do. Run it every few modules, not every session; T2 covers the gap.

### T5. The Gx call stream diff (`tools/gxtrace.py`, extends `calltrace.py`)

- Both clients already run under the same debugger attach. This records, for one frame, the
  sequence of Gx entry calls with arguments: render-state sets (`GxRsSet` id and value), transform
  sets, shader binds, shader constants (register, count, values), vertex and index stream binds,
  and primitive draws (type, counts). About fifteen functions on each side, all already linked.
- `gxtrace.py diff` aligns the two streams by draw and prints the first divergence: a state that
  differs, a constant that differs, a draw that one side has and the other does not.
- This is the verification instrument for "renders how the reference does". Scene-compare says
  the pixels differ; this says which call made them differ. It also runs from the same login and
  viewpoint the scene harness already sets up, so one launch serves both. Launching is still on
  your say-so.

### T6. Module-scoped measurement (small change to `recomp.py`)

- `--module` already exists. Add a per-module totals line to the report (functions, linked,
  faithful, stubs, and the list of unlinked addresses) so a module session opens and closes on
  one number, and a `--module` filter for `--fix` and `--diff` batches.
- Also close the anchoring gaps: functions between two anchors of different files are `?` today.
  Assign them by contiguity (the boundary is where the callee set changes), so `Map.cpp` shows all
  of its functions rather than the anchored subset.

## Method for one module session

1. `corpus.py --module X.cpp` (T1+T2 output). Read the header table, then the file top to bottom.
2. `layout.py` for each class the module owns; reconcile with the frozen header; fix the header
   first. Layouts before bodies, always.
3. Port in address order into the file named after the module (`src/world/map/CMap.cpp` for
   `Map.cpp`, and so on). Tag every definition. Static tables between functions are data to port
   too; they are the constants the reference's callers pass.
4. Where a stand-in already does the job, the port replaces it in the same commit and the
   stand-in code is deleted, not left beside it. `Terrain.cpp` shrinks every session.
5. Build, `recomp.py --pdb`, check the module line moved and nothing regressed. Commit with the
   module delta in the message.
6. Every second or third module: a run with T5 and scene-compare, and `verified` entries in
   `overrides.json` for what was seen.

A module of 100 to 250 small functions is one to two sessions once T1-T3 exist. That puts the
map job at roughly ten to fifteen sessions, against about a hundred at the current rate. That is
an estimate from the function sizes, not a promise.

## Order of attack

Ordered by what the next stage depends on, so nothing is ported against a stand-in it will have
to be re-ported against later.

1. **MapMem.cpp** (101 fns): allocators, shader loading from the archives
   (`Terrain.bls` and friends, `docs/ref/parity-map-memory.md`), index pools including
   `lowDetailIndexPool`. Everything else allocates through it.
2. **MapLoad.cpp, MapArea.cpp, MapChunk.cpp** (29 + 21 + 129): ADT and chunk loading, chunk
   buffers, the per-chunk matrix path already proven. This is where `LoadTile` and `ParseChunk`
   in `Terrain.cpp` get retired.
3. **Map.cpp** (237): `CMap::Update`, `CMap::Render`, the visibility lists and traversal, the
   chunk window, farclip blending, footprints. This is the stage that makes every other stage stop
   re-culling on its own, and where the reference's LOD gating lives.
4. **MapObjRead.cpp, MapObj / MapObjGroup / portal code** (the `?`-anchored WMO region, several
   hundred functions): WMO loading, materials (the 2026-09-23 loader gap), portals, BSP, MFOG and
   MOLT. Retires `LoadWmoInstance` and the WMO half of `Terrain.cpp`.
5. **MapChunkLiquid.cpp and the liquid region** (77 + 72): both buckets, procedural water, WMO
   liquid. The inventory's highest-impact visual gap.
6. **DetailDoodad.cpp** (162): the global instance pool, which fixes the `groundEffectDensity`
   semantics as a side effect.
7. **WorldParam.cpp** (187): the CVar callbacks that are empty today, so every quality setting
   does what the reference does. Small functions; fast.
8. **DayNight, sky, shadow, world text** (519): most of it is built and unverified; this pass is
   `--fix` and `--diff` against the corpus plus the T5 run, not fresh porting.
9. **M2Scene.cpp remainder and M2 LOD** (skin profile selection, `DrawBatchProj`, ribbons,
   particle wiring, the cache).
10. **Gx device** (1,023): last, because the stand-in works and the diffs from T5 will say which
    of its functions actually matter.

Movement, spells and the UI stubs are not in this plan; they start after the scene matches.

## State of the load chain (2026-09-25, after the long port session)

Thirty-five commits landed on 2026-09-25. Terrain.cpp went 6488 -> 6211 lines and the process
from about 1.08 GB resident to 890 MB. linked 4257 -> 4307, faithful 2159 -> 2201, verified
14 -> 30, stubs 580 -> 578.

**Items 1, 3 and 5 are done. Item 11 is two thirds done.**

- **1 terrain.** Was already complete; the comment claiming its three texture builders were
  unported was simply stale.
- **3 doodads.** The map places and draws the terrain doodads. `CreateDoodadDef` makes the
  model and hands it the full placement, `CMap::UpdatePendingEntities` works out its bounds and
  detail band once the model lands, and `TraverseChunkDoodads` decides each frame whether it
  draws. The stand-in's arrays, per-frame cull, unload path and walk are deleted. What is left
  of `TerrainForEachDoodad` yields only the doodads inside buildings, which retire with item 2.
- **5 blob shadows.** `RenderShaded` and `RenderFallback` are deleted. Doodads do not cast blobs
  and the invented walk that made them is gone.
- **11 close out.** `TerrainAreaIDAt` and `TerrainSphereVisible` now answer from the map. Only
  the `OnWorldRender` pass ordering remains, and it genuinely waits on items 4 and 6.

**Item 4 is complete except for the draw**: the MH2O reader, `CChunkLiquid` creation (load
verified), the row and frame chain, the point query including strict mode, and the camera
liquid. Only the two-bucket pass is missing.

**Item 6 is fully decoded and nothing in it needs research.** The two pools and the ring of 128
buffer pairs are built and verified, the 799-kind table and the readiness gate are in, and every
table and constant the scatter builder uses has been read out of the reference's data section.

### Facts worth not re-deriving

- The WMO draw always takes the shader path: the reference's shader-level global reads 5.
- `VisitMapObjDefGroup`'s two branches are the other way round from how they read: group flag
  bit 3 is the ordinary exterior group and goes to the portal walk, bit 16 is the shortcut.
- `CMapObj::Render` transforms each frustum record by the def's inverse placement, which lets
  the per-batch cull test a raw group-space box.
- The portal rectangle's components are ordered vertical-first, and the walk's window matches.
- `VBBList`'s block-sharing mode is dead in 3.3.5a.
- **The doodad draw bit (0x80) is set by `FUN_007b5740` once `CM2Model::IsLoaded` says the model
  arrived.** It means "placed". An older note here said nothing ORs it in; that was wrong, and
  the search failed because it only looked at load-time code.
- **The detail level is not stored anywhere**: `CMapStaticEntity::Place` computes it from the
  widest side of the placed box against thresholds of 1, 4, 15 and 100 yards.
- **Blob shadows are cast only by `CMapEntity` objects.** `BlobShadowDraw` has one caller, its
  gate has two, and no `CWorld::AddObject` caller is in the map module. Doodads are
  `CMapStaticEntity` and can never reach the caster; props are shadowed by the baked MCSH.
- **`CWorld::AddObject` builds `m_flags7c`, and bit 0x800 is inverted** -- an object opts *out*
  of shadows by setting its own bit 1.
- **`CMapDoodadDef` had no constructor and nothing set `Type_DoodadDef`**, so every test asking
  whether an entity is a doodad quietly answered no, including a live one in
  `CMap::LinkToMapObjDefGroup`. Only Chunk, Entity and DoodadDef are set even now; Area,
  MapObjDef, MapObjDefGroup and Light are still missing and have no readers yet.
- The map addresses a point in cells: the tile's row comes from x and its column from y, which
  is why the chunk indices look transposed. `CMap::ChunkAt` is that addressing named.
- A terrain cell is four triangles fanned through its centre vertex, not a bilinear quad.
- The light block holds three fog sets; the sky interpolates +0x8c and +0xb0 into +0xa0, so the
  map object's fog selection is between the current fog and one end of that blend.

### What the detail doodad work established (2026-09-25, later)

- **Batches are keyed by TEXTURE, not by model.** `CDoodadModel +0x08` is a texture the loaded
  callback resolves from the model's first `M2Texture`, and that is what the instance batches
  on -- so a chunk with several kinds of grass still draws in one call.
- **The skin profile's counts are at +0x04 and +0x0c**, which read like `M2Array` offsets and
  are not: the SKIN chunk opens with a four-byte magic. Three separate sites agree.
- **`MIN_NORMAL_Z` is a slope against a UNIT normal.** The cross product through a cell's centre
  comes out pointing under the ground and scaled by twice the triangle's area. `HeightAt` can
  ignore both because its division cancels them; the scatter cannot, and unnormalized it rejects
  every placement on every chunk -- which is exactly what the first run did, on flat ground.
- **`FUN_007984a0` is the detail doodad pass, not a model pass.** It reads a chunk's vtable,
  matrix and position, which is why it looks like one; the chunk comes from the instance's
  `+0x98`. Its field accesses are what fill in the instance's `+0x94..+0xa3` tail.
- **The vertex colour ramp and the module's shaders are the same switch.** The flag that skips
  the CPU brightness ramp is the one that decides whether `Shaders\Vertex\DetailDoodad` gets
  loaded, so a port without the shaders must keep the ramp.
- **30 of the 580 `GroundEffectDoodad` rows set the slope flag**, and none are in the starting
  zone. The slope-aligned branch of the vertex fill is ported and has run zero times.
- **Frozen cannot reproduce these structs' offsets**, for two structural reasons: it is a 64-bit
  build, and its `TSBaseArray` has virtual methods so every `TSGrowableArray` carries a vtable
  the reference's does not. Do not write static_asserts against the reference offsets.

### The occlusion volumes are NOT built from the terrain

An earlier note here said the volumes come from the low-detail terrain and that their table is
filled from the .wdl. **Both are wrong.** They are 62 HAND-AUTHORED convex polygons baked into
the reference's .data at 0x00af0040, 280 vertices in total, across maps 0, 571, 575, 600, 603,
609 and 631. Each record is {mapId, flags, vertex pointer, vertex count}; the bounding box beside
it is zero in the image and computed from the vertices on first use.

Their windings are MIXED -- fifteen wind one way, fifteen the other, thirty-two are vertical --
so roughly half of them come out inside-out. That is not a bug to fix: an inside-out pyramid asks
for a sphere on the outer side of every side plane at once, which for a convex cone is
impossible, so such a volume simply never occludes. Measured: over 109,194 sphere tests the two
correctly-wound volumes on map 609 occluded 800 and 52,397 times and the inverted one occluded
exactly zero.

### A trap for anything tested at the default spawn

**Map 609 spawns on Acherus, 138 yards above the ground.** The camera sits at z 429 with the
nearest terrain chunk centre at z 291 and 226 yards away, so ANY per-chunk distance gate is shut
there -- the detail doodad pass drew nothing at its real 70 yard gate and the port was not why.
Widen the gate, or move, before concluding a distance-gated port is broken.

### What each remaining item needs

Checked against the tree on 2026-09-25, not carried forward. The previous version of this table
said item 2 still needed the large portal internals and item 7's volumes came from the .wdl.
Both were wrong, and both cost time before being caught, so every row below was re-verified
against `matches.tsv` and the source rather than trusted.

| item | state | what is actually left |
|---|---|---|
| 1 terrain | **done** | -- |
| 2 map objects | **renders**; def creation, both row visits, the whole portal walk, `CMapObj::Render` and floor light are all ported and tagged | the WMO **blob receivers** -- `BlobShadowDrawWmo` is still a Terrain.cpp stand-in -- and the group **doodad collision queries** (`FUN_007c9dd0` 820 and `FUN_007cab70` 1348, plus five helpers). Neither is on the render path |
| 3 doodads | **done** | -- |
| 4 liquids | the reader, creation, row insert, visit, point query, camera liquid and now `CreateSurface` are ported | the **draw**: seven `IMaterial` implementations, the two-bucket manager and its dispatch (`FUN_008a2240`), and the geometry the factory defers. `CreateSurface` is one line from being wired and that line is the first thing to try |
| 5 blob shadows | **done** | -- |
| 6 detail doodads | **done** | only `FUN_007b10e0`, the shader-path constants, unreachable while the module's shaders are not loaded |
| 7 occluders | the .wdl loads and the 62 volumes build and occlude | the low-detail terrain **mesh** itself (`FUN_007cd910`, `FUN_007cc810`), and the extruded volume path behind the polygon clipper `FUN_007f9650` |
| 8 sky | not started | `SkyRender` and `SkyBodiesRender` are still Terrain.cpp's own |
| 9 weather | not started | `TerrainSetWeather` and `WeatherRender` are still Terrain.cpp's own; ~14 KB across 17 reference functions, not the "~290 lines" the queue estimates |
| 10 map shadow | partly | `FUN_007bb670` (448, the receiver plane) and `FUN_007bb570` (248) are unported; `ShadowMap.cpp` has three binds still marked TODO |
| 11 close out | queries done | `OnWorldRender`'s ordering, which waits on 4 |

### What is left in Terrain.cpp (5592 lines)

The stand-in is down from 6488 but the remaining entry points name exactly what is left to do:
`TerrainSetWeather` and `WeatherRender` (item 9), `LiquidRender` (item 4's draw),
`BlobShadowDrawWmo` (item 2's receivers), `SkyRender` and `SkyBodiesRender` (item 8),
`UnderwaterOverlayRender`, and the load/update/view scaffolding that holds them together.
`TerrainRender` still runs the view and visibility half when the frame has not already done it.

## What "done" means for a module

- Every reference function in the module has a tagged frozen counterpart or an `overrides.json`
  entry saying why not (excluded, diverged with reason).
- The module's faithful count equals its linked count minus recorded divergences.
- The stand-in code it replaced is gone from the tree.
- One T5 trace shows the module's draws and states matching the reference for the harness scene.

## First step

Build T1 and T2 (the corpus and the name registry) and generate `Map.cpp.c` and `MapMem.cpp.c`
from them. That is one session, and it is the session that changes the speed of every one after.
