# Rendering: the road to 100%

Written 2026-10-01 from the 2026-10-01 recomp report, the two port queues (`--next 80 --render`
and `--next 40 --fix --render`), `tools/livestubs.py`, and the open items in `docs/ref/parity-*.md`
and the session notes. It is a plan, not a status page: `docs/recomp/REPORT.md` is the status.

**Decision 2026-10-01: completion first, verification last.** Runs, traces and scene compares are
deferred until the render surface is practically 100% linked and faithful. Until then a cycle is
measured only by the report's static numbers, and the gates that end a cycle are the static ones
in CLAUDE.md: a failed build, a lost link, a lost binding, a negative delta. The cost is known and
accepted: ports stack up unseen, and the first run at the end will find a batch of defects at once
(the 2026-09-14 batch crashed on first launch; the 2026-09-28 shadow map needed three fixes from
one texture dump). The commit log and `overrides.json` notes are how that run will be debugged, so
every port still records what it diverges from and why.

## What 100% means here

The render surface is the 4,838 reference functions in the modules that draw the world
(`RENDER_MODULES` in `tools/recomp/recomp.py`). Completion means:

1. every one of them is **linked** to a frozen function, and none of those is a stub;
2. every one is **faithful**: same calls in the same order, then the branch and constant checks;
3. `livestubs.py` reports zero empty functions with live render call sites.

Verification (every render-spine function **verified** by trace or scene compare, and the
scene-compare suite at or above 99%) is the last phase, started when 1 to 3 are practically done.

Where it stands (updated at the end of every run; the log at the bottom has the history):

| | 2026-10-01 start | now (2026-10-01 17:59) | at completion |
|---|---|---|---|
| render surface linked | 1,267 / 4,838 (26%) | 1,441 / 5,390 (27%) | 5,390 |
| render surface faithful | not measured | 911 (17%) | 5,390 |
| render surface stubs | 20 | 32 | 0 |
| empty functions with live render call sites | 37 | 37 | 0 |
| render surface attributed by anchor guess | 63% | 49% | low |

The 3,949 unlinked functions by area (module names as the report's anchors give them):

| area | unlinked | share |
|---|---:|---:|
| entities (`Unit_C`, `Player_C`, `GameObject_C`, `ObjectEffect`, `Effect_C`, `SpellVisuals`, `UnitMissileTrajectory_C`, `Missile_C`, `MovementShared`, `Movement`, `Passenger`) | 1,846 | 47% |
| map streaming and the world layer (`Map`, `MapMem`, `MapChunkLiquid`, `DetailDoodad`, `WorldParam`, `World`, `WorldScene`, `WorldFrame`, `Camera`, `MapWeather`, `WorldText`) | 678 | 17% |
| textures, decoders, effects (`Texture*`, `blp`, `tga`, `FFXEffects`, `EffectGlow`, `PassGlow`, `Lightning`, `ShaderEffectManager`) | 576 | 15% |
| graphics device (`CGxDevice`, `CGxDeviceD3d`, `CGxD3dDevice`, `CGxD3d9ExDevice`, `CGxDeviceD3d9Ex`, texture paths) | 343 | 9% |
| models (`M2Scene`, `M2Model`, `M2Shared`, `M2Cache`, `M2Light`, `ParticleSystem2`, `CharacterModelBase`, `ModelBlob`, `GfxSingletonManager`) | 243 | 6% |
| map geometry (`MapChunk`, `MapLoad`, `MapArea`, `MapObj`, `MapObjGroup`, `MapObjRead`, `AaBsp`, `MapLowDetail`) | 179 | 5% |
| liquid, shadow map, shader effects (`Liquid`, `ShadowMap`, `MapShadow`, `ShaderEffect`) | 84 | 2% |

Caveats on the denominator. Half of the render surface is still attributed to its module by the
nearest path string rather than a known boundary, so rows can be mis-sized; `anchors.py` cut
that from 63% but a single-anchor module still claims everything up to the next anchor
(`GfxSingletonManager.cpp` holds the particle emitter plane, `AaBsp.cpp` the terrain shader
selection, `MapLowDetail.cpp` the chunk liquid). `MODULE_RANGES` is the fix per module. And
`DayNight.cpp` and `Sky.cpp` have no path string in the binary at all, so their functions count
under `FFXEffects.cpp`, which is in the set.

## How a cycle runs now

The loop in CLAUDE.md, minus the run: `recomp.py`, `--next`, port, build, `clangparse.py`,
`recomp.py --pdb`, commit with the delta. Throughput rules, learned from the September loops:

- **Fix before link.** `--next 20 --fix --render` then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name; a tag costs minutes and lifts a whole caller.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of
  callers' fidelity and feeds the matchers (two seeds lifted 11 ports on 2026-09-27).
- **Port chains from the top.** Check what feeds a stub before writing it; two cycles of
  local-light work once landed below three empty branches.
- **Close a module, then move.** A module at 100% stops generating false order-matcher links for
  its neighbours; a module at 60% keeps generating them.
- **Tag the identified-but-not-ported.** A tag or an `overrides.json` entry counts as linked and
  takes seconds; an untagged known function is a wasted cycle for the next session.
- **Record divergence as you go.** `status: "diverged"` with the reason, in the same commit. The
  final run will be debugged from these notes.

## Phase 0: fix the ruler (one cycle)

- ~~Add `MODULE_RANGES` for the seven unanchored render modules so they enter the denominator.~~
  **Done 2026-10-01, differently:** the binary carries the path strings the export dropped, and
  `tools/recomp/anchors.py` recovers them through the `push imm32` that references each one.
  It agrees with the export on all 1,476 anchors both know and adds 239 more across 63
  modules; `MapWeather`, `MapShadow`, `MapObj`, `MapObjGroup`, `WorldScene`, `M2Model`,
  `M2Cache`, `World`, `WorldFrame`, `Camera` and the split D3D device bodies are visible for
  the first time, and 19 modules joined `RENDER_MODULES`. `DayNight` and `Sky` have no string
  and stay under their neighbour.
- ~~Make the report print render-surface faithful and stub counts as a line of their own.~~
  **Done 2026-10-01:** the totals table has the line and `history.jsonl` carries both columns.
- ~~Strike the inventory rows that cite `Terrain.cpp` (deleted 2026-09-26) and retire
  `parity-depth.md`, which points entirely into deleted code, so no cycle is planned from them.~~
  **Done 2026-10-01:** both carry a banner under the title saying what is stale and pointing
  here. The inventory's reference addresses are still right, so it was marked rather than cut.

**Phase 0 is closed.**

## Phase 1: models (157 unlinked, the unfaithful roots)

Smallest area, highest fidelity gain per cycle, and its roots are the least faithful large
functions in the client. Closing it first also stabilises the matchers for everything that
hangs off `CM2Model`.

- **Roots** (`--fix`, fidelity in brackets): `CM2Model::InitializeLoaded` [58%, 5.6 KB, 42
  callers], `CM2Scene::Animate` [67%], `CM2Model::AnimateMT` [16%], `AnimateMTSimple` [75%],
  `SetWorldTransform` [17%], `CM2SceneRender::Draw` [86%], `SetupLighting` [56%],
  `SetupTextures` [67%], `SelectLights` [67%], `CM2Lighting::SetupGxFog` [33%],
  `ReplaceTexture` [43%], `OptimizeVisibleGeometry` [89%].
- **Stubs with live callers:** `DrawBatchDoodad`, `DrawBatchProj` (`FUN_00829aa0`, plus
  installing the projection callback `FUN_0077f500` from world init), `SubstituteSpecializedShaders`
  (`FUN_00837680`), the second geometry builder `FUN_0082be60`.
- **Particles** (`parity-particles.md`): shader permutation selector `FUN_00873160` /
  `FUN_00872de0`, ~~`ParticleColor.dbc` override `FUN_0097a990`~~ (done 2026-10-01 with its
  getter, the model-level setter `FUN_00825410` and model call 11, and the model30 inheritance
  in `InitializeLoaded`; the ramp half waits on the fast path), ramp fast path `FUN_00979d60`,
  the type-3 emitter `0x009820f0`, the spawned-model pass `FUN_0097e8d0`. Retire
  `ParticleFx.cpp` once the runtime is wired, so only the ported sim runs.
- **Ribbons** (`parity-ribbons.md`, nothing ported): `CRibbonEmitter` `FUN_009808a0` /
  `FUN_00980b70`, `DrawRibbon` `FUN_00820f40`, the model's ribbon array and its
  `InitializeLoaded` walk.
- **Instanced caster draw** `FUN_0082da40 -> FUN_00829e40 -> FUN_00829ba0`;
  `CollectShadowCasters` is already ported as its input.
- **Model cache** (`parity-model-cache.md`): hash `FUN_0081c390`, pending release
  `FUN_0083dc90`, `GarbageCollect` `FUN_0081c290`, `UpdateShared` `FUN_0081c790`.
- **Unlinked bodies the queue ranks highest:** `FUN_0082ec30` (1.2 KB), `FUN_008292a0` (2 KB),
  `FUN_0083dfa0` / `FUN_0083e140` in `M2Shared`; `ModelBlob` (28 functions, 0 linked).

Exit: `M2Scene`, `M2Shared`, `CharacterModelBase`, `ModelBlob` at 100% linked, no stubs, roots
faithful.

## Phase 2: the environment (539 unlinked, plus the unfaithful roots)

Most-ported area; finishing it closes the map modules and their matcher noise.

- **Unfaithful roots first** (`--fix`): `CMap::Update` [11%], `CMap::MapMemInitialize` [0%],
  `CMap::Render` [42%], `CWorldScene::UpdateCamera` [56%], `CMapObj::WalkPortals` [39%],
  `CWorldScene::SubmitOccluderBox` [17%], `CGWorldFrame::OnWorldUpdate` [16%],
  `DetailDoodad::CreateInstance` [8%], `CMap::UpdateMapObjDefs` [53%],
  `CMapRenderChunk::DrawLocal` [67%].
- **Shadow map to the reference's architecture** (`parity-shadowmap.md`): cascades
  `FUN_00874890`, `FUN_00874fb0`, `FUN_00875760`; the four `MapShadow.cpp` callbacks
  (`FUN_007bac10`, `FUN_007bafd0`, `FUN_007bd200`, `FUN_007bbc50`) over the 0xb90-byte cascade
  struct; the five caster walks; blur `FUN_008750b0`; three targets; hwPCF / D24X8; device-lost
  hook `FUN_00873fe0`.
- **Blob shadows** (`parity-shadows.md`): oriented-rectangle footprint and the unit box
  `FUN_0071ed80`; receiver query map-object half `FUN_007a6940 -> FUN_007aef00 -> FUN_007cb7b0`
  with BSP box walk `FUN_007ca920` (needs the loaded map-object instance list); doodad receivers
  `FUN_007ce960`; M2 receivers `FUN_0077f350 -> FUN_007a2aa0`.
- **Terrain through the original's shaders** (`parity-map-memory.md`): the archive
  `Terrain.bls` permutations, constant setup `FUN_007cfbe0`, permutation selection;
  `MapMemInitialize` to 100% (`FUN_007c3d90`, `FUN_007afee0`, `FUN_007cb990`, `FUN_007b2760`,
  `FUN_007a03c0`, the pools and the `WAREAMED` / `WDETAILDOODADINST` heaps).
- **Liquid** (`parity-liquid.md`): WMO liquid mesh factory `FUN_007d43f0` and its writers
  (`FUN_007a7b00`, `FUN_007a7920`, `FUN_007a7f60`, `FUN_007cbdc0`); the queue `FUN_00793d20`;
  the post-liquid pass `0x00790a80` with decals `FUN_0079d5e0`; pool allocation in
  `CChunkGeomFactory::Build`.
- **Stages with no port at all:** occluders `FUN_00796c10` (1,637 bytes), barriers
  `FUN_00794b50` (2,200 bytes, the one call missing from `OnWorldRender`), footprints
  `FUN_0079fcc0`, low-detail terrain `FUN_007cd910` / `FUN_007cc810`, weather (`MapWeather`,
  24 functions, driver `FUN_0078ca50`; `Weather.cpp` is not a port and goes), the fog override
  `FUN_007ed820`, the per-frame sky override at `0x007f0573`, and the glare pair `FUN_007f3230` /
  `FUN_007eecc0`, which the queue still shows unlinked although `DrawGlare` exists: tag or port.
- **The seven unanchored modules** (`DayNight`, `Sky`, `MapObj`, `MapObjGroup`, `M2Model`,
  `MapShadow`, `MapWeather`) once phase 0 makes them visible; `--module <name>.cpp` on each.

Exit: every map, liquid, shadow and sky module at 100% linked and faithful.

## Phase 3: the plumbing (1,096 unlinked)

Mechanical, well-anchored, and the device half is where frozen's design is whoa's rather than
the reference's. `RsPop` and `TextureCreate` already score 100%, so the pattern is known.

- **Texture async** (`parity-texture-async.md`): `AsyncTextureWait` `FUN_004b6550`,
  `TextureIncreasePriority` `FUN_004b6c50`, `SFile::IsStreamingMode`, `CTextureAtlas`; the
  `CreateBlpAsync` and `CreateTgaTexture` stubs; `TextureAllocGxTex` [25%],
  `AsyncFileReadWait` [56%]; `TextureCache` (195 / 39; `FUN_004f4460` has 33 callers;
  `MirrorInitialize` stub). `blp` and `tga` to 100%.
- **Full-screen effects:** `FFXEffects` 219 / 18 and `EffectGlow` 64 / 2. Render-to-texture is
  done; the glow (`FUN_004f8770`, `FUN_008c1770`, `FUN_008c1010`, `FUN_008c1100`) is not.
- **The device:** `CGxDeviceD3d9Ex` 238 / 17, `CGxDevice` 54 / 11, `CGxDeviceD3d` 15 / 1,
  `CGxD3d9ExTexture`. State sync, buffer pools, shader load, texture upload, to the reference's
  shape; `GxRs_TexGen` / `ColorOp` / `AlphaOp` implemented or recorded as diverged.
- **Live stubs to zero:** the 37 from `livestubs.py` (`M2Init` with 63 call sites,
  `CClientEnvironment::AddRef` with 35, `CM2Cache::GarbageCollect`, `M2BlendValue`, the
  `CGxFont` pair), each ported or recorded as a deliberate divergence.

Exit: every texture, effect and device module at 100% linked, no stubs.

## Phase 4: entities (1,779 unlinked)

Half of everything left, last because it is the largest and the least anchored, and because
the three phases before it close the modules its matchers lean on. Work it from the top down.

- **Animation chain top** (`unit-animation-chain-port.md`): `CGUnit_C::SetAnimation`
  `FUN_007385c0` [63%, 57 callers], `SetBoneSequence` [38%], `UpdateAnimation` `FUN_0073ac30`
  wired to the stand / emote / death / movement sites, the unit model builder `FUN_0073e410`,
  `m_animTier` via `FUN_007167c0`.
- **The CEffect list:** `ObjectEffect.cpp` is 81 functions with 13 linked; `FUN_00745230`
  (2.9 KB, 29 callers, the top of the render queue) creates them and `FUN_006f61d0` has 125
  callers. The vehicle passenger table blocks three appliers above it.
- **Movement:** `MovementShared`, 85 functions with 4 linked (`FUN_00988490`, `FUN_00987e30`,
  `FUN_0098c240`, `FUN_0098bff0`, `FUN_0098b0e0`, `FUN_0098bd10` lead the queue).
- **Missiles:** `UnitMissileTrajectory_C`, 93 / 4 (`FUN_007022d0`, `FUN_006fcd60`,
  `FUN_007015d0`).
- **Game objects and players:** `GameObject_C` 285 / 21 (`FUN_0070f160` first); `Player_C`
  728 / 59 (`FUN_006dcb40`, `FUN_006dc3f0`, `FUN_006ddbb0`); `Unit_C` 703 / 95, the rest of the
  render queue.

Exit: the entity row at 100% linked and faithful. With it, criteria 1 to 3 are met.

## Phase 5: verification, once

Deferred by decision until the surface is practically complete. Then, in one block:

1. Build, install with the PDB, and run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a
   map-0 character, `FROZEN_FORCE_TIME=12`, `FROZEN_AUTO_SCREENSHOT`, and `FROZEN_SHADOW_DUMP`.
   Expect it to fault; `tools/crashstack.py` and the commit log are the debugger.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive
   per-frame agreement from 58% (2026-09-18) to 100% on the render spine, marking each function
   `verified` as its counts match.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone,
   a WMO interior, underwater, rain, night) and drive each viewpoint to 99% or better.
4. Tighten fidelity from call order to branch and constant checks, and re-close whatever drops.

Exit: 100%.

## Order and pace

Phase 0 is one cycle. Phases 1 to 4 run in that order, each closed before the next so the
matchers stop guessing; `--fix` and `--helpers` batches are interleaved throughout because they
are the cheapest links there are. The late-September loops linked 150 to 350 functions on a good
day of cycles, so 3,949 links is on the order of fifteen to twenty-five such days, and the
fidelity work on the roots is on top of that. Everything above is a reference address or a frozen
name on purpose: the next cycle starts at `--next 20 --fix --render`, and this page says why.

## Progress log

One line per run, newest last. Render-surface figures are linked / total, then faithful, then
stubs, then live empty functions.

| run | linked | faithful | stubs | live empty | what moved |
|---|---|---|---|---|---|
| 2026-10-01 start | 1,267 / 4,838 | n/a | 20 | 37 | baseline from the 16:29 report |
| 2026-10-01 17:05 | 1,398 / 5,327 | 869 | 32 | 37 | phase 0: `anchors.py` recovered 239 dropped anchors, 19 modules joined the set, the report prints faithful/stub for the surface; linked and faithful totals unchanged (4,748 / 2,502), so the jump is denominator, not ports |
| 2026-10-01 17:14 | 1,398 / 5,327 | 870 | 32 | 37 | phase 1: `CM2Model::SetWorldTransform` 17% -> 92%, faithful. The missing half was not billboarding as its comment said: it is the tilt onto a surface normal (header flags bits 0 and 1) and the per-sequence blend into that tilt (sequence flags 2 / 4 / 8), which is how a creature settles onto the slope it dies on. Callers pass no axis yet, so straight-up is the default and nothing on screen changes until phase 4 threads the ground normal through. Client faithful 2,502 -> 2,503 |
| 2026-10-01 17:25 | 1,439 / 5,390 | 907 | 32 | 37 | phase 1: the ParticleColor.dbc override end to end -- `CM2ParticleEmitter::SetColors` / `GetColors` (`FUN_0097a990` / `FUN_0097ab10`), `CM2Model::SetParticleColors` (`FUN_00825410`) with model call 11, and the model30 inheritance in `InitializeLoaded` (58% -> 64%); `CameraCreate` tagged (`FUN_004bfca0`); the `M2ModelParticle` constructor's +0x81 and +0x85 corrected to start at 1, its address excluded as compiler-emitted. Then a ruler fix: `MODULE_RANGES` now gives `[0x978ad0, 0x97d370)` to `ParticleSystem2.cpp`, where 42 linked particle functions had been counted as a UI frame. Client linked 4,748 -> 4,753, faithful 2,503 -> 2,506; surface faithful +37, almost all from the range |
| 2026-10-01 17:45 | 1,439 / 5,390 | 909 | 32 | 37 | ruler, no ports: `clangparse.py` keyed every call to a FUNCTION template by its bare name, so calls to the five `M2AnimateTrack` instantiations the PDB carries never matched the reference's links to them. It now spells the instantiation the way the PDB does, and `recomp.py` folds an instantiation back to its bare name where the reference links the bare one (the `M2Init` family) so nothing that matched stops matching. `AnimateMT` 16% -> 48%, `AnimateMTSimple` 75% -> 100% (held off faithful by its branch check), `AnimateTextureTransformsMT` and `AnimateAttachmentsMT` now faithful. Client linked 4,753 -> 4,766, faithful 2,506 -> 2,526; no function lost faithful. Phase 0's last item closed: stale banners on the render inventory and `parity-depth.md` |
| 2026-10-01 17:52 | 1,441 / 5,390 | 911 | 32 | 37 | phase 1: `AnimateMT` 48% -> 64%, from two passes that were missing outright. The RIBBON track pass did not exist -- the ribbon driver forwarded colour, alpha, heights and texture slot from state nothing animated, so every ribbon ran on its constructor defaults. The PARTICLE track pass existed as `AnimateParticleTracks` with no caller; it is now the port of `FUN_0082d2f0` (visibility track, the rate/active gates from the emitter's own bits and live particles, the model's 0x400 bit, then the ten tracks, all defaulting to 0 where the stand-in guessed 1.0) and runs from `AnimateMT`'s tail. Found on the way: frozen's particle `enabled` byte was the visibility track's value, and the constructor's second write is +0x84 not +0x81. The 16-bit `M2AnimateTrack` (`FUN_0082bb50`) now exists and links, kept out of line like its siblings. Two hand verdicts recorded with reasons. Client linked 4,766 -> 4,768, faithful 2,526 -> 2,528 |
| 2026-10-01 17:55 | 1,441 / 5,390 | 911 | 32 | 37 | phase 1: `AnimateMT` 64% -> 89% call order. The bone BILLBOARD is now the reference's: a switch on `boneFlags & 0x78` at 0x82ff3d (spherical 0x8; cylindrical about X, Y, Z for 0x10 / 0x20 / 0x40), transcribed from the disassembly because the decompiler drops which row each `C3Vector::Normalize` acts on, then the reference's rescale by the original row lengths and pivot-preserving translation. It replaces two branches reasoned from what a glow sprite should look like, which a comment defended by claiming the reference does no billboarding there -- the jump table says otherwise. No totals moved: `AnimateMT` is held off faithful only by its branch check (0.43 of the reference's), most of which is the one block still unported, the parent-inheritance variants for `boneFlags & 7`. That is next |
| 2026-10-01 17:59 | 1,441 / 5,390 | 911 | 32 | 37 | phase 1: `AnimateMT` 89% -> 96% call order, branch ratio 0.43 -> 0.48 (the faithful bar is 0.5). The parent-inheritance variants (0x82f843..0x82fc25) ported from the disassembly: the parent's matrix copied and its 3x3 rebuilt against the model placement by `boneFlags & 6` (2 = parent rotation with model scale, 4 = model rotation with parent scale, 6 = both from the model), translation from the model on bit 0 or else recomputed to hold the pivot. It had been held back over not knowing which variant meant what; the arithmetic does not need the names. Two TODOs remain in the bone transform: the `*= +0x88` matrix on flag 0x80, and the no-rotation-track branch. Not installed: `Frozen.exe` was running, so `build/dist` still has the previous build |
