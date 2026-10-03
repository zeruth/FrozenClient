# Rendering: the road to 100%

A plan, not a status page: `docs/recomp/REPORT.md` is the status, regenerated every measured run.
Every item below names a reference address or a frozen function, so a cycle can start from this
page without re-deriving anything. Rewritten 2026-10-03 (evening) from a fresh measurement.

Two standing decisions:

- **Completion first, verification last** (2026-10-01). Formal verification (traces, scene
  compares, `verified` in `overrides.json`) waits until the render surface is practically 100%
  linked and faithful. Until then a cycle is measured by the report's static numbers, and the
  gates that end it are the static ones in CLAUDE.md: a failed build, a lost link, a lost binding,
  a negative delta. Every port records what it diverges from and why, in the commit and in
  `overrides.json`. **Phase 4 changed the practice, not the rule:** entities cannot be judged
  without moving among them, so since 2026-10-03 the user runs the client after each batch and
  reports what is wrong. Those runs are looks, not verification; they find crashes and plain
  breakage early, which a static number never will.
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

## Where it stands (measured 2026-10-03 14:22)

| | 2026-10-01 start | 2026-10-03 03:07 | **2026-10-03 14:22** | at completion |
|---|---:|---:|---:|---:|
| render surface linked | 1,267 / 4,838 (26%) | 2,330 / 5,143 (45%) | **2,816 / 5,143 (55%)** | 5,143 |
| render surface faithful | not measured | 1,537 (30%) | **1,786 (35%)** | 5,143 |
| render surface stubs | 20 | 16 | **21** (five new, see phase 4) | 0 |
| live empty functions (this platform) | 37 | 5 | **5**, all empty in the reference too | those 5 |
| attributed by anchor guess | 63% | 48% | **48%** | low |
| D3D9 device census (vtable 0x00a2e718, 228 slots) | 199 linked | 228 / 228 | **228 / 228** | 228 |
| reference-wide mapped / faithful | | 5,994 / 3,439 | **6,778 / 3,856** | |

The run gained 791 links and lost 4 (`CCharacterComponent::FreeComponent`, `AddLink`,
`CGUnit::Unit`, `CWorld::GetOutdoorAmbient`, all positional or call-graph inferences that moved
when their neighbours were tagged). The denominator moves as anchors are recovered and template
instantiations are excluded, so compare linked counts, not percentages.

**What is left, read rather than counted.** 2,327 render-surface functions are unlinked:

| area | phase | unlinked | what it really is |
|---|---|---:|---|
| entities: `Unit_C`, `Player_C`, `GameObject_C`, `Movement`, spell visuals, missiles, effects, vehicles | 4 | ~1,400 | the real remaining work, more than half of it |
| environment: world, map, buildings, liquid, weather, camera, frame | 2 | ~430 | fidelity of the roots plus a tail of leaves; see phase 2 |
| textures, effects, base device | 3 | ~340 | **about 70 real**; the rest is misattributed (see phase 3) |
| D3D9Ex and OpenGL devices | out of scope | 129 + ~100 misbucketed | not used by this build on Windows |
| models and particles | 1 | 8 | each blocked, with its reason at the code |

Half the surface is still placed in its module by the nearest path string rather than a known
boundary. Read a function's body before trusting a `?` module in the queue; `MODULE_RANGES` in
`recomp.py` is the fix per module.

## How a cycle runs

The loop in CLAUDE.md: `recomp.py`, `--next`, port, build, `refresh-compile-db.bat` if a file was
added, `clangparse.py`, `recomp.py --pdb`, commit with the delta. Rules learned the hard way:

- **Measure the build you made.** `FROZEN_PDB=<repo>/build/bin/Release/Frozen.pdb recomp.py --pdb`
  measures a fresh build without installing it into `build/dist`. Without it the measurement reads
  `build/dist/bin/Frozen.pdb`, which may be a day old.
- **Refresh the compile database after adding a file.** `Collide.cpp` and `CMapCollide.cpp` scored
  nothing until `refresh-compile-db.bat` ran; the database is a glob snapshot.
- **`--diff` regenerates the report.** It rewrites `REPORT.md`, `map.json`, `matches.tsv` and
  appends a history row. After a real measurement, commit before diffing; never restore those
  files with `git checkout` once a measurement is in them.
- **Fix before link.** `--next 20 --fix --render`, then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name.
- **A tag above a variable binds nothing.** Put it on the line directly above the definition.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of callers.
- **Port chains from the top.** Check what feeds a stub before writing it.
- **Check the gate before calling something dead.** A pass that looks unreachable can be on by a
  value in `.data`; read the initial bytes before concluding a system is off.
- **Close a module, then move.** A module at 100% stops feeding false order-matcher links to its
  neighbours.
- **Read every new inferred link.** The order, call-graph and string matchers are confidently
  wrong often enough to matter.
- **Resolve register arguments in the asm.** Ghidra drops `ECX` on `__thiscall` helpers, `EAX` and
  `EDI` arguments to `__fastcall` and custom-convention helpers, x87 return values, and reuses
  stack slots. Read the objdump, never guess from the decompilation. Phase 4 paid for this twice:
  `CollidePointOverFacet` (0x75d0a0) sweeps along an up vector passed in `EDI` and was ported with
  the facet's normal, so falls never landed on slopes; `IsCombatAnimation` (0x71d590) takes its id
  in `EAX`, and the two calls in 0x73bbd0 test different ids.
- **Check callback argument meaning at both ends.** The M2 sequence-done callback's fourth
  argument is "replaced" and its fifth the overshoot; a handler that read the fifth inverted every
  animation hand-off. When a port receives a callback, read what the caller pushes.
- **A stub with a live caller is a bug.** `UnitUpdateSmoothFacing` and `PostMouseModeChanged` were
  empty, each had live callers, and each produced a visible failure (the camera returning to the
  spawn heading; the cursor held at the window's middle). `livestubs.py` only covers the render
  path; grep for `// TODO` bodies with callers when a feature misbehaves.
- **A hand verdict needs a read.** An order miss caused by switch layout or inlined templates is
  a verdict in `overrides.json`, written after reading both bodies.
- **Check a decoder numerically.** Cheaper than a run, and exact.
- **Record divergence in the same commit**, `status: "diverged"` with the reason.
- **Bisect a visual regression** before reasoning about it: a second worktree at the last good
  commit builds in minutes, and `FROZEN_AUTO_SCREENSHOT` captures the same spot on both.
- **Symbolize a crash against the build that crashed.** The CLion build has no PDB; match the
  faulting bytes against the PDB build (same source, same layout) or use
  `%LOCALAPPDATA%\CrashDumps`. A `0xC0000409` exit writes no `crash.log`: it is a stack cookie,
  so look for an undersized local buffer.

## Done

- **Phase 0, the ruler** (2026-10-01). `anchors.py` recovers the path-string anchors the export
  dropped; the report prints faithful and stub counts for the surface.
- **Phase 1, models** (2026-10-02). `M2Model` 149/151, `M2Scene` 64/67, `M2Shared`, `M2Light`,
  `ParticleSystem2`, `MapShadow` complete; `M2Cache` 17/20. Left, each with its reason at the code
  or in `overrides.json`: Storm's archive byte accounting (`FUN_008245b0`, `FUN_004b57a0`), the
  `CharacterModelBase` cameras and DressUp frame (need `CSimpleModel::SetCameraByID`), tempest
  helpers owned by `src/world`, `M2Cache` `0x81ca10`.
- **Phase 3a, the D3D9 device** (closed 2026-10-03). All 228 vtable slots linked. The base
  `CGxDevice` constructor stays diverged until `CGxCaps` is layout-faithful.
- **Phase 2 landed** (2026-10-02): projected textures, portal views, the map memory order, the
  liquid bank and buffer pool, box, segment and facet queries, the camera (105/128), the cascaded
  shadow map, object placement, MapWeather, the BSP cache, WMO doodads per group, global-WMO maps,
  water ripples, the DayNight light block and sky, the low-detail horizon, the whole of
  `CMap::Render`, the load barriers. The stand-in renderer is gone (4b40fc4d).
- **Phase 3, textures and effects** (2026-10-03, ported, not run): the full-screen effect system
  and the world glow (93b727d9), death, nether and special effects, screen effects from auras
  (0d14c881), `blp`, the texture cache, cursors, the live stubs, the TGA writer and DXT walkers.
- **Phase 2 follow-ups** (2026-10-03): the hidden-entity list drains (1723140a, units no longer
  vanish for good), occlusion volumes get their cap plane (03c00356), the particle and light
  stand-ins are retired (8fb1cb17, a63df0e7), passes ask for fog themselves (f4e7c701), entity
  shadows walk inside `CMap::Render` (06592a45) and the decal query collects its M2 receivers
  (821dc3ea).

## Phase 4: entities (current)

Started 2026-10-03 under the directive to finish the phase in one push, porting each dependency as
it is met instead of deferring it. All of it is built; the parts marked **seen** have been run by
the user.

| commit | what | seen |
|---|---|---|
| 04ba0a7f | the reference record caches (`DBCache`) replace `ItemCache` and `NameCache` | yes |
| ceb0639b | `Object_C`, `Effect_C`, `SpellVisuals`; mounts; the world frame's object handler; passenger transport transforms | yes |
| 6f07c9a2 | the unit aura store and its visuals (replaces `AuraCache`, `UnitVisuals`) | |
| 80113dee | the reference field handlers: registration, saved copies, dispatch | yes |
| 5709cc7c | unit model building, weapons and the sheath | yes: players and NPCs draw |
| d7d1cb8e | `GameObject_C`: type behaviours, animation states, the template cache | partly: game-object particles (the Ebon Hold blue fires) do not show |
| b0195c27 | the world clock, not the frame tick, for every absolute time | |
| 26c96df0 | building groups and placements carry their type bits | |
| aac0bdcd, e7ec424e | **local movement**: the event queue, integration, collision (`Collide.cpp`, the box facet query), the movement packets, `SetActiveMover`, time sync, bindings from `DefaultBindings.wtf` driving `InputControl` | yes: walks, the server follows |
| 6e63a0d5 | `CObjectHeapList::New` reuses freed blocks (FUN_004d3250); allocation failed once chunks unloaded behind a walking player | yes: the crash is gone |
| b12b1101 | the sweep's contact buffers hold seven planes (0x70 bytes in FUN_00760fc0) | yes: the crash is gone |
| e6f44168, f2cb6f7f | **mouse look**: relative mode on Windows, `BUTTON1`/`BUTTON2` as bindings, `TurnOrAction` = bit 0x1 and `CameraOrSelectOrMove` = bit 0x2 (frozen had them swapped), the camera turning the player, the event layer recording the mouse mode, the player's smooth facing | **owed a run** |
| a83c754d | a fall lands on sloped ground (the `EDI` up vector) | **owed a run** |
| 55c99346 | animation follow-ups (FUN_0073b510), the unit model's sequence handler (FUN_0073c090 / FUN_0073bbd0), JumpLandRun, the hard-landing sound | **owed a run** |

Where the modules stand (linked / reference functions, 2026-10-03 14:22):

| module | linked | unlinked | was (03:07) |
|---|---:|---:|---:|
| `Unit_C` | 233 / 643 | 410 | 545 |
| `Player_C` | 86 / 479 | 393 | 425 |
| `Movement` | 55 / 249 | 194 | 244 |
| `GameObject_C` | 130 / 285 | 155 | 262 |
| `Passenger` | 20 / 77 | 57 | 63 |
| `InputControl` | 102 / 156 | 54 | |
| `Missile_C` | 3 / 53 | 50 | 50 |
| `UnitMissileTrajectory_C` | 2 / 40 | 38 | 38 |
| `SpellVisuals` | 32 / 65 | 33 | 65 |
| `MovementShared` | 56 / 83 | 27 | 79 |
| `ObjectEffect` | 17 / 41 | 24 | 30 |
| `CharacterModelBase` | 10 / 24 | 14 | 14 |
| `Effect_C` | 34 / 40 | 6 | 37 |
| `Lightning` | 6 / 7 | 1 | 7 |

Open, in order of what the player sees:

1. **What the last batch owes a run.** Right-drag turns the player and the camera stays behind
   the new heading; left-drag orbits the camera only; either button's release gives the cursor
   back where it was; jumps and landings animate through JumpStart, Jump, JumpEnd or JumpLandRun;
   stopping returns to Stand; landing on a slope ends the fall.
2. **Game-object particles.** The Ebon Hold blue fires (entry 191613) build their model but show no
   particles. The 2026-10-03 probes (removed) watched the fire nearest the player; the
   investigation was paused for movement and starts again from the world frame's model visit
   (`CGWorldFrame` object handler -> `CM2Scene` particle consume).
3. **Remote movement.** Other units' movement packets (FUN_00741b60, FUN_00732450) and server
   splines (`CMovementData_C::StepSpline` is a stub because nothing sets `m_spline`); NPCs walk by
   snapping until these land.
4. **Other units' smooth facing.** `CGUnit_C::UpdateSmoothFacing` (FUN_00735f60) ports the active
   player's branch; everyone else takes the raw facing. The rest is facing the target or charmer
   when standing, the four-sample turn average, and the stand-state turn rate.
5. **The animation chain's remainder.** The non-root bone follow-up FUN_00737bd0 (upper-body
   splits), the swing trail on a replaced attack (FUN_00732500), the death effect FUN_00717ba0 (the
   CEffect list), the player's queued emote steps (FUN_006e2e10, Player_C +0x1944), `m_animTier`
   via FUN_007167c0.
6. **Mouse and input remainder.** The world frame's wheel is still a stand-in (the reference turns
   it into `MOUSEWHEELUP` / `MOUSEWHEELDOWN` bindings); a short left click's select-or-interact
   (FUN_004f7880); the right press's cursor-mode work (FUN_0051fb00, FUN_0051fa50's first half);
   focus loss during mouse look (the 0x1244 callback, FUN_00512d00 -> FUN_005fc960).
7. **Transports and vehicles.** `SetFloorObject` (FUN_006ec7b0) returns 0; the seat tables, the
   vehicle camera (0x759580..0x75af40) and every `PHASE4(Vehicle_C)` marker wait on them.
8. **The five new stubs**, all `GameObjectTypes.cpp`: `CGGameObjectText::Use` and
   `OnStatsLoaded`, `CGGameObjectGuildBank::Use`, `CGGameObjectMeetingStone::Virtual0A0`,
   `CGGameObjectCapturePoint::OnStatsLoaded`. Tagged with empty bodies; they open UI frames.
9. **The rest by module, top down:** the unit per-frame update FUN_00734390 and `UpdateForFrame`
   FUN_0073dab0 (replacing the world frame's `UpdateVisibleObject` and `UpdateIdleAnimation`
   stand-ins), the remaining unit field handlers in FUN_00741d00, the footstep event FUN_00723a50
   (footprints, camera shakes, footstep sounds, which unblocks phase 2's footprints), the CEffect
   list (`FUN_00745230`, `FUN_006f61d0` with 125 callers), missiles (`FUN_006fda20`,
   `FUN_006fdfb0`), `Player_C`, then `ObjectEffect`, corpses, dynamic objects and `PlayerName`.

Exit: the entity rows at 100% linked and faithful. With them, criteria 1 to 3 are met.

## Phase 2: the environment (not closed)

1. **Shadows.** No silhouettes reach the ground. The cascades' look-at up vectors are never written
   in the reference (cascade base +0x28, read at 0x874c60, 0x874d0a, 0x874ec6), so every cascade
   view is the identity and frozen reproduces that; the open question is why the collected casters
   do not show. Next: one noon run with the dump,
   `FROZEN_FORCE_TIME=12 FROZEN_SHADOW_DUMP=<dir> Frozen.exe 2> shadow.log`. Empty maps point at
   the caster walks; full maps point at the light matrices or the sampling.
2. **Fidelity of the roots** (call order, 2026-10-03 14:22): `CMovementData_C::Collide` 42%,
   `OnWorldRender` 52% (from 48%), `UpdateCamera` 57%, `SendMovement` 64%, `DrawLocal` 67%,
   `UpdatePlayerMovement` 71%, `WalkPortals` 72%, `ProcessEvents` 74%, `CMap::Render` 84%. At or
   near 100%: `OnFrameRender`, `CMap::Update`, `UpdateAnimation`, `CWorld::Update` 94%,
   `MapMemInitialize` 93%. Run `--diff` on each before porting anything: a low score is often a
   helper under its own name, and calls inside lambdas are invisible to the parser (the collision
   port uses several).
3. **The tail by module** (unlinked): `World` 65, `Map` 56, `WorldFrame` 44, `DetailDoodad` 44,
   `MapObj` 38, `Camera` 23, `Liquid` 20, `MapWeather` 19, `WorldParam` 17, `AaBsp` 17,
   `MapObjGroup` 16, `MapChunkLiquid` 14, `MapObjRead` 11, `WorldScene` 11, `WorldText` 11. Read
   before porting:
   - **Misattributed.** "WorldText" 0x7e8000..0x7ea400 is `FFXEffects`; the `WorldFrame` remainder
     is spell and unit work (phase 4).
   - **Blocked on the leave-world path.** The scene teardown `FUN_00798310` and `CWorld::Destroy`
     `FUN_007837f0` are only reached from `FUN_00406510` (logout to the glue screens).
   - **Not ported in the movement facet query:** building liquid (FUN_007af000), dynamic objects
     (FUN_007a5240), the low-detail heights (FUN_007a4590) and the unloaded-tile box (query flag
     0x80000000), each noted in `CMapCollide.cpp`.
   - **Dead by construction, not ported:** the object list at 0x00b2eb68 and the barrier draw's
     four models (DAT_00cd85f8). Both are noted at their call sites.
4. **Bookkeeping the report flags:** seven `CWorldParam` callbacks (0x78d940 .. 0x78e110) have
   bodies but `overrides.json` still says `stub`, so they cannot count as faithful; the liquid FFP
   creators (0x8a4850, 0x8a4870, 0x8a48d0) and `StarsInitialize` (0x9abb00) are annotated while
   their override says `unlinked`. Correct each after reading the body.

## Phase 3: what the ~340 are

Read function by function on 2026-10-03, phase 3's modules hold about 70 real unlinked functions.
The rest is in the wrong bucket:

| bucket | unlinked | really |
|---|---:|---|
| `TextureBlob.cpp` | 95 | realm connection, login states, tempest maths, sound. The real TextureBlob is 0x4bfe70..0x4c1140 and is ported |
| `FFXEffects.cpp` | 76 | past 0x7ead80 it is DayNight. The FFX remainder is about ten pass-class deleting destructors and the `Pass*` array resize `FUN_007e7ff0` |
| `CGxD3dDevice.cpp`, `CGxDeviceD3d.cpp` | 60 + 13 | the OpenGL device. Out of scope |
| `PassGlow.cpp` | 24 | a Win32 file writer, not glow |
| `ShaderEffectManager.cpp` | 14 | the sound memory cache |
| `TextureCache.cpp`, `Texture.cpp` | 18 + 17 | real, mostly Storm container instantiations and deleting destructors: exclude or pin them |
| `CGxDevice.cpp` | 15 | real: the base constructor (diverged), the shader lists, `DeviceAdapterID` / `DeviceAdapterInfo` |

Left: give `FFXEffects`, `PassGlow`, `TextureBlob`, `ShaderEffectManager` and the GL device their
`MODULE_RANGES` boundaries; exclude or pin the container instantiations; port the base device
remainder; `LoadingScreenInitialize`'s state table `FUN_0040b0b0` and tile grid `FUN_00407c50`.
The five live empty functions are empty in the reference too and stay.

## Phase 5: verification, once

Started when phases 2 to 4 are practically done, in one block:

1. Run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a map-0 character,
   `FROZEN_FORCE_TIME=12` and `FROZEN_AUTO_SCREENSHOT`. Expect faults; `tools/crashstack.py` is the
   debugger. Owed a first look, newest first:
   - phase 3: the world glow, the death effect while a ghost, an aura with a `ScreenEffect.dbc`
     entry, a screenshot opening as a valid RLE TGA, the cursors, text fading through
     `SetGradient`, textures on any path that decodes DXT on the CPU;
   - the load barriers, the low-detail horizon, the DayNight sky at noon and at dusk, model
     fade-outs, WMO doodads per group, global-WMO maps, MapWeather in rain and snow, the texture
     module, the camera beyond the default view, projected textures, the liquid bank at a water
     line, units standing in water or a building;
   - from phase 3a: the device cursor, resize and alt-tab, the QPC timer, the FPS caps,
     fixed-function combiners.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive per-frame
   agreement on the render spine to 100%, marking each function `verified`.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone, a
   WMO interior, underwater, rain, night, a tile edge while it streams) and drive each viewpoint to
   99% or better.
4. Tighten fidelity from call order to branch and constant checks and re-close what drops.

Exit: 100%.

## Order and pace

Phase 4 is the current phase and stays so until its rows are closed, with each batch handed to
the user for a run before the next. Phase 2's shadows run and root fidelity, and the phase-3
bookkeeping, are fitted in where a phase-4 chain reaches them. 2026-10-03 linked 486 render-surface
functions in one session; the real remainder is about 1,900, so on the order of four such
sessions, with the fidelity work on the roots on top.

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
| 2026-10-03 03:07 | 2,330 / 5,143 | 1,537 | 16 | 5 | phase 3: FFX and the glow, screen effects, blp, texture cache, cursors, live stubs, the TGA writer and blitters, the D3D9 census closed |
| 2026-10-03 14:22 | 2,816 / 5,143 | 1,786 | 21 | 5 | phase 4: DBCache, Object_C / Effect_C / SpellVisuals, auras, field handlers, unit models, GameObject_C, local movement and collision, input bindings and mouse look, the animation follow-ups |
