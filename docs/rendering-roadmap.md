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
(`RENDER_MODULES` in `tools/recomp/recomp.py`): 5,214 as of the last run. Completion is:

1. every one **linked** to a frozen function, none of them a stub;
2. every one **faithful**: the reference's calls in the reference's order, then branch and
   constant shape;
3. `tools/livestubs.py` reporting no empty function with a live render call site;

and then, last, **verified**: every render-spine function seen behaving like the reference in a
trace or scene compare, with the scene-compare suite at 99% or better.

## Where it stands

| | 2026-10-01 start | 2026-10-02 morning (phase 1 done) | **2026-10-02 evening** | at completion |
|---|---:|---:|---:|---:|
| render surface linked | 1,267 / 4,838 (26%) | 1,699 / 5,293 (32%) | **2,222 / 5,214 (43%)** | 5,214 |
| render surface faithful | not measured | 1,149 (22%) | **1,478 (28%)** | 5,214 |
| render surface stubs | 20 | 25 | **16** | 0 |
| live empty functions (this platform) | 37 | 35 | **17** | 0 |
| attributed by anchor guess | 63% | 49% | 49% | low |
| D3D9 device census (vtable 0x00a2e718, 228 functions) | 199 linked | 227 linked, 223 faithful | unchanged | 228 / 228 |

The denominator moves as template instantiations are excluded and anchors are recovered, so
compare linked counts, not percentages.

**The stand-in renderer is gone.** As of 4b40fc4d every call in `CGWorldFrame::OnWorldRender`'s
order and in `CMap::Render` goes to ported reference code; what remains is fidelity inside those
passes and the modules beneath them. None of it has been run since phase 1 (see phase 5).

Remaining work by area (unlinked):

| area | phase | unlinked | notes |
|---|---|---:|---|
| entities (`Unit_C`, `Player_C`, `GameObject_C`, `Movement`, effects, missiles, spell visuals) | 4 | ~1,800 | well over half of what is left |
| world layer and map (see the phase 2 table) | 2 | ~700 | the tail: fidelity and leaves |
| textures and full-screen effects (`blp`, `tga`, `FFXEffects`, glow, `Lightning`) | 3 | ~300 | `Texture.cpp` landed |
| D3D9Ex and OpenGL devices | out of scope | ~130 | not used on Windows |
| models and particles | 1 | ~10 | each blocked, see below |

Half the surface is still placed in its module by the nearest path string rather than a known
boundary, so rows can be mis-sized: read a function's body before trusting a `?` module in the
queue. `MODULE_RANGES` in `recomp.py` is the fix per module. The DayNight light and sky code
(0x7ea000..0x7f4000) is one such unanchored block: 81 of its 159 functions are linked.

## How a cycle runs

The loop in CLAUDE.md: `recomp.py`, `--next`, port, build and install with the PDB,
`refresh-compile-db.bat` if a file was added, `clangparse.py`, `recomp.py --pdb`, commit with
the delta. Rules learned the hard way:

- **Fix before link.** `--next 20 --fix --render`, then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name; a tag costs minutes and lifts every caller.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of
  callers and feeds the matchers.
- **Port chains from the top.** Check what feeds a stub before writing it. The footprints are the
  current example: the draw is easy and its producer is three unported levels down (phase 4).
- **Check the gate before calling something dead.** A pass that looks unreachable can be on by a
  value in `.data`: the load barriers' mode is initialised to 1 in the image, with no code ever
  writing it. Read the initial bytes (a small PE reader in the scratchpad does it) before
  concluding a system is off.
- **Close a module, then move.** A module at 100% stops feeding false order-matcher links to its
  neighbours.
- **Tag the identified-but-not-ported.** A tag or an `overrides.json` entry takes seconds.
- **Read every new inferred link.** The order, call-graph and string matchers are confidently
  wrong often enough to matter. Two found this way: `FUN_009a81f0` linked as
  `CWorld::GetFarClip` (it is an object-list tick), and `FUN_007a03c0` linked as
  `CMap::MapMemInitializeHeaps` (it is the footprint textures, now rejected in `overrides.json`).
- **Resolve register arguments in the asm.** Ghidra drops `ECX` (`this`) on `__thiscall` helpers,
  loses x87 arguments to CRT intrinsics, and reuses stack slots, so which object, list or matrix a
  call works on is read from the dump (`llvm-objdump` into the scratchpad, then grep), never
  guessed from the decompilation.
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
  (0x00a2e718) over 0x681000..0x6ac000: render-state translation, transforms and texgen,
  textures, buffers and pools, lifetime and reset, caps, window and window procedure, scene and
  present with the frame cap, render targets, queries, capture, gamma, vertex declarations,
  stereo, the immediate-mode primitive, both cursors, `SLog`, the W32 time manager and
  `TextureLoadImage`. The device also sends the fixed-function stage states, which the reference
  sky, the barriers and the debug overlays draw through.
- **Phase 1, models** (2026-10-02). `M2Model` 149/151, `M2Scene` 64/67, `M2Shared`,
  `ParticleSystem2`, `M2Light` and `MapShadow` all complete, `M2Cache` 17/20. Seen running: the
  world loads and draws with doodad instancing on. Left, each with its reason at the code or in
  `overrides.json`: Storm's archive byte accounting (`FUN_008245b0`, `FUN_004b57a0`); the
  `CharacterModelBase` cameras and DressUp frame (need `CSimpleModel::SetCameraByID`); tempest
  helpers owned by `src/world` (`0x9838d0`, `0x983940`, `0x983ae0`, `0x983fb0`); `M2Cache`
  `0x81ca10`.

### Phase 2, the environment: what landed (2026-10-02)

| commit | what |
|---|---|
| 74322478 | projected textures go live; the eleven world console commands |
| 76579549 | the portal view lists and the far-plane fill over them |
| f0d41af2 | `MapMemInitialize` in the reference's order, the loading-screen load loop |
| 3c988667 | liquid material bank and chunk buffer pool |
| 32a8286c | the map-object box queries: blob-shadow WMO and M2 receivers |
| e6fdb71f | **fix**: shadowed terrain shaders load after `MapMemInitialize` clears its slots (found by bisect; seen running) |
| f3bb2d87 | the camera (`Camera.cpp`, now 105/128): views, zoom, free look, smoothing, collision, shakes, the model camera, the 22 Lua functions |
| 989e7731 | the shadow map to the reference's architecture: main map, lit pass, three cascades, hardware-PCF depth maps |
| 4decbbdc | objects place their map entity (`CWorld::UpdateObject`, `CMap::UpdateEntity`, WMOAreaTable.dbc) |
| 6f568a9c, bdf9e007 | the world segment query (buildings, terrain cells, liquid, ray models) and the frustum facet query; the camera sweep; the WorldParam callbacks |
| 55fccb0d | **MapWeather** (69/88), replacing frozen's own weather; underwater particulates replace the overlay stand-in |
| 4514898a, 8ec44351 | the BSP node cache and the three cached leaf queries; the ground height and group liquid segment queries |
| ba845923, f90c5c5a | global-WMO maps load; `CMap::Update` to the reference; the camera building lookup's order |
| 828ea806 | water ripples; the liquid initialise to the reference |
| 20539963, bc4160d2 | horizon occluder edges into the distance rows; doodads and entities of visible building groups through the portal frusta |
| 18de6361 | WMO doodads made per group as the reference does, retiring the cull stand-in |
| 9e8cc99d, 1b4785ae, a0eb22a6 | **Texture.cpp** (105/122): atlases, the GxTex reuse cache, async BLP, texture blobs, load progress |
| 9d377066, d5dd48de, 9a4e6022 | `CMap::Render`: unseen frame entities, the indoor candidate-group pass, model fade-outs (`SWModelFadeout`) |
| 828539e7, 8cdf5458, 73ea1c39 | **the DayNight light block** (`DayNightLight.cpp`, `FUN_007f3230`): band interpolation, area lights, fog, sun direction and bodies; models lit from it through `CMapLight` |
| f5bfe91f | **the DayNight sky** (`DayNightSky.cpp`): stars, dome, bodies with the horizon clip, clouds with the reference noise table, glares with the occlusion query, skybox models. The stand-in sky and clouds are deleted |
| d6dd5f1e | **the low-detail horizon**: WDL tiles and far buildings past the far clip (`FVBBList`, `CMapAreaLow`, `CWorldScene::RenderLowDetail`) |
| b1dd0673, c39ec12e | the end and top of `CMap::Render`: cursor image, weather box, the collision debug overlay (enable 0x200000), the frame flags, the portal view reset |
| 4b40fc4d | **the load barriers** (`FUN_00794b50` and the system feeding it): walls near unstreamed tile edges and loading buildings and doodads. The last missing call in `OnWorldRender` |

Lessons from these, kept because they change how the next one is done:

- **Ground textures broke in a merge batch; bisect found it, not reading.** 15f2abfb moved
  `CreateTerrainShadowShaders` above the loop that nulls the Terrain2/Terrain3 arrays. Fixed in
  e6fdb71f and seen running.
- **"The ground goes fully shadowed" was the missing cascades.** At quality 3 the terrain shaders
  sample four maps through twelve light-matrix rows at c37; the stand-in bound one. Fixed by
  989e7731.
- **The ported shadow map casts nothing visible yet.** Units now have map positions (4decbbdc).
  The cascades' look-at up vectors are never written in the reference (cascade base +0x28, read
  at 0x874c60, 0x874d0a, 0x874ec6), so `FUN_006c0050` fails its `up.SquaredMag() >= 0.01f` check
  and every cascade view is the identity; frozen reproduces that. Open: why the collected casters
  do not show on the ground. Next step: force `hwPCF 0` so the maps are R32F, dump one, and
  compare the depth written with the depth the terrain shader computes.
- **What frozen called occluders were load barriers.** `FUN_007946d0` had been ported as an
  occluder box measured from the camera; it feeds the barrier wall and measures from the active
  mover. The surrounding notes were corrected in 4b40fc4d.
- **Dead by construction, not ported:** the object list at 0x00b2eb68 (`FUN_009a80c0`,
  `FUN_009a81f0`) is never added to anywhere in the binary, and the barrier draw's four models
  (DAT_00cd85f8) are never created. Both are noted at their call sites.

## Phase 2: closed out (2026-10-02 evening)

The environment systems are all ported and drawing. The evening's run of fixes, each seen on screen
unless marked:

| commit | what |
|---|---|
| d0d4b29f | characters lit (`s_characterAmbient` started at 0; the per-zone ease ported), grass light (`CMapChunk::SelectLights`), the unit light fade, the world depth range `[0, 0.94]`, vertical mouse look |
| 7bef8e80 | terrain: the outdoor light whole (the stand-in direction was inverted), the block's fog, the real fog caps |
| a931bc01 | buildings, water and grass on the reference shaders and light; black grass; the garbage triangles (a grass batch kept the device stream buffers); interiors through doorways (the portal overlap test crossed its axes) |
| 29ba6ec5 | grass darkened in the chunk's baked shadow (built, not run) |
| 77c93791 | `FROZEN_SHADOW_DUMP`, the shadow-map dump, rebuilt for the cascaded maps |
| 736262ae | the M2 scene animates inside `CMap::Render`; the frame's own visibility loops are gone (built, not run: watch for a unit not drawing) |

What the per-module tables still list is, read function by function, mostly not phase 2:

- **Misattributed by the anchor guess.** "WorldText" 0x7e8000..0x7ea400 is `FFXEffects` (fog
  propagate, death, nether blur) -- phase 3. The `WorldFrame` remainder is spell and unit work: the
  spell-target ground decal `FUN_004f8a40` (cursor mode `DAT_00ac79a4`, texture `DAT_00b74350`) and
  the per-unit visitor `FUN_004f6a40` -> `FUN_0072b350` -- phase 4. Give these modules
  `MODULE_RANGES` entries rather than trusting the counts.
- **Blocked on the client's leave-world path.** The scene teardown `FUN_00798310` and
  `CWorld::Destroy` `FUN_007837f0` are only reached from `FUN_00406510` (logout to the glue
  screens), which tears down thirty mostly non-render subsystems. Port the chain from there.
- **Blocked on phase 4.** Footprints (the footstep event `FUN_00723a50`), missile trajectories
  `FUN_006fda20`, the blob-shadow unit box `FUN_0071ed80`.

Carried forward, in order:

1. **Shadows.** No silhouettes reach the ground, from anything. The binds match the reference.
   Next: one noon run with the dump --
   `FROZEN_FORCE_TIME=12 FROZEN_SHADOW_DUMP=<dir> Frozen.exe 2> shadow.log` -- which writes the
   main, lit and cascade maps and logs the light rows and per-pass caster counts. Empty maps point
   at the caster walks; full maps point at the light matrices or the sampling.
2. **Fidelity of the roots** (call order): `DetailDoodad::CreateInstance` 8%,
   `OnWorldUpdate` 18%, `MapMemInitialize` 26%, `OnWorldRender` 28%, `WalkPortals` 44%,
   `CMap::Update` 49%, `UpdateCamera` 50%, `DrawLocal` 67%, `CWorld::Update` 69%. Note that the
   two building caster walks scored 9% and 29% while being complete: their calls sit inside
   lambdas the parser does not see into, so read before trusting a low score.
3. **The collision debug overlay's second feeder**, `FUN_007d8840`.


## Phase 3: textures and effects (~300 unlinked)

- **Textures:** `Texture.cpp` is in (105/122). Left: `blp` 40/94 and `tga` 46/82 to 100%,
  `TextureAllocGxTex` at 62%, `AsyncFileReadWait` at 67%, the `TextureCache` mirror.
- **Full-screen effects:** `FFXEffects` 81/171, `EffectGlow` 2/38, `PassGlow` 0/26,
  `Lightning` 0/7; the world-frame glow calls (`FUN_004f8770`, `FUN_008c1770`, `FUN_008c1010`)
  are where it enters the frame.
- **What the D3D9 census left:** a read of `GxPrimVertexPtr` (0x682400) and `IRsSendToHw`
  (0x6a4c30) for a verdict, the zero fill at +0x3ae0 in the constructor (0x68fd50), vtable slot 21
  (0x6a1950), the base constructor recorded as diverged until `CGxCaps` is layout-faithful, and
  the money, object and spell item cursors (`FUN_00616510`, `FUN_00616630`, `FUN_00616720`).
- **Live stubs to zero:** 17 on this platform from `livestubs.py`, each ported or recorded as a
  deliberate divergence. The 17 in the GL backends belong to the Android port.

Exit: every texture and effect module at 100% linked, no stubs.

## Phase 4: entities (~1,800 unlinked)

The largest and least anchored phase, last because the phases before it close the modules its
matchers lean on. Top down:

- **Animation chain top** (`unit-animation-chain-port.md`): `CGUnit_C::SetAnimation`
  `FUN_007385c0` is at 63% and `UpdateAnimation` `FUN_0073ac30` is ported; next are the stand,
  emote, death and movement call sites, the unit model builder `FUN_0073e410`, and `m_animTier`
  via `FUN_007167c0`.
- **The footstep event** `FUN_00723a50`: footprints, camera shakes and footstep sounds. It
  unblocks the footprint module in phase 2.
- **The CEffect list:** `ObjectEffect` (11/41); `FUN_00745230` creates them and `FUN_006f61d0`
  has 125 callers. The vehicle passenger table blocks three appliers above it.
- **Movement:** `MovementShared` (4/83) and `Movement` (5/249). Unit smooth facing
  `FUN_00735f60` and input-control facing (the camera's recorded boundaries) land here.
- **Vehicles:** the vehicle camera (0x759580..0x75af40) and seats.
- **Missiles:** `UnitMissileTrajectory_C` (1/40, including `FUN_006fda20` from `CMap::Render`
  and `FUN_006fdfb0` from `OnWorldRender`) and `Missile_C` (3/53).
- **Game objects and players:** `GameObject_C` (23/285; its `UpdateWorldObject` override
  `FUN_0070cbe0` needs the rotation quaternion), `Player_C` (54/479), `Unit_C` (98/643).
- **Spell visuals:** `SpellVisuals` (0/65).

Exit: the entity row at 100% linked and faithful. With it, criteria 1 to 3 are met.

## Phase 5: verification, once

Started when phases 1 to 4 are practically done, in one block:

1. Run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a map-0 character,
   `FROZEN_FORCE_TIME=12` and `FROZEN_AUTO_SCREENSHOT`. Expect faults; `tools/crashstack.py` is
   the debugger. Owed a first look, newest first:
   - the load barriers: walls that never go away would mean a doodad's or building's 0x80
     "set up" flag is not being set;
   - the low-detail horizon past the far clip;
   - the DayNight sky (stars, dome, sun and moon with the horizon clip, clouds, glare) and the
     light block it shares with models and fog, at noon and at dusk;
   - model fade-outs; WMO doodads per group; global-WMO maps; MapWeather in rain and snow;
   - the texture module (async BLP, atlases, the reuse cache);
   - the camera beyond the default view, now that the segment query exists for its collision;
   - projected textures on something that projects; the liquid bank and buffer pool at a water
     line; units standing in water or a building;
   - from phase 3a: the device cursor, resize and alt-tab, the QPC timer, the 200/30 FPS caps,
     fixed-function combiners; from phase 1: doodads at a water line, one-bone doodads, DXT
     textures with their smallest mips.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive per-frame
   agreement on the render spine from 58% (2026-09-18) to 100%, marking each function `verified`.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone,
   a WMO interior, underwater, rain, night, a tile edge while it streams) and drive each viewpoint
   to 99% or better.
4. Tighten fidelity from call order to branch and constant checks and re-close what drops.

Exit: 100%.

## Order and pace

Phases 2 to 4 in that order, each closed before the next so the matchers stop guessing, with
`--fix` and `--helpers` batches interleaved because they are the cheapest links there are. A good
day has linked 150 to 350 functions; 2026-10-02 linked 523. The ~3,000 left is on the order of
ten such days, with the fidelity work on the roots on top.

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
| 2026-10-02 18:36 | 2,222 / 5,214 | 1,478 | 16 | 17 | phase 2 part 2: segment and facet queries, MapWeather, Texture.cpp, BSP cache, WMO doodads per group, the DayNight light and sky, the low-detail horizon, the rest of `CMap::Render`, the load barriers; the stand-in renderer is gone |
