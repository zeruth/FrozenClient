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

## State of the load chain (2026-09-25, seven cycles in)

Ported and committed, and now the path the terrain chunks draw through: the map memory block,
the chunk data layer, the tile layer, streaming (`CMap::Update` -> `UpdateAreas` ->
`CMapArea::CreateChunks`), the WDT and settings loads, the render chunk with its buffer pools
(`CMapRenderChunk`, commit d86ee654), the chunk pass and the render chunk draws (`CWorldScene`,
`CMapRenderChunk::DrawWorld` and friends, commit 6f658b56), and the scene camera plus the chunk
visibility traversal (`CWorldScene::UpdateCamera` / `Traverse` / `TraverseRowChunks`,
`CMap::Render`, commit 183e0281). `CGWorldFrame::OnWorldRender` calls `CMap::Render` before the
stand-in `TerrainRender`, whose chunk pass is gone. **Built, not yet seen running.** The first
run needs to check: chunks appear at all (the traversal, the render lists and
`s_terrainVertexShaders` from `Terrain.bls` are all first-time code), depth against the
stand-in's WMOs, and the sun/fog look (the terrain constants take the projection with its z row
negated, as the reference does; see `CWorldScene::SetupTerrainConstants`).

What the reference does on the same path that is still listed as `TODO FUN_...` in place:

1. **Per row of the traversal** (`CWorldScene::Traverse`): map object defs `FUN_0079a160` (and
   their bucketing `FUN_00792bd0` at the start of `CMap::Render`), liquids `FUN_007935a0`,
   entities `FUN_00793060` / `FUN_007987a0` and the chunk's doodad links `FUN_00799980`,
   occluders `FUN_00793760` (which feeds the horizon buffer through `FUN_007cfb10` /
   `FUN_0078f6a0`; until then `BoxOccluded` always sees an open horizon), low detail
   `FUN_007cd850` / `FUN_00791980`. The stand-in still draws WMOs, doodads and liquids after
   `CMap::Render`.
2. **Inside `CMap::Render`**: the portal path (`s_cameraGroup`), the camera liquid
   `FUN_00790920`, the interior clear colour and the sky flag, the map shadow `FUN_007bb670` /
   `FUN_007bb570`, the map object pass `FUN_007964a0`, liquids `FUN_00795f80` / `FUN_008a2240`,
   the sky and the decal passes. `CGWorldFrame::OnWorldRender` keeps doing the sky, entities and
   blob shadows around it.
3. **Alpha and shadow textures** of a render chunk (`FUN_007b9de0`, `FUN_007b9ee0`,
   `FUN_007b9f90` behind `CMapRenderChunk::UpdateAlphaTextures`): until they land every chunk
   draws its base layer only.
4. **Detail doodads** `FUN_007d3390` / `FUN_00792fa0` from `CMapChunk::PrepareRender`; the
   two-chunk render chunk pairing `FUN_007d6810` (`CMap::s_shaderVertexMode`); the
   fixed-function chunk draws (six functions, listed in overrides.json as not ported); the
   shadow-mapped terrain shaders (`CMap::s_terrain2PixelShaders`, loaded by the shadow map
   system).
5. **Inside the ported layers**: chunk liquids `FUN_007c5690` and their row insertion
   (`CMapChunk::UpdateLiquidVisibility` stops at the chunk test), sound emitters
   `FUN_007c6060`, MCRF references `FUN_007c6150`, the MH2O parse `FUN_007d4f10`, the entity and
   def releases in `CMapChunk::Destroy`, `BufDestroy`/`PoolDestroy` in the device.

## What "done" means for a module

- Every reference function in the module has a tagged frozen counterpart or an `overrides.json`
  entry saying why not (excluded, diverged with reason).
- The module's faithful count equals its linked count minus recorded divergences.
- The stand-in code it replaced is gone from the tree.
- One T5 trace shows the module's draws and states matching the reference for the harness scene.

## First step

Build T1 and T2 (the corpus and the name registry) and generate `Map.cpp.c` and `MapMem.cpp.c`
from them. That is one session, and it is the session that changes the speed of every one after.
