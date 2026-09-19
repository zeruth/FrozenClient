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
27,161 functions to Frozen's and writes `docs/recomp/REPORT.md`.

Three measures, deliberately never rolled into one, because each is a stronger claim than the last:

| | what it claims | where it stands |
|---|---|---|
| **Linked** | an original function has a known counterpart here | **2,254 / 27,161** &nbsp;·&nbsp; ~8% |
| **Faithful** | linked, not a stub, and reproduces ≥80% of the original's call sequence in order | **684** &nbsp;·&nbsp; ~3% of the client, ~30% of what is linked |
| **Verified** | a run was watched behaving like the original | **14** &nbsp;·&nbsp; barely started |

By surface, roughly:

| | covered |
|---|---|
| Lua bindings the original registers | 1,584 / 2,512 registered &nbsp;·&nbsp; ~63% |
| &nbsp;&nbsp;of those, actually implemented rather than a stub | 832 &nbsp;·&nbsp; **~33%** |
| Functions reachable from the world render entry point | 312 / 5,530 &nbsp;·&nbsp; ~6% |
| Original code, by bytes rather than function count | ~9% linked, ~2% faithful |

A missing binding makes FrameXML raise "attempt to call a nil value"; a stub keeps it quiet but
returns nothing, which is why the two are counted apart. So: the interface has the broadest
coverage but a third of it is still hollow, the engine underneath is early, and the distance from
"linked" to "verified" is the honest size of the work left. The report also carries
per-module coverage, the ranked queue of what to port next, and a history row per run, so progress
is a table rather than a feeling. A function is only ever marked verified by a trace or a scene
comparison, never by a clean build or a plausible reading of a decompilation.

## Roadmap

1. **In-world rendering parity.** Close out the stages in `docs/world-render-inventory.md` that are
   built but unconfirmed, then the known gaps: shadows, particles, unit movement.
2. **Subsystem ports.** Chat, the spell cast pipeline, inventory and the tooltip, sound. These are
   the large functions at the top of the report's unfaithful queue and the reason several Lua tables
   are still stubs.
3. **Lua surface.** Close the remaining 928 bindings so Blizzard's interface stops meeting `nil`.
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
