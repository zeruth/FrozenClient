# Rendering: the road to 100%

A plan, not a status page: `docs/recomp/REPORT.md` is the status, regenerated every measured run.
Every item below names a reference address or a frozen function, so a cycle can start from this
page without re-deriving anything. Rewritten 2026-10-03 from a fresh measurement.

Two standing decisions:

- **Completion first, verification last** (2026-10-01). Runs, traces and scene compares wait until
  the render surface is practically 100% linked and faithful. Until then a cycle is measured by
  the report's static numbers, and the gates that end it are the static ones in CLAUDE.md: a failed
  build, a lost link, a lost binding, a negative delta. Ports stack up unseen and the first runs
  will find defects in batches; every port therefore records what it diverges from and why, in
  the commit and in `overrides.json`. A short look at a run is still taken when a change touches
  what is on screen. It is a look, not verification.
- **The reference is the vanilla `WoW.exe` (12340) in `.reference`, and only that.**
  `RunicWorldGame.exe` is a branded patch of the same binary with four code sites changed
  (0x4da7e5, 0x52abd9, 0x4e0481, 0x7f5f9f); `recomp.GHIDRA_PROGRAM` names the vanilla program.

## What 100% means

The render surface is the reference functions in the modules that draw the world
(`RENDER_MODULES` in `tools/recomp/recomp.py`), 5,143 as of 2026-10-03. Completion is:

1. every one **linked** to a frozen function, none of them a stub;
2. every one **faithful**: the reference's calls in the reference's order, then branch and
   constant shape;
3. `tools/livestubs.py` reporting no empty function with a live render call site, other than the
   ones whose reference bodies are empty too;

and then, last, **verified**: every render-spine function seen behaving like the reference in a
trace or scene compare, with the scene-compare suite at 99% or better.

## Where it stands (measured 2026-10-03 03:07)

| | 2026-10-01 start | 2026-10-02 evening | **2026-10-03** | at completion |
|---|---:|---:|---:|---:|
| render surface linked | 1,267 / 4,838 (26%) | 2,222 / 5,214 (43%) | **2,330 / 5,143 (45%)** | 5,143 |
| render surface faithful | not measured | 1,478 (28%) | **1,537 (30%)** | 5,143 |
| render surface stubs | 20 | 16 | **16** | 0 |
| live empty functions (this platform) | 37 | 17 | **5**, all empty in the reference too | those 5 |
| attributed by anchor guess | 63% | 49% | **48%** | low |
| D3D9 device census (vtable 0x00a2e718, 228 slots) | 199 linked | 227 linked | **228 / 228** | 228 |
| reference-wide mapped / faithful | | 5,825 / 3,350 | **5,994 / 3,439** | |

The denominator moves as anchors are recovered and template instantiations are excluded, so
compare linked counts, not percentages. Nothing has been run since phase 1 (see phase 5).

**What is left, read rather than counted.** The module table puts 2,800 render-surface functions
unlinked. Grouped by phase, after reading the buckets that looked wrong:

| area | phase | unlinked (report) | what it really is |
|---|---|---:|---|
| entities: `Unit_C`, `Player_C`, `GameObject_C`, `Movement`, spell visuals, missiles, effects, vehicles | 4 | 1,859 | the real remaining work, two thirds of it |
| environment: world, map, buildings, liquid, weather, camera, frame | 2 | 470 | fidelity of the roots plus a tail of leaves; see phase 2 |
| textures, effects, base device | 3 | 343 | **about 70 real**; the rest is misattributed (see phase 3) |
| D3D9Ex and OpenGL devices | out of scope | 129 + ~100 misbucketed | not used by this build on Windows |
| models and particles | 1 | 8 | each blocked, with its reason at the code |

Half the surface is still placed in its module by the nearest path string rather than a known
boundary. Read a function's body before trusting a `?` module in the queue; `MODULE_RANGES` in
`recomp.py` is the fix per module.

## How a cycle runs

The loop in CLAUDE.md: `recomp.py`, `--next`, port, build, `refresh-compile-db.bat` if a file was
added, `clangparse.py`, `recomp.py --pdb`, commit with the delta. Rules learned the hard way:

- **Measure the build you made.** `FROZEN_PDB=<repo>/build/bin/Release/Frozen.pdb recomp.py --pdb`
  measures a fresh build without installing it into `build/dist` (which the user's runs use).
  Without it the measurement reads `build/dist/bin/Frozen.pdb`, which may be a day old.
- **`--diff` regenerates the report.** It rewrites `REPORT.md`, `map.json`, `matches.tsv` and
  appends a history row. After a real measurement, commit before diffing; never restore those
  files with `git checkout` once a measurement is in them (2026-10-03 lost one that way and had to
  re-run it).
- **Fix before link.** `--next 20 --fix --render`, then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name. `CGWorldFrame::OnFrameRender` sat at 0% call order for
  want of two tags (`CRenderBatch::QueueCallback`, `CSimpleFrame::OnFrameRender(batch, layer)`);
  tagged, it is 100%.
- **A tag above a variable binds nothing.** Put it on the line directly above the definition.
  `BuildDxtWeights` was untagged for that reason with its address written above its tables.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of callers.
- **Port chains from the top.** Check what feeds a stub before writing it.
- **Check the gate before calling something dead.** A pass that looks unreachable can be on by a
  value in `.data`; read the initial bytes before concluding a system is off.
- **Close a module, then move.** A module at 100% stops feeding false order-matcher links to its
  neighbours.
- **Read every new inferred link.** The order, call-graph and string matchers are confidently
  wrong often enough to matter (`FUN_009a81f0` as `CWorld::GetFarClip`, `FUN_007a03c0` as
  `CMap::MapMemInitializeHeaps`).
- **Resolve register arguments in the asm.** Ghidra drops `ECX` on `__thiscall` helpers, loses x87
  arguments and reuses stack slots. Read the objdump, never guess from the decompilation.
- **A hand verdict needs a read.** An order miss caused by switch layout or inlined templates is
  a verdict in `overrides.json`, written after reading both bodies. `IRsSendToHw` (0x6a4c30) at
  64% is one: every call is there, in a different case order.
- **Check a decoder numerically.** The DXT walker rewrite was proved by building the old and new
  `Blit.cpp` standalone and diffing their output over random blocks. Cheaper than a run, and exact.
- **Record divergence in the same commit**, `status: "diverged"` with the reason.
- **Bisect a visual regression** before reasoning about it: a second worktree at the last good
  commit builds in minutes, and `FROZEN_AUTO_SCREENSHOT` captures the same spot on both.

## Done

- **Phase 0, the ruler** (2026-10-01). `anchors.py` recovers the path-string anchors the export
  dropped; the report prints faithful and stub counts for the surface.
- **Phase 1, models** (2026-10-02). `M2Model` 149/151, `M2Scene` 64/67, `M2Shared`, `M2Light`,
  `ParticleSystem2`, `MapShadow` complete; `M2Cache` 17/20. Seen running: the world loads and draws
  with doodad instancing on. Left, each with its reason at the code or in `overrides.json`: Storm's
  archive byte accounting (`FUN_008245b0`, `FUN_004b57a0`), the `CharacterModelBase` cameras and
  DressUp frame (need `CSimpleModel::SetCameraByID`), tempest helpers owned by `src/world`, `M2Cache`
  `0x81ca10`.
- **Phase 3a, the D3D9 device** (2026-10-01/02, closed 2026-10-03). All 228 vtable slots linked.
  The census leftovers were closed in 466f87b3: slot 21 (`FUN_006a1950`) is `ICaptureReadBlank`, a
  zeroed capture with no caller; the constructor's 0x38-byte fill at +0x3ae0 is
  `m_d3dVertexDecl[14]`; `GxPrimVertexPtr`'s format table now falls back to PN as the reference's
  does. The base `CGxDevice` constructor stays diverged until `CGxCaps` is layout-faithful.
- **Phase 2 landed** (2026-10-02): projected textures, portal views, the map memory order, the
  liquid bank and buffer pool, box, segment and facet queries, the camera (105/128), the cascaded
  shadow map, object placement, MapWeather, the BSP cache, WMO doodads per group, global-WMO maps,
  water ripples, the DayNight light block and sky, the low-detail horizon, the whole of
  `CMap::Render`, the load barriers. The stand-in renderer is gone (4b40fc4d). That evening's fixes
  were seen on screen: lit characters and grass, the outdoor light, buildings, water and grass on
  the reference shaders, interiors through doorways.
- **Phase 3, textures and effects** (2026-10-03, ported, NOT run):

  | commit | what |
  |---|---|
  | 93b727d9 | the full-screen effect system (`src/ffx/`): seven shared targets, the scene copy (`GxTexCopyFromTarget` `FUN_006814d0`), passes, effects; the world glow (`EffectGlow` `FUN_008bfe80`, 35/38); `OnWorldRender` brackets the frame with `FFX::BeginScene` / `EndScene` |
  | c7ac22bc, 1574c59b | the death and nether effects, the fog propagate passes, `EffectSpecial` |
  | 0d14c881 | screen effects from auras: `SetScreenEffect` `FUN_004f7020`, `UpdateScreenEffect` `FUN_004f88b0`, `UpdateGlowParams` `FUN_004f8770`; `ScreenEffect.dbc` |
  | b17eca5b, d1375b5f | `blp` (20/23) in the reference's shape; the texture cache releases component textures |
  | 4d65e308 | the money, item and icon cursors |
  | 5d6ca3a5, 0a9bb469, 9bd3546a, c536b5c3 | the live stubs: glyph eviction, fixed-width steps, window focus, the deferred draw lists, weather caps, `CGxString::SetGradient`, `CalcWrapPointBillboarded` |
  | 04fbbdb8, cba5b06d, 9da3b448 | `tga` (82/82): the TGA writer behind screenshots, every `InitBlit` slot, the DXT aligned and general walkers (checked numerically) |

## Phase 2: the environment (current, NOT closed)

Still open, in order. Phase 2 is not done until every item here is:

1. **Shadows.** No silhouettes reach the ground, from anything. The binds match the reference.
   The cascades' look-at up vectors are never written in the reference (cascade base +0x28, read at
   0x874c60, 0x874d0a, 0x874ec6), so every cascade view is the identity and frozen reproduces that;
   the open question is why the collected casters do not show. Next: one noon run with the dump,
   `FROZEN_FORCE_TIME=12 FROZEN_SHADOW_DUMP=<dir> Frozen.exe 2> shadow.log`, which writes the main,
   lit and cascade maps and logs the light rows and per-pass caster counts. Empty maps point at
   the caster walks; full maps point at the light matrices or the sampling.
2. **Fidelity of the roots** (call order, 2026-10-03): `OnWorldRender` 48%, `UpdateCamera` 57%,
   `DrawLocal` 67%, `WalkPortals` 72%, `CMap::Render` 84%. Already there: `OnFrameRender`,
   `CMap::Update` and `DetailDoodad::CreateInstance` 100%, `CWorld::Update` 94%,
   `MapMemInitialize` 93%. Run `--diff` on each of the five before porting anything: a low score
   is often a helper under its own name, and calls inside lambdas are invisible to the parser.
3. **The tail by module** (unlinked): `World` 70, `Map` 65, `WorldFrame` 54, `DetailDoodad` 44,
   `MapObj` 38, `Camera` 23, `Liquid` 20, `MapWeather` 19, `WorldParam` 17, `AaBsp` 17,
   `MapObjGroup` 16, `MapChunkLiquid` 14, `MapObjRead` 12, `WorldScene` 11, `WorldText` 11, and
   under ten each in the rest. Read before porting:
   - **Misattributed.** "WorldText" 0x7e8000..0x7ea400 is `FFXEffects`; the `WorldFrame` remainder
     is spell and unit work, the spell-target ground decal `FUN_004f8a40` and the per-unit visitor
     `FUN_004f6a40` -> `FUN_0072b350` (phase 4).
   - **Blocked on the leave-world path.** The scene teardown `FUN_00798310` and `CWorld::Destroy`
     `FUN_007837f0` are only reached from `FUN_00406510` (logout to the glue screens), which tears
     down thirty mostly non-render subsystems. Port the chain from there.
   - **Blocked on phase 4.** Footprints (the footstep event `FUN_00723a50`), missile trajectories
     `FUN_006fda20`, the blob-shadow unit box `FUN_0071ed80`.
   - **Dead by construction, not ported:** the object list at 0x00b2eb68 (`FUN_009a80c0`,
     `FUN_009a81f0`) is never added to, and the barrier draw's four models (DAT_00cd85f8) are never
     created. Both are noted at their call sites.
4. **The collision debug overlay's second feeder**, `FUN_007d8840` (phase 4 movement).

## Phase 3: what the 343 are

Read function by function on 2026-10-03, phase 3's modules hold about 70 real unlinked
functions. The rest is in the wrong bucket:

| bucket | unlinked | really |
|---|---:|---|
| `TextureBlob.cpp` | 101 | realm connection, login states, tempest maths, sound. The real TextureBlob is 0x4bfe70..0x4c1140 and is ported |
| `FFXEffects.cpp` | 76 | runs to 0x7f3e00, but past 0x7ead80 it is DayNight: the light DBC loaders, `LightRef`, "SunGlare enabled". The FFX remainder is about ten pass-class deleting destructors and the `Pass*` array resize `FUN_007e7ff0`. `FUN_007ea9b0` reads M2 attachment positions and belongs elsewhere |
| `CGxD3dDevice.cpp`, `CGxDeviceD3d.cpp` | 60 + 13 | the OpenGL device: fixed-function state through `glEnable` and friends, `glTexImage2D` uploads, "Renderer: %s", "VBO KB total". Out of scope |
| `PassGlow.cpp` | 24 | a Win32 file writer (`CreateFile` / `WriteFile`), not glow |
| `ShaderEffectManager.cpp` | 14 | the sound memory cache ("SoundMemoryCache") |
| `TextureCache.cpp`, `Texture.cpp` | 18 + 17 | real, and mostly Storm container instantiations (`TSExplicitList<CACHEENTRY>`, `TSExplicitList<CTexture>`) and deleting destructors: exclude or pin them |
| `CGxDevice.cpp` | 15 | real: the base constructor `FUN_006865b0` (diverged, see 3a), the shader lists, `DeviceAdapterID` / `DeviceAdapterInfo` |
| `blp.cpp`, `EffectGlow.cpp` | 3 + 3 | `blp`: ClientServices around `Data\base.MPQ`; `EffectGlow`: three deleting destructors |

So the phase-3 work left is: give `FFXEffects`, `PassGlow`, `TextureBlob`, `ShaderEffectManager`
and the GL device their `MODULE_RANGES` boundaries, so the counts say what they mean; exclude or
pin the container instantiations; port the base device remainder. Also left from the phase:

- `LoadingScreenInitialize` (`FUN_0040b2b0`) creates the UI shaders now. Its state table
  `FUN_0040b0b0` and the tile grid `FUN_00407c50` are TODOs at the code.
- **`Lightning`** (7 in its bucket, ~45 in practice) is phase 4: it is driven by the spell-visual
  chain effects `FUN_007fca30` -> `FUN_007fae90` -> `FUN_009aafb0`, and has nothing to draw until
  they exist.
- The five live empty functions are empty in the reference too and stay: `M2Init(char)`, the
  generic `M2BlendValue`, `GxPrimUnlockVertexPtrs`, `CGxDevice::ValidateDraw`, `TextureLodBiasSet`.
  The 17 in the GL backends belong to the Android port, where `GxCapsWindowHasFocus` also reads 0
  until something sets the focus field.

Exit: the five misattributed buckets bounded, the container instantiations excluded, the base
device remainder ported. Then the phase-3 rows are at 100% or carry a reason.

## Phase 4: entities (1,859 unlinked, next)

The largest and least anchored phase, last because the phases before it close the modules its
matchers lean on. Top down:

| module | linked | unlinked |
|---|---:|---:|
| `Unit_C` | 98 / 643 | 545 |
| `Player_C` | 54 / 479 | 425 |
| `GameObject_C` | 23 / 285 | 262 |
| `Movement` | 5 / 249 | 244 |
| `MovementShared` | 4 / 83 | 79 |
| `SpellVisuals` | 0 / 65 | 65 |
| `Passenger` | 14 / 77 | 63 |
| `Missile_C` | 3 / 53 | 50 |
| `UnitMissileTrajectory_C` | 2 / 40 | 38 |
| `Effect_C` | 3 / 40 | 37 |
| `ObjectEffect` | 11 / 41 | 30 |
| `CharacterModelBase` | 10 / 24 | 14 |
| `Lightning` | 0 / 7 | 7 |

- **Animation chain top** (`unit-animation-chain-port.md`): `CGUnit_C::SetAnimation`
  `FUN_007385c0` is at 63% and `UpdateAnimation` `FUN_0073ac30` is ported; next are the stand,
  emote, death and movement call sites, the unit model builder `FUN_0073e410`, and `m_animTier`
  via `FUN_007167c0`.
- **The footstep event** `FUN_00723a50`: footprints, camera shakes and footstep sounds. It unblocks
  the footprint module in phase 2.
- **The CEffect list:** `FUN_00745230` creates them and `FUN_006f61d0` has 125 callers. The vehicle
  passenger table blocks three appliers above it.
- **Movement:** unit smooth facing `FUN_00735f60` and input-control facing (the camera's recorded
  boundaries) land here, and so does the collision overlay feeder `FUN_007d8840`.
- **Vehicles:** the vehicle camera (0x759580..0x75af40) and seats.
- **Missiles:** `FUN_006fda20` from `CMap::Render` and `FUN_006fdfb0` from `OnWorldRender`.
- **Game objects and players:** `GameObject_C`'s `UpdateWorldObject` override `FUN_0070cbe0` needs
  the rotation quaternion.
- **Spell visuals, then Lightning:** the chain effects above feed `Lightning`.

Exit: the entity rows at 100% linked and faithful. With them, criteria 1 to 3 are met.

## Phase 5: verification, once

Started when phases 2 to 4 are practically done, in one block:

1. Run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a map-0 character,
   `FROZEN_FORCE_TIME=12` and `FROZEN_AUTO_SCREENSHOT`. Expect faults; `tools/crashstack.py` is the
   debugger. Owed a first look, newest first:
   - phase 3: the world glow (`ffxGlow 1`), the death effect while a ghost, an aura with a
     `ScreenEffect.dbc` entry, a screenshot opening as a valid RLE TGA, the money, item and icon
     cursors, text fading through `SetGradient`, textures on any path that decodes DXT on the CPU;
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
     fixed-function combiners; from phase 1: doodads at a water line, one-bone doodads.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive per-frame
   agreement on the render spine from 58% (2026-09-18) to 100%, marking each function `verified`.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone, a
   WMO interior, underwater, rain, night, a tile edge while it streams) and drive each viewpoint to
   99% or better.
4. Tighten fidelity from call order to branch and constant checks and re-close what drops.

Exit: 100%.

## Order and pace

Phase 2's open items, then the phase-3 bookkeeping (a day at most: ranges, exclusions, the base
device), then phase 4, with `--fix` and `--helpers` batches interleaved because they are the
cheapest links there are. A good day has linked 150 to 350 functions; 2026-10-02 linked 523. The
real remainder is about 2,400, so on the order of eight such days, with the fidelity work on the
roots on top.

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
| 2026-10-02 18:36 | 2,222 / 5,214 | 1,478 | 16 | 17 | phase 2 part 2: queries, MapWeather, Texture.cpp, BSP cache, WMO doodads per group, DayNight light and sky, low-detail horizon, the rest of `CMap::Render`, load barriers; the stand-in renderer is gone |
| 2026-10-03 03:07 | 2,330 / 5,143 | 1,537 | 16 | 5 | phase 3: FFX and the glow, screen effects, blp, texture cache, cursors, live stubs, the TGA writer and blitters, the D3D9 census closed; `OnFrameRender` 0% -> 100% on two tags |
