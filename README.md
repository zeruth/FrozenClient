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

The client logs in, enters the world, and renders it. Concretely, this works today:

- Terrain with its texture layers and baked lighting, water, buildings and their interiors, doodads,
  and animated creatures and players.
- The player composited from equipped items, with idle animation.
- Sky, clouds, and the sun and moon, driven from the light tables in the game data.
- The console, the CVar system, and enough of the FrameXML host to load Blizzard's stock interface.

These are real but unfinished: entity and terrain shadows, particle effects, and the video options
panel all render or respond but are not yet a match for the original. Unit movement is not ported,
so footprints and ribbon trails have nothing to draw. Where the render pipeline is built but has
never been confirmed on screen, `docs/world-render-inventory.md` says so per stage, and this project
treats "it compiled" as worth nothing.

Frozen currently targets Windows. The macOS, Linux and Android paths are inherited from whoa and are
not exercised by this fork's work.

## Accuracy

The goal for 1.0.0 is not "looks right". It is that every function in the original client has a
counterpart here that makes the same calls in the same order, and that the ones that matter have
been watched doing it at runtime.

Guessing an implementation from what the screen looks like is how most of the graphics bugs in this
codebase got in, so accuracy is measured rather than asserted. `tools/recomp/` links the original's
27,161 functions to Frozen's and writes `docs/recomp/REPORT.md`. The numbers below are from the
2026-09-23 run.

Three measures, deliberately never rolled into one, because each is a stronger claim than the last:

| | what it claims | where it stands |
|---|---|---|
| **Linked** | an original function has a known counterpart here | **3,180 / 27,160** &nbsp;·&nbsp; ~11% |
| **Faithful** | linked, not a stub, and reproduces ≥80% of the original's call sequence in order | **1,248** &nbsp;·&nbsp; ~4% of the client, ~38% of what is linked |
| **Verified** | a run was watched behaving like the original | **14** &nbsp;·&nbsp; barely started |

By surface, roughly:

| | covered |
|---|---|
| Lua bindings the original registers (widget methods and global blocks) | 2,924 / 2,964 registered &nbsp;·&nbsp; ~99% |
| &nbsp;&nbsp;of those, actually implemented rather than a stub | 1,433 &nbsp;·&nbsp; **~48%** |
| Functions reachable from the world render entry point | 564 / 5,529 &nbsp;·&nbsp; ~9% |
| The render surface: the map, model, entity, texture and device modules that draw the world | 276 / 4,589 &nbsp;·&nbsp; **~6%** |
| Empty functions the render path still has call sites for | **42** &nbsp;·&nbsp; an upper bound, not a defect count |
| Original code, by bytes rather than function count | ~12% linked, ~2.4% faithful |

All 681 translation units now parse cleanly under libclang, so no row above is being computed
from guessed call data. That was not true until 2026-09-23: 24 files failed to parse, and the fix
moved **faithful** by 2 on its own. The cause was a quoting bug on this side rather than anything
wrong with the code -- `-DOSCL_IMPORT_REF=""` was being passed through with its quotes, so the
macro expanded to an empty string literal instead of to nothing, and every declaration using it
failed. 23 of the 24 were third-party video-decoder sources and one was frozen's own.

The last row is the one that moves week to week. Linked and faithful count functions that exist; it counts functions that **do not** and are called anyway. A few of those are worse than missing: a caller that changes its own control flow assuming the stub succeeded will do something wrong rather than nothing. Three such traps have been found and disarmed before anything switched them on: enabling the model cache's threading flag would have frozen on every second model, letting merged batches through would have stopped them drawing, and assigning the async-BLP hook would have stopped every BLP loading. Each was harmless only because a flag upstream was still off.

It is an upper bound and is meant to be read rather than totalled. `tools/livestubs.py` prints the list, and the list mixes four things: genuine live holes; stubs that are dead today because the only caller sits behind another stub (each one says so in a comment); deliberate divergences that are correct as they stand, like `StereoEnabled` returning false on a client with no stereo support; and a residue the test cannot settle on its own, such as `M2Init`'s scalar overloads, whose `return 1` correctly ends a template recursion. The number has fallen three times on 2026-09-23, and only the last of the three was real work: 47 to 44 is `CGxDeviceD3d::IStateSyncLights` and `CM2Lighting::SetupGxLights` actually being written, which builds out the fixed-function half of the model lighting path -- though that half stays cold until a caller reaches it, since the one frozen has sits behind a shader toggle that is currently forced on. 44 to 42 is the solid-colour texture cache, whose two halves were stubs -- and that one is on a path that runs: every request for a solid colour was allocating a fresh 8x8 texture and leaking it, including one per missing model texture. The first two falls changed no code at all, and happened because the test was counting the wrong things. 79 to 65: it treated any one-line `return <name>;` as empty, which made accessors look like holes. 65 to 47: it counted the OpenGL and GLES backends, which `src/gx/CMakeLists.txt` builds only on Mac and Android -- seventeen rows, `GLDevice::Draw` and the `CGxDeviceGLL::IStateSync*` family among them, that are not in the Windows binary at all and cannot be reached by a frame here. They are still work for the Android port, so the tool now lists them separately rather than dropping them.

Inside that render surface, the split is lopsided, and it is the honest picture of what is left:

| area | modules | ported |
|---|---|---|
| Models and their scene | `M2Scene`, `M2Shared`, `CharacterModelBase` | ~21% |
| The map's own geometry | `MapChunk`, `MapLoad`, `MapArea`, `MapObjRead` | ~13% |
| Textures | `Texture`, `TextureBlob`, `TextureCache` | ~10% |
| Entities in the world | `Unit_C`, `Player_C`, `GameObject_C`, `ObjectEffect` | ~2% |
| The map and its streaming | `Map`, `MapMem`, `MapChunkLiquid`, `DetailDoodad` | ~1% |
| The graphics device | `CGxDevice`, `CGxDeviceD3d9Ex`, the GL and D3D texture paths | ~3% |

The model side is furthest along because the scene render was ported first; the map and the
entities behind it are the thin part, and that is where the terrain renderer is still standing in
for the original rather than reproducing it.

A missing binding makes FrameXML raise "attempt to call a nil value"; a stub keeps it quiet but
returns nothing, which is why the two are counted apart. So: the interface has the broadest
coverage and is now about half filled in, the engine underneath is early, and the modules that
actually draw the world are the thinnest of all: the terrain renderer works, but it was written from
the screen rather than from the original, so almost none of it can be tagged as a port of a specific
original function. The distance from "linked" to "verified" is the honest size of the work left.
The report also carries
per-module coverage, the ranked queue of what to port next, and a history row per run, so progress
is a table rather than a feeling. A function is only ever marked verified by a trace or a scene
comparison, never by a clean build or a plausible reading of a decompilation.

## Roadmap

1. **In-world rendering parity.** Close out the stages in `docs/world-render-inventory.md` that are
   built but unconfirmed, then the known gaps: shadows, particles, unit movement.
2. **Subsystem ports.** Chat, the spell cast pipeline, inventory and the tooltip, sound. These are
   the large functions at the top of the report's unfaithful queue and the reason several Lua tables
   are still stubs.
3. **Lua surface.** Close the last 40 missing bindings so Blizzard's interface stops meeting `nil`,
   then fill in the 1,491 that are registered but still stubs.
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

Two environment hooks help when working on the client:

| hook | effect |
|---|---|
| `FROZEN_AUTO_LOGIN=account:password` | walks the glue into the world without a human at the keyboard |
| `FROZEN_AUTO_CHARACTER=name` | picks a specific character on the way through |

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
