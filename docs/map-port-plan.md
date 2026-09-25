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

## State of the load chain (2026-09-24, three cycles in)

Ported and committed, bottom up, nothing calling into it yet: the map memory block
(`CMap::Alloc*`/`Free*`, base-obj links, `LinkToMapObjDefGroup`), the chunk data layer
(`CMapChunk::ParseSubChunks`, `BuildIndices`, `AppendIndices`, the four vertex fillers,
`ComputeBounds`, `GetBounds`, `Load`, `Destroy`, ctor/dtor), and the tile layer (`CMap::CreateArea`,
`CMapArea::Load`/`BeginLoad`/`LoadCallback`/`ParseChunks`/`LoadTextures`/`CreateChunk`/
`CreateChunks`/`Destroy`, `CMap::UnloadArea`, `DestroyChunk`, `SafeOpen`, `LoadTexture`).

The reference chain from load to a drawn chunk, with what is still to port, in order:

1. **Streaming** `FUN_007b5950` (943 bytes, Map.cpp region): walks `s_areaLinkList`, unloads tiles
   outside the tile window, creates missing ones through `CreateArea` for every `s_areaInfo` cell
   with bit 0, sorts them by `AreaDistanceSq` to the camera (`_qsort` with `FUN_007b47f0`),
   starts each unloaded tile's read (`CMapArea::Load`), waits synchronously for tiles that cover
   the camera (`AsyncFileReadWait`), and calls `FUN_007b4df0(_, area, rect, 0)` per loaded tile,
   which reaches `CMapArea::CreateChunks`. Helpers `FUN_007b53b0/5420/5500/54a0` set the window.
   `CMap::Update` `FUN_007b6b00` calls it (and `FUN_007c3730` unload-all first when flagged).
2. **WDT** `CMap::LoadWdt` `FUN_007bf8b0` (MVER, MPHD into `s_wdtHeader`, MAIN into `s_areaInfo`,
   the global WMO when MPHD bit 0), then `FUN_007b7330` (terrain shader level from MPHD bit 1)
   and the settings pass `FUN_007bd8a0` (`s_chunkVerticesWorldSpace`, `s_terrainSpecular` ...).
   `CMap::Load` `FUN_007bfce0` is linked at 0.21 and needs the rest of its body.
3. **Chunk render buffers** `FUN_007d02c0` (calls `BuildVertices`/`AppendIndices`; callers
   `FUN_007d0420`, `FUN_007d3f70`), the render chunk class (`CMapRenderChunk`, 0xa0 bytes,
   ctor `FUN_007b9690`, dtor `FUN_007b9d60`, `FUN_007b7af0` init from `FUN_007c5440`), the
   per-chunk matrix `FUN_007d0050`, the terrain shader constants `FUN_007cfbe0`, the chunk draw
   `FUN_007d3390` (2662 bytes) / `FUN_007d3e10` / `FUN_007d28b0`, and the shader loading in the
   `MapMemInitialize` region (`FUN_0079e4b0`, see `docs/ref/parity-map-memory.md`).
4. **The pass** `CWorldScene` chunk pass `FUN_00798da0` from `CMap::Render` `FUN_0079a870`, then
   the switch in `CGWorldFrame::OnWorldRender` and the deletion of `TerrainRender`'s terrain half.

Still open inside the ported layers, each marked `TODO FUN_...` at its call site: chunk liquids
`FUN_007c5690`, sound emitters `FUN_007c6060`, MCRF references `FUN_007c6150` (doodad and WMO
def creation, MapLoad.cpp), the MH2O parse `FUN_007d4f10`, the per-frame refresh inside
`CreateChunks` (`FUN_007d6690`, `FUN_007c3e70`, `FUN_007c5b20`), the entity and def releases in
`CMapChunk::Destroy` (`FUN_007c3020`, `FUN_007c3250`), and the three device capabilities the
specular texture path reads.

## What "done" means for a module

- Every reference function in the module has a tagged frozen counterpart or an `overrides.json`
  entry saying why not (excluded, diverged with reason).
- The module's faithful count equals its linked count minus recorded divergences.
- The stand-in code it replaced is gone from the tree.
- One T5 trace shows the module's draws and states matching the reference for the harness scene.

## First step

Build T1 and T2 (the corpus and the name registry) and generate `Map.cpp.c` and `MapMem.cpp.c`
from them. That is one session, and it is the session that changes the speed of every one after.
