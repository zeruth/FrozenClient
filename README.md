# Frozen

[![Push](https://github.com/zeruth/FrozenClient/actions/workflows/push.yml/badge.svg)](https://github.com/zeruth/FrozenClient/actions/workflows/push.yml)

Frozen is an unofficial, open source reimplementation of the World of Warcraft 3.3.5a (build 12340)
game client in C++.

It is a fork of [whoa](https://github.com/whoahq/whoa), and it would not exist without it. whoa
built the foundation this project stands on: the module layout, the Storm and Tempest library
reimplementations, the glue and login flow, the FrameXML host with its Lua binding tables, and the
GX device abstraction over Direct3D and OpenGL. Everything below is continuation, not replacement,
and changes that are not specific to Frozen's goals belong upstream.

Where whoa stops at the character select screen, Frozen's aim is the world: get in, draw it, and
then make it match the original client function for function.

## Status

The client logs in, enters the world, and renders it. Concretely, this works today and has been
seen on screen:

- Terrain with its texture layers and baked lighting, buildings and their interiors, doodads and
  grass, and animated creatures and players. The terrain, its chunk streaming, the detail doodads
  and the liquid are now ports of the original's own code rather than a stand-in renderer: the
  5,592-line stand-in written from the screen was deleted outright on 2026-09-26.
- Water, lava and slime, animated and depth-shaded, through the original's four liquid materials.
  No liquid of any kind had drawn before 2026-09-27; four separate defects each sufficed to stop it.
- The player composited from equipped items, with idle animation.
- Sky, clouds, and the sun and moon, driven from the light tables in the game data.
- The console, the CVar system, and enough of the FrameXML host to load Blizzard's stock interface.

Real but unfinished, on the rendering side:

- **The map shadow map.** Until 2026-09-27 the whole path was dead behind a reallocation latch that
  nothing ever cleared, so it had never executed past its first instruction. As of 2026-09-28 it
  carries caster depth over its whole surface, confirmed by reading the texture back off the device
  rather than by looking at the screen. What has not been seen is the terrain sampling it: every
  run so far landed at night in game time, where a correct map and a broken one look identical.
  The original's three-cascade, multi-target shadow system is not ported; this is one target.
- **Entity blob shadows** draw, on terrain and on building floors, but the footprint is still an
  axis-aligned circle where the original turns an oriented rectangle, and model receivers are not
  drawn.
- **Particles** are a stand-in and ribbons draw nothing. Unit movement is not ported, so footprints
  and ribbon trails have nothing to draw either.
- **The sky dome's seven-ring geometry, its dawn and dusk highlight, and the fog formulas** are
  ported from the original but have not been watched at the time of day where they differ from
  what was there before.

Where the render pipeline is built but has never been confirmed on screen,
`docs/world-render-inventory.md` says so per stage, and this project treats "it compiled" as worth
nothing.

Frozen targets Windows first. An Android build exists (`android/`, Gradle plus the NDK) and reaches
the same in-game state as the desktop client, but it is far too slow to play; performance is the
open item there. The macOS and Linux paths are inherited from whoa and are not exercised by this
fork's work.

## Accuracy

The goal for 1.0.0 is not "looks right". It is that every function in the original client has a
counterpart here that makes the same calls in the same order, and that the ones that matter have
been watched doing it at runtime.

Guessing an implementation from what the screen looks like is how most of the graphics bugs in this
codebase got in, so accuracy is measured rather than asserted. `tools/recomp/` links the original's
functions to Frozen's and writes `docs/recomp/REPORT.md`. The numbers below are from the
2026-10-01 run, against the 26,942 non-thunk functions of the original binary.

Three measures, deliberately never rolled into one, because each is a stronger claim than the last:

| | what it claims | where it stands |
|---|---|---|
| **Linked** | an original function has a known counterpart here | **4,748 / 26,942** &nbsp;·&nbsp; ~18% |
| **Faithful** | linked, not a stub, and reproduces ≥80% of the original's call sequence in order | **2,502** &nbsp;·&nbsp; ~9% of the client, ~53% of what is linked |
| **Verified** | a run was watched behaving like the original | **33** &nbsp;·&nbsp; barely started |

By surface, roughly:

| | covered |
|---|---|
| Lua bindings the original registers (widget methods and global blocks) | 2,924 / 2,964 registered &nbsp;·&nbsp; ~99% |
| &nbsp;&nbsp;of those, actually implemented rather than a stub | 1,544 &nbsp;·&nbsp; **~52%** |
| Functions reachable from the world render entry point | 1,648 / 5,378 &nbsp;·&nbsp; ~31% |
| The render surface: the map, world, liquid, shadow, model, entity, texture and device modules that draw the world | 1,398 / 5,327 &nbsp;·&nbsp; **~26%** &nbsp;·&nbsp; 869 faithful, 32 stubs |
| Empty functions the render path still has call sites for | **37** &nbsp;·&nbsp; an upper bound, not a defect count |
| Original code, by bytes rather than function count | ~19% linked, ~6.5% faithful |

All 760 translation units parse cleanly under libclang, so no row above is being computed from
guessed call data. That was not true until 2026-09-23: 24 files failed to parse because of a
quoting bug in the tooling (a `-D` macro passed through with its quotes), and fixing it moved
**faithful** by 2 on its own.

The empty-function row is the one that moves week to week. Linked and faithful count functions
that exist; it counts functions that **do not** and are called anyway. A few of those are worse
than missing: a caller that changes its own control flow assuming the stub succeeded will do
something wrong rather than nothing. Three such traps have been found and disarmed before anything
switched them on: enabling the model cache's threading flag would have frozen on every second model,
letting merged batches through would have stopped them drawing, and assigning the async-BLP hook
would have stopped every BLP loading. Each was harmless only because a flag upstream was still off.

It is an upper bound and is meant to be read rather than totalled. `tools/livestubs.py` prints the
list, and the list mixes four things: genuine live holes; stubs that are dead today because the only
caller sits behind another stub (each one says so in a comment); deliberate divergences that are
correct as they stand, like `StereoEnabled` returning false on a client with no stereo support; and
a residue the test cannot settle on its own, such as `M2Init`'s scalar overloads, whose `return 1`
correctly ends a template recursion. The tool counts only what the Windows binary can reach: the
OpenGL and GLES backends are built only on Mac and Android, so their seventeen empty functions,
`GLDevice::Draw` among them, are listed separately as work for the Android port rather than as
holes in a frame here.

Inside that render surface, the split is lopsided, and it is the honest picture of what is left.
The seven rows add up to the headline figure:

| area | modules | linked |
|---|---|---|
| Models, their lights and particles | `M2Scene`, `M2Model`, `M2Shared`, `M2Cache`, `M2Light`, `ParticleSystem2`, `CharacterModelBase`, `ModelBlob` | 316 / 539 &nbsp;·&nbsp; **~59%** |
| Liquid, the shadow map and the shader effects | `Liquid`, `ShadowMap`, `MapShadow`, `ShaderEffect` | 108 / 192 &nbsp;·&nbsp; **~56%** |
| The map's own geometry | `MapChunk`, `MapLoad`, `MapArea`, `MapObj`, `MapObjGroup`, `MapObjRead`, `AaBsp`, `MapLowDetail` | 148 / 327 &nbsp;·&nbsp; **~45%** |
| The map, its streaming and the world layer | `Map`, `MapMem`, `MapChunkLiquid`, `DetailDoodad`, `WorldParam`, `World`, `WorldScene`, `WorldFrame`, `Camera`, `MapWeather`, `WorldText` | 364 / 1,042 &nbsp;·&nbsp; **~35%** |
| Textures, decoders and effects | `Texture`, `TextureBlob`, `TextureCache`, `blp`, `tga`, `FFXEffects`, `EffectGlow`, `PassGlow`, `Lightning`, `ShaderEffectManager` | 217 / 793 &nbsp;·&nbsp; **~27%** |
| Entities in the world | `Unit_C`, `Player_C`, `GameObject_C`, `ObjectEffect`, `Effect_C`, `SpellVisuals`, `UnitMissileTrajectory_C`, `Missile_C`, `MovementShared`, `Movement`, `Passenger` | 209 / 2,055 &nbsp;·&nbsp; **~10%** |
| The graphics device | `CGxDevice`, `CGxDeviceD3d`, `CGxD3dDevice`, `CGxD3d9ExDevice`, `CGxDeviceD3d9Ex`, the GL and D3D texture paths | 36 / 379 &nbsp;·&nbsp; **~9%** |

The environment is the part that moved. The stand-in terrain renderer was replaced module by
module with ports of the original's own chunk, streaming, liquid and doodad code and then deleted;
since then the model scene has caught up, with its ray casting, world bounds and shadow-caster
collection ported through the last week of September. Two areas are thin. The ENTITIES: `Unit_C`
and `Player_C` together are 1,122 of the original's functions, 379 of them on the render spine, and
149 are linked, so what a unit does between "here is its model" and "here is its pose" is largely
still missing. And the DEVICE: the Direct3D device that every pass above runs through is less than a
tenth linked, because frozen's device was written from whoa's design rather than from the original.

None of the seven rows means the area WORKS. Linked counts functions that have a counterpart; the
render surface has 32 stubs among its 1,398, and only 19 of them have ever been watched running.
Half of the render surface is also attributed to its module by the nearest path string rather than
by a known boundary, so a row can be mis-sized in either direction. (It was two thirds until
2026-10-01, when `tools/recomp/anchors.py` recovered the module strings the Ghidra export had
dropped; eleven of the modules named above were invisible to the report before that.)

A missing binding makes FrameXML raise "attempt to call a nil value"; a stub keeps it quiet but
returns nothing, which is why the two are counted apart. So: the interface has the broadest coverage
and is now about half filled in, the map and the model scene are genuinely ports, and the entity
layer under them is early. The distance from "linked" to "verified" is the honest size of the work
left -- 33 against 4,748.
The report also carries
per-module coverage, the ranked queue of what to port next, and a history row per run, so progress
is a table rather than a feeling. A function is only ever marked verified by a trace or a scene
comparison, never by a clean build or a plausible reading of a decompilation.

## Roadmap

1. **In-world rendering, linked and faithful.** Close the render surface module by module in the
   order `docs/rendering-roadmap.md` gives: models, then the environment (shadow cascades, the
   blob footprint, WMO liquid, occluders, barriers, weather), then textures and the device, then
   the entities (animation chain, effects, movement). Runs are deferred until that is practically
   complete; the roadmap says why and what that costs.
2. **Subsystem ports.** Chat, the spell cast pipeline, inventory and the tooltip, sound. These are
   the large functions at the top of the report's unfaithful queue and the reason several Lua tables
   are still stubs.
3. **Lua surface.** Close the last 40 missing bindings so Blizzard's interface stops meeting `nil`,
   then fill in the 1,380 that are registered but still stubs.
4. **Runtime verification at scale.** The call tracer and the scene comparison exist; the work is
   running them broadly enough to move the verified column, not just the linked one.
5. **1.0.0.** Every reference function linked and faithful, the render verified against the original
   scene by scene.

The long-term goal inherited from whoa, an Android port, is unchanged and waits on the above.

## Building

Install a recent CMake and a C++ toolchain, then from the repository root:

```
cmake -S . -B build
cmake --build build --config Release --target Frozen
```

The executable lands in `build/bin/Release`. Install it next to its PDB in `build/dist/bin`; a stale
PDB will symbolize crashes to the wrong function, which has cost more than one debugging session.

## Running

Run `Frozen.exe` with the working directory set to the root of a 3.3.5a (build 12340) installation.
It reads the MPQ archives out of `Data` directly: both the common layout and the older split one,
locale archives, and the numbered patch archives applied in order. A fully extracted data set also
works, and is what gets used when there is no `Data` directory at all.

Obtain the archives by installing World of Warcraft 3.3.5a from legally purchased original install
media. Frozen ships no game data.

Point it at a 3.3.5a-compatible server to log in and enter the world.

A few environment hooks help when working on the client. Rendering bugs here are reported by eye
and otherwise fixed blind, so the last three exist to turn a run into evidence:

| hook | effect |
|---|---|
| `FROZEN_AUTO_LOGIN=account:password` | walks the glue into the world without a human at the keyboard |
| `FROZEN_AUTO_REALM=name` | picks that realm on the way through, or the first one offered when unset |
| `FROZEN_AUTO_CHARACTER=name` | picks a specific character on the way through |
| `FROZEN_FORCE_TIME=hours` | overrides the game clock the server sends (0 to 24), so a run can happen in daylight |
| `FROZEN_AUTO_SCREENSHOT=6,13` | captures the client's own back buffer at those elapsed seconds; capturing the screen rectangle has misled this work before |
| `FROZEN_SHADOW_DUMP=path` | reads the map shadow map back off the device and writes it, the only way to tell a working caster pass from a broken one at night |

Lua errors go to stdout; redirect it to capture them.

## Contributing

Read [CONTRIBUTING.md](./CONTRIBUTING.md) first. The short version: this is a decompilation project,
so match the original's names, signatures, layouts and behaviour, and when a name is unknown, name
by behaviour. Do not import behaviour from other client versions. Tag a port with the address it
came from (`// ref: FUN_004932c0`) so the accuracy tooling can score it.

## FAQ

**Why fork whoa rather than contribute to it?**

Frozen pursues in-world rendering and a measured 1:1 recomp, which is a narrower and more invasive
goal than whoa's. Work that isn't specific to that goal is better off upstream.

**Why 3.3.5a?**

The game and its libraries have grown enormously since. At 3.3.5a it is possible to imagine this
implementation eventually being complete.

**Can I use this in my own projects?**

Probably a bad idea. The original game is closed source and this project is in no way official.

## Legal

This project is released into the public domain.

World of Warcraft: Wrath of the Lich King ©2008 Blizzard Entertainment, Inc. All rights reserved.
Wrath of the Lich King is a trademark, and World of Warcraft, Warcraft and Blizzard Entertainment
are trademarks or registered trademarks of Blizzard Entertainment, Inc. in the U.S. and/or other
countries.
