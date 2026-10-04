# Frozen

[![Push](https://github.com/zeruth/FrozenClient/actions/workflows/push.yml/badge.svg)](https://github.com/zeruth/FrozenClient/actions/workflows/push.yml)

An open source reimplementation of the World of Warcraft 3.3.5a (build 12340) client in C++, ported
function by function from the original binary.

Frozen is a fork of [whoa](https://github.com/whoahq/whoa), which provides its foundation: the
module layout, the Storm and Tempest libraries, the login flow, the FrameXML host and the graphics
device layer. whoa stops at character select; Frozen goes into the world. Work that is not specific
to that goal belongs upstream.

## Status

**Working** (seen running):

- Login, realm and character select, entering the world.
- The world: terrain, buildings and interiors, doodads and grass, water and lava, sky, clouds, sun
  and moon.
- Players and creatures, built from their display data and equipment, animated.
- Walking, collision and mouse look; the server follows the player.
- Blizzard's stock interface, on the original's Lua 5.1.1 with taint tracking and secure execution.

**Built, not yet run:** other units' movement, transports and vehicles, threat, corpses, melee
combat, name plates, the full-screen effects, logout, video settings, and the spell and item
tooltips.

**Missing:** world mouseover, the unit tooltip, spell casting and missiles, chat, the quest log, and
most of the large interface panels (mail, auction, trade skills, guild bank, LFG).

Windows is the target. An Android build (`android/`) reaches the same in-game state but is too slow
to play.

## Accuracy

The goal for 1.0.0 is that every function in the original client has a counterpart here that makes
the same calls in the same order. `tools/recomp/` measures this and writes `docs/recomp/REPORT.md`.

As of 2026-10-04, against the original's 26,739 functions:

| measure | meaning | count |
|---|---|---:|
| linked | has a known counterpart | 8,029 (30%) |
| faithful | linked, not a stub, ≥80% of the original's calls in order | 4,552 (17%) |
| verified | watched behaving like the original at runtime | 33 |

| surface | linked |
|---|---:|
| render surface (the modules that draw the world) | 3,316 / 5,143 (64%) |
| &nbsp;&nbsp;models and particles | 429 / 456 (94%) |
| &nbsp;&nbsp;map and world: terrain, buildings, liquid, sky, weather, shadows, camera | 1,117 / 1,486 (75%) |
| &nbsp;&nbsp;textures and effects | 498 / 718 (69%) |
| &nbsp;&nbsp;entities: units, players, game objects, movement, spell visuals, missiles | 1,056 / 2,055 (51%) |
| &nbsp;&nbsp;graphics device, including the D3D9Ex and OpenGL devices Windows does not use | 216 / 428 (50%) |
| Direct3D 9 device (vtable slots) | 228 / 228 |
| Lua bindings registered | 2,902 / 2,964 |
| &nbsp;&nbsp;implemented, not stubs | 1,618 |

The area rows are the render surface split by `RENDER_MODULES` in `tools/recomp/recomp.py` and
add up to its total. About half the surface is placed in its module by the nearest path string
rather than a known boundary, so individual rows can be off in either direction. Verification is deferred until the render surface is complete; see
[docs/rendering-roadmap.md](docs/rendering-roadmap.md).

## Building

```
cmake -S . -B build
cmake --build build --config Release --target Frozen
```

The executable lands in `build/bin/Release`. Use the Release build for anything visual, and keep the
PDB next to the executable.

## Running

Run `Frozen.exe` with the working directory set to a 3.3.5a (12340) installation; it reads the MPQ
archives from `Data`. Frozen ships no game data. Point it at a 3.3.5a-compatible server.

Lua errors go to stdout. Environment hooks for development:

| variable | effect |
|---|---|
| `FROZEN_AUTO_LOGIN=account:password` | logs in and enters the world unattended |
| `FROZEN_AUTO_REALM=name` | realm to pick (default: the first offered) |
| `FROZEN_AUTO_CHARACTER=name` | character to pick |
| `FROZEN_FORCE_TIME=hours` | overrides the game clock (0-24) |
| `FROZEN_AUTO_SCREENSHOT=6,13` | captures the back buffer at those elapsed seconds |
| `FROZEN_SHADOW_DUMP=path` | writes the map shadow map to disk |

## Contributing

Read [CONTRIBUTING.md](./CONTRIBUTING.md). In short: match the original's names, signatures,
layouts and behaviour; name by behaviour when the original name is unknown; tag each port with its
reference address (`// ref: FUN_004932c0`); record deliberate divergences in
`tools/recomp/overrides.json`. Do not import behaviour from other client versions.

## Legal

This project is released into the public domain. It is unofficial and not affiliated with
Blizzard Entertainment.

World of Warcraft: Wrath of the Lich King ©2008 Blizzard Entertainment, Inc. All rights reserved.
Wrath of the Lich King is a trademark, and World of Warcraft, Warcraft and Blizzard Entertainment
are trademarks or registered trademarks of Blizzard Entertainment, Inc. in the U.S. and/or other
countries.
