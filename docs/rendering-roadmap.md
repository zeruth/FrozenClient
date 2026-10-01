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

| | 2026-10-01 start | now (2026-10-01 21:30) | at completion |
|---|---|---|---|
| render surface linked | 1,267 / 4,838 (26%) | 1,457 / 5,390 (27%) | 5,390 |
| render surface faithful | not measured | 932 (17%) | 5,390 |
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

- **Roots** (`--fix`, fidelity in brackets): `CM2Model::InitializeLoaded` [58% -> 79%, 5.6 KB, 42
  callers], `CM2Scene::Animate` [67%], ~~`CM2Model::AnimateMT` [16%]~~ (faithful 2026-10-01), ~~`AnimateMTSimple` [75%]~~ (faithful 2026-10-01),
  ~~`SetWorldTransform` [17%]~~ (faithful 2026-10-01), `CM2SceneRender::Draw` [86%], `SetupLighting` [56% -> 78%; the rest is the device +0x1b4 getter, mis-linked as `GxCaps`],
  ~~`SetupTextures` [67%]~~ (faithful 2026-10-01), ~~`SelectLights` [67%]~~ (faithful 2026-10-01), `CM2Lighting::SetupGxFog` [33%],
  ~~`ReplaceTexture` [43%]~~ (faithful 2026-10-01), `OptimizeVisibleGeometry` [89%].
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

## Phase 3a: the D3D9 per-frame device path (moved ahead of entities, 2026-10-01)

Pulled forward by decision: every draw ends in the device, so a wrong state there is wrong on
every pixel, and the measure under-reports it -- the device is reached by virtual calls the spine
walk does not follow, most of the "unlinked device" bytes are the reference's OpenGL backend
(0x69xxxx; out of scope on Windows), and frozen's device was written from whoa's design rather
than ported. The real D3D9 surface is ~150 functions / 40 KB at 0x6a0000..0x6ac000 plus the weak
core. In order:

1. ~~`IRsSendToHw` (`FUN_006a4c30`) in full~~ (2026-10-01, 77 cases; the four tables verified byte
   for byte; see the progress log). Left: its texgen matrix half, which belongs to item 3.
2. ~~`DsSet` (`FUN_006a3c40`) completed.~~ (2026-10-01: all 182 slots reach D3D.) It is the reference's state/sampler cache and frozen's enum
   already mirrors its indices exactly; the body handles a handful and says `// TODO handle other
   device states`, so colour ops, texture-coordinate index, the extra sampler states (anisotropy,
   mip bias, max mip level) and point scale are cached but never SENT.
3. ~~Transform sync and viewport, and dirty tracking~~ (2026-10-01; see the progress log).
   `IStateSyncXforms`, its world and per-stage texture senders, `ISetTexGen`'s matrix half,
   `XformSetView`, `GxXformSetViewport`, `IRsDirty` and both `IRsForceUpdate` are faithful. The
   0% `GxXformSet` link was a matcher false positive: that address is view times projection, now
   `GxXformViewProj`; the reference has no out-of-line `GxXformSet`.
4. Texture creation and upload in the D3D9 texture file (0x6a7xxx..0x6aaxxx).

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
| 2026-10-01 18:05 | 1,441 / 5,390 | 912 | 32 | 37 | phase 1: **`AnimateMT` is faithful** -- 98% call order, branch ratio 0.57. The last pieces: the SECONDARY sequence's bookkeeping, which did not exist (nothing advanced the sequence a blend fades out of, and nothing ever expired it, so every blend sampled a stuck time and never ended -- its flag test is 0x80 where the primary's is 0x1); the `SetBoneMatrix` override applied on bone flag 0x80 (`*= matrix88`); the blend-weight fallback (a parentless bone other than bone 0 takes bone 0's weight); and the no-rotation-track TODO closed as the identity it already was. From 16% at the start of the day, almost all of it real behaviour: ribbon and particle tracks, billboarding, parent inheritance and secondary-sequence timing. Installed this time. Client faithful 2,528 -> 2,529 |
| 2026-10-01 18:11 | 1,442 / 5,390 | 913 | 32 | 37 | phase 1: `InitializeLoaded` 64% -> 71%. Its particle-emitter setup did the reference's work in a different order; reordered to the reference's sequence (initial track values, flag bits, head/tail, material bits, texture grid and animation, `SetMaterial`, track pointers, inherited colours, twinkle and motion constants, follow ramp, spline), checked line-for-line to add and drop nothing else. `SetFollow` (`FUN_00978dd0`) ported: nothing had ever set the follow ramp, so every particle inherited none of its emitter's movement. And a FIX TO ITERATION 5: the reference starts every particle record visible (state +0x80 = 1) at the end of this loop; frozen did not, and since the track pass now gates emission on that value, an emitter with no visibility track would never have emitted. Still unported here: the two spawned models (`FUN_00978b30`, `FUN_0097aeb0`) and the precompiled ramp (`FUN_0097d370`). Not installed: `Frozen.exe` was running |
| 2026-10-01 18:15 | 1,442 / 5,390 | 913 | 32 | 37 | phase 1: `InitializeLoaded` 71% -> 79%. The stretch between the emitters and the deferred-call replay was mostly absent. Ported: the instance-count bit 0x10; releasing the source model (`model30`), which frozen never let go of; the cut-down-animate bit 0x1000 -- nothing set it, so `AnimateMTSimple`, ported and faithful, was never chosen for a one-bone model; the state bits (0x800 set across the replay, 0x2 cleared); STARTING THE MODEL ON ITS STAND ANIMATION, which frozen left to whatever owner happened to set a sequence; and the per-frame report bit 0x400000. The reference's async-wait-and-reenter block is unreachable (gated on the loaded bit the line after setting it) and is left out with a note. Left in this function: the ribbon setup's static tables and `FUN_0082dac0`, the emitter constructor order, the spawned models, the ramp, and calling `SetRibbonFlag8` from the replay. Not installed: `Frozen.exe` was running |
| 2026-10-01 18:40 | 1,442 / 5,390 | 914 | 32 | 37 | **REGRESSION FIXED: exploding geometry.** d3b7704b set the 0x1000 bit, so one-bone models (most doodads) took `AnimateMTSimple` for the first time -- and frozen's port of it never wrote the bone matrices, so those models drew through whatever the bone buffer held. Reproduced on screen. The reference copies `matrixF4` into bone 0 at 0x82e495, after advancing bone 0's primary sequence; both now ported. The fidelity score had passed the function at 100% call order because that copy is the compiler's matrix assignment, which is excluded from the comparison -- the gap was invisible to the measure. `AnimateMTSimple` now faithful. LESSON for the rest of the roadmap: a port that turns on a code path nothing exercised before is where the deferred-verification cost lands first |
| 2026-10-01 18:50 | 1,444 / 5,390 | 917 | 32 | 37 | phase 1: `ReplaceTexture` 43% -> 100%, faithful. A replaceable texture now reaches a model's ribbons (`CM2Ribbon::ReplaceTexture`, `FUN_0097fad0`) and particle emitters (`CM2ParticleEmitter::ReplaceTexture`, `FUN_00978c40`) -- both were TODOs -- and the model drops its shared-instance bit afterwards, as the reference does. Corrected on the way: the ribbon's third texture array holds texture TYPE ids, not record addresses (the fill at 0x833912 is a load), so frozen compares through its record pointers. `CM2Scene::Animate`'s missing doodad-grouping tail is now decoded and written into the note at `IsBatchDoodadCompatible`, which had wrongly said nothing in `Animate` writes the instance count; the port waits on one unidentified model field (+0x1b8) and on a path that is dead until the doodad chain lands, so it was not guessed at. Client linked 4,769 -> 4,771, faithful 2,531 -> 2,534 |
| 2026-10-01 19:15 | 1,446 / 5,390 | 920 | 32 | 37 | phase 1, **the liquid plane, end to end.** Nothing in frozen ever told a model which side of the water it was on. The reference does it from the map: its world lighting callback (`FUN_00780cd0`) calls the placement object's `SelectUnderwater` (vtable slot 2), which for a doodad (`FUN_007c23f0`, now `CMapDoodadDef::SelectUnderwater`) asks the building's group liquid or the terrain liquid once, caches the answer, and applies it (`FUN_007c10c0`, now `CMapStaticEntity::ApplyWaterSide`): above only, below only, or straddling with the plane z = surface. `CM2Lighting::CameraSpace` now carries that plane into camera space (it moved only the lights) and marks the lighting done. `CM2SceneRender::SetupLighting` then does what it was missing: clips a straddling model against the plane -- through `matrix0` into clip space on a D3D-convention device with shaders, negated on pass 2 -- and publishes the element's `dword3c` for the shader selector. Frozen's stand-in lighting callback is kept (no `SelectLights` override is ported, so the reference's object branch would strip doodads of light); only the slot-2 call is added, and map doodads now pass their def as the callback argument. `TransformVector4` moved to tempest so the world and the model share the reference's one function. **This switches on a path nothing exercised before** -- doodads at a water line now clip against it, and submerged ones join the below-water list -- so it is the first place to look if a doodad near water renders wrongly. Client linked 4,771 -> 4,773, faithful 2,534 -> 2,537 |
| 2026-10-01 19:30 | 1,446 / 5,390 | 922 | 32 | 37 | phase 1: `CM2Scene::SelectLights` and `CM2SceneRender::SetupTextures` faithful. **A real lighting bug in the first**: the point-light grid bounds go through the C runtime's `floor` (`FUN_0088ce30`) in the reference and were truncated in frozen, which rounds toward zero -- so across the negative half of the world every sweep started a cell high and missed the low edge's lights. The second now walks every texture stage the batch names and then blanks the rest below two, where it walked exactly two and would have dropped a third or fourth texture; its texture-matrix branch is in the reference's order. Client faithful 2,537 -> 2,539 |
| 2026-10-01 19:45 | 1,446 / 5,390 | 924 | 32 | 37 | phase 1: `CM2Model::IsDrawable` and `~CM2Model` faithful. `IsDrawable` now requires everything ATTACHED to a model -- helm, weapons -- to be drawable too, cached in bit 0x200, where it reported the parent ready while an attachment's textures were still loading. The destructor's tail was three leaks: the deferred-call queue of a model destroyed before it loaded (and any texture handle a queued replacement held), the optimised geometry's GPU index buffer and pool (`UnoptimizeVisibleGeometry` was never called on destroy), and the two matrix frees now sit where the reference has them. Destructor faithful by hand verdict: 100% call order, the branch count reads low only because frozen's per-list unlink helpers hold the branches the reference inlines. Client faithful 2,539 -> 2,541 |
| 2026-10-01 20:30 | 1,457 / 5,390 | 932 | 32 | 37 | **phase 3a, item 1: `IRsSendToHw`.** The reference's D3D9 render-state translator has 77 cases; frozen's had ~33. The four lookup tables (source and destination blend, depth compare, cull) were checked byte for byte against the binary -- all match, so the existing cases were right. Added: the fixed-function material and specular enable, normalise-normals, lighting under its master-enable bit, fog now under its master-enable bit and cached (it bypassed both), per-stage colour and alpha combiners, texgen's coordinate half, per-stage texture-coordinate index (frozen's `Unk69`-`76`, now named `GxRs_TexCoord0`-`7`), point size / scaling / min / max / sprites, and the constant blend factor (`Unk84`, now `GxRs_BlendFactor`) -- which `GxBlend_ConstantAlpha` blends with and which was never set. Structured as the reference's helpers, each tagged; `ISetTexture`, `IShaderBindPixel` and `IShaderBindVertex` identified and given their fixed-function stage bookkeeping. **The fixed-function states are cached but not yet sent: `DsSet` has no case for them -- item 2.** Client linked 4,773 -> 4,784, faithful 2,542 -> 2,549 |
| 2026-10-01 20:45 | 1,457 / 5,390 | 933 | 32 | 37 | **phase 3a, item 2: `DsSet` completed.** It is the reference's state and sampler cache, and frozen's enum already mirrored its indices exactly -- but the body sent 11 of 182 slots. **Maximum anisotropy had been cached and never sent**, so every texture sampled without it; likewise texture-transform flags, the coordinate index, the stage combiners and their arguments, the material sources, ambient, clip planes and point scale. All reach D3D now. The colour-write slot takes the Gx mask and remaps it, as the reference's does, and `IRsSendToHw` sends its own colour write directly with its own cache, as the reference's does. **Visible risk**: frozen draws with no pixel shader in a few passes (`CWorldScene.cpp:445`, `CMapRenderChunk.cpp:965`), and the combiners `CShaderEffect::SetFixedFunc` asks for there were dropped until now -- those passes ran on D3D's default stage setup and now get what they ask for. `DsSet` faithful by hand verdict (100% calls; the 180-label switch is range tests here) |
| 2026-10-01 21:30 | 1,461 / 5,390 | 939 | 32 | 37 | **phase 3a, item 3: transform sync and dirty tracking.** The reference keeps eight more matrix stacks than frozen did, one per texture stage at +0x1c10, holding the matrix the stage's texgen mode generates; they are `m_texGenXforms` now. `ISetTexGen` fills them (inverse view, times inverse world for mode 1; the sphere-map scale-and-bias for mode 6; identity otherwise), and `IStateSyncXforms` sends the world transform (skipping runs of identities) and one texture transform per stage, texgen times the application's matrix, with the texture-transform flags the per-stage state 61+n asks for. **Two bugs fixed in `XformSetView`** (newly linked, `FUN_00689050`): it never re-ran texgen for stage 7, and a signed compare re-ran it for every stage in mode 0. `GxXformSetViewport` now validates and returns, as the reference does, instead of asserting, and checks `minZ >= 0 && maxZ <= 1` as well. The dirty list grows through `SetCount` as the reference's does. **Two matcher false positives corrected**: `FUN_0057c340` is the matrix stack's load-identity (`CGxMatrixStack::SetIdentity`), not `GxShaderConstantsSet`, and `FUN_00682130` is view times projection (`GxXformViewProj`), not `GxXformSet`. `FUN_006a43a0` is the no-argument `C44Matrix::Inverse`. **Visible risk**: all fixed-function only, and inert while a vertex shader is bound. With default states every stage's texture transform stays disabled. Client linked 4,784 -> 4,789, faithful 2,550 -> 2,563 |
