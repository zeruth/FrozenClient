# Rendering: the road to 100%

A plan, not a status page: `docs/recomp/REPORT.md` is the status. Every item below is a reference
address or a frozen name on purpose, so the next cycle can start from this page.

**Completion first, verification last** (decision 2026-10-01). Runs, traces and scene compares
wait until the render surface is practically 100% linked and faithful; until then a cycle is
measured by the report's static numbers, and the gates that end it are the static ones in
CLAUDE.md (a failed build, a lost link, a lost binding, a negative delta). The cost is accepted:
ports stack up unseen and the first run will find a batch of defects at once, so every port
records what it diverges from and why, in the commit and in `overrides.json`.

**The reference is the vanilla `WoW.exe` (12340) in `.reference`, and only that.** Until
2026-10-02 the Ghidra tooling read `RunicWorldGame.exe`, a branded patch of the same binary: same
layout and addresses, but 14 strings and four code sites differ (0x4da7e5 and 0x52abd9 skip the
GlueXML/FrameXML signature checks, 0x4e0481 lets every character name pass, 0x7f5f9f forces a
branch). `recomp.GHIDRA_PROGRAM` now names the vanilla program and all exported data comes from it.

## What 100% means

The render surface is the reference functions in the modules that draw the world
(`RENDER_MODULES` in `tools/recomp/recomp.py`): 5,377 today. Completion is:

1. every one **linked** to a frozen function, none of them a stub;
2. every one **faithful**: the reference's calls in the reference's order, then branch and
   constant shape;
3. `tools/livestubs.py` reporting no empty function with a live render call site;

and then, last, **verified**: every render-spine function seen behaving like the reference in a
trace or scene compare, with the scene-compare suite at 99% or better.

## Where it stands

| | 2026-10-01 start | now (2026-10-02) | at completion |
|---|---:|---:|---:|
| render surface linked | 1,267 / 4,838 (26%) | 1,575 / 5,377 (29%) | 5,377 |
| render surface faithful | not measured | 1,054 (20%) | 5,377 |
| render surface stubs | 20 | 33 | 0 |
| empty functions with live render call sites | 37 | 35 | 0 |
| D3D9 device census (vtable 0x00a2e718, 228 functions) | 199 linked, 154 faithful | 227 linked, 223 faithful | 228 / 228 |
| render surface attributed by anchor guess | 63% | 49% | low |

By area (2026-10-02; functions are the report's module counts, grouped):

| area | functions | linked | faithful | unlinked | share of unlinked |
|---|---:|---:|---:|---:|---:|
| entities (`Unit_C`, `Player_C`, `GameObject_C`, `Movement`, `Passenger`, effects, missiles, spell visuals) | 2,055 | 209 | 131 | 1,846 | 49% |
| world layer and map streaming (`Map`, `MapMem`, `MapChunkLiquid`, `DetailDoodad`, `World*`, `Camera`, `MapWeather`, `WorldText`) | 1,042 | 366 | 217 | 676 | 18% |
| textures and full-screen effects (`Texture*`, `blp`, `tga`, `FFXEffects`, glow, `Lightning`) | 793 | 235 | 156 | 558 | 15% |
| models and particles (`M2*`, `ParticleSystem2`, `CharacterModelBase`, `ModelBlob`, `GfxSingletonManager`) | 602 | 362 | 220 | 240 | 6% |
| map geometry (`MapChunk`, `MapLoad`, `MapArea`, `MapObj*`, `AaBsp`, `MapLowDetail`) | 327 | 148 | 106 | 179 | 5% |
| D3D9Ex and OpenGL devices (not used on Windows) | 202 | 72 | 54 | 130 | 3% |
| liquid, shadows, shader effects (`Liquid`, `ShadowMap`, `MapShadow`, `ShaderEffect`) | 192 | 108 | 64 | 84 | 2% |
| D3D9 device by module name (see the census instead) | 164 | 75 | 54 | 89 | 2% |
| **total** | **5,377** | **1,575** | **1,002** | **3,802** | |

Read the chart with two caveats. Half the surface is still placed in its module by the nearest
path string rather than a known boundary, so rows can be mis-sized (`MODULE_RANGES` is the fix per
module; `DayNight` and `Sky` have no string at all and count under their neighbours). The device
rows are the worst case: several of the plain D3D9 device's functions are anchored under the
D3D9Ex module names, so the D3D9 device is measured by its vtable census above, not by these rows.
Faithful by area counts links whose module is known, which is 1,002 of the report's 1,054.

## How a cycle runs

The loop in CLAUDE.md, minus the run: `recomp.py`, `--next`, port, build, `clangparse.py`,
`recomp.py --pdb`, commit with the delta. Rules learned the hard way:

- **Fix before link.** `--next 20 --fix --render`, then `--diff <addr>` on each. Most gaps are a
  helper frozen calls under its own name; a tag costs minutes and lifts every caller.
- **Seeds every few cycles.** `--next 40 --helpers`: one identified leaf lifts hundreds of
  callers and feeds the matchers.
- **Port chains from the top.** Check what feeds a stub before writing it.
- **Close a module, then move.** A module at 100% stops feeding false order-matcher links to its
  neighbours.
- **Tag the identified-but-not-ported.** A tag or an `overrides.json` entry takes seconds.
- **Read every new inferred link.** The order, call-graph and string matchers are confidently
  wrong often enough (`CGGameUI::Initialize` was string-matched to the wrong function until
  2026-10-02).
- **A hand verdict needs a read.** A branch-shape or call-order miss caused by inlined templates
  or block layout is a verdict in `overrides.json`, written only after reading the port against
  the decompilation. Merge it into an existing entry; replacing one can drop its `frozen` link.
- **Record divergence in the same commit**, `status: "diverged"` with the reason.

## Done

- **Phase 0, the ruler** (2026-10-01). `tools/recomp/anchors.py` recovers the path-string
  anchors the export dropped (239 more across 63 modules), 19 modules joined `RENDER_MODULES`,
  the report prints faithful and stub counts for the surface, and the stale inventory pages carry
  banners.
- **Phase 3a, the D3D9 device** (2026-10-01/02). Pulled ahead of the rest because every draw
  ends in it. Closed by a census of the device's vtable (0x00a2e718) over 0x681000..0x6ac000
  rather than an address range, which is how the range's misses were found. Ported: the
  render-state translator `IRsSendToHw` and the complete `DsSet`; transform sync, texgen and
  dirty tracking; texture create, upload, destroy and the reset walk; buffers, pools and the
  scratch fallback; device create, destroy, format change and resource release; caps; the
  window class, window, window procedure with its sizing helpers, and `DeviceWM`; scene begin,
  end and present with the frame cap and fixLag; render targets, depth-stencil and the
  back-buffer copies; occlusion queries; frame capture; gamma; device callbacks; the
  vertex-declaration cache; NVIDIA stereo; the immediate-mode primitive; the hardware and
  software cursor with the UI shaders and placeholder texture; Storm's `SLog` and Logs\gx.log;
  the base constructor and destructor. On its path: the W32 time manager, `TextureLoadImage`
  with the mip-bits cache and TGA readers, and the client cursor module.

## Open phases

### Phase 1: models (240 unlinked)

Smallest area with the least faithful large roots. Closing it stabilises the matchers for
everything hanging off `CM2Model`.

- **Unfaithful roots** (call order, branch ratio): `CM2Model::InitializeLoaded` [79%, 0.66; left:
  the ribbon setup's static tables and `FUN_0082dac0`, the emitter constructor order, the spawned
  models `FUN_00978b30` / `FUN_0097aeb0`, the ramp `FUN_0097d370`], `CM2Scene::Animate` [67%,
  0.29; the doodad-grouping tail, decoded in the note at `IsBatchDoodadCompatible`, waits on model
  field +0x1b8], `CM2SceneRender::Draw` [86%, 0.31], `SetupLighting` [78%, 0.71; the rest is the
  device +0x1b4 getter, mis-linked as `GxCaps`], `CM2Lighting::SetupGxFog` [33%, 0.07],
  `OptimizeVisibleGeometry` [89%, 0.41].
- **Stubs with live callers:** `DrawBatchDoodad`; `DrawBatchProj` (`FUN_00829aa0`, plus the
  projection callback `FUN_0077f500` from world init); `SubstituteSpecializedShaders`
  (`FUN_00837680`); the second geometry builder `FUN_0082be60`.
- **Particles** (`parity-particles.md`): the shader permutation selector `FUN_00873160` /
  `FUN_00872de0`, the ramp fast path `FUN_00979d60`, the type-3 emitter `0x009820f0`, the
  spawned-model pass `FUN_0097e8d0`. Retire `ParticleFx.cpp` once only the ported sim runs.
- **Ribbons** (`parity-ribbons.md`, nothing ported): `CRibbonEmitter` `FUN_009808a0` /
  `FUN_00980b70`, `DrawRibbon` `FUN_00820f40`, the model's ribbon array and its walk.
- **Instanced caster draw:** `FUN_0082da40 -> FUN_00829e40 -> FUN_00829ba0`.
- **Model cache** (`parity-model-cache.md`): hash `FUN_0081c390`, pending release
  `FUN_0083dc90`, `GarbageCollect` `FUN_0081c290`, `UpdateShared` `FUN_0081c790`.
- **Unlinked bodies ranked highest:** `FUN_0082ec30`, `FUN_008292a0`, `FUN_0083dfa0` /
  `FUN_0083e140` in `M2Shared` (65, 27 linked); `ModelBlob` (28, none linked);
  `CharacterModelBase` (24, 6 linked).

Exit: `M2Scene`, `M2Shared`, `CharacterModelBase`, `ModelBlob` at 100% linked, no stubs, roots
faithful.

### Phase 2: the environment (676 world-layer + 179 geometry + 84 liquid/shadow unlinked)

The most-ported area; finishing it closes the map modules and their matcher noise.

- **Unfaithful roots:** `CMap::Update` [11%], `CMap::MapMemInitialize` [0%], `CMap::Render`
  [42%], `CWorldScene::UpdateCamera` [56%], `CMapObj::WalkPortals` [39%],
  `CWorldScene::SubmitOccluderBox` [17%], `CGWorldFrame::OnWorldUpdate` [16%],
  `DetailDoodad::CreateInstance` [8%], `CMap::UpdateMapObjDefs` [53%],
  `CMapRenderChunk::DrawLocal` [67%].
- **Shadow map to the reference's architecture** (`parity-shadowmap.md`): cascades
  `FUN_00874890`, `FUN_00874fb0`, `FUN_00875760`; the four `MapShadow.cpp` callbacks
  (`FUN_007bac10`, `FUN_007bafd0`, `FUN_007bd200`, `FUN_007bbc50`) over the 0xb90-byte cascade
  struct; the five caster walks; blur `FUN_008750b0`; three targets; hwPCF / D24X8; the
  device-lost hook `FUN_00873fe0`.
- **Blob shadows** (`parity-shadows.md`): the oriented-rectangle footprint and unit box
  `FUN_0071ed80`; the map-object receiver query `FUN_007a6940 -> FUN_007aef00 -> FUN_007cb7b0`
  with the BSP box walk `FUN_007ca920`; doodad receivers `FUN_007ce960`; M2 receivers
  `FUN_0077f350 -> FUN_007a2aa0`.
- **Terrain through the original's shaders** (`parity-map-memory.md`): the archived
  `Terrain.bls` permutations, constant setup `FUN_007cfbe0`, permutation selection, and
  `MapMemInitialize` to 100% (`FUN_007c3d90`, `FUN_007afee0`, `FUN_007cb990`, `FUN_007b2760`,
  `FUN_007a03c0`, the pools and the `WAREAMED` / `WDETAILDOODADINST` heaps). This also retires
  the device's last frozen-only state, table fog in `IStateSetD3dDefaults`.
- **Liquid** (`parity-liquid.md`): the WMO liquid mesh factory `FUN_007d43f0` and its writers
  (`FUN_007a7b00`, `FUN_007a7920`, `FUN_007a7f60`, `FUN_007cbdc0`); the queue `FUN_00793d20`;
  the post-liquid pass `0x00790a80` with decals `FUN_0079d5e0`; pool allocation in
  `CChunkGeomFactory::Build`.
- **Stages with no port at all:** occluders `FUN_00796c10`, barriers `FUN_00794b50` (the one call
  missing from `OnWorldRender`), footprints `FUN_0079fcc0`, low-detail terrain `FUN_007cd910` /
  `FUN_007cc810`, weather (`MapWeather`, driver `FUN_0078ca50`; `Weather.cpp` is not a port and
  goes), the fog override `FUN_007ed820`, the per-frame sky override at `0x007f0573`, and the
  glare pair `FUN_007f3230` / `FUN_007eecc0` (tag or port; `DrawGlare` exists).

Exit: every map, liquid, shadow and sky module at 100% linked and faithful.

### Phase 3: textures and effects (558 unlinked)

- **Texture async** (`parity-texture-async.md`): `AsyncTextureWait` `FUN_004b6550`,
  `TextureIncreasePriority` `FUN_004b6c50`, `SFile::IsStreamingMode`, `CTextureAtlas`, the
  `CreateBlpAsync` and `CreateTgaTexture` stubs, `TextureAllocGxTex` [25%], `AsyncFileReadWait`
  [56%], `TextureCache` (41, 15 linked; `MirrorInitialize` is a stub). `blp` (94, 37 linked) and
  `tga` (82, 45 linked) to 100%; the image loaders landed with the cursor.
- **Full-screen effects:** `FFXEffects` (171, 17 linked), `EffectGlow` (38, 2), `PassGlow` (26, 0).
  The glow itself (`FUN_004f8770`, `FUN_008c1770`, `FUN_008c1010`, `FUN_008c1100`) is unported.
- **What the D3D9 census left:** a proper read of `GxPrimVertexPtr` (0x682400) and `IRsSendToHw`
  (0x6a4c30) for a verdict; the 0x38-byte zero fill at +0x3ae0 in the D3D9 constructor
  (0x68fd50); vtable slot 21 (0x6a1950, no caller found); the base constructor recorded as
  diverged until `CGxCaps` is layout-faithful; the money, object and spell item cursors
  (`FUN_00616510`, `FUN_00616630`, `FUN_00616720`). The D3D9Ex and OpenGL devices are out of
  scope on Windows.
- **Live stubs to zero:** the 35 from `livestubs.py` (`M2Init` with 63 call sites,
  `CClientEnvironment::AddRef` with 35, `CM2Cache::GarbageCollect`, `M2BlendValue`, the `CGxFont`
  pair), each ported or recorded as a deliberate divergence.

Exit: every texture and effect module at 100% linked, no stubs.

### Phase 4: entities (1,846 unlinked)

Half of everything left, last because it is the largest and least anchored and the phases before
it close the modules its matchers lean on. Top down:

- **Animation chain top** (`unit-animation-chain-port.md`): `CGUnit_C::SetAnimation`
  `FUN_007385c0` [63%, 57 callers], `UpdateAnimation` `FUN_0073ac30` wired to the stand, emote,
  death and movement sites, the unit model builder `FUN_0073e410`, `m_animTier` via
  `FUN_007167c0`. Threading the ground normal through also turns on the death tilt
  `SetWorldTransform` already supports.
- **The CEffect list:** `ObjectEffect` (41, 10 linked); `FUN_00745230` creates them and
  `FUN_006f61d0` has 125 callers. The vehicle passenger table blocks three appliers above it.
- **Movement:** `MovementShared` (83, 4 linked; `FUN_00988490`, `FUN_00987e30`, `FUN_0098c240`,
  `FUN_0098bff0`, `FUN_0098b0e0`, `FUN_0098bd10` first) and `Movement` (249, 5).
- **Missiles:** `UnitMissileTrajectory_C` and `Missile_C` (93, 4 linked; `FUN_007022d0`,
  `FUN_006fcd60`, `FUN_007015d0`).
- **Game objects and players:** `GameObject_C` (285, 21; `FUN_0070f160` first), `Player_C`
  (479, 54; `FUN_006dcb40`, `FUN_006dc3f0`, `FUN_006ddbb0`), `Unit_C` (643, 95).
- **Spell visuals:** `SpellVisuals` (65, none linked).

Exit: the entity row at 100% linked and faithful. With it, criteria 1 to 3 are met.

### Phase 5: verification, once

Started when phases 1 to 4 are practically done, in one block:

1. Build, install with the PDB, run with `FROZEN_AUTO_LOGIN`, `FROZEN_AUTO_CHARACTER` on a map-0
   character, `FROZEN_FORCE_TIME=12`, `FROZEN_AUTO_SCREENSHOT` and `FROZEN_SHADOW_DUMP`. Expect a
   fault; `tools/crashstack.py` and the commit log are the debugger. **Owed a first look,
   newest first:** the device cursor at login and in the world; resize and alt-tab (the reset path
   lost two frozen workarounds); the QPC timer; character creation now refusing invalid names;
   doodads at a water line (the liquid plane clips them); one-bone doodads (`AnimateMTSimple`);
   DXT textures gaining their smallest mips; fixed-function passes getting the combiners they ask
   for; the 200/30 FPS caps.
2. Trace both clients with `tools/recomp/calltrace.py` over the same frames and drive per-frame
   agreement on the render spine from 58% (2026-09-18) to 100%, marking each function `verified`.
3. Baseline `tools/scene-compare` on a fixed suite (outdoor noon, dawn in a `highlightSky` zone,
   a WMO interior, underwater, rain, night) and drive each viewpoint to 99% or better.
4. Tighten fidelity from call order to branch and constant checks and re-close what drops.

Exit: 100%.

## Order and pace

Phases 1 to 4 in that order, each closed before the next so the matchers stop guessing, with
`--fix` and `--helpers` batches interleaved because they are the cheapest links there are. Good
days of cycles have linked 150 to 350 functions, so 3,802 links is on the order of fifteen to
twenty-five such days, with the fidelity work on the roots on top.

## Progress log

One line per run, newest last: render surface linked / total, faithful, stubs, live empty
functions. The commit log has the detail.

| run | linked | faithful | stubs | live empty | what moved |
|---|---|---|---|---|---|
| 2026-10-01 start | 1,267 / 4,838 | n/a | 20 | 37 | baseline |
| 2026-10-01 | 1,398 / 5,327 | 869 | 32 | 37 | phase 0: recovered anchors grew the denominator (no ports) |
| 2026-10-01 | 1,398 / 5,327 | 870 | 32 | 37 | `SetWorldTransform` faithful: the surface-normal tilt and its per-sequence blend |
| 2026-10-01 | 1,439 / 5,390 | 907 | 32 | 37 | ParticleColor.dbc override end to end; `ParticleSystem2` given its range |
| 2026-10-01 | 1,439 / 5,390 | 909 | 32 | 37 | ruler: template instantiations matched; `AnimateMTSimple` 75% -> 100% |
| 2026-10-01 | 1,441 / 5,390 | 911 | 32 | 37 | `AnimateMT`: ribbon and particle track passes, which were missing |
| 2026-10-01 | 1,441 / 5,390 | 912 | 32 | 37 | `AnimateMT` faithful: billboarding, parent inheritance, secondary sequence |
| 2026-10-01 | 1,442 / 5,390 | 913 | 32 | 37 | `InitializeLoaded` 64% -> 79%: emitter order, `SetFollow`, the stand animation |
| 2026-10-01 | 1,442 / 5,390 | 914 | 32 | 37 | regression fixed: one-bone models never wrote their bone matrix |
| 2026-10-01 | 1,444 / 5,390 | 917 | 32 | 37 | `ReplaceTexture` faithful: ribbons and emitters take replaceable textures |
| 2026-10-01 | 1,446 / 5,390 | 920 | 32 | 37 | the liquid plane end to end: doodads clip at the water line |
| 2026-10-01 | 1,446 / 5,390 | 922 | 32 | 37 | `SelectLights` (a `floor` bug missed lights in negative space), `SetupTextures` |
| 2026-10-01 | 1,446 / 5,390 | 924 | 32 | 37 | `IsDrawable` waits on attachments; `~CM2Model` lost three leaks |
| 2026-10-01 | 1,457 / 5,390 | 932 | 32 | 37 | phase 3a: `IRsSendToHw`, 33 -> 77 cases |
| 2026-10-01 | 1,457 / 5,390 | 933 | 32 | 37 | phase 3a: `DsSet` sends all 182 slots (anisotropy had never been sent) |
| 2026-10-01 | 1,461 / 5,390 | 939 | 32 | 37 | phase 3a: transform sync, texgen, dirty tracking |
| 2026-10-01 | 1,468 / 5,390 | 944 | 32 | 37 | phase 3a: texture create, upload (DXT small mips), destroy (leak), reset |
| 2026-10-01 | 1,486 / 5,390 | 955 | 32 | 37 | phase 3a: buffers, device destroy (was empty), format change |
| 2026-10-01 | 1,492 / 5,390 | 957 | 32 | 37 | phase 3a: frame cap, fixLag, scene and present, window fit |
| 2026-10-01 | 1,496 / 5,390 | 959 | 32 | 37 | phase 3a: caps, `DeviceWM` focus, `CGxFormat` defaults |
| 2026-10-01 | 1,541 / 5,379 | 1,001 | 32 | n/m | phase 3a: the device census; queries, targets, capture, SLog, declarations, stereo |
| 2026-10-01 | 1,571 / 5,377 | 1,023 | 33 | n/m | phase 3a closed: cursor, window procedure, time manager, `TextureLoadImage` |
| 2026-10-01 | 1,575 / 5,377 | 1,054 | 33 | n/m | hand verdicts for ~45 census functions; NVAPI thunks tagged |
| 2026-10-02 | 1,575 / 5,377 | 1,054 | 33 | 35 | reference switched to the vanilla `WoW.exe`; two patch leaks fixed |
