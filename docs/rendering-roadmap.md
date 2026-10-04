# Rendering roadmap

The plan for bringing the render surface to 100%. Live status is in `docs/recomp/REPORT.md`; the
port-and-measure loop is in CLAUDE.md.

## Definition of done

The render surface is the reference functions in the modules that draw the world
(`RENDER_MODULES` in `tools/recomp/recomp.py`): 5,143 functions. It is done when:

1. every function is **linked** and none is a stub;
2. every function is **faithful**: the reference's calls in the reference's order;
3. `tools/livestubs.py` lists no empty function with a live render call site, except those empty
   in the reference too;
4. every render-spine function is **verified** by a call trace or scene comparison, with the scene
   suite at 99% or better.

Steps 1-3 come first. Verification (step 4) starts once they are practically complete. Until then
the user runs each batch to catch crashes and breakage; those runs do not count as verification.

The reference is the vanilla `WoW.exe` (12340) in `.reference`, never `RunicWorldGame.exe`.

## Current state (2026-10-04)

| | count |
|---|---:|
| linked | 3,316 / 5,143 (64%) |
| faithful | 2,048 (40%) |
| stubs | 21 |
| live empty functions | 5, all empty in the reference |
| module attributed by guess | 48% |

Unlinked, by area:

| area | phase | linked | unlinked | notes |
|---|---|---:|---:|---|
| entities | 4 | 1,056 / 2,055 | 999 | the main remaining work |
| map and world | 2 | 1,117 / 1,486 | 369 | |
| textures and effects | 3 | 498 / 718 | 220 | about 70 real; the rest is misattributed |
| graphics device | 3 | 216 / 428 | 212 | 188 are the D3D9Ex and OpenGL devices, unused on Windows; 24 base device |
| models and particles | 1 | 429 / 456 | 27 | each blocked, reason noted at the code |
| **total** | | **3,316 / 5,143** | **1,827** | |

Entities on the surface are `Unit_C`, `Player_C`, `GameObject_C`, `Movement`, `MovementShared`,
`Passenger`, `SpellVisuals`, `Missile_C`, `UnitMissileTrajectory_C`, `ObjectEffect` and
`Effect_C`. Phase 4 also covers modules outside the surface (input, vehicles, corpses, name plates);
the table below lists both.

## Phases

| phase | scope | status |
|---|---|---|
| 0 | measurement tooling | done |
| 1 | models and particles | done except 27 blocked functions |
| 2 | environment: map, buildings, liquid, sky, weather, camera, shadows | mostly done; tail open |
| 3 | textures, effects, D3D9 device | device done (228/228); effects ported, not run |
| 4 | entities | **current** |
| 5 | verification | not started |

## Phase 4: entities

On the render surface (these sum to the entity row above):

| module | linked | left |
|---|---:|---:|
| `Player_C` | 112 / 479 | 367 |
| `Unit_C` | 396 / 643 | 247 |
| `Movement` | 135 / 249 | 114 |
| `GameObject_C` | 211 / 285 | 74 (mostly misattributed `Unit_C`) |
| `Missile_C` | 4 / 53 | 49 |
| `Passenger` | 32 / 77 | 45 |
| `UnitMissileTrajectory_C` | 2 / 40 | 38 |
| `SpellVisuals` | 34 / 65 | 31 |
| `ObjectEffect` | 26 / 41 | 15 |
| `MovementShared` | 70 / 83 | 13 |
| `Effect_C` | 34 / 40 | 6 |
| **total** | **1,056 / 2,055** | **999** |

Off the surface, also phase 4:

| module | linked | left |
|---|---:|---:|
| `InputControl` | 106 / 156 | 50 |
| `Corpse_C` | 43 / 63 | 20 |
| `Vehicle_C` | 28 / 40 | 12 |
| `UnitCombat_C` | 16 / 25 | 9 |
| `VehicleCamera_C` | 69 / 75 | 6 |
| `PlayerName` | 32 / 36 | 4 |
| `UnitVehicle_C` | 32 / 36 | 4 |
| `VehiclePassenger_C` | 48 / 49 | 1 |
| `DynamicObject_C` | 24 / 25 | 1 |

Next, in order:

1. **Run the unrun batch.** Remote movement and splines, unit field handlers, the per-frame unit
   update, swimming and flight, animation events, transports, vehicles, threat, object effects,
   corpses, dynamic objects, melee, name plates.
2. **World mouseover.** `CGWorldFrame::OnLayerUpdate` (FUN_004fa040) is missing, so
   `SetMouseover` (0x4f5980) and the game UI's mouseover (FUN_0051f790) never run: no highlight,
   no `UPDATE_MOUSEOVER_UNIT`, no unit tooltip, no object cursors (0x4f8190, 0x4f7a50, 0x4f59f0,
   0x4f8000).
3. **Spell cast pipeline, then missiles.** `SMSG_SPELL_START` / `SMSG_SPELL_GO` (FUN_0080fee0 →
   FUN_00806700 / FUN_0080e1b0) is the only path to `Missile_C` and `UnitMissileTrajectory_C`.
4. **CEffect list** (FUN_00745230, FUN_006f61d0), then the death effect (FUN_00717ba0) and the
   pre-resurrect message 0x494.
5. **`Unit_C` remainder:** interaction (FUN_00731260), combat animation events (FUN_00756240),
   cast sound (FUN_00746d60), combat log and damage text (`UnitCombatLog_C`).
6. **`Player_C`:** emote and dance steps (FUN_006e2e10) first, then top down.
7. **Game-object particles** (the Ebon Hold fires, entry 191613): models build, particles do not
   show. Start from the world frame's object handler → `CM2Scene` particle consume.
8. **Game-object type stubs** in `GameObjectTypes.cpp`: `CGGameObjectText::Use` / `OnStatsLoaded`,
   `CGGameObjectGuildBank::Use`, `CGGameObjectMeetingStone::Virtual0A0`,
   `CGGameObjectCapturePoint::OnStatsLoaded`.

## Phase 2: environment remainder

1. **Shadows.** Casters do not reach the ground. Diagnose with one noon run:
   `FROZEN_FORCE_TIME=12 FROZEN_SHADOW_DUMP=<dir> Frozen.exe`. Empty maps mean the caster walks;
   full maps mean the light matrices or the sampling.
2. **Root fidelity.** `CMovementData_C::Collide`, `OnWorldRender`, `UpdateCamera`, `SendMovement`,
   `DrawLocal`, `UpdatePlayerMovement`, `WalkPortals` and `ProcessEvents` were below 75% on
   2026-10-03. Run `--diff` on each before porting.
3. **Tail by module** (unlinked): `Map` 56, `World` 38, `MapObj` 37, `WorldFrame` 33,
   `DetailDoodad` 24, `Camera` 23, `Liquid` 20, `AaBsp` 17, `MapWeather` 17, `MapObjGroup` 16,
   `WorldParam` 15, `MapChunkLiquid` 14, `MapObjRead` 11, `WorldScene` 11.
   - `WorldFrame`'s remainder is mostly the mouseover stack (phase 4, item 2).
   - Scene teardown (FUN_00798310) and `CWorld::Destroy` (FUN_007837f0) need the leave-world path
     (`ClientDestroyGame`, FUN_00406510), which is still a stub.
   - The movement facet query lacks building liquid (FUN_007af000), dynamic objects
     (FUN_007a5240), low-detail heights (FUN_007a4590) and the unloaded-tile box.
4. **Override bookkeeping.** Seven `CWorldParam` callbacks (0x78d940..0x78e110) are ported but
   marked `stub`; the liquid FFP creators (0x8a4850, 0x8a4870, 0x8a48d0) and `StarsInitialize`
   (0x9abb00) are ported but marked `unlinked`.

## Phase 3: remainder

Most of the ~220 unlinked functions are in the wrong module:

| bucket | unlinked | actually |
|---|---:|---|
| `FFXEffects` | 76 | DayNight past 0x7ead80; ~10 real (pass destructors, `FUN_007e7ff0`) |
| `TextureBlob` | 64 | realm connection, login, maths, sound |
| `PassGlow` | 24 | a file writer |
| `TextureCache`, `Texture` | 35 | real: container instantiations and destructors |
| `ShaderEffectManager` | 14 | the sound memory cache |
| `CGxDevice`, `CGxDeviceD3d` | 23 | base constructor (diverged), shader lists, adapter info; the rest is the GL device |

To do: give these modules `MODULE_RANGES` boundaries, exclude or pin the container instantiations,
port the base device remainder and the loading screen (FUN_0040b0b0, FUN_00407c50).

## Phase 5: verification

1. Run with auto-login on a map-0 character at noon, with screenshots, and work through every
   ported-but-unrun item in phases 2-4.
2. Trace both clients over the same frames (`tools/recomp/calltrace.py`) and drive per-frame
   agreement on the render spine to 100%.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn, a building interior,
   underwater, rain, night, a streaming tile edge) and drive each view to 99%.
4. Tighten fidelity from call order to branches and constants.

## Progress log

| run | linked | faithful | stubs | live empty |
|---|---|---:|---:|---:|
| 2026-10-01 | 1,267 / 4,838 | — | 20 | 37 |
| 2026-10-02 | 2,222 / 5,214 | 1,478 | 16 | 17 |
| 2026-10-03 | 2,816 / 5,143 | 1,786 | 21 | 5 |
| 2026-10-04 | 3,316 / 5,143 | 2,048 | 21 | 5 |
