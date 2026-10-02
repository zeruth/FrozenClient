# Rendering: the road to 100%

A plan, not a status page: `docs/recomp/REPORT.md` is the status, regenerated every cycle. Every
item below names a reference address or a frozen function so the next cycle can start from this
page without re-deriving anything.

Two standing decisions:

- **Completion first, verification last** (2026-10-01). Runs, traces and scene compares wait until
  the render surface is practically 100% linked and faithful. Until then a cycle is measured by
  the report's static numbers, and the gates that end it are the static ones in CLAUDE.md (a
  failed build, a lost link, a lost binding, a negative delta). The cost is accepted: ports stack
  up unseen, and the first runs find defects in batches. Every port therefore records what it
  diverges from and why, in the commit and in `overrides.json`. A short look at a run is still
  taken when a change touches what is on screen; it is a look, not verification.
- **The reference is the vanilla `WoW.exe` (12340) in `.reference`, and only that.** Until
  2026-10-02 the tooling read `RunicWorldGame.exe`, a branded patch of the same binary with four
  code sites changed (0x4da7e5, 0x52abd9, 0x4e0481, 0x7f5f9f). `recomp.GHIDRA_PROGRAM` names the
  vanilla program and all exported data comes from it.

## What 100% means

The render surface is the reference functions in the modules that draw the world
(`RENDER_MODULES` in `tools/recomp/recomp.py`): 5,246 as of the last run. Completion is:

1. every one **linked** to a frozen function, none of them a stub;
2. every one **faithful**: the reference's calls in the reference's order, then branch and
   constant shape;
3. `tools/livestubs.py` reporting no empty function with a live render call site;

and then, last, **verified**: every render-spine function seen behaving like the reference in a
trace or scene compare, with the scene-compare suite at 99% or better.

## Where it stands

| | 2026-10-01 start | 2026-10-02, after phase 1 | 2026-10-02, phase 2 in progress | at completion |
|---|---:|---:|---:|---:|
| render surface linked | 1,267 / 4,838 (26%) | 1,699 / 5,293 (32%) | **1,897 / 5,246 (36%)** | 5,246 |
| render surface faithful | not measured | 1,149 (22%) | **1,258 (24%)** | 5,246 |
| render surface stubs | 20 | 25 | 25 | 0 |
| render surface attributed by anchor guess | 63% | 49% | 49% | low |
| D3D9 device census (vtable 0x00a2e718, 228 functions) | 199 linked | 227 linked, 223 faithful | unchanged | 228 / 228 |

The denominator moves as template instantiations are excluded and anchors are recovered, so
compare linked counts, not percentages.

Remaining work by area (unlinked; the areas are module groups from the report):

| area | phase | unlinked | notes |
|---|---|---:|---|
| entities (`Unit_C`, `Player_C`, `GameObject_C`, `Movement`, `Passenger`, effects, missiles, spell visuals) | 4 | ~1,840 | half of everything left |
| world layer, map streaming, map geometry, liquid, shadows (see the phase 2 table) | 2 | ~750 | in progress |
| textures and full-screen effects (`Texture*`, `blp`, `tga`, `FFXEffects`, glow, `Lightning`) | 3 | ~560 | |
| D3D9Ex and OpenGL devices | out of scope | ~130 | not used on Windows |
| models and particles | 1 | 29 | each blocked, see below |

Half the surface is still placed in its module by the nearest path string rather than a known
boundary, so rows can be mis-sized: read a function's body before trusting a `?` module in the
queue. `MODULE_RANGES` in `recomp.py` is the fix per module.

## How a cycle runs

The loop in CLAUDE.md: `recomp.py`, `--next`, port, build and install with the PDB,
`refresh-compile-db.bat` if a file was added, `clangparse.py`, `recomp.py --pdb`, commit with
the delta. Rules learned the hard way:

- **Fix before link.** `--next 20 --fix --render`, then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name; a tag costs minutes and lifts every caller.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of
  callers and feeds the matchers.
- **Port chains from the top.** Check what feeds a stub before writing it.
- **Close a module, then move.** A module at 100% stops feeding false order-matcher links to its
  neighbours.
- **Tag the identified-but-not-ported.** A tag or an `overrides.json` entry takes seconds.
- **Read every new inferred link.** The order, call-graph and string matchers are confidently
  wrong often enough to matter.
- **Resolve register arguments in the asm.** Ghidra drops `ECX` (`this`) on `__thiscall` helpers
  and reuses stack slots, so which frustum, list or matrix a call works on is read from the dump
  (`llvm-objdump` into the scratchpad, then grep), never guessed from the decompilation. The
  shadow-map port depended on this at a dozen call sites.
- **A hand verdict needs a read.** A branch or order miss caused by inlined templates is a
  verdict in `overrides.json`, written after reading the port against the decompilation. Merge
  into an existing entry; replacing one can drop its `frozen` link.
- **Record divergence in the same commit**, `status: "diverged"` with the reason.
- **Bisect a visual regression** before reasoning about it: a second worktree at the last good
  commit builds in minutes, and `FROZEN_AUTO_SCREENSHOT` captures the same spot on both.

## Done

- **Phase 0, the ruler** (2026-10-01). `tools/recomp/anchors.py` recovers the path-string
  anchors the export dropped, 19 modules joined `RENDER_MODULES`, and the report prints faithful
  and stub counts for the surface.
- **Phase 3a, the D3D9 device** (2026-10-01/02). Closed by a census of the device's vtable
  (0x00a2e718) over 0x681000..0x6ac000. Render-state translation (`IRsSendToHw`, `DsSet`),
  transforms and texgen, textures, buffers and pools, device lifetime and reset, caps, window and
  window procedure, scene and present with the frame cap, render targets, queries, capture,
  gamma, vertex declarations, stereo, the immediate-mode primitive, both cursors, `SLog`, and on
  its path the W32 time manager and `TextureLoadImage`.
- **Phase 1, models** (2026-10-02; 29 left of 240). Model cache with the animate thread,
  `CM2Scene::Animate` and the doodad grouping, `CM2Scene::Draw`, batched particles, draw
  callbacks, projected decals, the instanced shadow-caster draw, CPU skinning, the particle
  emitter clone path and model particles, `CM2Model` queries and bounds, `CharacterModelBase`.
  Seen running: the world loads and draws with doodad instancing on. The first run found two bugs
  (a frame freeze from the old caster pass, exploding instanced doodads from
  `CM2Shared::SetIndices`), both fixed.

  Left, each with its reason at the code or in `overrides.json`: model and texture byte
  accounting through Storm's archive internals (`FUN_008245b0`, `FUN_004b57a0`); the
  `CharacterModelBase` cameras and DressUp frame (need `CSimpleModel::SetCameraByID` and a
  frame-owned `CCharacterComponent`); tempest helpers owned by `src/world` (`0x9838d0`,
  `0x983940`, `0x983ae0`, `0x983fb0`); `M2Cache` `0x81ca10`. Branch ratios to read with `--diff`:
  `Animate` 0.37, `CM2SceneRender::Draw` 0.31, `SetupLighting` 78% call order.

## Phase 2: the environment (current)

### Landed (2026-10-02)

| commit | what |
|---|---|
| 74322478 | projected textures go live (`CWorld::ProjectionCallback` `FUN_0077f500`, `DecalDrawBoundReceivers` `FUN_007e3aa0`); the eleven world console commands |
| 76579549 | the portal view lists and the far-plane fill over them |
| f0d41af2 | `MapMemInitialize` in the reference's order, the loading-screen load loop and `AsyncFileReadWaitAll`; chunks release their doodad defs; occlusion clear and polygon test |
| 3c988667 | liquid: the material bank as the reference builds it, the chunk buffer pool |
| 32a8286c | the map-object box queries: blob-shadow WMO receivers (`FUN_007a6940` .. `FUN_007ca920`), M2 receivers `FUN_007a2aa0` |
| e6fdb71f | **fix**: the shadowed terrain shaders load after `MapMemInitialize` clears its slots (see below) |
| f3bb2d87 | **the camera** (`Camera.cpp`, 0x5fd630..0x607b00, 104 of 128 linked): views, zoom and timed moves, free look, smoothing, bobbing, terrain tilt, target heights, collision, shakes (CameraShakes.dbc), the model camera, the 22 Lua functions |
| 989e7731 | **the shadow map** to the reference's architecture: `ShadowMap.cpp` and `MapShadow.cpp` (main map, lit pass, three amortised cascades, hardware-PCF depth maps, the four callbacks, the five caster walks, the binds) |
| 4decbbdc | **objects place their map entity**: `CGObject_C::UpdateWorldObject` and the unit override, `CWorld::UpdateObject` `FUN_00780240`, `CMap::UpdateEntity` `FUN_007a1bc0` with its liquid, zone and baked-shadow helpers, WMOAreaTable.dbc |

What these runs showed:

- **Ground textures broke in the merge batch, and the cause was found by bisect, not by
  reading.** The camera and projected textures were cleared first, by A/B runs. 15f2abfb alone
  reproduced it: it moved `CreateTerrainShadowShaders` above the loop in `MapMemInitialize` that
  nulls the Terrain2/Terrain3 arrays. The reference clears its slots at 0x0079e8b6..0x0079e974 and
  loads the shadowed sets at 0x0079e979, after the clearing. Every shadowed terrain shader fell
  back to the unshadowed one while the pass kept its shadowed setup, so chunks drew the wrong
  layers once shadows came on a few frames in. Fixed in e6fdb71f and seen running: the road
  matches the pre-merge build.
- **"The ground goes fully shadowed" was the missing cascades.** At quality 3 the terrain shaders
  sample four maps (stage 5 plus three cascades on 6..8) through twelve light-matrix rows at c37;
  the stand-in bound one map and zeroed nine rows, so three taps of four read as occluded.
  989e7731 replaces the stand-in, and the ground no longer darkens.
- **The ported shadow map runs but casts nothing visible yet.** Two causes were found and one
  fixed. Units had no map position (fixed in 4decbbdc; the lit pass now collects the units near
  the player, 13 model batches where it had 0). And the cascades' look-at up vectors are never
  written in the reference (cascade base +0x28, read at 0x874c60, 0x874d0a, 0x874ec6), so
  `FUN_006c0050` fails its `up.SquaredMag() >= 0.01f` check and every cascade view is the
  identity; frozen reproduces that and the cascades collect nothing. What is still open is why
  the collected casters do not show on the ground: the device view and the sampling matrices
  agree, and a hardware-PCF depth map cannot be read back to look. Next step when this is picked
  up again: force `hwPCF 0` so the maps are R32F, dump one, and compare the depth written with the
  depth the terrain shader computes.

### Owed a look at a run

- The camera beyond the default follow view: zoom, mouse look, view switching, the model camera
  in a cinematic. It does not collide yet, because the world segment query is not ported (below).
- Projected textures (`projectedTextures 1` is in the test config) on something that projects.
- The liquid material bank and buffer pool at a water line.
- Units standing in water or a building: `CMap::UpdateEntity` now sets their liquid and light
  targets, and nothing has looked at a unit there.

### Left, by module

From the 2026-10-02 13:49 run. "Left" is reference functions with no frozen link.

| module | ref | linked | left | next |
|---|---:|---:|---:|---|
| `World.cpp` | 138 | 21 | 117 | the world segment query `FUN_0077f310` -> `FUN_007a3b70` (map objects `FUN_007a30d0`, terrain cells `FUN_007a39f0` / `FUN_007a3570`); the frustum facet query `0x77f8d0` -> `FUN_007ad700`. The camera's collision waits on both |
| `WorldMap.cpp` | 120 | 25 | 95 | read the bodies first: much of this may be the world map UI rather than rendering |
| `Map.cpp` | 150 | 61 | 89 | the unfaithful roots below, then `--next --module Map.cpp` |
| `MapWeather.cpp` | 96 | 26 | 70 | **next up**: the module at 0x783b90..0x78d610 replaces frozen's `Weather.cpp`. Its classes (ground-height cache, rain/patter/snow/sand packets, mist sheets, the four archived vertex shaders) are already worked out in `src/world/MapWeather.hpp` on branch `worktree-agent-ab711d744fbc42c0d` |
| `WorldFrame.cpp` | 69 | 9 | 60 | `CGWorldFrame::OnWorldUpdate` 16% faithful; the frame's per-object placement loop is still frozen's own |
| `DetailDoodad.cpp` | 134 | 85 | 49 | `CreateInstance` 8%; the shader path (`FUN_00874760` is ported and waits on `s_useShaders`) |
| `MapObj.cpp` | 79 | 35 | 44 | `WalkPortals` 39% |
| `AaBsp.cpp` | 48 | 8 | 40 | the BSP the segment query walks |
| `WorldText.cpp` | 35 | 4 | 31 | floating combat and name text |
| `WorldParam.cpp` | 70 | 40 | 30 | the stubbed CVar callbacks (footstep bias, alpha bit depth, ground effect density and distance, specular, base mip) |
| `MapObjGroup.cpp` | 68 | 43 | 25 | |
| `MapChunkLiquid.cpp` | 70 | 48 | 22 | the WMO liquid mesh factory `FUN_007d43f0` and its writers |
| `WorldScene.cpp` | 47 | 26 | 21 | occluders `FUN_00796c10`, barriers `FUN_00794b50` |
| `Liquid.cpp` | 89 | 69 | 20 | the post-liquid pass `0x00790a80` with decals `FUN_0079d5e0` |
| `MapObjRead.cpp` | 36 | 18 | 18 | |
| `MapLowDetail.cpp` | 37 | 23 | 14 | low-detail terrain `FUN_007cc810` |
| `ShaderEffectManager.cpp` | 23 | 9 | 14 | |
| `MapMem.cpp` | 87 | 73 | 14 | |
| `MapLoad.cpp` | 15 | 8 | 7 | |
| `ShadowMap.cpp` / `MapShadow.cpp` | 56 | 50 | 6 | the device-restore hook needs a registry frozen does not have |
| `MapChunk.cpp`, `MapArea.cpp`, `ShaderEffect.cpp` | 62 | 55 | 7 | |

Work that spans modules:

- **Unfaithful roots:** `CMap::Update` 11%, `CMap::Render` 42%, `CWorldScene::UpdateCamera` 56%,
  `CMapObj::WalkPortals` 39%, `CWorldScene::SubmitOccluderBox` 17%, `CGWorldFrame::OnWorldUpdate`
  16%, `DetailDoodad::CreateInstance` 8%, `CMap::UpdateMapObjDefs` 53%,
  `CMapRenderChunk::DrawLocal` 67%.
- **Terrain through the original's constant setup** (`parity-map-memory.md`): `FUN_007cfbe0` and
  the permutation selection, and `MapMemInitialize` to 100% (`FUN_007c3d90`, `FUN_007afee0`,
  `FUN_007cb990`, `FUN_007b2760`, `FUN_007a03c0`, the pools and heaps). This also retires table
  fog in `IStateSetD3dDefaults`, the device's last frozen-only state.
- **Stages with no port at all:** footprints `FUN_0079fcc0`, the fog override `FUN_007ed820`, the
  per-frame sky override at `0x007f0573`, and the glare pair `FUN_007f3230` / `FUN_007eecc0` (tag
  or port; `DrawGlare` exists).
- **Blob shadows** (`parity-shadows.md`): the oriented-rectangle footprint and unit box
  `FUN_0071ed80`, doodad receivers `FUN_007ce960`.
- **Dependency boundaries the camera recorded** (`src/ui/game/CameraDeps.cpp`): unit smooth
  facing `FUN_00735f60`, input-control facing `FUN_005fb260` / `FUN_005fbe70`, the vehicle camera
  (0x759580..0x75af40). The last two belong to phase 4; the segment query is phase 2 (above).

Exit: every map, liquid, shadow, sky and world-layer module at 100% linked and faithful.

## Phase 3: textures and effects (~560 unlinked)

- **Texture async** (`parity-texture-async.md`): `AsyncTextureWait` `FUN_004b6550`,
  `TextureIncreasePriority` `FUN_004b6c50`, `SFile::IsStreamingMode`, `CTextureAtlas`, the
  `CreateBlpAsync` and `CreateTgaTexture` stubs, `TextureAllocGxTex` 25%, `AsyncFileReadWait`
  56%, `TextureCache` (`MirrorInitialize` is a stub), `blp` and `tga` to 100%.
- **Full-screen effects:** `FFXEffects` (171, 17 linked), `EffectGlow` (38, 2), `PassGlow` (26,
  0); the glow itself (`FUN_004f8770`, `FUN_008c1770`, `FUN_008c1010`, `FUN_008c1100`).
- **What the D3D9 census left:** a read of `GxPrimVertexPtr` (0x682400) and `IRsSendToHw`
  (0x6a4c30) for a verdict, the zero fill at +0x3ae0 in the constructor (0x68fd50), vtable slot 21
  (0x6a1950), the base constructor recorded as diverged until `CGxCaps` is layout-faithful, and
  the money, object and spell item cursors (`FUN_00616510`, `FUN_00616630`, `FUN_00616720`).
- **Live stubs to zero:** the 35 from `livestubs.py` (`M2Init`, `CClientEnvironment::AddRef`,
  `CM2Cache::GarbageCollect`, `M2BlendValue`, the `CGxFont` pair), each ported or recorded as a
  deliberate divergence.

Exit: every texture and effect module at 100% linked, no stubs.

## Phase 4: entities (~1,840 unlinked)

Half of everything left, and last, because it is the largest and least anchored and the phases
before it close the modules its matchers lean on. Top down:

- **Animation chain top** (`unit-animation-chain-port.md`): `CGUnit_C::SetAnimation`
  `FUN_007385c0` (63%, 57 callers), `UpdateAnimation` `FUN_0073ac30` wired to the stand, emote,
  death and movement sites, the unit model builder `FUN_0073e410`, `m_animTier` via
  `FUN_007167c0`.
- **The CEffect list:** `ObjectEffect` (41, 10 linked); `FUN_00745230` creates them and
  `FUN_006f61d0` has 125 callers. The vehicle passenger table blocks three appliers above it.
- **Movement:** `MovementShared` (83, 4 linked) and `Movement` (249, 5). Unit smooth facing and
  input-control facing (the camera's boundaries) land here.
- **Vehicles:** the vehicle camera and seats. The camera port and `CGUnit_C::UpdateWorldObject`
  already call into them through recorded boundaries.
- **Missiles:** `UnitMissileTrajectory_C` and `Missile_C` (93, 4 linked).
- **Game objects and players:** `GameObject_C` (285, 21; its `UpdateWorldObject` override
  `FUN_0070cbe0` needs the rotation quaternion), `Player_C` (479, 54), `Unit_C` (643, 98).
- **Spell visuals:** `SpellVisuals` (65, none linked).

Exit: the entity row at 100% linked and faithful. With it, criteria 1 to 3 are met.

## Phase 5: verification, once

Started when phases 1 to 4 are practically done, in one block:

1. Run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a map-0 character,
   `FROZEN_FORCE_TIME=12` and `FROZEN_AUTO_SCREENSHOT`. Expect faults; `tools/crashstack.py` is
   the debugger. Owed a first look, newest first: everything under "Owed a look" in phase 2; the
   device cursor; resize and alt-tab (the reset path lost two frozen workarounds); the QPC timer;
   character creation refusing invalid names; doodads at a water line; one-bone doodads; DXT
   textures with their smallest mips; fixed-function combiners; the 200/30 FPS caps.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive per-frame
   agreement on the render spine from 58% (2026-09-18) to 100%, marking each function `verified`.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone,
   a WMO interior, underwater, rain, night) and drive each viewpoint to 99% or better.
4. Tighten fidelity from call order to branch and constant checks and re-close what drops.

Exit: 100%.

## Order and pace

Phases 2 to 4 in that order, each closed before the next so the matchers stop guessing, with
`--fix` and `--helpers` batches interleaved because they are the cheapest links there are. A good
day has linked 150 to 350 functions, so the ~3,350 left is on the order of fifteen such days,
with the fidelity work on the roots on top.

## Progress log

One line per measured run: render surface linked / total, faithful, stubs, live empty functions.
The commit log has the detail.

| run | linked | faithful | stubs | live empty | what moved |
|---|---|---|---|---|---|
| 2026-10-01 start | 1,267 / 4,838 | n/a | 20 | 37 | baseline |
| 2026-10-01 | 1,398 / 5,327 | 869 | 32 | 37 | phase 0: recovered anchors grew the denominator |
| 2026-10-01 | 1,439 / 5,390 | 907 | 32 | 37 | ParticleColor.dbc override end to end; `ParticleSystem2` given its range |
| 2026-10-01 | 1,446 / 5,390 | 924 | 32 | 37 | `AnimateMT`, `InitializeLoaded`, `ReplaceTexture`, the liquid plane, `SelectLights` |
| 2026-10-01 | 1,496 / 5,390 | 959 | 32 | 37 | phase 3a: render states, transforms, textures, buffers, frame cap, caps |
| 2026-10-01 | 1,575 / 5,377 | 1,054 | 33 | n/m | phase 3a closed: device census, cursor, window procedure, hand verdicts |
| 2026-10-02 | 1,575 / 5,377 | 1,054 | 33 | 35 | reference switched to the vanilla `WoW.exe` |
| 2026-10-02 | 1,699 / 5,293 | 1,149 | 25 | n/m | phase 1: models 240 -> 29 unlinked; doodad instancing live |
| 2026-10-02 | 1,897 / 5,246 | 1,258 | 25 | n/m | phase 2 part 1: projected textures, portal views, map memory order, liquid bank and pool, box queries, the camera, the shadow map, object placement |
