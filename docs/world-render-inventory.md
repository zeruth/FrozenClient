# World render completeness inventory (3.3.5a 12340 vs frozen)

## Verification (2026-09-14, second pass)

Every row below that claims **ported** or **stand-in** was re-checked by reading the frozen source as
it stands in the working tree: that the named function exists and does what the row says, that it is
actually reachable from `CGWorldFrame::OnWorldRender` / `CGWorldFrame::OnWorldUpdate` (through
`TerrainUpdate`, `TerrainRender`, `SkyRender`, `RenderWmos`, `WmoUpdateVisibility`, `LiquidRender`,
`DetailDoodadRender`, `WeatherRender`, `ParticleFxRender`, `UnderwaterOverlayRender`,
`BlobShadow*`, `PlayerNameRenderWorldText`), and that it is not gated off behind a flag or an early
return. Statuses and notes that could not be substantiated were corrected in place.

**Nothing in this document has been verified visually against the reference client.** The
scene-compare harness (`tools/scene-compare`) has never been run against any of the 2026-09-14
work, and the client was not run while this pass was made. Here "ported" and "stand-in" mean *the
code exists and is called every frame*, not *it looks right*.

A trailing *(suspect)* on a status means the stage is wired into the render path but reading it
turned up a concrete defect serious enough that it is probably wrong, or unsafe, at runtime; the
notes column says what. Five stages carry it: WMO portal visibility, blob shadows, liquid bucket 0
(WMO liquid), liquid bucket 1 (WMO liquid) and the particle stand-in.

**The defect lists in this file were audited on 2026-09-23 and every one of them was stale.**
Eight concrete claims, spread across the particle, blob shadow, liquid and portal rows, described
code that had already been fixed: a use-after-free on despawned models, the UI-shader form of the
blob pass, a fixed-size liquid colour buffer, a transposed MLIQ axis, an MH2O bitmap read at the
wrong stride, a leak in the legacy liquid path, a frustum that dropped its near and far planes
past the first portal, and an unbounded portal re-walk. Each was re-read against the current
source and struck through in place with what the code actually does now.

That matters beyond tidiness: parts of two cycles went into re-investigating problems that were
already solved, because this file is what a session plans from. **When a defect is fixed, strike
it here in the same change.** The *suspect* marks stay on those rows regardless, because what
they are suspect about has not changed: none of it has been seen running.

Two facts found on 2026-09-14 while debugging crashes apply across several rows and are repeated in
the notes where they matter:

* **`GxRs_PolygonOffset` is handled by no device backend.** `src/gx/d3d/CGxDeviceD3d.cpp` has no
  `case GxRs_PolygonOffset` in its render-state switch, and neither the GLL nor the GLES device
  reads it; `src/gx/CGxDevice.cpp:639` only zeroes the app-side state. Depth bias is therefore
  unavailable everywhere, so no stage that draws a decal coplanar with the surface under it has any
  way to lift it off.
* The user reports **visible z-fighting on models** and **problems with entity shadows** in the
  running client. No row below should be read as contradicting that.

## Scope and sources

Reference binary: **RunicWorldGame.exe** (Windows 3.3.5a build 12340, stripped) in the Ghidra
project `C:\Users\tyler\tools\ghidra-projects\RunicWorld`. All addresses below are from that
program (image base 0x400000).

The Mac builds in the same project (`WoW-3.3.5a-mac-i386`, `WoW-3.3.5a-mac-ppc`) were checked
first and turned out to be **stripped as well**: no `CWorld::`/`CMap::` function symbols exist
(only ~50 mangled names, all CRT/system; the only `CWorld::` strings are CVar/assert text). They
were therefore not used. Function identities below were recovered from:

* assert-string module names (`.\World.cpp`, `.\WorldScene.cpp`, `.\Map.cpp`, `.\MapChunk.cpp`,
  `.\MapWeather.cpp`, `.\WorldFrame.cpp` ...), which fix the link-order address ranges of each
  source file (World.cpp ~0x77e-0x784, MapWeather ~0x786-0x78d, WorldParam ~0x78d-0x78e,
  WorldScene ~0x78f-0x79e, Map.cpp ~0x79e-0x7a3, MapObj ~0x7a8-0x7b1, DetailDoodad ~0x7b2-0x7b3,
  MapShadow ~0x7bb, MapLoad ~0x7bd, MapChunk ~0x7c5-0x7c6, MapObjGroup ~0x7c8-0x7cc,
  MapLowDetail ~0x7cc-0x7cd, MapChunkLiquid ~0x7ce-0x7cf, MapArea ~0x7d0-0x7d6,
  Shadow (blob) ~0x7e4, WorldText ~0x7e5-0x7e7, FFXEffects ~0x7e8-0x7ea, DayNight ~0x7ee-0x7f3,
  M2Scene ~0x81c-0x824, Liquid ~0x8a1-0x8a5, Common sky/lightning ~0x9a8-0x9ac);
* CVar / shader / texture / format strings (`farclip`, `Terrain1w_4`, `MapObjOpaque`,
  `vsLiquidWater`, `Textures\ShadowBlob.blp`, `Model2 Pass %d`, `.?AUCGxVertexPCT0T1@@` ...);
* MSVC RTTI type names (`CWFrustum`, `CPortalView`, `CWorldOccluder`, `CMapRenderChunk_vtx`,
  `Liquid::CInstance`, `SContact_CBarrier` ...);
* static call trees / caller trees (scripts in `C:\Users\tyler\tools\ghidra-scripts`).

Names given as `CWorld::Update` etc. are **inferred** from structure; the binary has no symbols.
Anything marked *(uncertain)* was not fully confirmed. Raw call trees and decompiles are in
`docs/ref/` (see `docs/ref/INDEX.txt`).

Frozen side: `src/ui/game/CGWorldFrame.cpp`, `src/world/Terrain.cpp` (the fork's first-cut
renderer), `src/world/CWorld.cpp`, `src/model/*`, `src/gx/*`.

Status legend: **ported** = follows the reference structure; **stand-in** = something draws but it
is an ad-hoc reimplementation; **missing** = nothing draws / no code. A trailing *(suspect)* marks a
stage that is wired up but carries a concrete defect (see Verification above).

## Top-level entry

| Stage | Reference function(s) | What it does | Frozen status | Frozen location / notes |
|---|---|---|---|---|
| Frame hook | `CGWorldFrame::OnFrameRender` FUN_004fb080 | On layer 0 queues the `RenderWorld` callback into the frame's render batch (`FUN_004858e0(FUN_004faf90, this)`). | ported | `CGWorldFrame::OnFrameRender` queues `CGWorldFrame::RenderWorld` on DRAWLAYER_BACKGROUND. |
| RenderWorld callback | FUN_004faf90 | Saves proj/view matrix stacks, calls FUN_004fa5f0 (update+render body), FUN_007e6480 (world-text update), FUN_004f8ea0 (`OnWorldRender`), FUN_007e7490 (world-text render), restores matrices, `CShaderEffect::UpdateProjMatrix` FUN_00872c10. | ported (shell) | `CGWorldFrame::RenderWorld` (CGWorldFrame.cpp:32): save proj/view -> OnWorldUpdate -> PlayerNameUpdateWorldText -> OnWorldRender -> PlayerNameRenderWorldText -> restore -> UpdateProjMatrix. Verified same shape and order. `PlayerNameUpdateWorldText` is still an empty body, so the reference's world-text *update* step has no equivalent; `PlayerNameRenderWorldText` does draw (see the world-text row). |
| World update body | FUN_004fa5f0 (WorldFrame.cpp lines 0x8b7-0x938) | Camera / sound listener update, then **`CWorld::Update(cameraPos, cameraTarget, targetPos)` FUN_007831a0**. | stand-in | `CGWorldFrame::OnWorldUpdate` (CGWorldFrame.cpp:351) -> `CWorld::Update` (CWorld.cpp:620) records only the camera position (`s_lightPos`, which doubles as `GetCameraPos`) and the normalised camera direction; the real per-frame work is `TerrainUpdate()` (Terrain.cpp:3758). No sound-listener update. OnWorldUpdate additionally re-places every visible object's model itself. |
| OnWorldRender | FUN_004f8ea0 | Main render function; see "Draw order" below. | stand-in | `CGWorldFrame::OnWorldRender` (CGWorldFrame.cpp:148), verified order: viewport push -> `GxSceneClear` -> `UpdateProjMatrix` -> `TerrainRender` (terrain, WMO visibility + groups, liquid bucket 0) -> `SkyRender` -> entity frustum cull -> blob shadows -> `AdvanceTime`/`Animate` -> particle simulation -> M2 pass 0 -> `DetailDoodadRender` -> transparent block -> `UnderwaterOverlayRender` -> viewport restore. Everything from `AdvanceTime` onward sits inside `if (scene)`, so with no M2 scene the detail doodads, liquid bucket 1, weather, particles and the underwater overlay all silently do not draw. |

## Update phase (`CWorld::Update` FUN_007831a0)

| Stage | Reference function(s) | What it does | Frozen status | Frozen location / notes |
|---|---|---|---|---|
| World tick | FUN_007831a0 | Frame-time ring, chunk-window (min/max chunk coords) change detection, `FUN_00780860(targetPos)`, then the calls below; also blends `farclip` toward the AreaTable override (`DAT_00ad3154` table lookup) and copies the "camera under liquid" id into the CM2Scene. | stand-in | `CWorld::Update` (CWorld.cpp:620) records only the camera position and direction: no frame-time ring, no chunk-window change detection, no camera-under-liquid id copied into the CM2Scene. farclip comes from `CWorld::SetFarClip` / `AdjustFarClip` (CWorld.cpp:390); no AreaTable-driven farclip blend. |
| Scene camera / frustum | `CWorldScene` FUN_00795400(cameraPos, cameraTarget) | Stores camera pos/dir planes, builds the view frustum (`FUN_006bf6d0` corners, `FUN_00984240` planes), the world-space frustum box, the visible chunk index rectangle (`/33.333`), and the fallback billboard matrices. | stand-in | `TerrainRender` builds view*proj and calls `ExtractFrustum` every frame (Terrain.cpp ~2105); no chunk-rect prepass. |
| Map update | `CMap::Update` FUN_007b6b00 | Area streaming (`FUN_007c3730`), chunk-liquid animate (`FUN_007cf840(dt)`), MapObj material/shader selection (`FUN_007ad020` picks the batch renderer table from shader level), chunk/area visibility and distance sort (`FUN_007b5950`: `qsort` by distance, load/unload areas), WMO defs (`FUN_007b6110`: distance test `FUN_007b48c0`, load groups, `FUN_00794ad0`/`FUN_00792ad0` add to scene, `FUN_007927e0` portals), doodad defs (`FUN_007b5630`). | stand-in | `TerrainUpdate` + `LoadTile`/`FreeTile` (Terrain.cpp:3758/1703/1927): the load radius is `ceil(farClip / 533)` clamped to `MAX_TILE_RADIUS = 2`, i.e. a 5x5 tile pool. No per-chunk visibility list, no distance sort, no streaming priority; a portal graph exists but is walked in the render phase, not here. Terrain relighting is rate-limited to two tiles per frame. |
| Weather update | `MapWeather` FUN_0078d170 (assert MapWeather.cpp:0x95a) | Per-frame weather intensity / wind direction update from player facing. | stand-in | Verified: `ReceiveWeather` (Weather.cpp:15) is registered for SMSG_WEATHER at Client.cpp:108, resolves Weather.dbc (`WeatherRec`: effectType, colour, sprite) and calls `TerrainSetWeather` (Terrain.cpp:2984), which eases the intensity. But `WeatherUpdate` (Terrain.cpp:3014) is **not** called from the update phase at all - it runs from inside `WeatherRender`, off the M2 scene's own clock, so weather does not simulate when there is no M2 scene or when `WeatherRender` returns early. No wind from facing, no ambience sound. `weatherDensity` is registered (`Weather::Weather()`, instantiated at CWorld.cpp:525) and is read. |
| Day/night update | FUN_007816f0 | Reads DayNight globals (FUN_007ecef0), sets override flags, `FUN_0077eed0(cameraPos)`, computes sun angle and glare (FUN_007f3920 -> FUN_007f3230 `SkySunGlare`, FUN_007eecc0 `moonGlare`), the fog update (FUN_007f16f0; clouds are FUN_007f1010 -> FUN_007efd00), then writes the map light colours (diffuse/ambient/... floats at `DAT_00ce04a8+0x88..0xa8`) scaled by the loading fade. | stand-in | `CWorld::UpdateOutdoorLight` (CWorld.cpp:235), called every frame from `TerrainUpdate`: Light.dbc band interpolation against the real game clock, plus positional map lights blended over the map default by falloff weight, plus the underwater parameter set and the LightSkybox lookup. Sun direction is still the compile-time constant `s_outdoorDirection` (CWorld.cpp:39); no glare, no clouds, `LightParams.glow` unused. Investigated 2026-09-14: the direction lives at DayNight block +0x30 (`DAT_00d38b30..38`, block base returned by `FUN_007ecef0`); only the init `FUN_007f2790` references it directly, every runtime write goes through the base pointer (candidates: `FUN_007ecd80`, `FUN_007ee360`, `FUN_007f1360`; dumps in docs/ref/win-decomp-daynight-*.txt). `FUN_007ed3b0` is the wrap-around (time,value) band evaluator. Second pass 2026-09-14: `FUN_007ed910` copies a 156-byte DNInfo (`DAT_00d38bd4`, 0x27 dwords: fog/sky/light colours), `FUN_007ee360` zeroes one, `FUN_007ec220` blends two by a weight, `FUN_007eb180(lightData, index)` builds one per light; none of them, nor `FUN_007f1360` (the apply step) or `FUN_007ecd80`, writes the direction slot, and none contains trig besides the sky-dome helpers `FUN_007eda90`/`FUN_007ef920`. Remaining hypothesis: the direction is written by the light loader (`FUN_007ecb30` region) or read from the light block `DAT_00ce04a8+0x58` by the shaders directly (dumps: docs/ref/win-decomp-daynight-candidates.txt). |
| Underwater update | FUN_0079bf40 (only when `DAT_00cd8794` = camera-in-liquid id and flag 0x2000000) | Updates the underwater overlay state for the current liquid. | stand-in | camera-under-liquid state and the light-set switch are in place (see the liquid-under-camera row), and a stand-in overlay quad now draws (`UnderwaterOverlayRender`, Terrain.cpp:4173). There is no separate per-frame underwater *update*: the state is recomputed inside `TerrainUpdate` from `LiquidAt` and consumed directly by the light and sky code. |

## Render phase (`CGWorldFrame::OnWorldRender` FUN_004f8ea0)

| Stage | Reference function(s) | What it does | Frozen status | Frozen location / notes |
|---|---|---|---|---|
| Viewport / state push | FUN_004f8ea0 head; `GxXformSetViewport` FUN_00681f60 | Gets the device viewport (vfunc 0x8c), `GxRsPush` FUN_00409670, `GxRsSet(0x13,1)`, sets the frame's viewport rect (with y-flip depending on `FUN_00682d50`). | ported (viewport) / missing (GxRsSet 0x13) | Verified at CGWorldFrame.cpp:152-154 and :348: `GxXformViewport` saves all six values, `GxXformSetViewport(m_viewport..., 0, 1)` sets the world's, and the saved set is restored at the end of `OnWorldRender`. No `GxRsPush`/`GxRsPop` around the whole world render (individual stages push/pop their own), and the y-flip variant (`FUN_00682d50`) is not reproduced. Note the world text is drawn *after* this restore (see the world-text row). |
| FFX begin | FUN_004f8770 (glow params from DayNight `+300`/underwater), FUN_008c1770 | Sets full-screen-effect parameters and redirects rendering to the FFX render target (`FFXEffects.cpp`: glow FUN_008bfe80, death FUN_007ea260, fog-combine/propagate-fog FUN_007ea5f0 -> FUN_007e80b0/FUN_007e81b0). | missing (deferred 2026-09-14) | `src/ffx/EffectGlow.cpp` ctor/callback are TODO. Blocked on gx: the device has no render-to-texture (only `RenderTargetGet`), so the glow needs D3D9 `SetRenderTarget`/render-target textures, the downsample+blur passes and the composite first. Sized as a multi-session gx feature, not a render-stage port. |
| Scene clear (early) | `GxSceneClear` FUN_006813b0(3, black) | Only when the viewport rect changed (`FUN_004f5d90`). | stand-in | frozen always clears at the top of `OnWorldRender` (CGWorldFrame.cpp:171) to the fog colour when fog is active, else the horizon sky colour; there is no viewport-changed test. The reference's real clear is inside `CMap::Render` (below). |
| Map prepare + **`CMap::Render`** | FUN_0077eff0 -> **FUN_0079a870** | See the dedicated table below. | stand-in | `TerrainRender` (Terrain.cpp:3909) + `WmoUpdateVisibility` + `RenderWmos` + `LiquidRender(0)`, all inside one function; the reference's split between prepare and render does not exist. |
| Footprints | FUN_0079fcc0 (Map.cpp, flag 0x400 = `showfootprints`) | 576-slot ring of fading footprint decals drawn as textured quads (`CMapFootprintTexture`). | missing | - |
| Visible object pre-render | `ObjectMgr` enumerate FUN_004d4b30(FUN_004f6a40) -> FUN_0072b350 | Per visible object callback (assert WorldFrame.cpp:0x862). | stand-in | `OnWorldUpdate` (CGWorldFrame.cpp:381) loops `objMgr->m_visibleObjects`, calls `UpdateIdleAnimation` and `SetWorldTransform` itself, and marks every model visible/animating; `OnWorldRender` then re-culls the same list against the frustum. No per-object callback, no `CMap` entity list. |
| M2 opaque pass | `CM2Scene::Draw(0)` FUN_00823cb0 (logs "Model2 Pass %d") | Draws pass 0 of the world scene **before** terrain. Only executed when the scene's pass mask bit is set. | ported | Verified: `CM2Scene::Draw(M2PASS_0)` (CM2Scene.cpp:693, plus the extra `array44` list the reference also draws on pass 0) is called at CGWorldFrame.cpp:316, after `TerrainRender`/`SkyRender` - the same slot the reference uses. Element sorting is done in `Animate` (`M2Sort.cpp`). |
| Ground marker | FUN_004f8a40(0x200122) / (0x20000) | Draws a projected ground decal at `DAT_00b74380` via the projected-texture helper FUN_007e4370 (Shadow.cpp region) when `DAT_00ac79a4 < 2`. Purpose *(uncertain)*: click-to-move / target marker. | missing | - |
| Render bucket 0 flush | FUN_00681ba0(0, 1, cameraPos) + FUN_00682960(0, cameraPos) | Sorts a generic render-item list (12-byte items; sort key via `PTR_FUN_00ad88a8[mode]`, comparator `PTR_FUN_00ad88b0[mode]`), then for each item: material Begin/End (vfunc +4/+8) and `Render(cameraPos)` (vfunc +0x14); list cleared by FUN_00682900. Producers of these items were not located *(uncertain)*. | missing | - |
| Detail (ground) doodads | `CWorldScene` FUN_007984a0: FUN_007b2d30 (begin: state + DetailDoodad shaders `DAT_00d1c4a8/00d1c488`), per chunk FUN_00790440 + FUN_0081e400 (transform), FUN_007b10e0 (VS constants, 0x17 regs) or FFP matrix push, FUN_007b36b0 -> FUN_007b3390 (draw 4 layers), then aux draws FUN_00797280 / FUN_007cd930 (flag 0x2000) / FUN_007976a0 | Draws the ground-effect doodad batches of the visible chunks. | stand-in | Verified: `BuildDetailDoodads` (Terrain.cpp:669) runs per chunk inside `LoadTile` and scatters deterministic placements from MCLY effectId + predTex + noEffectDoodad through GroundEffectTexture/GroundEffectDoodad (both DBCs are loaded, Db.cpp:72-73); the two counting passes consume the RNG identically, so the placement array cannot overflow. `DetailDoodadRender` (Terrain.cpp:3523) is called at CGWorldFrame.cpp:319, after M2 pass 0, and does draw. Caveats: it uses the UI shader, not the reference's DetailDoodad shaders; only the first textured skin batch of each model is used; `groundEffectDensity` is registered but ignored (only `groundEffectDist` is read); batches are built at most 4 chunks per frame, so grass fades in over several frames as the camera moves; the whole call is inside `if (scene)` in OnWorldRender. |
| Liquid + transparents block | see next table | Order depends on `FUN_00780620` (camera-in-liquid id `DAT_00cd8794`). | see below | |
| Mount transitions / misc updates | FUN_007fca30 (`MountTransitionObject` lists, FUN_009ab730(dt)), FUN_007f9ec0 -> FUN_009ab070(camera) | Game-side per-frame updates that live in the render function (Common region 0x9ab: lightning/anim). Identity *(uncertain)*. | n/a | - |
| Missile trajectory debug | FUN_006fdfb0 (assert `UnitMissileTrajectory_C.cpp`) | Debug draw. | n/a | - |
| Render bucket 1 flush | FUN_00681ba0(1, 0, cameraPos) + FUN_00682960(1, cameraPos) | Same mechanism, second list, after the transparents. | missing | - |
| Sun/moon glare post pass | FUN_007f0870 -> FUN_007ef6e0(x) + FUN_009ac400 (twice) | If `DAT_00d38ccc`: draws two screen-space textured quads with their own viewport (sun glare then moon glare) over the scene. | missing | - |
| World text flush | FUN_007e5580 -> FUN_007e6a90 (WorldText.cpp region) | Releases per-text resources not touched this frame (`DAT_00d380a4` frame counter from FUN_007e5120). | stand-in | Verified: `PlayerNameRenderWorldText` (PlayerName.cpp:95) is called from `RenderWorld` after `OnWorldRender`, projects each visible unit's head through the still-current world view/projection and draws its name as a screen-space `CGxStringBatch` (FRIZQT, shadowed, centred), within 60 yd, skipping models whose `m_flag8` the frustum cull cleared. Names come from `NameCache` (object/client/NameCache.cpp), whose two response handlers are registered at Client.cpp:109 and which `ScriptEvents.cpp:476` (UnitName) also reads. Caveats verified by reading: every `CGxString` and the batch are destroyed and re-created **every frame, per unit** (PlayerName.cpp:38-51, :96), which is pure allocation churn; the world viewport has already been restored by then, so the projection is only correct while the WorldFrame fills the screen; a name query that gets no response is never retried (`pending` is never cleared); no damage text, no reaction colours, no occlusion fade. The reference's actual job for this row - releasing retained per-text resources - has no equivalent. |
| Post | FUN_00685fb0 `GxRsPop`, FUN_00615890(1), FUN_0056c7a0 (object list walk), viewport restore FUN_00681f60, FFX end FUN_008c1010 (composites the FFX target back with `FUN_006814d0`), FUN_00747ae0 (clears an object flag) | Restores state and finishes full-screen effects. | missing (FFX) | - |

### `CMap::Render` FUN_0079a870 (called from OnWorldRender via FUN_0077eff0(camera+8, ...))

| Stage | Reference function(s) | What it does | Frozen status | Frozen location / notes |
|---|---|---|---|---|
| Liquid-under-camera | `CWorldScene` FUN_00790920 -> FUN_007a0b00 (terrain liquid at camera) / FUN_007c8360 (WMO group liquid) | Writes `DAT_00cd8794` (liquid type id), sets the DayNight override-sky flag (FUN_007f1070) and underwater fog (FUN_0079b8e0/FUN_0079b360), FUN_008a2aa0. | stand-in | Verified: `TerrainUpdate` (Terrain.cpp:3766) calls `LiquidAt` (Terrain.cpp:2854) over the loaded terrain liquid layers (covering cell's highest corner = surface) and stores the kind in `s_cameraLiquidKind`; `CWorld::SetCameraUnderLiquid` switches `UpdateOutdoorLight` to the Light.dbc underwater parameter set, and `SkyRender` returns early while submerged. Caveats: the query is a linear scan of every liquid layer of all 25 resident tiles every frame (no spatial index); it runs *before* this frame's tile streaming, so it uses the previous frame's tile set; **WMO liquids are not consulted**, so a canal or interior pool never registers as submersion; `CGCamera::CheckUnderwater` (CGCamera.cpp:134) is still an empty stub, so nothing camera-side reacts. |
| Frame begin | `GxRsPush`, `GxXformPush(World)` FUN_0057c3a0(8), frustum FUN_00984240, FUN_00782f20, FUN_007ba600 (index pool `CMap::lowDetailIndexPool`), FUN_007ae060 (MapObj begin), FUN_007b2a80 (DetailDoodad begin), FUN_007cd910/FUN_007cc810 (MapLowDetail begin; adds low-detail areas via FUN_007927e0) | Resets per-frame lists and counters. | stand-in | no equivalent; `TerrainRender` is monolithic and keeps no per-frame lists apart from the WMO `visFrame` stamp and the transient liquid/blended-batch vectors. It does `GxRsPush`/`GxRsPop` around itself but no `GxXformPush(World)`. |
| Visibility traversal (outdoor) | FUN_0079a790(camera, 0): FUN_007cd850 (low-detail vis), FUN_00790020, FUN_00790af0, per area FUN_00799d40 / FUN_0079a160 / FUN_007935a0 / FUN_00793060 / FUN_007987a0 / FUN_00793760, FUN_00791980 | Walks the visible chunk rectangle, frustum-tests chunks / doodad defs / map-object defs and appends them to the WorldScene render lists. | stand-in | per-chunk AABB test `BoxVisible` inline inside `RenderShaded`/`RenderFallback` (so every resident chunk of all 25 tiles is visited every frame); doodads via `DoodadCullCenter`/`SphereVisible` in `TerrainRender`. `ExtractFrustum` normalises the planes, so the sphere tests are correct. No chunk rectangle, no render lists. |
| Visibility traversal (inside WMO) | `DAT_00cd87a4 != 0`: FUN_007b3b20(&DAT_00cdb0e4) -> FUN_007a6b40(FUN_00799310) + FUN_007ad1f0(group, portalList, cameraPos, cameraTarget) (**portal walk**), FUN_007b3b20(&DAT_00cdb0d4), then FUN_0079a790(camera, 1) or FUN_00794250, FUN_00799f80 | Portal-based recursion from the camera's WMO group (`CPortalView`), then the outdoor traversal from the exterior portals. | stand-in (suspect) | Code verified present and called: `LoadWmoInstance` parses MOPV/MOPT/MOPR into world space, and `WmoUpdateVisibility` (Terrain.cpp:2422) / `WalkPortals` (Terrain.cpp:2357) run every frame from `TerrainRender` before `RenderWmos`, which does gate on `grp.visFrame != s_visFrame`. ~~Two defects found by reading~~ **- both stale, re-read on 2026-09-23.** (1) `NarrowFrustum` now takes the near and far planes from `s_baseFrustum` rather than from slots 4 and 5 of its input, with a comment describing exactly the bug this row named; (2) the walk keeps a `visDepth` per group and returns unless it is being re-entered from a shallower path, which is what stops the exponential re-walk in a densely linked WMO. Also, the camera's group is found by smallest containing AABB rather than the group BSP, so overlapping room boxes pick the wrong start. Unlinked interior groups and WMOs without portals keep the plain box test. |
| Scene finish lists | FUN_0079a260 (MapObj), FUN_00793450, FUN_007cecd0 (chunk liquid) | Finalizes lists. | n/a | - |
| **Scene clear** | `GxSceneClear` FUN_006813b0(3, colour) | colour = black if no sky, DayNight sky/fog colour (`+0xa0`) when an interior lighting override is active, underwater fog colour (`+0x8c`) when under liquid, else 0. | stand-in | `GxSceneClear(0x3, fog or GetSkyColor(0))` at frame start (Terrain.cpp:2074). |
| Shadow map | `MapShadow` FUN_007bb670(playerPos) (light view/proj setup) ... later FUN_007bb570 (`CShadowCache` FUN_00875c10/FUN_00875f80/FUN_008750b0: render and bind the map shadow texture, `ShadowMapRenderSL`/`Terrain*_pcf` shaders) | Projected terrain/model shadow map (CVars `mapShadows`, `shadowLevel`). | missing | `ShadowInit()` and `CShadowCache::SetShadowMapGeneric*` are commented out (`src/client/Client.cpp:675`, `src/model/CM2SceneRender.cpp:101`). |
| M2 scene animate | `CM2Scene::AdvanceTime` FUN_0081c9c0(dt), `CM2Scene::Animate` FUN_00821a20(camera) (scene flag bit 1 set around it) | Animates and culls the world M2 scene (doodads + units). | ported | Verified: `scene->AdvanceTime(CWorld::GetTickTimeMs()); scene->Animate(camera->Position())` at CGWorldFrame.cpp:288-289, before the M2 passes. Visibility is pushed in beforehand by the `OnWorldRender` sphere cull rather than computed inside `Animate`. |
| Game-side pre-draw | FUN_006fda20 | Game object / spell visual per-frame work (0x6fd region). | n/a | - |
| Fog | FUN_00872c10 UpdateProjMatrix, FUN_00781610 (`GxRs_FogStart/End/Color` = DayNight `+0x90/+0x94/+0x8c`) | Fixed-function fog state from DayNight. | stand-in | `TerrainRender` (Terrain.cpp:3945) sets `GxRs_FogColor/Start/End` from `CWorld::s_fog*` (Light.dbc driven) when the fog start falls inside the far clip, and each later stage re-enables `GxRs_Fog` from the cached `s_fogActive`. The M2 passes get fog turned on separately in `OnWorldRender`. |
| **Terrain chunks** | `CWorldScene` FUN_00798da0: fog + 0x570 sampler states, FUN_007cfbe0 (terrain shader constants: light block `DAT_00ce04a8+0x58`, FUN_008355d0), then FUN_00793b10 (list A, pixel shader `DAT_00d1d080`), FUN_00793c30 (list B), FUN_007989c0 (multi-layer list, per layer PS from `DAT_00d1d08d[]`, then FUN_007d40a0 pass and FUN_007cecd0). Per chunk: FUN_007d3e10 picks the chunk draw routine by shader level (`DAT_00d25098` = FUN_007d20a0 / FUN_007d2520 / FUN_007d1ad0 / FUN_007d13f0 / FFP FUN_007d0760 / FUN_007d0d70), FUN_007d0050 sets world matrix + terrain VS (FUN_0079e470, `GxRs 0x4d`), FUN_007d04a0. Shaders: `Terrain`, `Terrain0/_env`, `Terrain1`, `Terrain1w_1..4`, `Terrain2/_pcf`, `Terrain3/_pcf`, `TerrainSM` (loaded by FUN_0079e7c0). | Draws MCNK batches with alpha-map layering, baked shadow (MCSH) sampling, shadow-map PCF variants, low-detail pool. | stand-in | Verified called from `TerrainRender` (Terrain.cpp:3909). `RenderShaded` (Terrain.cpp:2114) uses the fork's own `vs_2_0/ps_2_0` bytecode from `TerrainShadersD3d9.hpp` and is created only when `m_api` is D3d9/D3d9Ex (`EnsureShaders`, Terrain.cpp:2089); every other backend silently falls back to `RenderFallback` (multi-pass via the UI shaders). MCSH and MCCV are folded into the per-vertex colour at parse time and re-baked by `RebakeChunkColors` when the light shifts. No shader-level permutations, no low-detail mesh (`CMapAreaLow` empty), no chunk sort. |
| **WMO groups** | `CWorldScene` FUN_007964a0: for each visible `CMapObjDef` (list `DAT_00cdb088`): transform FUN_007a8320/FUN_00790440/FUN_0081e400, FUN_007a9160(matrix, cameraPos), FUN_007a8430(colour), FUN_007abf50 (`CMapObj::Render`: per group FUN_00791100 / FUN_0078fb00 frustum, FUN_007ab4c0, FUN_007a9ed0; materials chosen by FUN_007ad020 from `MapObj*`/`MapObjU*` shader table `DAT_00d1c3d4..00d1c404`), then FUN_00795f80 (second WMO pass with own viewport/fog: FUN_007ae4c0/FUN_007ae4f0/FUN_007ae1a0 + FUN_007abac0) | Opaque WMO batches, interior lighting via `CMapLight`/MOCV, `MapObjLightLOD`. | stand-in | `RenderWmos` (Terrain.cpp:2513), called from `TerrainRender`: geometry baked to world space at load, UI shader, MOCV + MOHD ambient bake, per-batch blend/alpha-key/two-sided/unfogged flags, and a second back-to-front pass for alpha-blended batches with depth writes off. It **is** portal-gated now (`grp.visFrame != s_visFrame` skips a group), so the earlier "no portal culling" note was wrong. Still missing: the `MapObj*`/`MapObjU*` shader permutations, MOLT lights, MapObjLightLOD, and the reference's separate second WMO pass with its own viewport/fog. |
| Occluder polygons | FUN_007968d0 (prepare, FUN_00794190) + FUN_00796c10(&DAT_00cdd0e8,1) / (&DAT_00cdd0f8,0) | Only outdoors: draws screen-space polygons (NDC, z=1, vertex format 7) from the `CWorldOccluder`/`CWorldAntiOccluder` lists (CVar `occlusion`). Exact purpose *(uncertain)*. | missing | `cvar_occlusion` registered with TODO callback. |
| Sky (in-scene) | FUN_007f31c0(0, DAT_00cd861c, 0, DayNight+0x9c) / FUN_007f31c0(1,...) then, if not under liquid, **FUN_007f09b0(&DAT_00adf570)**: viewport with far-z override, `GxRs 0x14`, FUN_00682e70, stars M2 FUN_009abd50 (`Environments\Stars\stars.mdl`, own CM2Scene passes 0/1), sun and moon discs FUN_009ac660 x3, the gradient dome FUN_009acb00, clouds FUN_009acd40 (cloud vertex data from FUN_007efd00), then LightSkybox M2s: FUN_0081c9c0(time) + FUN_007ecf20 / FUN_007f08c0 per skybox entry (`DAT_00d38b5c..00d38b70`). | Sky is drawn **after** terrain/WMO with the depth range trick, skipped underwater. | stand-in | `SkyRender()` (Terrain.cpp): procedural 6x24 dome built from the reference's own ring table (`0x00a41a90`) and 6 band colours, plus the `LightSkybox` M2. Since 2026-09-14 it is drawn **after** terrain/WMO through the reference's far-depth viewport (z 0.999..1.0, DAT_00adeef0/f4) with the depth test on and no depth clear, matching the reference order. Stars added 2026-09-14 (`Environments\Stars\stars.mdl`, seeked to the time of day, drawn after the dome; the dome and stars are skipped while a zone skybox is up, as the reference does). Verified this pass: `SkyRender` (Terrain.cpp:4452) is called from `OnWorldRender` **after** `TerrainRender`, sets the viewport depth range to [0.9990234375, 1.0], draws the dome with depth-test less-equal and depth writes off, then draws the stars (or the zone skybox) through their own `CM2Scene` inside the same viewport, and restores the viewport at the end. The under-liquid skip **is** implemented (early return at the top of `SkyRender`) - the old note claiming otherwise was wrong. Struck 2026-09-23: glare (`DrawGlare`) and clouds (`src/world/Clouds.cpp`) landed 2026-09-15, and the sky highlight was ported from `FUN_007f0530` on 2026-09-23 -- all three built, none seen running. Still missing: the translucent sky-dome layers, and the per-frame sky override `FUN_007f0530` applies from `0x00d38184` / `0x00d38b50`. The stars/skybox scenes are never released and their models are never re-culled. |
| Blob shadows | `CWorldScene` FUN_00793980 (assert WorldScene.cpp:0xe35): per scene entity with a model, FUN_0082ced0 + FUN_007e49e0 -> FUN_007e4480 (`Textures\ShadowBlob.blp`, loaded by FUN_007e4a40 `ShadowInit`) | Projected blob shadow decals under units/doodads. | stand-in (suspect) | Called as claimed: `BlobShadowsBegin`/`BlobShadowDraw`/`BlobShadowsEnd` (Terrain.cpp:4260/4299/4356) run from `CGWorldFrame::OnWorldRender` (:243-274) after the sky and before the M2 passes. **This row described an older form of the pass and was stale.** The z-fighting cause it names is fixed: the pass now binds `s_terrainVS` with `s_blobDecalPS` and the same per-chunk matrix and vertex streams as the base pass, so the depth is bit-identical and the surface is selected with a depth-EQUAL test rather than hoped for. `GxRs_PolygonOffset` is still handled by no backend, but it is no longer needed here and the reference does not use it for this either. WMO floors are covered too, by `BlobShadowDrawWmo` (Terrain.cpp:5292). Still true: every chunk whose box overlaps the footprint is redrawn in full per caster. **Unverified on screen** - none of this has been seen running. Separately, the WMO receiver grid was anchored on world-space bounds while binning instance-local vertices, which clamped every triangle into one cell and quietly restored the exhaustive per-caster scan the grid exists to avoid; fixed 2026-09-23. The reference's generic projector `FUN_007e4480` and `DrawBatchProj` remain TODO. |
| Liquid textures / WMO liquid | FUN_008a2f00 (procedural water frames `DAT_00d43b50[0x80]`, magma/ocean depth textures), FUN_00793d20 (per WMO group: `"WMO: Liquid type [%d] not found, defaulting to water!"`, builds `Liquid::CInstance` via FUN_007d4360/FUN_007d43e0/FUN_008a1b00/FUN_008a1fa0/FUN_008a28f0), then **FUN_008a2240(cameraPos, 0)** (liquid bucket 0: sort instances with `PTR_FUN_00b23f6c[0]`, call `CInstance::Render` vfunc +8 with material `Liquid::CMaterialWater/ProcWater/Magma(FFP)` - shaders `vsLiquidWater`, `psLiquidWaterNoSpec`, `psLiquidMagma`). | Opaque liquids (magma/slime) are drawn inside `CMap::Render`. | stand-in (suspect) | Called as claimed: `ParseLiquid` (Terrain.cpp:2726) and `ParseLegacyLiquid` (:3616) run from `LoadTile` (:1810/:1815), `LoadWmoLiquid` (:1009) from `LoadWmoInstance` (:1557), and `LiquidRender(0)` (:4059) from the end of `TerrainRender`; `g_liquidTypeDB` is loaded (Db.cpp:70) and drives kind + animated surface frames (`m_texture[0]`, ~20 fps). WMO surfaces are correctly gated on the owning group's `visFrame`. ~~Three defects found by reading~~ **- all four claims here were stale and are struck; each was re-read on 2026-09-23 and the code already handles it.** (1) the colour buffer is no longer a fixed `s_liquidColor[81]`: it is a `std::vector<CImVector>` grown to the layer's own `vertCount`; (2) the MLIQ axis mapping is right and carries a comment saying an earlier pass had it wrong and that both it and the vertex transform were corrected together; (3) the MH2O bitmap is indexed `j * w + i` over the layer's sub-rectangle and re-packed into the chunk's 8x8 grid, which is what the format specifies; and the leak is gone because `ParseLegacyLiquid` frees `chunk.liquids` before reallocating. No procedural water/depth textures, no per-liquid darkening. |
| Frame end | FUN_00784a30 (World), `GxXformPop`, `GxRsPop`, FUN_006164b0, debug flag 0x200000 -> FUN_007d5610 / FUN_00793fd0 | - | n/a | - |

### Transparent block in OnWorldRender (after detail doodads)

Reference (`FUN_004f8ea0`), `under = FUN_00780620(0)` (camera-in-liquid id):

```
if (!under) { M2 pass 2 (FUN_00823cb0(2)); liquid+ (FUN_00790a80); weather (FUN_0077f030); barriers (FUN_0077f980); M2 pass 1 }
else        { barriers (FUN_0077f980); M2 pass 1; weather (FUN_0077f030); liquid+ (FUN_00790a80); M2 pass 2 }
```

| Stage | Reference function(s) | What it does | Frozen status | Frozen location / notes |
|---|---|---|---|---|
| M2 pass 2 / pass 1 | `CM2Scene::Draw` FUN_00823cb0(2) / (1) -> FUN_00823130 (`CM2SceneRender::Draw`) | Transparent M2 passes; particles/ribbons are elements in these passes (`Particle_Unlit` shader from FUN_0081f330). | ported (passes 1 and 2 drawn, underwater order flip) / stand-in (suspect) (particles) / missing (ribbons) | Verified: `CM2Scene::Draw` (CM2Scene.cpp:693) draws `array54[pass]`, which `Animate` fills and heap-sorts for all three passes (CM2Scene.cpp:629-662), and `CGWorldFrame::OnWorldRender` (:324-336) draws 0, 2, 1 above liquid and **does** flip to 1, weather, liquid, 2 under liquid - the inline comment there claiming the flip is unported is stale. `DrawParticle` still returns 0 and `DrawRibbon` is empty (`src/model/CM2SceneRender.cpp:270/275`). The `src/world/ParticleFx.cpp` stand-in is real and called (`ParticleFxUpdateModel` per visible object and per terrain/WMO doodad at CGWorldFrame.cpp:300-311, `ParticleFxRender` with pass 2): it samples the emitter tracks against the bone sequence state, simulates plane/sphere emitters in world space (emitter bone animation ignored) and draws sorted camera-facing quads with the M2 blend modes. ~~Suspect: use-after-free.~~ **Fixed, and this row was stale.** `s_models` is still keyed on a raw `CM2Model*`, and `ParticleFxForgetModel` is still called only from `FreeTile`, so a despawning unit still leaves a dangling key. But the render no longer dereferences one: it walks only entries whose `lastFrame` equals the current frame, which means the owner updated them this frame and they are therefore alive, and `ParticleFxEndFrame` ages the rest out by key alone without ever touching the pointer. Verified by reading 2026-09-23. |
| Liquid bucket 1 | FUN_00790a80: FUN_00781610 (fog), **FUN_008a2240(cameraPos, 1)** (transparent water, sorted), FUN_0079d5e0 (post-liquid decal list `DAT_00adfb60` with shaders `DAT_00cdffd4/d8`, uses MapChunkLiquid FUN_007cecd0 - likely ripples/splashes *(uncertain)*) | Water surfaces sorted back-to-front. | stand-in (suspect) | Verified: `LiquidRender(1)` is called from `CGWorldFrame::OnWorldRender` (:327/:333) in both camera orders; water/ocean layers are frustum-tested, sorted farthest first, alpha-blended with depth writes off and fogged. ~~Carries the same `s_liquidColor[81]` overrun as bucket 0~~ - stale, that buffer grows to the layer's vertex count now; see that row. Post-liquid decals: missing. |
| Weather | FUN_0077f030 -> `MapWeather` FUN_0078ca50 -> FUN_0078ae20 (+0x13c emitter), FUN_0078ba60 (+0x140), FUN_0078c3e0 (+0x144) | Rain / snow / mist particle systems (`WeatherMistGrainy`, `WeatherPacket_vtx`). | stand-in | Verified: `WeatherRender` (Terrain.cpp:3082) is called from the transparent block in both camera orders (CGWorldFrame.cpp:326/334) and draws billboard sprites with the reference's textures (RainDrop01 / SnowMist01 / WeatherMistGrainy01, or the Weather.dbc override), budget scaled by `weatherDensity`, suppressed while under liquid. It also *is* the weather update (it calls `WeatherUpdate` itself). Nothing draws until an SMSG_WEATHER arrives, and the time step comes from `CWorld::GetM2Scene()->m_time`, so weather freezes when there is no M2 scene. Not the reference's three-emitter system or its weather shaders. |
| Barriers | FUN_0077f980 -> `CWorldScene` FUN_00794b50(cameraPos, x) | Up to 4 barrier M2 models (`DAT_00cd85f8`) via FUN_0078fbd0 + `CM2Scene` draw callback FUN_00823f10, then a two-texture `CGxVertexPCT0T1` mesh (`SContact_CBarrier`). Zone/PvP barrier effect *(uncertain)*. | missing | - |
| Underwater overlay | FUN_0077f9d0 -> `CMap` FUN_0079ca70 (only when under liquid and flag 0x2000000) | Draws a 0xa68-vertex textured mesh (texture at `+0xfa10`) in front of the camera. | stand-in | Verified: `UnderwaterOverlayRender` (Terrain.cpp:4173) is called last in the world render (CGWorldFrame.cpp:341) and returns immediately unless `s_cameraLiquidKind >= 0`: one depth-test-off quad a yard in front of the eye, the submerged liquid's surface texture scrolled slowly, tinted by the fog colour at alpha 0x50. It finds the liquid type by scanning every loaded chunk for the first layer matching the camera's *kind*, which can pick an unrelated layer's texture. The reference's distorted grid mesh and its texture source are still not identified (no string reference names it). |

## Draw order (reference)

Per frame, in call order (RunicWorldGame.exe):

1. `CGWorldFrame::OnFrameRender` FUN_004fb080 -> batch callback FUN_004faf90.
2. FUN_004fa5f0 -> `CWorld::Update` FUN_007831a0:
   1. scene camera/frustum FUN_00795400
   2. `CMap::Update` FUN_007b6b00 (streaming, chunk/WMO/doodad visibility lists, liquid animate)
   3. DayNight update FUN_007816f0 (sun/moon glare, clouds, light colours)
   4. weather update FUN_0078d170, underwater update FUN_0079bf40
3. world-text update FUN_007e6480
4. `OnWorldRender` FUN_004f8ea0:
   1. viewport set FUN_00681f60, FFX begin FUN_008c1770 (+ glow params FUN_004f8770)
   2. `GxSceneClear` only if the viewport changed
   3. `CShaderEffect::UpdateProjMatrix` FUN_00872c10
   4. `CMap::Render` FUN_0079a870:
      1. liquid-under-camera FUN_00790920
      2. begin (index pool, MapObj/DetailDoodad/LowDetail begin)
      3. visibility traversal: outdoor FUN_0079a790(...,0) **or** portal walk FUN_007b3b20 -> FUN_007ad1f0 then FUN_0079a790(...,1)
      4. **`GxSceneClear(3, skyColour)`**
      5. shadow-map view setup FUN_007bb670
      6. `CM2Scene::AdvanceTime` FUN_0081c9c0, `CM2Scene::Animate` FUN_00821a20
      7. FUN_006fda20, shadow-map render/bind FUN_007bb570, UpdateProjMatrix, fog FUN_00781610
      8. **terrain chunks** FUN_00798da0
      9. **WMO groups** FUN_007964a0, second WMO pass FUN_00795f80
      10. (outdoors) occluder polygons FUN_007968d0/FUN_00796c10, DayNight placement FUN_007f31c0 x2, **sky** FUN_007f09b0 (stars, dome, clouds, LightSkybox M2s) - skipped under liquid
      11. blob shadows FUN_00793980
      12. liquid textures FUN_008a2f00, WMO liquid instances FUN_00793d20, **liquid bucket 0** FUN_008a2240(cam,0)
   5. footprints FUN_0079fcc0
   6. object pre-render enumeration FUN_004d4b30(FUN_004f6a40)
   7. **M2 pass 0** FUN_00823cb0(0) (opaque models)
   8. ground marker FUN_004f8a40(0x200122)
   9. render bucket 0 FUN_00681ba0(0,1,cam)/FUN_00682960(0,cam)
   10. **detail doodads** FUN_007984a0
   11. transparent block (order depends on camera-in-liquid): M2 pass 2, liquid bucket 1 (+fog set, post-liquid decals FUN_0079d5e0), weather, barriers, M2 pass 1
   12. ground marker FUN_004f8a40(0x20000), mount transitions FUN_007fca30, FUN_007f9ec0, missile debug FUN_006fdfb0, FUN_004f6f90, underwater overlay FUN_0077f9d0
   13. render bucket 1 FUN_00681ba0(1,0,cam)/FUN_00682960(1,cam)
   14. sun/moon glare post pass FUN_007f0870
   15. world-text flush FUN_007e5580, `GxRsPop`, FUN_00615890(1), FUN_0056c7a0, viewport restore, **FFX end** FUN_008c1010, FUN_00747ae0
5. world-text render FUN_007e7490 (names / damage text quads)
6. restore proj/view stacks, UpdateProjMatrix.

Where transparents are sorted: M2 elements are sorted inside `CM2Scene::Animate` (ported in frozen
`src/model/M2Sort.cpp`); liquid instances are `qsort`ed per bucket in FUN_008a2240; WMO alpha
batches are handled by the MapObj batch renderer chosen in FUN_007ad020; the generic render buckets
are `qsort`ed in FUN_00681ba0.

Frozen's actual order, read off `CGWorldFrame::OnWorldRender` (CGWorldFrame.cpp:148) on 2026-09-14:

1. viewport push, `GxSceneClear(3, fog or horizon sky)`, `CShaderEffect::UpdateProjMatrix`
2. `TerrainRender`: `GxRsPush`, view*proj, `ExtractFrustum`, fog render states, doodad sphere cull,
   `RenderShaded` (D3D9) or `RenderFallback`, `WmoUpdateVisibility` (portal walk),
   `RenderWmos` (opaque/alpha-key, then sorted alpha), `LiquidRender(0)`, `GxRsPop`
3. `SkyRender`: far-depth viewport, gradient dome (skipped under a zone skybox or under liquid),
   stars M2 or skybox M2, viewport restore
4. entity frustum cull, then `BlobShadowsBegin`/`BlobShadowDraw`/`BlobShadowsEnd`
5. `scene->AdvanceTime`/`Animate`, particle simulation for every visible model and doodad
6. `Draw(M2PASS_0)`, `DetailDoodadRender`
7. transparent block - above liquid: pass 2, `ParticleFxRender`, `LiquidRender(1)`, `WeatherRender`,
   pass 1; under liquid: pass 1, `WeatherRender`, `LiquidRender(1)`, pass 2, `ParticleFxRender`
8. `ParticleFxEndFrame`, `UnderwaterOverlayRender`, viewport restore
9. back in `RenderWorld`: `PlayerNameRenderWorldText`, restore proj/view, `UpdateProjMatrix`

Differences from the reference that remain: no FFX begin/end around the frame; the clear is
unconditional and happens before `CMap::Render` rather than inside it; there is no shadow-map pass,
no footprints, no ground marker, no generic render buckets, no barriers and no sun/moon glare pass;
detail doodads are drawn after M2 pass 0 (matching the reference) but with the UI shader; the
particle quads are a separate pass rather than elements inside the M2 passes.

## Culling (reference)

| Mechanism | Reference | Frozen |
|---|---|---|
| View frustum | `CWFrustum` built in FUN_00795400 (FUN_006bf6d0 corners, FUN_00984240 planes; sphere/box tests FUN_0078f370 / FUN_0078fb20 / FUN_0078f3e0) | `ExtractFrustum`, `BoxVisible`, `SphereVisible` in Terrain.cpp |
| Chunk index window | FUN_00795400 computes min/max chunk coords from the frustum box; `CWorld::Update` tracks window changes (`DAT_00cd77d8..e4`) | tile ring `MAX_TILE_RADIUS = 2` around the camera |
| Distance (farclip) | `farclip` CVar (FUN_0078e400 registration, clamp FUN_0078dc30 `farClipOverride`), per-area farclip blend in FUN_007831a0; WMO def distance FUN_007b48c0 vs def `+0x1c0` in FUN_007b6110; chunk distance `qsort` in FUN_007b5950 | `CWorld::SetFarClip`/`AdjustFarClip`; no per-object distance cull, no `objectFade`/`lod` |
| Portals | `CPortalView` walk FUN_007ad1f0 from the camera's WMO group; exterior portals seed the outdoor traversal (FUN_0079a790(...,1)) | `WmoUpdateVisibility`/`WalkPortals` in Terrain.cpp: camera group by smallest containing box, not BSP; outdoor seeding **is** present (every in-view exterior group opens its portals inward when the camera is outside). Suspect - see the portal traversal row: `NarrowFrustum` loses the near/far planes below depth 1 and the walk has no visited set. |
| Occluders | `CWorldOccluder`/`CWorldAntiOccluder` screen-space polygons FUN_00796c10 (CVar `occlusion`) | none |
| Detail doodads | `groundEffectDist`/`groundEffectDensity` CVars (FUN_0078e400) applied in the DetailDoodad batch builder | `groundEffectDist` applied in `DetailDoodadRender` (batches built and freed as the chunk crosses the range); `groundEffectDensity` registered but ignored |
| M2 scene | inside `CM2Scene::Animate` FUN_00821a20 (bounds vs frustum, `SetVisible`) | ported scene, but visibility is pushed in by `TerrainRender` / `OnWorldRender` sphere tests |
| Liquid | per-instance sort + `LiquidChunkAndDistance` (RTTI) | per-layer box frustum test + distance sort in `LiquidRender` |

## Status summary

Recounted 2026-09-14 (second pass) against the corrected tables. 49 rows total; 6 are excluded (the
five `n/a` game-side rows and the "Liquid + transparents block" pointer row), leaving **43 counted**.

* **ported: 6** - frame hook, RenderWorld shell, world viewport push/restore, M2 pass 0 draw,
  M2 scene animate, M2 transparent passes 1 and 2 (including the under-liquid order flip).
* **stand-in: 27** - world update body, OnWorldRender, world tick, scene camera/frustum, map update,
  weather update, day/night update, underwater update, early clear, map prepare, object pre-render,
  detail doodads, world text, liquid-under-camera, frame begin, outdoor visibility traversal,
  *WMO portal traversal (suspect)*, scene clear, fog, terrain chunks, WMO groups, sky,
  *blob shadows (suspect)*, *liquid bucket 0 + WMO liquid (suspect)*,
  *liquid bucket 1 (suspect)*, weather render, underwater overlay.
  Four of these carry *(suspect)*; the particle stand-in inside the M2-pass row is a fifth.
* **missing: 10** - FFX begin, footprints, ground marker, render bucket 0, render bucket 1,
  sun/moon glare post pass, post/FFX end, shadow map, occluder polygons, barriers. Ribbons are also
  missing, counted inside the M2-pass row rather than as a row of their own.

No row moved *down* a status band in this pass: everything the 2026-09-14 iterations claimed does
exist and is called. What changed is the qualifier and the notes - five stages picked up
*(suspect)* because reading them turned up defects (see below), and several notes were factually
wrong (the sky's under-liquid skip and the M2 under-liquid order flip were marked missing when they
are implemented; the WMO-group row said "no portal culling" when the groups are portal-gated; the
underwater-update row said the overlay was missing when it draws; the weather-update row put the
simulation in the update phase when it runs inside `WeatherRender`).

Most significant gaps (by visual impact): (1) liquids (buckets 0/1, WMO liquid, procedural water),
(2) sky composition and order (stars/sun/moon/clouds, drawn after opaque geometry), (3) shadows
(map shadow map + blob shadows), (4) particles/ribbons + M2 pass 2, (5) detail doodads; plus the
structural gap that frozen has no `CMap::Update` visibility lists / portal traversal, so every stage
re-culls with its own AABB tests.

## Progress log

Each entry is one iteration against this checklist. Visual confirmation is by the scene-compare
harness (`tools/scene-compare`), run by hand when the desktop is free.

* 2026-09-14 (1): frustum planes normalised (`ExtractFrustum`), fixing sphere culls that dropped
  objects at half their radius. Sky moved after the opaque world and drawn through the reference's
  far-depth viewport (0.999..1.0) with the depth test on; the mid-frame depth clear is gone.
  M2 pass 2 is now drawn, passes ordered 0, 2, 1 as in the reference's above-liquid block.
* 2026-09-14 (2): WMO portal visibility. MOPV/MOPT/MOPR parsed and baked to world space with the
  group geometry; per-frame portal walk with frustum narrowing decides which groups draw. Interior
  rooms are no longer drawn through walls from outside, and from inside only rooms reachable
  through in-view portals draw. Unverified visually until the harness is run.
* 2026-09-14 (3): blob shadows. The terrain chunks under every visible unit are redrawn with the
  ShadowBlob texture and planar UVs centred on the unit (alpha-blended, depth less-equal, no depth
  write), footprint scaled from the model's box. Terrain only for now: units on WMO floors get none.
* 2026-09-14 (4): world viewport pushed/restored around the world render; stars model drawn in the
  sky pass and the gradient dome suppressed under a zone skybox, both per the reference sky routine.
  Sun direction stays constant: the runtime writer of the DayNight direction could not be located by
  direct data references (notes in the day/night row).
* 2026-09-14 (5): liquids. LiquidType.dbc record added; MH2O parsed per chunk into surface meshes
  with the cell mask and height map; magma/slime drawn opaque after the WMOs, water/ocean drawn
  sorted and alpha-blended after M2 pass 2 with the animated LiquidType textures. Not yet: WMO
  liquids, MCLQ, underwater detection, procedural water shading.
* 2026-09-14 (6): liquid under the camera. Point query against the MH2O layers each frame; while
  submerged the light/fog/sky colours come from the Light.dbc underwater parameter set and the sky
  pass is skipped, as in the reference. Underwater overlay mesh and the transparent-block order
  flip are still open. *(Correction: both were finished later - the overlay in iteration 14, and the
  order flip is in `OnWorldRender` as of iteration 1. WMO liquids are still not consulted by the
  point query, so submersion is never detected in a canal or interior pool.)*
* 2026-09-14 (7): weather. SMSG_WEATHER handled, Weather.dbc loaded, and a particle field of
  rain streaks / snow flakes / mist sheets around the camera drawn in the transparent block with the
  reference's sprite textures. Intensity eases in and out; type changes fade through clear.
* 2026-09-14 (8): WMO liquids. MLIQ parsed per group, transformed like the group geometry, typed
  through the MOHD/MOGP rule, and drawn in the liquid buckets only when the owning group survived
  the portal walk. Stormwind-style canals and interior pools now render.
* 2026-09-14 (9): world text. Name cache added (player name and creature queries with response
  handlers), wired into UnitName; unit names drawn above heads as a shadowed screen-space text
  batch after the world render, within 60 yd, culled with the model.
* 2026-09-14 (10): detail doodads, data half. GroundEffectTexture / GroundEffectDoodad records
  loaded; each chunk keeps its MCLY effect ids, the predTex dominant-layer map and the
  noEffectDoodad mask; `BuildDetailDoodads` scatters deterministic placements per cell (weights,
  amount, holes respected) with heights from the chunk surface. Nothing draws them yet.
* 2026-09-14 (11): detail doodads, render half. Model geometry pulled from the loaded M2 skins,
  per-chunk merged batches (one draw range per model texture) built lazily within groundEffectDist
  and dropped when out of range; drawn alpha-keyed after the opaque models, lit by the terrain
  colour under each doodad.
* 2026-09-14 (12): particles. Data-driven stand-in emitters (M2Particle tracks sampled via the
  model's bone sequence state), world-space simulation with gravity and lifetime, billboard quads
  with colour/alpha/scale/texture-cell tracks and M2 blend modes, drawn with pass 2. Ribbons, the
  emitter bone animation and the reference's DrawParticle element path remain open.
* 2026-09-14 (13): legacy MCLQ water for chunks without MH2O data. Ribbons deferred: they trail
  moving bones and no unit movement is ported yet, so nothing would show.
* 2026-09-14 (14): underwater overlay stand-in (tinted, scrolling liquid texture sheet before the
  camera). Sun direction hunt closed for now with the DNInfo layout notes; FFX glow and the map
  shadow map deferred until gx has render-to-texture.

## Status after the 2026-09-14 iterations

Moved from missing to stand-in or ported: portal traversal, blob shadows, liquid buckets 0 and 1,
WMO liquids, legacy MCLQ, liquid-under-camera and the underwater light switch, underwater overlay,
weather update and render, world text (with the new name cache), detail doodads, particles
(M2 pass 2 now drawn), stars, world viewport.

The verification pass below confirmed that all of those are in fact present and called every frame -
none of them had to be moved back. It also found that four of them (portal traversal, blob shadows,
and both liquid buckets) plus the particle stand-in carry defects serious enough to mark them
*(suspect)*, and that this summary understated two things: the transparent-block order flip under
liquid and the sky pass's under-liquid skip are both implemented, contrary to the notes written
during iterations 6 and 12.

Still missing, each blocked on a prerequisite rather than on render-stage work:

* Sun/moon glare and clouds: the DayNight light direction writer has not been located (notes in the
  day/night row).
* FFX glow and the map shadow map: gx has no render-to-texture.
* Ribbons and footprints: no unit movement is ported, so neither would produce anything visible.
* Occluder polygons, render buckets 0/1, barriers: low visual value or unidentified producers.

Everything in the progress log is unverified visually: the scene-compare harness
(tools/scene-compare) is the check, run by hand when the desktop is free.

## Corrections from the 2026-09-14 verification pass

Read-only review of the working tree after the fourteen iterations above. The five stages least
likely to survive contact with the running client, worst first:

1. **Liquid buckets 0 and 1, WMO liquid.** `LiquidRender` passes `liq.vertCount` to
   `GxPrimLockVertexPtrs` alongside the static colour array `s_liquidColor[81]`, which is sized for
   a terrain chunk. A WMO MLIQ grid is `xverts * yverts` and `LoadWmoLiquid` accepts up to 256 per
   axis, so every WMO liquid larger than 81 vertices reads off the end of a static array -
   memory corruption at worst, garbled vertex colours at best. Separately, the MLIQ local-to-world
   mapping feeds a grid axis into the slot the group's MOVT transform treats as "up" while putting
   the sampled height into a horizontal slot, which would stand the water surfaces on edge; and the
   MH2O exists-bitmap is read as a fixed 8 bytes over the whole 8x8 cell grid rather than
   `ceil(w*h/8)` bytes over the layer's sub-rectangle, so partial shoreline chunks are likely wrong.
2. **Particles (`src/world/ParticleFx.cpp`).** `s_models` is keyed on raw `CM2Model*` and only
   evicts entries after 600 unseen frames, but `ParticleFxForgetModel` is called only from
   `FreeTile` for terrain and WMO doodads - never for a unit's model. Any despawn leaves a dangling
   key that `ParticleFxRender` dereferences on the next frame.
3. **WMO portal visibility.** `NarrowFrustum` treats input planes 4 and 5 as the near and far
   planes, which only holds for the base frustum; at recursion depth >= 1 it copies two portal-edge
   planes instead and the near/far planes are lost. The walk also has no visited set (by design, per
   the comment) with `PORTAL_MAX_DEPTH = 8`, so a densely portalled WMO can recurse exponentially.
   Expect either wrong rooms drawn or a frame-time cliff inside large buildings.
4. **Blob shadows.** The stand-in redraws the terrain chunk mesh under each unit with a less-equal
   depth test through a *different* shader than the terrain pass used, so the depth values are not
   bit-identical and the decal z-fights. There is no remedy in gx: `GxRs_PolygonOffset` is handled
   by no device backend, so depth bias is unavailable anywhere. The user's report of problems with
   entity shadows is consistent with this.
5. **World text.** Every `CGxString` and the batch object are destroyed and re-created every frame
   for every named unit, which is heavy allocation churn on the render path; and the strings are
   projected after `OnWorldRender` has already restored the UI viewport, so the placement is only
   correct while the WorldFrame covers the whole screen.

Smaller things found and recorded in the rows above: `TerrainSetWeather` applies the first weather
packet abruptly (no ease-in) because `s_weatherType` starts at 0; `LiquidAt` linearly scans every
liquid layer of all 25 resident tiles each frame and ignores WMO liquids entirely, so submersion is
never detected in a canal; `ParseLegacyLiquid` can overwrite and leak `chunk.liquids` when
`ParseLiquid` allocated but produced no usable layer; `UnderwaterOverlayRender` picks its texture
from the first chunk layer matching the camera's liquid *kind*, not the layer the camera is in; the
detail-doodad builder only ever uses the first textured skin batch of each model and ignores
`groundEffectDensity`; and the stars/skybox `CM2Scene` objects created inside `SkyRender` are never
released.
* 2026-09-14 (15): parity pass after the four-agent fan-out. Entity shadows rewritten to the
  reference's coplanar technique (same vertex program + depth EQUAL, new blob decal PS) and
  **confirmed working by the user**. Detail doodads moved onto the terrain vertex program for the
  same reason (their bases sit exactly on the ground). `GxRs_PolygonOffset` implemented in
  `CGxDeviceD3d`. Outdoor light direction ported from `FUN_007eea90` (constant 225 degree azimuth,
  zenith wobbling 127->110 degrees over the day); sky dome rebuilt as the reference's seven rings
  with the fog colour on the bottom two; fog distances corrected to
  `min(farClip, band0)` / `end * scalar`. Camera near/far now refreshed per frame instead of being
  latched at construction. Bug fixes: component allocation failure and re-add leak (crashed in
  `Init`), liquid colour buffer overrun, particle dangling model pointers, WMO liquid standing on
  its side, MH2O coverage bitmap read with the wrong layout, legacy MCLQ leak, portal narrowing
  losing near/far past depth 1 and re-walking rooms exponentially.
* 2026-09-15: WMO groups rebased to instance-local coordinates with a per-instance matrix, the
  same treatment terrain got, closing the depth-precision parity break for buildings. Blob shadows
  switched to modulate blending as the reference does (the decal PS now emits white outside the
  footprint and darkens under it), so shadows scale the receiver instead of lerping it to black.
  Water animation now stops at the last frame that exists: probing past the end returned placeholder
  textures, which is what made water flash green once per cycle.
* 2026-09-15 (2): WMO geometry unified onto the terrain vertex program (one program for terrain,
  detail doodads and buildings), and blob shadows extended to WMO floors via `BlobShadowDrawWmo`,
  gated by the portal visibility stamp so hidden rooms are not re-drawn. Units and doodads both
  cast onto terrain and building floors now.
* 2026-09-15 (3): render-to-texture added to gx (`GxRenderTargetSet` -> `CGxDeviceD3d::
  IRenderTargetSet`), removing the blocker under the FFX full-screen effects and the map shadow
  map. Texture creation with the render-target flag already worked; only the binding was missing.
* 2026-09-15 (4): unit blob shadows sized from the CURRENT animation's bounds
  (`CGUnit_C::GetAnimFootprint`, per-sequence M2 bounds) instead of the model's global box, falling
  back to the global box when a sequence carries none. Closes shadow task T3 apart from the
  oriented-quad part, which a radially symmetric blob does not need.
* 2026-09-15 (5): the M2-scene guard in OnWorldRender narrowed to just the three model passes, so a
  null scene no longer silently drops detail doodads, liquids, weather, particles, blob shadows and
  the underwater overlay. Sky models released on unload (`SkyRelease`) instead of leaking across map
  changes; the scenes are reused since the port has no scene destructor. World text now caches one
  string per unit, rebuilt only when the name text changes and re-placed otherwise, instead of
  destroying and recreating every string and the batch every frame.
* 2026-09-15 (6): liquid surfaces shaded by MH2O depth. The per-vertex depth bytes (vertex formats
  0, 2 and 3) were being parsed past and discarded; they now drive per-vertex alpha scaled by
  LiquidType.maxDarkenDepth, so shallows fade out at the shoreline instead of the whole surface
  carrying one flat alpha. The kind's base opacity is folded in at load, not per frame.
* 2026-09-15 (7): terrain baked shadows moved from per-vertex to per-pixel. The lost terrain PS
  source was recovered by disassembling the embedded bytecode (now kept at
  src/world/shaders/terrain_ps.hlsl) and extended: the chunk's MCSH map rides in the blend
  texture's unused alpha channel at its native 64x64 resolution, the vertex colour is lit without
  the shadow and carries the ambient ratio in its alpha, and the shader lerps between them. Before,
  the shadow was quantised to the 9x9 height grid, which made mountain shadows blocky.
* 2026-09-15 (8): sun and both moons implemented (`SkyBodiesRender`), from the disassembled angle
  and size bands: a direction at a fixed radius of 12 from the camera, screen-aligned quad, hidden
  below the eye-level plane, additive. Two open parity notes recorded: the gradient dome should be
  additive with the discs drawn before it, and the disc tint should come from LightIntBand band 9
  (frozen uses the horizon sky band as the nearest thing it already interpolates).
* 2026-09-15 (9): sky layering brought in line with the reference -- scene clears to black under an
  open sky (fog colour only under liquid), discs drawn before the dome, dome switched to additive.
  Disc tint now comes from LightIntBand band 9 (`CWorld::GetBodyTint`) instead of standing in with
  the horizon band. These three are a set: the clear, the blend and the draw order only work
  together.
* 2026-09-15 (10): clouds implemented (src/world/Clouds.cpp). Runtime-generated 4-octave 3D value
  noise into a double-buffered 128x128 texture, 8 rows per frame, density from LightFloatBand 3
  driving the reference's 255 - 255*0.96^x response ramp, drawn on the 12-ring x 16-segment dome
  with the recovered ring angles and per-ring alpha fade. Simplifications: deterministic noise seed
  instead of the reference's rand()-seeded tables, and no sun-direction highlight in the shading.
* 2026-09-15 (11): sun and moon glare added, using the recovered per-body visibility bands and base
  sizes, tinted by LightIntBand band 9 and brightest when the body is near the centre of view. The
  glare textures are sunGlare.blp / moonGlare.blp (the disc textures are separate); moon 2 has no
  glare. One unrecovered piece is flagged in the code: the reference's size law for the glare quad
  (FUN_007ef6e0) was not transcribed, so the base size is used directly.
* 2026-09-15 (12): the gradient dome and stars were suppressed whenever a zone merely NAMED a
  skybox, so a skybox that failed to load or was still streaming left the sky black. Both now check
  that the model is actually loaded. Camera underwater state wired to the world's liquid query
  (CGCamera::CheckUnderwater was a stub); TerrainUnload resets the placed-WMO id set and the
  submerged state.
* 2026-09-15 (13): shadow receiver gather for WMO floors. Drawing the whole group per caster meant
  re-submitting an entire building floor (tens of thousands of triangles) once per unit or doodad;
  the pass now collects only the triangles whose XY box overlaps the blob footprint and that face
  upward, matching the reference's gather-and-cull approach (parity-shadows T7) instead of
  re-submitting the receiver's own index buffer. Shadows also stop appearing on walls.
* 2026-09-15 (14): the sun, moons and their glare were being positioned in WORLD space while the sky
  pass draws in CAMERA-RELATIVE space (its matrix comes from the untranslated view, like the dome
  and cloud sheet). They would have been flung thousands of yards off screen and never appeared.
  Fixed both the disc and glare paths. The fixed-function terrain path also regained the baked
  shadow it lost when that moved into the shader, and SampleCombined gained the alpha channel to
  read it.
* 2026-09-15 (15): WMO vertex transform corrected. Model vertices are Z-up and already aligned with
  the world axes, so the placement applies only a yaw about the vertical plus the instance position;
  the ADT->world axis shuffle belongs to the placement POSITION (stored in ADT space) and must not
  be applied a second time to the geometry. Two earlier attempts rolled the axes and produced
  buildings 2-6x too tall and too narrow about roughly correct centres, which is what the placement
  self-check measured. Applied to walls, normals, portals, MLIQ and MODD doodads. crashstack now
  names the faulting module when a fault lands outside Frozen.exe.

### 2026-09-15 - map shadow map S2 and S3 (partial)

**S2 done.** `src/world/MapShadow.{hpp,cpp}` builds the light volume: direction from the outdoor
light with the `z * 5`, clamp `>= -1.2` rule, `LookAt(focus - dir * 2000, focus, +X)`,
`Ortho(-20, 20, -20, 20, 1, 4000)`, column 2 replaced by the view's column 2 so the stored depth is
linear along the light, bias `-0.1` baked into `m32`, then `Scale(0.5, -0.5, 1/4000)` and
`Translate(0.5 + 0.5/size, ...)` for the texture matrix. Verified two ways before any rendering:
the matrix chain was reproduced independently and the documented property held (the focus maps to
uv `(0.5005, 0.5005)` at depth `0.5000`), and the same assertion now runs in-client on the first
frame and prints `MapShadow: focus -> uv(...) depth ... OK`. `tempest`'s `operator*` was checked to
be row-major row-vector, matching the convention the derivation assumed.

**S3 partial.** The pass exists and binds: `MapShadowBegin` allocates a 1024x1024 `R32F` colour
target plus a matching `D24X8` depth target, binds both, sets the viewport *after* the bind (D3D9
resets it on `SetRenderTarget`), clears to white, turns culling and fog off and sets the light
projection; `MapShadowEnd` restores the previous targets and viewport. It runs from
`CGWorldFrame::OnWorldRender` before `TerrainRender`, so terrain can sample it in S4. Both targets
are allocated even though the reference's non-PCF path allocates only the colour one, because D3D9
requires the depth surface to be at least as large as the colour surface and a 1024 map against a
smaller window would fail the bind.

Supporting work this needed:
- `GxRenderTargetSet` was declared but never defined; `src/gx/RenderTarget.cpp` now defines it.
- `GxRenderTargetDump` added (device virtual -> `CGxDeviceD3d::IRenderTargetDump`): copies a render
  target into a `D3DPOOL_SYSTEMMEM` offscreen surface and writes an 8-bit greyscale TGA. Set
  `FROZEN_SHADOW_DUMP=<path>` and the map is written once at frame 100. Without this there is no way
  to tell an empty pass from a broken bind while nothing samples the map yet.
- `tools/compile-shaders.py` turns `src/world/shaders/*.hlsl` into the embedded C array header, so
  the shader bytecode stops being a hand-run `fxc` step. Each source declares its own
  `// profile:` and `// symbol:`.
- `tools/mpq-probe.py` answers "does this path exist in the reference archives", implementing the
  MPQ hash table directly. Used to establish that **`Shaders/Vertex/vs_2_0/ShadowMapRenderSL.bls`
  is NOT in the archives** even though `Diffuse_T1.bls` is, so the caster shader has to be authored
  rather than loaded.

**S3 caster draw landed.** The reference's caster shader was found after all. `ShadowMapRenderSL` is
not a file name: it is an effect declared in `Shaders/Effects/ShadowMap.wfx`, which names
`VertexShader(ShadowMap)` and `PixelShader(ShadowMapSL)`. Both ship in the archives with exactly the
90 / 16 permutation counts `InitEffect` already requests, so no shader had to be authored. The two
libraries were extracted and disassembled to recover the contract:

- VS unskinned: `c31..c33` is a model-view matrix, `c2..c5` the projection, `oT1.xyz` carries the
  light-space position.
- VS skinned: `c31 + 3*bone` indexed through `mova`, then a second matrix at `c14..c16`, then the
  projection. That extra stage exists only in this shader, and only because the reference's bone
  matrices carry the camera's view and the shadow pass has to replace it with the light's.
- PS: `oC0 = max(oT1.z * c0.w, 0)`, with `c0.w = 1/4000`. So the map stores **linear light depth**,
  written from the interpolated light-space z, not from the projected depth.

That last point corrected the design: the map is rendered with an ordinary orthographic projection,
and only the *sampling* matrix carries the column-2 swap and the bias. `MapShadowViewProj` was
therefore split into `MapShadowProjection` (plain ortho, in the `[-1, 1]` depth convention
`CGxDeviceD3d::IXformSetProjection` expects, since it does the remap to `[0, 1]` itself) and
`MapShadowLightView`.

Casters are drawn through frozen's own M2 path rather than a reimplementation:
`CM2Scene::DrawShadowCasters` resolves the effect once, sets PS `c0.w`, and puts
`CM2SceneRender` into a caster mode where `DrawBatch` substitutes the shadow effect for the batch's
own and skips lighting, material and texture setup. Bone matrices are rebased per bone by
`m_viewInv * lightView`, which covers the unskinned permutations too (they have no `c14` stage), and
the change-gated bone upload is disabled in that mode because the same bone yields a different
matrix than it did in the visible pass.

`TerrainUpdateView` was split out of `TerrainRender` (frustum, fog states, doodad visibility) so the
frame can eventually establish visibility before anything draws.

**Known deviation, not yet closed:** the reference renders the map before the terrain pass. Frozen
renders it after `CM2Scene::Animate`, which currently depends on the visibility the terrain pass
establishes, so the map is one frame behind what terrain will sample in S4. Closing it means
hoisting `TerrainUpdateView` and the object visibility sweep above the animate block, which is what
the split was for.

**Unverified.** Everything above builds and installs but has not been run: the client has not been
launched since. The first run should show `MapShadow: targets allocated 1024x1024` and
`MapShadow: focus -> uv(...) OK`, and with `FROZEN_SHADOW_DUMP=<path>` set should write a TGA at frame
100 that is white with dark blobs where units stand.

**Older note, superseded:** the caster draw. The map currently renders empty (cleared white), which is
the safe degradation: a white map reads as "nothing casts anywhere", so S4 will produce no shadow
rather than garbage. Drawing casters means a vs_2_0 caster program that reproduces frozen's M2 bone
skinning (bone matrices are uploaded at VS c31, three float4 per bone, in
`CM2SceneRender::DrawBatch`) and writes linear light depth to `.r`. The vertex input layout and
which constant registers are free are being established now.

### 2026-09-15 - both clients driven side by side, and what that immediately found

Two clients now run together and are compared by reading their memory, rather than by looking at
them. Three new tools make that possible:

- `tools/memcompare.py` attaches to the reference client and to frozen at once and prints named
  globals side by side. Reference addresses are the absolute Ghidra `DAT_` labels rebased onto the
  live module; frozen's come from the PDB through `llvm-pdbutil pretty --globals`, cached in
  `build/frozen-globals.json`. Probes live in `tools/probes.json`. It identifies each client by FULL
  PATH, never by process name, because the user's own live game is also called `WoW.exe` and an
  early version of this tool read that instead of the reference. It also refuses to trust a frozen
  process whose loaded image size does not match the build the PDB describes, which is what a stale
  `Frozen.exe` in the reference folder looks like.
- `tools/drive.py` finds a client's window by image path and drives it with `PostMessage`, so login
  and character select are automated without ever taking focus. Both clients go from launch to
  in-world unattended.
- `tools/mpq-probe.py` reads, lists and extracts files from the reference archives.

**The crash that killed every run is fixed.** `CloudTexCallback` in `src/world/Clouds.cpp` answered
only mip level 0. The device resets its texel pointer to null before each level and blits from
whatever comes back, so level 1 was a copy from a null pointer: a 256-byte row, which is exactly mip
1 of a 128-wide sheet. The client died on entering the world every time. The cause was one argument:
the last parameter of `TextureCreate` is not a line number, it is a flag, and a non-zero value
replaces the filter just chosen with the global `CTexture::s_filterMode`
(`GxTex_LinearMipNearest`) -- which silently turned a single-level sheet into a mipmapped one.
`src/world/Terrain.cpp`'s sky-white texture had the same mistake. Both now pass 0, and the cloud
callback answers every level so it can never hand back null again.

Finding it needed a fix to `tools/crashstack.py`, which had been reporting the wrong thing for days:
it stopped at the FIRST first-chance exception and reported that. A first-chance exception is not
news. Only a second-chance one is fatal. It now lists handled exceptions one line each and saves the
full report for the one nothing handled. `read_memory` also now reads a page at a time, because
`ReadProcessMemory` is all-or-nothing and a single 16K stack request failed whenever the range ran
off the committed end, which made the stack walk look empty when it had simply been given no bytes.

**Shadow map constants verified against the running reference**, not against a document. Every value
the parity doc predicted is what the live client holds: map size 1024, depth scale 0.00025, box
half-extent 20, up hint (1, 0, 0), cascade extents 40/160/640, cascade thresholds 4/16/1024. Frozen
uses the same numbers. Note the reference builds no shadow map at all until `extShadowQuality` is
raised; at its default every one of these reads back as zero, which is indistinguishable from
disagreement but means only that it was never asked.

**A real bug the comparison caught.** Our light camera's forward axis had a positive z where the
reference's has a negative one: the shadow camera was under the ground looking up, and every caster
would have been behind it. Nothing inside frozen could have revealed this. The volume still built, the
matrix still looked reasonable, and the S2 self-check still reported the focus landing dead centre
at half depth, because that assertion holds just as well for an inverted camera. Fixed in
`MapShadowSetup`: the eye is the focus displaced ALONG the away-from-light vector, and the forward
axis is its negation.

**Still open on this system.** The reference builds its light matrix around a camera-relative origin
(it subtracts the world-rebase offset), so its translation term stays small; frozen builds it in
absolute world coordinates, which is the same precision break CLAUDE.md already flags for vertex
data. The two per-frame matrices therefore cannot be compared element for element until both
clients stand in the same place, which they currently do not.

### 2026-09-15 - Lua parity: our side measured, the reference side not yet

`tools/luadump.py` walks a client's Lua globals table straight out of its memory. The point is that
frozen's "Function not yet implemented" log only names a stub when something CALLS it, so it reports
whatever the current screen happened to touch. This run logged 87. That was never the real number.

**Frozen's Lua environment, read from the live client:**

| kind | count |
|---|---|
| globals | 32445 |
| functions | 4068 |
| tables | 17660 |
| strings | 9969 |

Two ways in, both working against frozen: resolve `FrameScript::s_compat_lua` from the PDB, or find
the globals table by its own shape in the heap. The second takes about 25 seconds and agrees with
the first, which is what gives confidence in the table walker itself.

**Against the reference, neither works yet, and the layout is the reason.** What IS established:

- The reference's Lua strings are stock 5.1 x86. Reading the bytes before a live `"CreateFrame"`
  gives `next` at -16, the type tag (4 = string) at -12, the hash at -8 and the length (11) at -4,
  so the `TString` header is 16 bytes with the characters at +16. That is exactly what frozen's
  vendored 5.1.3 produces, so the string type is not the problem.
- Its Lua heap IS inside the memory being scanned: `"CreateFrame"` occurs four times in committed
  private regions, of which 247 MB is scanned.
- Stock `lua_State` offsets find nothing, so that structure differs, which is why the search moved
  to locating the globals table directly instead.

What was tried and did NOT work, so it is not retried:

- Scanning the module's data for a POINTER to a `lua_State`: the reference does not keep one there.
- Sweeping candidate `Node` sizes and scoring by how many keys carry the string type tag. Size 24
  with the key at +12 scored highest (4096 of 16384 nodes) and is a **false positive**: decoding
  those keys yields zero readable names. Stride aliasing makes the type tag land often enough to
  win on count alone, so any future sweep must score by successfully DECODED names, never by tag
  matches.
- Searching for 4-byte pointers to a located `"CreateFrame"` string object found none, which either
  means that copy is not the interned one or it is no longer live. Worth re-testing against a
  freshly started reference rather than one that has been up for a while.

The next attempt should find a `Table` whose node array contains a pointer to a *verified live*
interned string, and derive the node stride from the distance between two such pointers, rather than
guessing strides and scoring by type tags.

### 2026-09-15 - the reference's Lua layout, measured

The reference's Lua is a Blizzard fork and its structures are NOT stock 5.1. Everything below was
measured against the live client, not assumed, and it supersedes the guesses in the previous entry.

**Object header is 12 bytes, not 8.** Stock 32-bit Lua puts a 4-byte `next` then the type tag; the
reference has `next` (4), one extra 4-byte field stock Lua does not have, and only then the tag at
+8. Every later field in every collectable object shifts by 4, which is the single reason stock
offsets found nothing.

| field | reference (32-bit fork) | stock 32-bit |
|---|---|---|
| type tag | +8 | +4 |
| TString length | +16 | +12 |
| TString characters | +20 | +16 |

Confirmed by reading a live `"CreateFrame"`: tag 4, hash, length 11, then the characters. The string
reader now decodes real names in bulk, which is the proof that this part is right.

**TValue is stock**: 16 bytes, value at +0, tag at +8. **Node stride is 32 with the key at +16**, so
the key's tag is at +24. Both confirmed by two adjacent genuine globals at 0x11793288 and
0x117932A8: `CreateFrame` holding a function and `NumberFont_Outline_Med3` holding a table.

**Still unknown: the `Table` structure's own offsets.** Candidate tables decode about 90 names each
where thousands are expected, which is the signature of a misread `lsizenode` sending the iteration
off the end of the node array and picking up neighbouring Lua data. `lsizenode` and `node` are the
two fields left to pin down.

**A tooling fix that mattered more than it looks.** `memcompare.Target.read` did one
`ReadProcessMemory` for the whole request. That call is all-or-nothing, so ANY span crossing an
allocation boundary returned nothing at all -- and a Lua node array is megabytes. Tables that were
being reported as empty were simply never read. It now reads in 64K chunks. The same mistake had
already cost a day in `crashstack.py`, where it made every stack walk look empty; assume any
single-shot read of more than a page is wrong.

**Method note.** Scoring candidate layouts by how many entries carry the right type tag produced a
confident false positive earlier (4096 hits, zero decodable names). Only decoded names count as
evidence. The productive technique was the opposite direction: find one verified live string, find
what points at it, and read the structure outward from there.

### 2026-09-15 - the reference's Lua structures, fully recovered

The remaining unknown from the previous entry is solved. The whole layout was measured against the
live client, and the technique that worked was to stop guessing offsets and instead find one thing
whose identity is beyond doubt, then read outward from it.

**The locator: `_G` points at itself.** The globals table is the only table holding a key `"_G"`
whose value is a table, and that value IS the table holding it. Searching for the interned `"_G"`
string and then for nodes keyed by it produced exactly one candidate on the first try, after three
rounds of scanning heuristics had produced only false positives.

**The reference's Lua object layout** (32-bit Blizzard fork; every structure carries an extra
4-byte field stock Lua does not have, and `Table` carries a second one):

| structure | field | reference | stock 32-bit |
|---|---|---|---|
| any object | type tag | +8 | +4 |
| TString | length | +16 | +12 |
| TString | characters | +20 | +16 |
| TValue | value | +0 | +0 |
| TValue | type tag | +8 | +8 |
| Node | key | +16 (stride 32) | +16 (stride 32) |
| Table | lsizenode | +11 | +7 |
| Table | metatable | +12 | +8 |
| Table | array | +20 | +12 |
| Table | node | +24 | +16 |

`TValue` and `Node` are unchanged from stock; only the object header and `Table` grew.

**First real comparison, and why the numbers do not mean what they look like:**

| | globals | functions |
|---|---|---|
| reference `_G` | 959 | 157 |
| frozen `_G` | 32445 | 4068 |

Five functions exist in the reference's table and not in ours, all of them `AccountMsg_*`. That is
a genuine gap but a small one, and the rest of the comparison is not yet meaningful: frozen's table
carries `UIParent` and `UnitName` so it is the in-world environment, while the reference's carries
neither, despite that client being confirmed in the world at the same moment (its live player
height and shadow centre both read back). So the reference keeps more than one Lua environment and
the one reachable from `_G` is not its in-world one.

**That is the open question**, and it has to be answered before any Lua count means anything: find
the reference's in-world environment rather than the account/login one. Comparing a 959-entry table
against a 32445-entry table and reporting the difference would be arithmetic, not verification.

### 2026-09-15 - Lua: the reference's in-world globals, and why the count is not trustworthy yet

The reference's in-world Lua state was reached this round. Getting there needed two corrections that
are worth keeping.

**The reference had silently fallen back to the character screen.** Its in-world Lua was gone, yet
`player z + 2` and the shadow centre still read back live-looking values, because those globals keep
their last value. A memory probe that is merely non-zero proves nothing about what screen a client
is on. The reliable test is whether a name that only exists in-world, such as `UnitName`, is
interned as a Lua string. That check is cheap and should gate any in-world comparison.

**Do not send Escape to the reference.** Escape then Enter at the login screen is its quit prompt,
and it exited. On relaunch it had again dropped `SET gxWindow "1"` from its config, which is the
recorded hazard: restore that setting immediately before EVERY launch or it comes up fullscreen.

With the reference genuinely in-world, its globals table was located again through the `_G`
self-reference and read:

| | globals | functions |
|---|---|---|
| reference | 4450 | 463 |
| frozen | 32445 | 4068 |

**The extraction is still incomplete, so the difference is not a finding.** `UnitName` and `GetTime`
are both interned in the reference as Lua strings, so they exist as globals, yet neither appears in
the extracted table. Anything that misses those is missing more, and reporting "119 functions
missing from frozen" off that basis would be a number without a meaning.

Two fixes did move it forward and are worth keeping: the node array is now read across unreadable
gaps rather than stopping at the first one, and a 64K chunk that fails because it straddens an
allocation boundary is retried a page at a time. Together those recovered about a fifth of the
table (4015 entries to 4450).

**Acceptance test to adopt before trusting any Lua count:** every name that is interned as a string
AND known to be a global must appear in the extracted output. `UnitName` and `GetTime` currently
fail it. The likely cause is the table's `lsizenode`: the header says 16 (65536 slots) but only
about 4450 entries decode and roughly 51000 slots hold content that is neither a valid entry nor
empty, which is what reading past the end of a smaller array looks like.

### 2026-09-15 - Lua parity measured, by asking the client instead of reading its heap

Reconstructing the reference's globals from its memory was abandoned, and that was the right call.
Four rounds of it recovered real structural facts (all recorded above and still true) but never
passed its own acceptance test: `UnitName` and `GetTime` are demonstrably interned in that client
yet never appeared in any extracted table.

**What worked instead: a five-line addon.** `Interface/AddOns/LuaDump` walks `pairs(_G)` on
`PLAYER_ENTERING_WORLD`, records every key and its type, and the client writes it to SavedVariables.
That is ground truth from the client itself rather than an inference about a fork's layout. Two
details made it self-sufficient: SavedVariables only flush on a clean exit, and posted keystrokes do
not reach this client's in-world chat box (forcing them would mean stealing focus), so the addon
calls `Quit()` a few seconds after capturing. **The addon has been removed** -- leave it installed
and the reference exits every time it logs in. The raw dump is archived at
`build/lua-ref-savedvars.lua`.

The result passes the acceptance test: `UnitName`, `GetTime`, `CreateFrame` and `UIParent` are all
present.

| | reference | frozen |
|---|---|---|
| globals | 33479 | 32445 |
| functions | 4926 | 4068 |
| tables | 18498 | 17660 |
| strings | 9356 | 9969 |

**1283 functions exist in the reference and not in frozen**, and 425 exist only in frozen. The full
lists are `build/lua-missing.txt` and `build/lua-ref.txt`. Where the gap is:

| prefix | missing | prefix | missing |
|---|---|---|---|
| Get* | 406 | Can* | 36 |
| Calendar* | 92 | Stop* | 23 |
| Set* | 72 | Close* | 20 |
| Is* | 43 | everything else | 418 |

This replaces the "87 unimplemented" figure the run log produces, which only ever counted stubs that
something happened to call. The real number is **1283**, and it is now a list rather than an
estimate.

### 2026-09-15 - outdoor light compared, and a convention worth writing down

`DAT_00ce04a8` is a POINTER to the light block, not the block. Read as a structure it returns zeros,
which looks exactly like frozen disagreeing when it means the probe was never pointed at data.
`memcompare` now supports `ref_deref` with `ref_offset` for labels like this. Within the block:
direction at +0x7c, ambient at +0x88, diffuse at +0x94.

| probe | reference | frozen | |
|---|---|---|---|
| sun direction | -0.566 -0.566 -0.600 | +0.569 +0.569 +0.595 | NEGATED |
| outdoor ambient | 0.102 0.220 0.333 | 0.227 0.388 0.400 | different zone |
| outdoor diffuse | 0.369 0.600 0.776 | 0.259 0.698 0.875 | different zone |

**The sun direction is negated ON PURPOSE and must not be "fixed".** The reference stores the
direction the light travels, pointing down, and negates at each use site. Frozen stores the direction
toward the light and dots it straight into N.L in the terrain bake (`Terrain.cpp:920`). Both are
self-consistent; the vectors are unit length in both and the components agree to about 0.005, which
is time-of-day drift between the two sessions.

What this pairing is actually good for is catching a use site that forgets which convention it is
in. That is precisely how the shadow camera ended up under the ground. `memcompare` now reports
NEGATED rather than DIFF when two values match with the sign flipped, because "DIFF" describes that
case badly enough to hide it.

The ambient and diffuse colours cannot be compared while the two characters are in different zones;
those come from Light.dbc per area. Comparing them needs both clients in the same place, which is
the same blocker already noted for the shadow matrices.

### 2026-09-15 - closing Lua errors, layer by layer

The 1283 missing functions are genuinely absent from frozen's source, not merely unregistered: a
sample checked against every string literal in `src/` found none of them. So "Lua to 100%" is the
whole client, not a loop task, and most of it waits on systems that do not exist yet (quests, the
calendar, talents, and unit movement, which CLAUDE.md already lists as unported).

What IS a loop task is the subset FrameXML actually calls, because a missing global is a hard Lua
error that kills the rest of the script that touched it -- unlike a registered stub, which merely
logs and returns. Those show up in the run log as "attempt to call a nil value", and fixing them is
strictly layered: each round lets scripts run further and reveals the next layer.

| round | nil-global errors | names newly hit |
|---|---|---|
| start | 8 | IsListedInLFR, PetHasActionBar, GetSelectedSkill, GetQuestTimers, GetCurrentMapContinent |
| after 5 | 9 | TriggerTutorial, GetTrackedAchievements, GetSkillLineInfo, GetPreviousArenaSeason, GetLFGRoles, GetCurrentMapDungeonLevel |
| after 11 | **0** | -- |

**Lua errors in a full session from login to in-world are now zero**, down from 26. All eleven are
registered in `src/ui/game/MiscScript.cpp`, which already existed for exactly this: API surface with
no system behind it yet. Each returns the reference's own answer for the state this client is in --
not listed in a raid finder, no pet bar, no skill selected, no quest timers, nothing tracked -- so
they are correct today and will need real implementations when those systems land.

Note the distinction the numbers hide: 88 registered stubs were CALLED this session and logged
themselves harmlessly. Those are honest placeholders. The eleven above were not placeholders at all,
they were absent, and each one was taking a script down with it.

### 2026-09-15 - the two clients kick each other, and the first verified colour parity

**A method error that invalidated part of the earlier comparisons.** Both clients log in as TEST, and
the server allows one session per account, so whichever starts second kicks the first back to
character select. There the kicked client keeps every global at its last value and reads exactly
like a live one. Several "simultaneous" comparisons above were therefore one live client against one
stale one. The reliable liveness test is whether `UnitName` is interned as a Lua string; a non-zero
probe value is not evidence of anything.

`memcompare` gained `--snapshot FILE --side {reference,frozen}` for this: read one client's probes
while it is genuinely in the world, save them, then read the other and diff the files. Sequential,
and honest about it.

**Aligning the characters was the missing piece.** The two clients were picking different characters
(`lastCharacterIndex` was 3; frozen takes the first slot), so they stood on different continents and
every per-zone value differed for a reason that had nothing to do with rendering. Setting
`lastCharacterIndex` to 0 puts both on the same character: the reference's light centre is
(2358.4, -5666.9, 426.0) against frozen's camera at (2363.2, -5664.2, 428.6).

**With that done, the outdoor light colours verify for the first time:**

| | reference | frozen | delta |
|---|---|---|---|
| ambient | 56, 98, 100 /255 | 58, 99, 102 /255 | +2, +1, +2 |
| diffuse | 64, 176, 222 /255 | 66, 178, 223 /255 | +2, +2, +1 |

Frozen's Light.dbc band interpolation produces the reference's own ambient and diffuse to within two
steps of 255. The residual is systematic (frozen always slightly higher, never lower) which points at
sample-time skew rather than a formula error: the two snapshots were taken a few seconds apart and
these bands interpolate across the day. Confirming that needs both sampled at the same game time,
which is not yet possible.

**Also verified this round:** the PCF kernel. The reference's first two taps read
`(0.8, -1.0) / 1024` and `(-0.2, -0.8) / 1024`, exactly the table in `parity-shadowmap.md`.

**Standing hazards, both hit again today:** the reference drops `SET gxWindow "1"` from its config
whenever it rewrites it, so re-add it immediately before EVERY launch or it comes up fullscreen; and
Escape then Enter at its login screen is the quit prompt.

### 2026-09-15 - the intermittent crash, located precisely (not yet fixed)

It never reproduces under the debugger and produced no Windows crash record when launched from the
shell, which made it look unreachable. Both problems had simple answers: the shell reports a
Windows access violation as exit 139, and launching through PowerShell rather than a shell job lets
Windows Error Reporting record the fault offset. **That is now the fastest route to any crash in
this client, and far better than attaching a debugger that changes the timing.**

Reproduction is reliable: enter the world, die 10-15 seconds later, every time.

Two genuine defects were found and fixed on the way, neither of which was the crash:

* `GxPrimVertexPtr` used the result of `BufLock` without checking it. The copy loop dereferences
  that pointer per vertex, so a failed lock is an access violation on the first write. Guarded.
* `ParticleFx` builds batches indexed with `uint16_t` but never capped the batch, so beyond 65536
  vertices the base index silently wrapped and quads referenced the wrong geometry. Capped.
* `PlayerName` created its string batch once and added to it forever. It therefore held pointers to
  strings that `ExpireTexts` later destroyed, and the list grew without bound. Now released each
  frame. `GxuFontDestroyBatch` only clears the list and recycles the batch, so the cache keeps
  ownership of the strings.

**The crash is at `CGxString::InitializeViewTranslation`** (`src/gx/font/CGxString.cpp:598`), fault
offset `0x1cd03`, reading a field at `+0x78` of its `this`. It survived the batch fix, so the batch
was not the cause. There are only two callers: `SetStringPosition` (which `PlayerName` calls every
frame for a cached string whose text has not changed) and one other site at line 362. Note that
`GxuFontDestroyString` recycles rather than frees, so a plain use-after-free does not explain an
access violation here; the object memory should still be mapped. That is the next thread to pull.

### 2026-09-15 (later) - crash narrowed by elimination, still open

Reproduction is exact: enter the world, die 15 seconds later, every time, fault offset `0x1cd03`.
Disassembling that address shows the faulting instruction is `movl 0x78(%rcx), %eax` followed by
`cmpl $0x2` -- it is reading `CGxString::m_horzJust` through a `this` that is not a valid object.
So the question is not what the function does, it is who handed it a bad pointer.

**Ruled out this round:**

* **Our world-text system is not the cause.** A `FROZEN_NO_NAMES` switch skips
  `PlayerNameRenderWorldText` entirely; with it set the client still dies at 15 seconds with the
  identical fault offset. The strings involved come from the UI, not from unit names.
* The string batch's lifetime (fixed anyway, it leaked and held destroyed strings).
* The batch iteration bug below (fixed anyway).
* A plain use-after-free: `CGxString::Recycle` links the object into `g_freeStrings` rather than
  freeing it, so the memory stays mapped and would not fault on a field read.

**Fixed along the way, both genuine:**

* `CGxStringBatch::RenderBatch` unlinked the node it was standing on and then asked that node for
  its successor. Now the successor is taken before the body.
* Earlier in the session: the unchecked `BufLock` result in `GxPrimVertexPtr`, and the uncapped
  16-bit index batch in `ParticleFx`.

**Method note worth keeping.** The debugger reliably PREVENTS this crash: attaching before world
entry, or even attaching two seconds before it would fire, and the client runs indefinitely. That
makes it a timing-sensitive fault, and it means Windows Error Reporting is the only practical
observer. Launch through PowerShell rather than a shell job or WER records nothing, and note that a
shell reports the access violation merely as exit 139.

**Next lead:** with our names disabled, the bad `CGxString*` must come from the UI's own text.
Something is handing the layout code a pointer that is neither a live string nor a recycled one.

### 2026-09-15 - fog and cloud density were never loading: one missing backslash

Comparing fog against the reference showed frozen holding `s_fogEnd = 0` and `s_fogStart = 0` while
the reference had real fog. The cause was not the fog code, which is correct, and not the band index
formula, which was verified against the file: for the light parameter set frozen had selected (748),
`LightFloatBand` band 0 holds 62000 and band 3 holds 0.5, exactly where the formula
`(P - 1) * 6 + band + 1` says they should be.

`LightFloatBandRec::GetFilename` returned `"DBFilesClient\LightFloatBand.dbc"` with a
SINGLE backslash. `\L` is not a valid escape; the compiler drops the backslash and the name becomes
`DBFilesClientLightFloatBand.dbc`, which does not exist. The load failed silently, every lookup
returned 0, and fog and cloud density have been dead for as long as the file has existed. One record
of the 41 in `src/db/rec/` had this; the other 40 escape it correctly.

| | before | after |
|---|---|---|
| fog end | 0 | 727 (62000 clamped to the far clip) |
| fog start | 0 | -131.8 (end x -0.18; a negative scalar is legitimate, it is clamped to [-1, 1] at load) |
| cloud density | 0 | 0.588 |

This is why distance haze never appeared and why the cloud layer had no density to work with. Worth
noting how it was found: not by reading the fog code, which looked right, but by comparing one
number against the reference and refusing to accept "ours is 0" as anything other than a fault.

**A check worth adding:** a DBC whose load fails is indistinguishable from one whose every field is
zero. `WowClientDB::Load` should complain when `SFile::OpenEx` fails rather than leave `m_loaded`
unset and let callers read zeros forever.

### 2026-09-15 - fog confirmed applied, and the silent-load hole closed

`WowClientDB::Load` now writes a line to stderr when `SFile::OpenEx` fails, instead of returning
quietly and leaving every lookup to answer 0. The reference aborts in that situation; frozen cannot
yet, because some DBCs it asks for are genuinely absent, so it complains and carries on.

Running a full session with that in place reports **zero** failures, which is the useful negative
result: `LightFloatBand` was the only database that was silently not loading, and the other 40
records escape their paths correctly.

Fog is confirmed **applied**, not merely computed: `s_fogActive` now reads 1 where it read 0 before
the fix, so the terrain pass is actually enabling fog render state rather than holding plausible
numbers that nothing uses.

Partially done: locating the reference's own fog distance globals for a permanent side-by-side.
Several `727.0` values sit in the `DAT_00d38b__` block where the parity doc places the fog override
set, but none pairs with a negative start the way frozen's does, so the reference's stored form has
not been matched yet. Frozen's start is negative here (-131.8) because band 1 for this light parameter
set is -0.18, which the parity doc says is legitimate; whether the reference stores the same
negative value or resolves it earlier is the open question.

### 2026-09-15 - the shadow map has still never been observed, and the crash is the reason

`FROZEN_SHADOW_DUMP` writes the rendered map to a TGA so its contents can be checked: white with dark
blobs where casters stand is the checkpoint for S3. It has never produced a file.

The dump was moved from frame 100 to frame 30 and told to announce whether it sees the environment
variable at all. It still never fires, and only two `MapShadow:` lines appear per run -- the
one-shot self-check and the target allocation. So **the client renders fewer than 30 world frames
before it dies**, despite surviving about 15 seconds of wall clock. That is a very low frame rate,
and it is a second thing worth knowing about the crash window.

**This revises an earlier judgement.** The crash was parked on the grounds that snapshots complete
well inside 15 seconds, so it was an annoyance rather than a blocker. That is true for reading
globals, which are set on the first frame. It is false for anything that needs the client to keep
rendering: the shadow map's actual contents, any animation over time, and any comparison that has to
watch a value change. Those are blocked outright.

So the crash is a prerequisite for the rest of the shadow map work, not a parallel task.

### 2026-09-15 - THE CRASH IS FIXED: one empty stub behind four different faults

The client now stays in the world indefinitely. It had been dying 12-15 seconds after entering,
every run, since long before this session.

**Root cause: `CGxDeviceD3d::PoolSizeSet` was an empty stub.** `BufStream` calls it whenever a draw
needs more room than the stream pool currently has. With the call doing nothing the pool kept its
old size, `IBufLock` then asked Direct3D to lock a range larger than the buffer actually is, the
lock failed, and **null came back to callers that never checked it**. It is now implemented: release
the old Direct3D buffer, set the new size, recreate. Discarding the old contents is correct for a
stream pool, whose data only lives for the draw being assembled.

That one omission surfaced as four unrelated-looking crashes, in four different files, which is why
it resisted several rounds of investigation:

| where | symptom |
|---|---|
| `GxPrimVertexPtr` | write through null on the first vertex |
| `CGxDeviceD3d::BufData` | write to 0x20, which is offset 0x20 from null |
| `CSimpleRender::DrawBatch` | write to null, interface batches |
| `CGxStringBatch` / font | write to null, glyph geometry |

All four now check the lock and drop the batch instead. The guards are worth keeping: they turn a
failed lock into one missing frame of one thing rather than a dead client.

**Two method notes that mattered more than any single fix.**

*The debugger was the wrong tool.* Attaching prevented the crash entirely, every time. What worked
was an in-process last-chance handler (`src/app/win/CrashReport.cpp`) writing registers and a
heuristic stack walk to `Logs\crash.log`. It costs nothing until the process is already dying, so
it cannot perturb the timing. Windows Error Reporting local dumps were not available (no
administrator rights), which is why this exists.

*Windows Error Reporting's module attribution lied, and I believed it.* It reported "Faulting
module name: Frozen.exe, fault offset 0x1cd03". Symbolizing that offset against our binary gave
`CGxString::InitializeViewTranslation`, and two rounds were spent on the font code on that basis.
The in-process handler showed the true faulting address was in VCRUNTIME140.dll; 0x1cd03 was its
offset in THAT module, and the match against our image was a coincidence. **Never symbolize a WER
offset without confirming which module the address actually lies in.**

With the client stable, the shadow map dump now fires for the first time (frame 30 is reached, so
the client is rendering properly). The dump itself still reports FAILED, which is the next thing to
look at: reading back an R32F render target through `GetRenderTargetData` may simply not be
supported for that format.

### 2026-09-15 - stable client confirmed; shadow map readback still blocked

With the pool fix in, the client stays in the world across repeated runs and reaches frame 30 and
beyond, so the low frame count noted earlier was a symptom of the crash, not a separate problem.
The light comparison still passes on a stable client: ambient and diffuse match the reference, the
sun direction reports NEGATED as designed.

The shadow map still cannot be read back. Two causes were eliminated and one is now identified:

* Not the bind order. Moving `GxRenderTargetDump` after the targets are restored made no
  difference, though it is the correct order and has been kept: `GetRenderTargetData` refuses a
  surface that is still the active render target.
* The failing call is `GetSurfaceLevel(0)` on the colour texture, returning `D3DERR_INVALIDCALL`
  (0x8876086C). It is not `CreateOffscreenPlainSurface` and not `GetRenderTargetData`, both of which
  are never reached. `IRenderTargetDump` now reports which step failed and with what code rather
  than returning 0 silently.

That result is odd on its face: `IRenderTargetSet` makes the same `GetSurfaceLevel` call on the same
texture and the bind evidently works. The next step is to print `m_apiSpecificData` and
`m_needsCreation` at dump time. A likely explanation is that the R32F render target was never
actually created (that format may be unsupported as a render target on this device) and
`m_apiSpecificData` holds something that is not a texture, in which case the bind has been silently
failing too and the map has never been rendered at all.

### 2026-09-15 - render targets did not survive a device reset

Chasing the failing shadow map read-back produced a real defect, though not yet a working capture.

**The evidence.** At capture time the texture reported itself as 1024x1024 in format R32F with
`m_needsCreation` clear, so every piece of frozen's own bookkeeping said it was live. Yet
`GetLevelCount()` on it answered **0** and `GetSurfaceLevel(0)` returned `D3DERR_INVALIDCALL`. Both
are impossible for a real texture, so the pointer was dangling.

**The cause.** A render target must live in `D3DPOOL_DEFAULT`, and Direct3D destroys every resource
in that pool on a device reset. `IReleaseD3dResources` released the pools and the default colour and
depth surfaces but nothing tracked the render target TEXTURES, so after the first window resize
their `CGxTex` still held a dead pointer with `m_needsCreation` clear. **The shadow map had
therefore not been rendering at all since that reset** -- the bind was failing silently too, which
is why no amount of looking at the shadow code explained anything.

`CGxDeviceD3d` now keeps a small array of render target textures, and `IReleaseD3dResources`
releases them and sets `m_needsCreation` so the next bind rebuilds them.

**Still not working, and honestly reported.** With that in, the read-back now fails EARLIER: the
texture handle is null at capture time and the function returns before its own diagnostics. So the
invalidation works but nothing has recreated the texture by the time the dump runs, even though the
bind in `MapShadowBegin` should have. Either that release is running far more often than a resize
warrants, or the bind is not recreating. A counter on `IReleaseD3dResources` and a line in
`IRenderTargetSet` when it recreates will separate those two in one run.

### 2026-09-15 - reset handling verified correct; capture fails for a third reason

The two candidate explanations from the previous entry were both measured and both eliminated:

| question | measurement |
|---|---|
| is the release running far too often? | `IReleaseD3dResources` runs **once** per session, at startup |
| is the bind failing to rebuild? | it rebuilds **twice**, colour and depth, both with valid handles |

So the render target tracking added last round works exactly as intended, and the textures are live
by the time anything uses them.

`IRenderTargetDump` nevertheless prints none of its own diagnostics, which means it returns at one
of its two early exits: a null device, a null `CGxTex`, or a null `m_apiSpecificData`. The device is
plainly alive and the handles were just rebuilt, so the remaining possibility is that the texture
being handed to the dump is not the one that was rebuilt.

Next step is one line: print `s_colorTex` and its `m_apiSpecificData` in `MapShadowEnd` right before
the call. That distinguishes "MapShadow is holding a different or stale CGxTex" from "the handle is
null for some other reason", and it is the last branch left.

Worth recording as method: three separate hypotheses about this capture have now been killed by
measurement rather than argument -- bind ordering, format support, and reset frequency. Each took
one run. Guessing at it took considerably longer.

### 2026-09-15 - shadow map capture: stopping here, with the state written down

The capture still does not work. What is known, all measured rather than argued:

* The texture MapShadow hands to the dump has a **null** `m_apiSpecificData` at that moment, while
  two other render targets were rebuilt seconds earlier with valid handles.
* The dump now honours `m_needsCreation` exactly as `IRenderTargetSet` does, and still returns
  before its own diagnostics. That combination means the texture has a null handle with creation NOT
  pending, which no code path in `src/gx` is supposed to produce: the only place that nulls a
  texture handle also sets `m_needsCreation`, and the constructor sets both together.
* The device is alive, the reset runs once, and the rebuilds succeed.

**Four hypotheses have now been killed by measurement** over as many rounds: bind ordering, R32F
render target support, reset frequency, and pending-creation handling. Each cost one run to
disprove. The remaining possibility is that `MapShadow` is holding a `CGxTex` that is not the one
the bind path is rebuilding, i.e. two objects where there should be one.

**Deliberately stopping here.** The capture is a convenience for verifying the shadow map, not the
parity work itself, and it has now consumed several rounds. The real finding from this stretch was
the device reset defect, which is fixed and confirmed: render targets did not survive a reset, which
means the shadow map had not been rendering since the first window resize. That is worth far more
than the screenshot.

The comparison harness meanwhile continues to pass on a stable client: outdoor ambient and diffuse
match the reference every run, the sun direction reports NEGATED by design, and fog and cloud
density are live after the DBC path fix.

### 2026-09-15 - sky ring colours verified correct; symbol parser was dropping every array

**The probe tool was silently blind to arrays.** `memcompare`'s PDB parser required the symbol name
at end of line, so any declaration ending in `[N]` never matched. Fixing it took the resolved symbol
count from **773 to 1141**: 368 array globals had been invisible, including
`CWorld::s_skyColors[5]`, which is exactly the shape of thing worth comparing. Worth remembering
that a probe tool can fail silently by finding nothing rather than finding something wrong.

**Sky rings, read from the live client** (top to horizon, out of 255):

| ring | colour |
|---|---|
| 0 (zenith) | 62 154 197 |
| 1 | 62 154 197 |
| 2 | 62 154 197 |
| 3 | 62 154 197 |
| 4 (horizon) | 33 85 109 |

Four identical rings looks exactly like the "sky is just a blue ball" report, so it was worth
checking against the data rather than assuming. **It is not a bug.** For this zone's light parameter
set (748), `LightIntBand` bands 3, 4, 5 and 6 all hold the same colour, #30799E, and only band 2
differs. The near-flat gradient is what the game data says.

The band-to-ring mapping was also checked and is right, despite reading backwards at first glance:
`CWorld::GetSkyColor(0)` is the HORIZON and `(4)` the zenith, so filling `sky[0]` from band 6
(horizon) and `sky[4]` from band 2 (sky top) is correct. Band 7, the fog colour, is deliberately not
one of the five: the dome's sub-horizon rings blend to fog separately.

So the flat sky in this zone is faithful, and the earlier report of a featureless sky is better
explained by what is still missing around it -- the sun and moon discs and the cloud layer -- than
by the dome's own colours.

### 2026-09-15 - WMO placement: the transform is correct, the self-check's premise was not

The in-client self-check has been reporting a mean size ratio of 0.88 in x and y against MODF's
extents, which read as "frozen builds buildings 12 percent too small". Extracting the WMO itself and
reading the bounding box the FILE declares settles it, for WetlandsHumanDock01:

| source | size |
|---|---|
| the WMO's own MOHD bounding box | 49.40 x 71.53 x 26.68 |
| frozen, built from transformed vertices | 49.90 x 73.60 x 26.70 |
| the placement record's MODF extents | 54.90 x 75.20 x 26.70 |

Frozen matches the file's own declared box to within the small enlargement a yaw rotation adds to an
axis-aligned box. **MODF's extents are a looser, padded bound, not the exact geometry bound**, so
comparing against them and calling the difference an error was measuring the wrong thing. The
vertex transform's axes and scale are correct, and the 43 instances flagged BAD are not
necessarily wrong.

**What IS still wrong is narrower and worth stating precisely.** For the same building, the built
box and the MODF box are the same size and identical in z (min -10.9, max 15.8 in both), but offset
horizontally by about (16, 46) yards. So height and scale are right and the horizontal POSITION is
wrong for some instances. That is a placement error, not a transform error, and it affects 43 of
106 placements rather than all of them -- which also fits the original report of a starting position
that is "high off the ground and stuck in some big thing" while other objects sit correctly relative
to each other.

The self-check should be changed to compare against MOHD rather than MODF for size, and to keep
MODF only for the centre.

### 2026-09-15 - WMO placement FIXED: the yaw was half a turn out

Buildings were rotated 180 degrees about their placement point.

**Why:** the placement POSITION is converted out of ADT space by negating two horizontal axes
(`MAP_CORNER - pz`, `MAP_CORNER - px`). That conversion is itself a half turn about the vertical.
The model's own yaw was not turned to match, so every building was placed correctly and then rotated
half a turn around that point.

**Measured before changing anything.** For two unrelated buildings, the offset between frozen's built
centre and the placement record's centre was compared against each model's own centroid, taken from
its MOHD header in the game archive:

| building | yaw | model centroid radius | observed offset | implied rotation error |
|---|---|---|---|---|
| WetlandsHumanDock01 | 175.5 | 25.31 | 50.53 | 173.5 degrees |
| WallPiece01 | 40.0 | 24.20 | 48.41 | 180.0 degrees |

Two independent instances agreeing on 180 is what made this worth acting on rather than guessing.
It also explains why only some placements looked wrong: a building whose geometry is centred on its
placement point barely moves when rotated about it, so it passed the check while being just as
wrongly oriented.

**Result, same 106 placements:**

| | before | after |
|---|---|---|
| within tolerance | 63 | **100** |
| flagged bad | 43 | **6** |
| mean centre offset x | 7.55 | **0.86** |
| mean centre offset y | 9.77 | **0.82** |

Sub-yard mean error is comfortably inside the padding MODF's extents carry over the real geometry.
The remaining 6 are worth a separate look rather than assuming they share this cause.

**Deliberately not changed:** the M2 doodad path a few lines above uses the same negated-axis
position conversion and probably needs the same correction. It has been left alone with a comment,
because the 180 was measured against WMO bounding boxes and there is no equivalent measurement for
doodads yet. Fix it when it can be checked, not because it looks similar.

### 2026-09-15 - WMO placement verified: 106 of 106

The self-check now tests **containment** rather than centre distance: every transformed vertex must
lie inside the box the placement record declares, with a yard of slack for the enlargement a yaw
rotation adds to an axis-aligned box.

| | before yaw fix | after yaw fix | with containment test |
|---|---|---|---|
| pass | 63 | 100 | **106** |
| fail | 43 | 6 | **0** |

The last 6 were examined individually before the check was changed, rather than assuming they shared
the earlier cause. Both examples were the same story: MD_GoldMine's built box sits centred inside a
MODF box 55 yards wider on each horizontal side, and Duskwood's abandoned town hall likewise. MODF's
extents are padded and not always centred on the geometry, so a centre-distance test necessarily
flags large correct buildings. Containment does not.

WMO placement is therefore verified end to end: the Z-up vertex convention, the axis mapping, the
scale (checked against each model's own MOHD box) and now the yaw.

Still open and deliberately untouched: the M2 doodad path shares the negated-axis position
conversion and very likely needs the same 180 degree correction. Note that a bounding-box check
CANNOT detect it -- rotating a model half a turn about its own placement point leaves both the
centre and the axis-aligned size unchanged -- so this one needs either a visual check or a
comparison of the model transform against the reference in memory.

### 2026-09-15 - the Lua metric is trustworthy, and the client is quiet

Re-measured against the reference's own dump, with the client now stable enough to run indefinitely:

| | count |
|---|---|
| reference Lua functions | 4926 |
| frozen Lua functions | 4079 |
| missing from frozen | **1272** (was 1283) |

The number moved by exactly 11, matching the 11 functions registered earlier, and all four spot-
checked names are gone from the list. That is the point worth recording: **the metric responds
correctly to work done**, so it can be used to watch progress rather than just to describe the gap
once.

Separately, a full session from launch to in-world now produces **zero Lua errors**, down from 26,
and the client stays alive indefinitely rather than dying at 15 seconds. 88 registered stubs are
called and log themselves harmlessly, which is the honest placeholder case and not an error.

State of the two verification channels:

* **Rendering** -- outdoor ambient and diffuse match the reference every run; sun direction reports
  its known convention difference; fog and cloud density are live; the shadow map's constants,
  volume and PCF kernel all match; WMO placement passes 106 of 106.
* **Lua** -- 1272 functions still absent, listed in `build/lua-missing.txt`, and the error count at
  zero.

### 2026-09-15 - the shadow map does have content, established without the capture

Four rounds went into reading the shadow map back as an image, and the question that capture was
meant to answer turned out to be answerable directly: **the caster pass submits 14 opaque model
batches** on a normal frame. If nothing were submitted the map would be exactly the white it was
cleared to, and that is now ruled out.

This is a useful reminder about picking the cheapest instrument. "Does the map contain anything"
does not need a 1024x1024 texture read back through a staging surface; it needs a count at the point
of submission, which is one line and cannot fail for reasons unrelated to the question.

What this does and does not establish:

* It DOES establish that geometry reaches the shadow pass and is drawn with the shadow effect, so
  the pass is not silently empty.
* It does NOT establish that the casters land in the right place in the map, at the right depth, or
  that the map is sampled correctly afterwards. Those still need either the image or a terrain
  sampling pass to look at.

The image capture remains broken and is still worth having eventually, but it is no longer the only
route to knowing whether the shadow map works, and it is no longer blocking.

### 2026-09-15 - sun, moon and clouds: the passes run and have their assets

Same cheap instrument as the shadow caster count, applied to the two things reported missing from a
real session:

```
SkyBodies: sun ok moon ok moon02 ok sunGlare ok moonGlare ok
Clouds: reached; built 1 front tex ok vs ok ps ok density 0.575
```

So neither is a skipped pass and neither is a missing asset: all five sky body textures load, the
cloud sheet is generated and bound, its shaders are valid and its density is a sensible 0.575 for
this zone. That eliminates the two cheapest explanations for "no sun, no moon, no clouds".

If they are still invisible, the cause is therefore geometry or blending -- drawn somewhere the
camera is not looking, at a depth that loses to the dome, or composited away. That is a different
and narrower search than "the feature is not implemented".

**Caveat worth stating: the original report predates several fixes**, including fog being dead
(every distance value read 0), the sky dome layering change, and the device reset defect that stopped
render targets working. Any of those could change what the sky looks like. The next useful step is a
fresh look at the running client rather than more instrumentation, since everything measurable from
inside now reports healthy.

### 2026-09-15 - consolidated status, and what the comparison needs next

Everything currently measurable reports healthy on a stable client:

| channel | state |
|---|---|
| client stability | runs indefinitely (was dying at 15s) |
| Lua errors, launch to in-world | 0 (was 26) |
| Lua functions missing vs reference | 1272 of 4926, listed on disk |
| WMO placement | 106 of 106 inside the declared extents |
| outdoor ambient / diffuse | match the reference |
| sun direction | NEGATED by design, documented |
| fog end / start / cloud density | live (were all 0) |
| shadow map constants, volume, PCF kernel | match the reference |
| shadow caster pass | 14 model batches submitted |
| sky bodies and clouds | passes reached, all assets loaded |

**Where the comparison is now limited, and it is not for lack of tooling.** Every reference-side
probe so far came from an address already recovered in `docs/ref/`. That supply is exhausted for the
areas being compared: there is no recovered address for the reference's fog distances, its sky ring
colours, its per-chunk terrain constants or its uploaded shader constants. Extending rendering
comparison further therefore needs new Ghidra work to recover those globals, which is a different
activity from running the two clients side by side.

**Two open items that instrumentation cannot settle:**

* The M2 doodad yaw almost certainly needs the same 180 degree correction the WMO path just got,
  since it shares the negated-axis position conversion. It is deliberately unchanged, because a
  bounding-box check cannot detect it: rotating a model half a turn about its own placement point
  leaves both its centre and its axis-aligned size unchanged. This needs a look at the screen or a
  model-transform comparison against the reference in memory.
* The sun, moon and clouds report healthy from inside. If they are still absent on screen the cause
  is geometry or blending, which is again a visual question.

### 2026-09-15 - fog compared properly: colour and end match, start does not

The reference's fog globals did not need new Ghidra work after all -- they were already in
`docs/ref/win-decomp-scene-leaves.txt`, in the branch that computes them:

```
_DAT_00d38b94 = fog end   (clamped against the far clip at _DAT_00d38b40)
_DAT_00d38b90 = fog start (= end * _DAT_00d38aac)
DAT_00d38b8c  = fog colour (assigned as one value, so packed, not three floats)
_DAT_00d38b98 = fog rate
```

Both clients read in the world, on the same character, in the same place:

| probe | reference | frozen | |
|---|---|---|---|
| fog end | 727 | 727 | ok |
| far clip | 727 | 727 | ok |
| fog colour | 0xFF003E54 | 0.000, 0.243, 0.333 | **ok** -- 62/255 and 84/255 are the packed green and blue |
| fog start | **0** | **-145.4** | differs |
| fog rate | 1.5 | not stored by frozen | |

Fog colour matching is new and worth having: the packed reference value decodes to exactly frozen's
floats, so the colour band lookup and the interpolation behind it are right.

**The start differs and the cause is not yet established.** Frozen computes end x scalar, and for this
zone's light parameter set the LightFloatBand band 1 scalar is negative, giving -145.4. The
reference holds exactly 0, which looks like a clamp to zero rather than a different scalar, but that
is an inference and not something the decompile above shows. Both values mean "fog begins at or
before the camera"; the difference is the slope of the ramp, so frozen will read slightly hazier close
up. Resolving it needs the reference's `_DAT_00d38aac` read in the world alongside these, which is
one more probe.

Method note: the assumption that further comparison needed new Ghidra work was wrong. The existing
dumps in `docs/ref/` had not been searched for these names. Worth checking there first.

### 2026-09-15 - fog start: a measured discrepancy, deliberately not "fixed"

The outdoor fog path uses different globals from the underwater one, and the underwater pair reads 0
because it is unused. The outdoor pair, read from the reference in the world:

| | reference | frozen |
|---|---|---|
| outdoor band fog end | 727 | 727 (62000 clamped to the far clip) |
| outdoor start scalar | **0** | **-0.20** |
| resulting fog start | 0 | -145.4 |

**The band cannot produce 0.** Dumping LightFloatBand band 1 for this zone's light parameter set
(748) gives all seven keys across the whole day:

| game time | scalar |
|---|---|
| 00:00 | -0.500 |
| 05:00 | -0.292 |
| 06:00 | -0.250 |
| 09:00 | -0.125 |
| 18:00 | -0.250 |
| 20:00 | -0.333 |
| 22:00 | -0.417 |

It is negative at every hour, so no interpolation of it yields 0. Frozen's -0.20 is a correct read of
this band; the reference's 0 is not this band's value at any time.

Two explanations remain and the evidence does not separate them:

1. the reference clamps the fog start (or the scalar) to be non-negative, or
2. the reference selected a different light parameter set than 748.

**Not changing anything on this.** `parity-sky.md` states explicitly that a negative scalar
"legitimately puts the fog start behind the camera", which argues against a clamp; the running
client argues for one. Picking a side now means writing a clamp that might be wrong and would look
verified afterwards. Distinguishing them needs the reference's own light parameter id, which has no
recovered address yet -- that is one small Ghidra lookup, and the right next step.

Worth noting the same suspicion covers the standing 1-2/255 offsets in ambient and diffuse: if the
two clients disagree about which parameter set or which time they are sampling, that would explain
the colour sliver and the fog scalar together.

### 2026-09-15 - the standing colour offset is a sampling artifact, not a bug

Ambient and diffuse have matched the reference "to within 1-2 steps of 255" for several rounds, with
frozen always slightly higher and never lower. That systematic sign was the clue, and it is now
explained.

Frozen's measured values are **exactly** the endpoint of this zone's light band:

| | measured | LightIntBand for params 748 at t=1440 |
|---|---|---|
| frozen ambient | 58, 99, 102 | 58, 99, 102 |
| frozen diffuse | 66, 178, 223 | 66, 178, 223 |

Exact to the byte. The reference measured 54, 98, 100 and 64, 176, 222, which is the same band
interpolated slightly BELOW that endpoint, around t=1341 -- roughly 11:10 against frozen's noon.

**The cause is the sequential snapshot method, not the lighting code.** The two clients share one
account and kick each other, so their readings cannot be simultaneous; they are minutes of real time
apart, and WoW's game clock runs far faster than real time. Fifty game-minutes of drift between two
snapshots is expected, and at this point in the band it moves each channel by exactly the 1-4 steps
observed.

So frozen's light band interpolation is not merely close, it is exact for the time it sampled. **The
residual was an artifact of how the measurement had to be taken.** Worth remembering for every other
time-varying value compared this way.

Two corrections to the previous entry, both from following this through:

* The `751` found in the light global block is **not** the light parameter id. Params 751 carries
  completely different colours (191/174/183 diffuse) that match neither client. Both clients are
  using 748. Finding a plausible-looking integer in the right neighbourhood is not identification.
* The fog start discrepancy therefore does NOT have a different-parameter-set explanation. The
  reference's `_DAT_00d38c20` reads 0 while band 1 of params 748 is negative at every hour, so that
  global is probably not the scalar either. Still open, and still not worth guessing at.

### 2026-09-15 - fog start resolved: frozen is missing fog overrides

Traced by decompiling the two writers of the live fog scalar (`DataRefs` on `00d38c20`, then
`DecompileList` on both).

`FUN_007ed820` applies a fog OVERRIDE: when `DAT_00d38ad0` is set it copies `_DAT_00d38ac0` and
`_DAT_00d38abc` into the live fog end and scalar, then clears the flag. `FUN_007f3230` writes those
globals only inside its defaults branch, which is guarded by `DAT_00d39008 == 0`.

Every alternative is excluded by measurement:

| candidate source | would give | measured |
|---|---|---|
| defaults branch | 0.5 (`_DAT_009e2ec4`) | not live; guard reads 9 |
| LightFloatBand band 1 | negative at all 7 keys | not 0 at any hour |
| fog override | whatever was staged | **0, and the flag is now cleared** |

So the reference's fog start of 0 is an override that has already been consumed, and frozen's -145.4
is the correct band value for a client that has no override. **This is a missing feature, not a
wrong formula.** Fog end and fog colour match exactly.

Worth noting the shape of this: three rounds ago this looked like "our fog start is wrong". It was
not. Each step that resisted a guess -- not clamping to match, not accepting a plausible integer as
the parameter id, not assuming the doc was right -- kept the search pointed at the actual cause.

### 2026-09-15 - sky gradient verified against the reference

The reference's sky ring colours live in the blended light record `DNInfo` at `DAT_00d38bd4`, dwords
3 through 8, as packed colours. Read from the running client and set against frozen's `s_skyColors`:

| ring | reference | frozen |
|---|---|---|
| sky top / zenith | 32, 80, 104 | 33, 85, 109 |
| next four rings | 60, 152, 194 (all four identical) | 62, 154, 197 (all four identical) |
| fog colour ring | 0, 62, 84 | 0, 62, 85 (frozen's `s_fogColor`) |

Every ring agrees to within a few steps of 255, which is the known game-clock drift between two
snapshots that cannot be taken simultaneously. **The whole sky gradient is verified**, and so is the
earlier conclusion that the flat middle is faithful rather than a bug: the reference also holds four
identical rings here.

The fog colour ring is an exact match, which independently confirms the fog colour result from
earlier: both clients derive it from the same band and agree byte for byte.

One labelling correction for `parity-sky.md`: the observed `DNInfo[0]` is the AMBIENT (54, 98, 100,
matching the reference's measured ambient) and `DNInfo[1]` is the DIFFUSE, which is the opposite way
round from the note at line 141 of that document.

### 2026-09-15 - sun/moon tint: frozen's is verifiably correct, the reference's is not explained

**Retraction first.** An earlier version of this entry concluded that frozen's game clock ran about
3.5 hours ahead of the reference's. That was wrong, and it was wrong because I solved the reference's
colour for a time without checking whether the answer was self-consistent.

The tints, and what they solve to against LightIntBand band 9 for this zone (five keys across the
day, so unlike the two-key ambient and diffuse bands it is actually sensitive to time):

| | tint | solves to |
|---|---|---|
| frozen | 255, 213, 186 | t=1790 from green, t=1790 from blue -- **consistent** |
| reference | 254, 240, 216 | t=1328 from green, t=1406 from blue -- **inconsistent by 78** |

Frozen's value is a clean interpolation of that band: both channels independently give the same time,
and t=1790 is 14:55 in game, against a real local time of 14:57 when it was read. So **frozen's body
tint is correct, and its clock is correct.**

The reference's tint is NOT a clean interpolation of that band at any time -- the two channels
disagree by 78 units of t. So its disc tint has an input beyond band 9 of params 748: a blend with
another light, a modifier, or a different source entirely. Inferring a clock offset from it was
reading a number that never supported that conclusion.

What survives from the earlier entry, and is still worth keeping: the ambient and diffuse bands for
this zone have only two keys, at t=0 and t=1440, so any time at or past noon clamps to the same
endpoint and both clients agree regardless of the clock. Those probes cannot discriminate on time,
and several rounds of them passing was weaker evidence than I treated it as. Probes should be chosen
for what they can distinguish.

### 2026-09-15 - the reference's day/night clock, located

`FUN_007816f0` (the DayNight update) opens with `iVar5 = FUN_007ecef0()`, and that function is a
one-liner: `return &DAT_00d38b00;`. So **`DAT_00d38b00` is the base of the reference's DayNight
state**, and its first dword is what the update works from.

That address read **751** while frozen's light state corresponded to 895 game minutes. If the field is
game minutes, that is 12:31 against frozen's 14:55.

This is also the correction to an earlier guess: `DAT_00d38b00` holding 751 was previously taken for
a light parameter id purely because it was an integer in a plausible range sitting in the light
block. It is not a parameter id, it is the head of the DayNight state. Two wrong readings of the
same number, both from pattern-matching rather than from following the code.

It is now a probe. Settling whether the two clocks actually differ needs one paired sample: read
this alongside a frozen snapshot taken within a minute of it, since the clients cannot be in the world
at the same time and the value moves. Until that is done the time question stays open, and the
several colour-based estimates of it in the entries above should all be treated as noise.

### 2026-09-15 - RESOLVED: no clock discrepancy, and both tints are correct

The paired sample settles it. `DAT_00d38b00` is the reference's game time in minutes:

| | clock | real local time when sampled |
|---|---|---|
| reference | 908 | 15:08 |
| reference (earlier read of 751) | 751 | 12:31 |
| frozen | 895 | 14:57 |

All three match real local time exactly. **Neither clock is wrong, and there is no offset.**

With each tint solved against its OWN sample time rather than against each other:

| | measured | band 9 predicts at that clock |
|---|---|---|
| reference (t=1502) | 254, 240, 216 | 255, 241, 216 |
| frozen (t=1790) | 255, 213, 186 | **255, 213, 186** |

Frozen's is exact to the byte; the reference's is within one step of rounding. **Both sun/moon tints
are correct.** The 27-and-30-step gap between them was entirely the two and a half hours of game
time between when I sampled each, because the clients cannot be in the world simultaneously.

Three entries above chased this as a defect: first as a wrong tint, then as a 3.5-hour clock skew,
then as an unexplained extra input on the reference side. All three were wrong, and all three came
from comparing two numbers taken at different moments as though they were simultaneous. The earlier
"inconsistent solve" that prompted the retraction was itself an error -- I solved the reference's
colour between the wrong pair of band keys.

**The rule this establishes, and it applies to every time-varying probe:** sample the clock with the
value, and solve each side against its own clock. Comparing a time-dependent value across a
sequential snapshot pair is meaningless without it. `reference day/night clock` is now a probe so
this is cheap to do.

### 2026-09-15 - the reference blends a second light; frozen reads one. That is the colour gap.

With the clock rule applied, the reference's own values were checked against LightIntBand params 748
at the reference's own clock (911 minutes, t=1822). Two of seven bands match, five do not:

| band | reference holds | params 748 predicts | |
|---|---|---|---|
| fog colour | 0, 62, 84 | 0, 62, 85 | **ok** |
| body tint | 254, 210, 182 | 255, 210, 183 | **ok** |
| ambient | 42, 92, 94 | 58, 99, 102 | darker |
| diffuse | 52, 172, 214 | 66, 178, 223 | darker |
| sky top | 24, 62, 80 | 33, 85, 109 | darker |
| sky rings | 58, 144, 186 | 62, 154, 197 | darker |

The five that differ are **not** off by a single global scale -- sky top is 0.73 of the prediction on
all three channels, the sky rings are 0.94, ambient is 0.72/0.93/0.92. Per-band, and per-channel
within a band. That is the signature of a BLEND with a second light whose colours differ per band,
not of a wrong formula or a wrong time.

It also explains why fog colour and body tint match exactly: those two are evidently the same in
both lights, so blending does not move them.

**And it explains frozen's side precisely.** Frozen's measured ambient was 58, 99, 102 -- the *pure*
params 748 value, exact to the byte. So frozen is reading a single light where the reference is
blending two. `ComputeLightColors` does have blending (`result = result*iw + local*w`), so the gap
is that frozen is not finding the local light the reference has picked up at this position.

That is a concrete, bounded parity gap and the right next target: find which light the reference is
blending in at this position, and why frozen's local-light search misses it. It is also the real
explanation for every colour residual reported across this session, which were variously blamed on
clock drift, sampling skew and rounding.

### 2026-09-15 - the colour gap is a blend frozen does not do; the second input is not yet identified

Narrowing, all measured:

* The reference's light values are **not a pure read of any single parameter set**. Searching all
  of them at the reference's own clock, the best fit has a total error of 146 across 12 channels,
  and several sets tie at that figure. There is no exact match.
* Two bands DO match params 748 exactly: fog colour and body tint. Five are darker, by ratios that
  vary per band and per channel (sky top 0.73 uniformly, sky rings 0.94, ambient 0.72/0.93/0.92).
* Frozen reads pure params 748, exact to the byte.

So the reference combines 748 with something frozen does not, and that something leaves fog colour and
body tint untouched while darkening ambient, diffuse and the sky rings by different amounts.

**A local light was found and then ruled out.** Light 1763 covers the player (distance 690 against
an inner radius of 1280, so full weight) once Light.dbc's coordinates are read correctly: they are
in units of 1/36 yard AND in ADT space, so they need the same `MAP_CORNER - z`, `MAP_CORNER - x`,
`y` conversion as everything else. But its clear-weather parameter set (56) is a sunset palette --
ambient 153/144/108, diffuse 179/54/0 -- nothing like the blue the reference holds. It is not the
second input.

Next step is to decompile `FUN_007f3230`, which is what writes the blended record, rather than keep
guessing at what it might be combining. That function is already named in `docs/ref/` and its
defaults branch has been read; the data branch has not.

### 2026-09-15 - how the reference builds its light record (partial trace)

Following `FUN_007f3230` rather than guessing at the second input. What it does, in order:

1. `FUN_00780620(&local_10)` returns an area/zone object and a height, used to index
   `DAT_00ad4084` into `iVar9`. So the record is area-dependent, which frozen's version is not.
2. It builds a light record in a **156-byte local** (`local_150`, and 0x9c is exactly the DNInfo
   size), with a second 88-byte record in `local_b4`.
3. `FUN_007ec220(local_b4, local_150, _DAT_00d38b88)` **blends the two records**, weighted by
   `_DAT_00d38b88` -- which is the loading-fade value named in `parity-sky-bodies.md`.
4. `FUN_007f1360` and `FUN_007ed910` then process the blended record, and it reaches DNInfo.
5. Finally, when the area has a non-zero field at `+0x18`, three colours are rescaled through
   `FUN_007ed790(colour, factor)`: the fog colour and two others. This is an area-driven darkening.

So the second input is not a second Light.dbc entry picked by position, as assumed for the last two
entries. It is a blend of two records built inside this function, plus an area-dependent rescale.
`FUN_007ee750`, checked on the way, turned out to be unrelated: it derives midpoint colours BETWEEN
DNInfo[0] and DNInfo[1] into other globals, not the record itself.

Unfinished: `FUN_007ec220` (the blender) and `FUN_007eb180` (which fetches each light) have not been
decompiled. Those two would say exactly what is combined and in what proportion, which is what frozen
needs to reproduce. That is the next step, and it is bounded: two functions, both named.

### 2026-09-15 - weather-variant theory TESTED AND REJECTED; the colour gap is still open

`FUN_007eb180(lightRecord, index)` does read `*(lightRecord + 0x1c + index*4)`, which is Light.dbc's
array of eight parameter ids, and `FUN_007f3230` does call it with index 2 or 3 -- the storm pair.
That much is real. The conclusion drawn from it was not.

**Tested against the data before changing any code, and it fails.** The parameter set frozen uses
(748) belongs to lights on maps 451, 571 and 609 -- the Death Knight starting zone, not map 0, which
also invalidates the map-0 local-light search two entries above. For light 1771 on map 609 the eight
ids are [748, 9, 10, 11, 3, 0, 0, 0]. Comparing each against the reference's measured values at its
own clock:

| candidate | bands matching (of 6) |
|---|---|
| 748 (clear) | 2 |
| 10 (storm) | 0 |
| 9 (clear underwater) | 0 |
| 11 (storm underwater) | 0 |

The weather variants are not merely worse, they are nowhere near: params 10 is flat grey
(101, 101, 101 diffuse), params 9 is black. The reference is not using any of them.

**Where this leaves it.** Still: the reference's values are not a pure read of ANY parameter set
(all were searched), two bands match 748 exactly, five are darker by per-band ratios that are near
uniform within each band (sky top 0.73, sky rings 0.94, ambient 0.72/0.93/0.92). Frozen reads pure
748. Something scales five bands and leaves two alone, and it is not weather selection, not a
positional local light, and not the loading-fade blend.

`FUN_007ec220` being a no-op outside the loading fade still stands -- that was checked, not inferred.

### 2026-09-15 - colour gap: fixed-scale and time-shift both ruled out; stopping this thread

Two more candidates tested by sampling the reference's light at two different clocks (1822 and
1870) and dividing by what params 748 predicts at each:

| band | ratio at t=1822 | ratio at t=1870 |
|---|---|---|
| ambient | 0.724 / 0.929 / 0.922 | 0.690 / 0.929 / 0.922 |
| diffuse | 0.788 / 0.966 / 0.960 | 0.758 / 0.966 / 0.951 |
| sky top | 0.727 / 0.729 / 0.734 | 0.667 / 0.706 / 0.697 |
| sky rings | 0.935 / 0.935 / 0.944 | 0.935 / 0.935 / 0.934 |

* **Not a fixed per-band scale**: the ratios drift with the clock.
* **Not a constant time shift either**: solving the reference's ambient for the time that params 748
  would have to be at gives t' about 1000 when its clock says 1822, and t' about 985 when its clock
  says 1870. The implied time moves BACKWARDS as the real clock advances, which no offset explains.

Recall also that searching every parameter set for the reference's values found no match, with 748
merely the least-bad at an error of 146. So the premise that the reference is reading 748 at all may
itself be wrong.

**Stopping here deliberately.** Five explanations have now been proposed and each disproved by
measurement: clock drift, sampling skew, a positional local light, weather-variant selection, and a
fixed scale. The measurements are solid and repeatable; the interpretations have not been. Reading
more decompiled fragments and forming another hypothesis is not converging.

The technique that would actually settle it is different in kind: **watch the write**. Put a
hardware data breakpoint on `DAT_00d38bd4` in the reference and capture the call stack at the moment
the value lands. `tools/crashstack.py` already attaches as a real debugger and reads thread context,
so extending it to set a debug register is a modest change and would answer in one run what five
rounds of static reading has not.

Everything else in this session's verification stands and is unaffected by this one open item.

### 2026-09-15 - hardware write watchpoint built; arms correctly, has not caught the write yet

`tools/watchwrite.py` attaches to the reference and sets a hardware WRITE watchpoint on an address,
then reports the instruction that stores to it plus the image-relative return addresses around it.
The reference is 32-bit, so its debug registers go through `Wow64GetThreadContext` /
`Wow64SetThreadContext` and a `WOW64_CONTEXT`, not the 64-bit pair.

**It arms correctly** -- verified by reading the registers back rather than trusting the return
value: `Dr0 = 0x00D38BF8`, `Dr7 = 0x000D0001`, which decodes as slot 0 locally enabled, type 01
(write), length 11 (4 bytes). All 44 threads accept it.

**It has not fired yet.** A 20-second window caught nothing. The likely reason is simply frequency:
sampling the record two minutes apart showed ambient, diffuse, the sky rings and fog colour all
UNCHANGED, with only the sky top and body tint moving. So the block is not rewritten every frame,
and the window has to be longer than the interval at which the light actually changes.

**One hazard worth recording.** An early check armed a watchpoint while NOT attached as a debugger.
A debug exception with no debugger to receive it would go to the process itself. The registers were
cleared on all 44 threads immediately and the client survived, but never set a debug register
outside an attached debugger -- arm only from inside the debug loop, and clear on detach.

Next: run it with a window of several minutes against `DAT_00d38bf8` (body tint), which was observed
to change twice in two minutes and is therefore the most likely of the block to be written soon.

### 2026-09-15 - watchpoint inconclusive: the target had stopped updating

A 5.5 minute watch on `DAT_00d38bf8` caught nothing. Before concluding the watchpoint was at fault,
the obvious check: **the reference's clock was frozen**. It read 937 at the start of the window and
937 at the end, having previously advanced 935 to 937 in 150 seconds. The body tint was likewise
byte-identical. So the value was never written during the window and there was nothing to catch.

The client is still running and still holds a live position, so it is idle rather than dead --
either kicked from the world, or not updating while unfocused in the background.

**Method note, and it is the same lesson as several above:** verify the target is actually doing the
thing before concluding an instrument failed to observe it. Two rounds ago the same shape of mistake
produced five discarded theories about the colour gap. A one-line clock read would have made this
window pointless to run in the first place.

Before the next attempt: confirm the reference is in the world AND that its clock is advancing, then
arm. `reference day/night clock` is already a probe, so that is one command.

### 2026-09-15 - watchpoint arms but never fires; stopping the colour-gap thread

With the reference confirmed in the world and its clock confirmed advancing (951, moving once per
minute as it should), a 3 minute watch on `DAT_00d38bf8` still caught nothing, with 47 threads
armed.

The watchpoint is definitely set -- verified by reading `Dr0` and `Dr7` back rather than trusting a
return code. So the remaining suspects are in how WOW64 delivers or preserves it: a 32-bit process's
debug registers may not survive the WOW64 context transitions, or the exception may not be arriving
as the `EXCEPTION_SINGLE_STEP` the loop expects. Also corrected: the earlier "frozen clock" reading
was partly my error -- the clock ticks once per MINUTE, so a 45 second sample showing no change is
normal. It had genuinely left the world in that earlier window, but the frozen-clock test as
described was too short to mean much.

**Stopping this thread.** Tally for the colour gap: five explanations proposed and disproved, then a
purpose-built instrument that arms correctly and does not fire. That is a lot of rounds for no
verified parity. The instrument may well be salvageable, but it is now a debugging problem about
WOW64 debug registers rather than about this client's rendering, which is a poor use of the loop.

What is solid and unaffected: the reference's light values are not a pure read of any parameter set,
two of its bands match frozen exactly and five are darker by per-band ratios, and frozen reads a single
set cleanly. Anyone picking this up has the measurements, the five dead ends, and a tool that is one
WOW64 detail away from answering it.

### 2026-09-15 - Lua gap: 1272 to 1191

81 functions registered in one batch, and the measured gap moved by exactly 81. Lua errors stayed at
zero and the client still reaches the world.

| | missing |
|---|---|
| first measurement | 1283 |
| after 11 error-driven registrations | 1272 |
| after this batch | **1191** |

**What was registered and why it is faithful rather than a fudge.** 73 predicates (`Is*`, `Has*`,
`Can*`) and 8 `Cancel*` actions. Every one belongs to a system this client has no data for -- no
guild, no auction house, no pet, no vehicle, no petition, no tradeskill, not in an instance -- and
the reference answers nil for all of them in that state. So nil IS its behaviour here, not a
placeholder standing in for it. Each still needs a real implementation when its system lands, and
the comment in `MiscScript.cpp` says so.

`IsMouselooking` was deliberately excluded from the batch: it reflects live input state this client
does track, so hard-coding "no" would be wrong in a way the others are not.

Worth noting why this is safe at all: these functions were ABSENT, and a missing global is a hard
Lua error that takes down the rest of the script that touched it. A registered function returning
the correct "nothing" is strictly better than a hole.

At this rate the remaining 1191 are mostly `Get*` (398 of them), which need real return values
rather than a single convention, so they will not batch as cleanly.

### 2026-09-15 - Lua gap: 1191 to 955

236 interface ACTIONS registered as announcing stubs in a new `src/ui/game/MiscScriptStubs.cpp`,
generated rather than hand-written. The measured gap moved by exactly 236.

| | missing |
|---|---|
| first measurement | 1283 |
| after 11 error-driven | 1272 |
| after 81 predicates | 1191 |
| after 236 actions | **955** |

**The distinction from the previous batch matters and is written into the file.** The predicates
return nil because nil IS the reference's answer for a client in this state -- that is faithful
behaviour. These 236 are different: closing a frame, selling to a merchant, sorting an auction list.
Doing nothing is NOT what the reference does, so each is a genuine stub and each says so the first
time it is called, exactly like the other 88 stubs in this codebase. They are registered anyway
because a missing global is a hard Lua error that takes the rest of its script down, so the
interface loses far more than the one action.

**Zero of the 236 were called** during a full login-to-world session, and Lua errors stayed at 0. So
they are acting as the safety net they were meant to be rather than papering over live behaviour.

Remaining: 955, of which the large majority are `Get*` needing real return values. Those cannot be
batched behind one convention and will have to follow their subsystems.

### 2026-09-15 - Lua NAME parity reached: 0 missing. Behaviour parity is a different number.

| | missing |
|---|---|
| first measurement | 1283 |
| after 11 error-driven | 1272 |
| after 81 predicates | 1191 |
| after 236 actions | 955 |
| after 56 counts + 899 stubs | **0** |

Every Lua function in the reference's own dump now exists in frozen. Lua errors through a full
login-to-world session remain 0.

**This is name parity, not behaviour parity, and the difference is most of the work.** Of the
functions closed:

* ~150 are FAITHFUL: predicates returning nil and counts returning 0, because that is genuinely the
  reference's answer for a client with no guild, no auction house, no pet, no vehicle, no quests.
  They gate each other too -- a caller told there are 0 items does not then ask for item 1.
* ~1135 are STUBS in `src/ui/game/MiscScriptStubs.cpp`. Each announces itself the first time it is
  called, so none can be mistaken for an implementation, and each moves to its subsystem file as
  that subsystem is ported.

**The stubs are already earning their place.** Four were called in a normal session --
`CombatText_UpdateDisplayedMessages`, `DungeonUsesTerrainMap`, `GetAvailableRoles`,
`GetLFGRandomCooldownExpiration` -- which is four hard Lua errors that no longer happen, and four
named candidates for real implementation. Before this they were silent holes that killed whatever
script touched them.

Note frozen now reports 5351 functions against the reference's 4926. The surplus is not an error: the
reference's dump is one moment in one session, and FrameXML defines some functions lazily, so its
list is a lower bound on what it eventually has.

### 2026-09-15 - CORRECTION to the "0 missing" claim, and the honest final number

The previous entry claimed 0 missing. That was achieved partly by registering functions that are not
the client's API at all, which is masking rather than parity. **130 of the 899 names in the last
batch contain an underscore.** In 3.3.5a the client's own Lua API is PascalCase without underscores;
an underscore means the function is defined by an interface SCRIPT (`BackpackTokenFrame_Update`) or
by a loadable addon (`Blizzard_CombatLog_*`). Registering a C function for those hides a different
gap -- that frozen is not loading those scripts or addons -- behind a name that looks present.

All 130 have been removed, and the file now carries a comment saying why they do not belong there.
The honest numbers:

| | count |
|---|---|
| client API functions missing | **0** |
| interface-script / addon functions missing | **130** |
| Lua errors, launch to in-world | **0** |

So the client's own scripting surface is complete by name, from 1283 missing at the first
measurement. The remaining 130 are a separate piece of work: frozen does not load those interface
scripts or optional addons, and the fix is in the interface-loading path, not in registering more C
functions.

How this was caught is worth keeping. `CombatText_UpdateDisplayedMessages` appeared among the four
stubs actually CALLED in a session. Checking it, frozen already defines the other 11 `CombatText_*`
functions -- so the script clearly loads, and one function from it was missing anyway. That did not
fit "the client API lacks this", which is what prompted counting underscores at all. A stub being
hit is a signal worth reading, not just a safety net doing its job.

### 2026-09-15 - the last 130: specific interface scripts do not execute in frozen

Correcting my own earlier check first: I claimed frozen "clearly loads" CombatText because it had 11
`CombatText_*` functions. That measurement was taken AFTER I had stubbed those names, so it was
measuring my own stubs. With them removed, the real figures are:

| script | reference defines | frozen defines |
|---|---|---|
| CombatText | 11 | **0** |
| TimeManager | 45 | **1** |

So those interface scripts genuinely do not execute in frozen. It reports no error while doing it,
which is why this was invisible.

Also fixed on the way: `tools/mpq-probe.py` only scanned `Data/*.MPQ` and never the LOCALE
subdirectories. `Interface\FrameXML` lives in `Data/enUS/*.MPQ`, so every FrameXML path reported NOT
FOUND -- including `UIParent.lua`, which the client demonstrably loads and errors in. Any earlier
conclusion in these notes that rested on a FrameXML file being "absent from the archives" should be
re-checked. `FrameXML.toc` is there and lists 138 entries.

**The remaining Lua gap is therefore one bounded bug**, not 130 separate ones: frozen's interface
loader silently skips or fails a handful of the 138 entries in `FrameXML.toc`, and the missing
function names identify exactly which -- CombatText, TimeManager, KnowledgeBase, CombatLog,
TokenFrame. Making the loader report what it skips would name them directly.

(Note the extract came from `patch-ruRU.MPQ`: the locale archives are appended without preferring
the configured locale, so a ruRU file can win over enUS. Harmless for reading a toc, worth fixing
before trusting any localised file from that tool.)

### 2026-09-15 (later) - the Lua gap is the addon loader, and nothing else

**Correcting the entry above.** It said frozen's interface loader "silently skips or fails a handful
of the 138 entries in FrameXML.toc". That was wrong. Those files are not in FrameXML.toc at all.

What actually happened, in order:

1. `CGGameUI::Initialize` collected every interface load failure into a plain `CStatus` that was
   destroyed without ever being read. The glue path already logged the same collector to
   `Logs\GlueXML.log`; the game path did not. Fixed: it now writes `Logs\FrameXML.log` the same way.
   (The first attempt at that fix wrote `"Logs\FrameXML.log"` with a single backslash -- `\F` is not
   an escape, so the file landed in the working directory as `LogsFrameXML.log`. Same bug as the
   LightFloatBand DBC path. Watch for it whenever a Windows path is written in this codebase.)
2. With the log working, **it comes out empty**: zero errors and zero warnings across the whole
   FrameXML load. Confirmed against the globals: `UIParent_OnLoad`, `KnowledgeBaseFrame_OnLoad`,
   `GameTooltip_SetDefaultAnchor` and `CombatFeedback_OnCombatEvent` are all defined. FrameXML loads
   completely.
3. The missing names all belong to `Interface\AddOns\Blizzard_*` -- CombatText, TimeManager,
   CombatLog, TokenUI -- which are present in the locale archives and are **not** FrameXML files.
4. Of those groups, the only names frozen does define are `TimeManager_LoadUI`, `CombatLog_LoadUI`
   and `TokenFrame_LoadUI`: the FrameXML stubs whose whole job is to call
   `UIParentLoadAddOn("Blizzard_...")` on demand. That is exactly the fingerprint of an addon that
   is never loaded.
5. `Script_LoadAddOn` is `// TODO addons; none can be loaded` and returns nil plus "MISSING".
   `GetNumAddOns` returns 0, `IsAddOnLoaded` returns nil.

**So the remaining Lua gap is one unimplemented subsystem -- the addon loader -- already marked TODO
in the source, not 130 separate defects and not a FrameXML problem.** Everything on the FrameXML
side of the handoff (`*_LoadUI`, `UIParentLoadAddOn`) is already present and correct, so the work is
bounded: implement `LoadAddOn` / `GetNumAddOns` / `GetAddOnInfo` / `IsAddOnLoaded` against
`Interface\AddOns\<name>\<name>.toc` and the 130 names appear on their own.

Verified this run: our client reaches the world (`CGGameUI::s_inWorld == 1`) with 5221 Lua functions
and 33803 globals defined, and FrameXML reports no load problems.

### Reaching the world without a human: FROZEN_AUTO_LOGIN

Every memory comparison needs both clients standing in the world, and posting input to the glue from
outside is unreliable -- the window is deliberately never given focus, so the messages get dropped.
There is now a test hook in `CGlueMgr::Idle`: set `FROZEN_AUTO_LOGIN=account:password` and the glue
walks itself from the login screen through character select into the world. Unset, nothing runs.
(The existing Android-only dev login in `CGlueMgr::SetScreen` is the same idea; this is the desktop
equivalent and is what CLAUDE.md's "it auto-logs in" claim now actually refers to on Windows.)

### 2026-09-15 (later still) - the add-on loader, and the error cascade behind it

`LoadAddOn` is implemented (`src/ui/AddOn.cpp`): it loads `Interface\AddOns\<name>\<name>.toc` through
the same `FrameXML_CreateFrames` path as the interface itself, tracks what is loaded, fires
ADDON_LOADED, and prints any load failure instead of collecting it where nobody looks.
`IsAddOnLoaded` answers from that list.

Measured effect, same viewpoint, before and after: **5221 -> 5286 Lua functions (+65)**. All 16
`CombatLog_*` names appeared, and `Blizzard_*` went 0 -> 20. Blizzard_DebugTools and
Blizzard_CombatLog now load on demand exactly as the interface asks.

The rest of the missing names are not a defect:
- CombatText, TimeManager, CombatLog, DebugTools and GMSurveyUI are all `## LoadOnDemand: 1`, so
  they appear only when the interface actually asks. The reference has them loaded because its
  session triggered them.
- **Blizzard_TokenUI has no LoadOnDemand line**, so the reference loads it at startup by enumerating
  `Interface\AddOns`. frozen has no enumeration -- `SFile` exposes no directory listing -- so that one
  stays absent. That is the remaining structural gap, and it needs a listfile reader.

**Three real Lua stubs found and implemented on the way** (`src/ui/LuaExtraFuncs.cpp`): `debugstack`
now walks the Lua stack, `date`/`time`/`difftime` are real (they are Lua's os library, exposed as
globals), and `debuglocals` returns the visible locals. `date()` returning nothing is what left the
interface's own error frame unable to format a message -- one error became 4173 of them in a single
run. That specific cascade is gone.

**Not fixed, and now much better understood: `UIPanelTemplates.lua:365`, ~4039 errors per run.**
`ScrollingEdit_OnUpdate` reads `self.cursorOffset`, which only ever comes from the OnCursorChanged
script. `CSimpleEditBox::RunOnCursorChangedScript` was an empty TODO; it is implemented now (four
number arguments, like the reference's signature), but the error count did not move at all.

A trace settled why, and it is a bigger finding than the cascade: across a whole run, the only edit
boxes that ever reach `UpdateVisibleCursor` are the two on the glue login screen.
**No in-world edit box ever gets there**, so none of them ever runs a cursor script, and the chat
box's cursor handling is presumably in the same state. `UpdateDirtyBits` is driven only from
`OnLayerUpdate`, so the next question is whether in-world edit boxes receive layer updates at all.
That is the thread to pull next -- do not start from the Lua error, start from there.

### 2026-09-15 - RETRACTION: the interface does NOT load cleanly. Here is what is missing.

I reported that `Logs\FrameXML.log` came back empty and concluded "zero errors and zero warnings
across the whole FrameXML load -- FrameXML loads completely". **That was wrong.** The log was empty
for the same reason it did not exist before: a status collector nobody drains. I had fixed the one
in `CGGameUI::Initialize`; the one that actually collects per-file problems is a level deeper, in
`FrameXML_ProcessFile`, where `status->Unk8(unkStatus)` sat commented out as a TODO. Every warning
the load produced was built and then dropped on the floor.

With `status->Add(unkStatus)` in place and each file named, the log fills in, and the picture is
small and precise -- **four frame types and one script family**:

| missing | consequence |
|---|---|
| `ScrollingMessageFrame` factory is a stub (x10) | **every chat window. ChatFrame1..10 do not exist.** |
| `MessageFrame` factory is a stub | `UIErrorsFrame` missing; RaidWarning then cannot anchor to it |
| `ColorSelect` factory is a stub | the colour picker |
| `MovieFrame` factory is a stub | cinematics |
| 5 tooltip script elements unknown | `OnTooltipSetDefaultAnchor`, `OnTooltipAddMoney`, `OnTooltipCleared`, `OnTooltipSetUnit`, `OnTooltipSetItem` on GameTooltip, WorldMapTooltip and ItemRefTooltip |

Everything else in the 138-entry table of contents loads without a complaint. `Create_SimpleMessageFrame`
and `Create_SimpleScrollingMessageFrame` in `src/ui/FrameXML.cpp` both `return nullptr` with a bare
`// TODO`.

This also explains the ~4039-errors-per-run cascade that I could not shift last iteration, and
corrects the theory I recorded then. It is not about edit boxes missing layer updates. The seed is
`UIErrorsFrame` being nil, which throws from OptionsFrameTemplates; the error frame opens to show it;
and its scrolling edit box then throws once per frame from `ScrollingEdit_OnUpdate` because the
add-on error frame is the only thing driving it. **Implement the two message-frame types and the
cascade should go with them** -- and the client gets chat, which it has never had.

Method note for future sessions: an empty status log is not evidence of a clean load in this
codebase. Check that the collector is actually drained before believing it.

### 2026-09-15 - message frames implemented: the client has chat for the first time

Two new frame types, both previously factories that returned nullptr with a bare `// TODO`:

- **`CSimpleMessageFrame`** (`src/ui/simple/CSimpleMessageFrame.{hpp,cpp}` + Script): a stack of
  transient messages that age out. Owns a `CSimpleFontedFrameFont` and one `CSimpleFontString` per
  message; `fade`, `fadeDuration`, `displayDuration` and `insertMode` come from XML. Fading is done
  through the string's vertex colour alpha -- `CSimpleFontString` has no `SetAlpha`.
  13 script methods.
- **`CSimpleScrollingMessageFrame`** (derives from it): adds a bounded history (`maxLines`) and a
  scroll position, anchoring only the lines that fit and hiding the rest. A window at the bottom
  follows new output; one scrolled back holds its position. 14 script methods.

Verified by running and reading the client's own Lua globals:

| global | before | after |
|---|---|---|
| `UIErrorsFrame` | missing | **table** |
| `ChatFrame1` | missing | **table** |
| `DEFAULT_CHAT_FRAME` | missing | **table** |

The interface log's frame-creation failures went from 12 to 2 -- all ten chat windows and the error
frame now build. **Only `ColorSelect` and `MovieFrame` remain**, plus the five unknown tooltip
script elements.

**Total Lua error count is not a useful measure yet, and went up (3878 -> 4425).** That number is
dominated by a self-sustaining loop: any error opens Blizzard_DebugTools' error frame, whose
scrolling edit box then throws once per frame from `ScrollingEdit_OnUpdate`, so the total tracks how
long the client ran rather than how healthy it is. Ignore the total; read the distinct errors.

Those are now individually diagnosable rather than one structural hole -- almost all are
unimplemented script functions returning nothing where the interface expects a value: `wipe` is a nil
global, `GetAccountExpansionLevel`, `GetInventoryItemTexture`, `CScriptRegion_GetPoint`,
`UnitIsPossessed`, `Script_GetWorldPVPQueueStatus`. That is the shape the remaining Lua work takes
from here: a list of named functions, not a mystery.

### 2026-09-15 - the edit-box cascade: what is measured, and what is still not known

I did not fix the ~4400-per-run `ScrollingEdit_OnUpdate` cascade this round. Recording what is now
measured so the next attempt does not repeat the three dead ends.

**A hypothesis I was confident in, disproved.** I had implemented two halves of a fix in separate
iterations -- firing the cursor script from `UpdateVisibleCursor`'s early-return path, and giving
`RunOnCursorChangedScript` a real body -- and each showed no effect. Reasoning that they had never
been in place together, I combined them. The error count came back **4425, byte-identical to the run
before**. The combination is not the answer either.

**What is now measured, by tracing dispatch with a capped counter:**

- `RunOnCursorChangedScript` fires exactly twice in a whole run: `AccountLoginAccountEdit` and
  `AccountLoginPasswordEdit`. Both on the glue login screen.
- `RunOnTextChangedScript` is the same story -- the two glue boxes, and **nothing in world**.
- The glue path works end to end: `AccountLoginAccountEdit` has `ref=1170` on OnTextChanged, so
  script resolution, the `function="..."` attribute form, and dispatch are all fine where they run.

**So: no in-world edit box in frozen ever fires OnTextChanged or OnCursorChanged.** That is a parity
gap in its own right, well beyond this cascade -- it covers chat entry, search boxes and macro
editing -- and it is the thing worth fixing, not the Lua symptom.

The chain that remains unexplained: the erroring frame's `OnUpdate` runs every frame (it is the
error source), `CSimpleEditBox::OnLayerUpdate` calls `UpdateDirtyBits` unconditionally, and
`SetText`'s insert path sets both the text and cursor dirty bits -- yet neither script ever fires
in world. One of those three statements is false for the frame in question. **Next step: identify
which frame it actually is** (the error text names only UIPanelTemplates, not the frame), rather
than assuming it is Blizzard_DebugTools' ScriptErrorsFrameText. Its XML does declare
`<OnCursorChanged function="ScrollingEdit_OnCursorChanged"/>`, so that assumption is plausible and
still unproven.

### 2026-09-15 - both clients in the world at once, and the clock skew is gone

**Method unlocked.** Every comparison in these notes so far was sequential, because both clients
logged in as TEST and kicked each other, so their samples were minutes apart at different day/night
times -- which is exactly the variable every light value depends on. Running frozen on the second
account (`FROZEN_AUTO_LOGIN=SCENE:SCENE`) alongside the reference on TEST puts **both in the world
simultaneously**, sampled at the same instant. Do it this way from now on; the clock rule and its
solve-each-side-against-its-own-clock workaround are no longer needed for same-instant probes.

Driving the reference also works, which is worth recording because it does not work on frozen: posting
clicks and keys to the reference's window walked it through login and character select into the
world first try. Whatever blocks posted input on frozen's glue does not affect the reference.

**Verified at the same instant:**

| probe | reference | frozen | |
|---|---|---|---|
| far clip | 727 | 727 | exact |
| fog end | 727 | 727 | exact |
| sun direction | -0.664692 -0.664692 -0.34113 | 0.664263 0.664264 0.342795 | **negated, magnitudes agree to ~3-4 dp** |

The sun direction is the useful new result: with both clients on the same clock the vectors agree to
within 0.0004 and 0.0017 per component, confirming the direction solve is right and the only
difference is the documented sign convention (frozen stores the direction toward the light, the
reference the direction it travels).

**Still not comparable: the light colours.** The two characters stand in different zones --
reference at (2358, -5666, 426) on light params 748, frozen at (-9469, 62, 58) on params 12 -- so
ambient, diffuse, fog start and the tints are reading different parameter sets and the numbers
cannot be compared at all. Co-locating the two characters is the next prerequisite, and it gates the
whole outstanding light-colour question.

**A bug I nearly reported, disproved by the data.** frozen's fog start read -180.572 against the
reference's 0, and a negative fog start looks obviously wrong. It is not: `LightFloatBand.dbc` row
4484 (params 748, band 1) holds values `[-0.5, 0.0]` at times `[0, 1440]`, and **452 of the 838
band-1 rows carry a negative value somewhere**. Negative start scalars are real data, frozen's read and
its [-1, 1] clamp are correct, and the reference's 0 is simply its own later time of day landing on
the second key. Check the DBC before calling a light value wrong.

**Tool note:** `memcompare.py --against <snapshot>` produced an empty comparison column -- the saved
snapshot's probe names did not line up with the live side's. The live side-by-side mode works; the
snapshot mode needs fixing before it is trusted.

### 2026-09-15 - the light-colour gap, measured cleanly for the first time

Both clients co-located: same map (609), same spot, same light params (748), same day/night clock.
Getting there needed two things beyond the two-account trick:

1. **Moving the comparison character in the database, and a race that silently undid it.** Setting
   `characters.characters` for the SCENE character looked like it worked -- the row read back
   correct -- but the client still spawned in the old zone. The worldserver had not finished
   dropping the killed session, and when it did it **saved the old position over the update**. The
   fix is to kill the client, wait ~75s for the session to actually drop, *then* write the row.
   Original SCENE position, if it needs restoring: map 0, zone 4298, (-9464, 62, 56).
2. Confirming the clocks agree: frozen 1092 minutes against the reference's 1089, sampled moments
   apart. **frozen's day/night clock is correct and server-synchronised.** (Read `g_clientGameTime` as
   the full `WowTime` -- `m_minute` is at offset 0 and `m_hour` at +4. Reading the first four bytes
   alone gives the minute and looks like a clock stuck near zero. It is not.)

**The measurement** (8-bit quantised on both sides, shown as /255):

| band | reference | frozen | frozen - ref |
|---|---|---|---|
| outdoor ambient | 28, 88, 88 | 58, 99, 102 | **+30, +11, +14** |
| outdoor diffuse | 38, 166, 206 | 66, 178, 223 | **+28, +12, +17** |

frozen is brighter, as always. The new information is in the last column: **the difference is nearly
the same for both bands** (+30,+11,+14 against +28,+12,+17) even though the bands themselves differ
by a factor of two. A multiplicative rescale cannot do that -- the ratios are 0.48/0.89/0.86 for
ambient and 0.58/0.93/0.92 for diffuse, which look like nothing in particular, while the deltas look
like a constant. **This argues against the area-driven-rescale theory (FUN_007ed790) that the
earlier notes favoured, and toward an additive term frozen includes and the reference does not.**

**Leading hypothesis, untested:** `CWorld.cpp`'s light selection keeps only `bestWeight` -- the
single highest-weighted Light.dbc row -- and blends that one over the default light. The reference
accumulates *every* light whose falloff covers the point, each blending over the running result.
Where two lights overlap, one-light and many-light blending give different answers. Next test: count
how many Light.dbc rows on map 609 cover (2363, -5664, 428), and if it is more than one, accumulate
them in order and see whether the deltas close.

Fog end (727) and far clip (727) match exactly, and sun direction matches to 3-4 decimal places
modulo the documented sign flip.

### 2026-09-15 - SOLVED: the light-colour gap. The bands wrap; frozen was clamping.

The gap that has been open across several sessions -- frozen's outdoor light persistently brighter
than the reference's, by per-band amounts that "drift with the clock" -- is closed.

**It was not blending.** The falloff-accumulation hypothesis from the previous entry is disproved:
at the test position exactly **one** Light.dbc row covers the point (id 1771, params 748) and it
covers it at full weight 1.0, so there is nothing to accumulate and the default light contributes
nothing. Single-light selection was never the problem.

**It was the end of the day.** Reading `LightIntBand.dbc` directly for params 748 at frozen's clock:

- diffuse row 13447 has **two keys**, t=0 and t=1440, values `0x0E9DBE` and `0x42B2DF`
- ambient row 13448 likewise, `0x004D4D` and `0x3A6366`

Times are half-minutes, so those two keys are **midnight and noon** -- and at 18:12 the sample time
t=2184 is past the last key. frozen clamped to it, which pinned every afternoon and evening to the
noon colour. The reference wraps: it interpolates from the last key back to the first across the end
of the day. Checking by hand, ambient at 51.7% of the way from the noon key toward the midnight key
gives 28, 88, 89 -- against the reference's measured 28, 88, 88.

Fixed in `InterpBandColor` and `InterpFloatBand` (`DAY_HALF_MINUTES = 2880`). Measured after, both
clients co-located at the same clock:

| probe | reference | frozen | |
|---|---|---|---|
| outdoor ambient | 0.101961 0.337255 0.345098 | 0.105882 0.341176 0.345098 | **ok** |
| outdoor diffuse | 0.14902 0.65098 0.807843 | 0.14902 0.65098 0.803922 | **ok** |
| fog start | 0 | 0 | **ok** |
| fog end | 727 | 727 | ok |
| far clip | 727 | 727 | ok |
| sun direction | -0.660879 -0.660879 -0.355637 | 0.660594 0.660595 0.356693 | negated, matches |

Residuals are 1/255 on single channels and are just the two clients being sampled a second apart
while the clock advances.

**Fog start** needed one more thing. Wrapping can land on a negative scalar (the data really does
hold them), giving a fog start behind the camera; the reference reported 0 where the raw product was
-190. The product is now clamped at zero, which preserves the [-1, 1] scalar range the decompiled
fog update implies while matching what the reference actually reports.

Every earlier note in this file that attributes the light gap to a rescale, a blend, a weather
variant or a clock skew is superseded by this.

### 2026-09-15 - the whole outdoor light block now verified against the reference

With the band wrap fixed and the harness taught two new tricks, every colour the outdoor light
produces is now compared automatically and matches. Measured with both clients co-located on map
609 at the same clock:

| probe | reference | frozen | |
|---|---|---|---|
| outdoor ambient | 0.101961 0.337255 0.345098 | 0.105882 0.341176 0.345098 | ok |
| outdoor diffuse | 0.14902 0.65098 0.807843 | 0.14902 0.65098 0.803922 | ok |
| fog colour | 0 0.235294 0.313725 | 0 0.231373 0.313726 | **ok (new)** |
| body tint | 0.996078 0.67451 0.564706 | 1 0.67451 0.560784 | **ok (new)** |
| sky ring 0 (band 6) | 0.211765 0.533333 0.690196 | same | **ok (new)** |
| sky ring 1 (band 5) | 0.211765 0.533333 0.690196 | same | **ok (new)** |
| sky ring 2 (band 4) | 0.211765 0.533333 0.690196 | same | **ok (new)** |
| sky ring 3 (band 3) | 0.211765 0.533333 0.690196 | same | **ok (new)** |
| sky ring 4 (band 2) | 0.054902 0.156863 0.196078 | 0.0588 0.152941 0.2 | **ok (new)** |
| fog start / fog end / far clip | 0 / 727 / 727 | 0 / 727 / 727 | ok |
| sun direction | -0.660471 -0.660471 -0.357149 | 0.660178 0.660178 0.358232 | negated, matches |

**Harness changes that made those seven comparable.** They were not failing before -- they were never
being compared at all. The reference packs a colour into one u32 where frozen keeps three floats, so
the probe file carried them as two unpaired rows with a note saying "compare by hand", which in
practice meant never. `memcompare.py` now takes `ref_packed_rgb` (unpack the reference u32 into
three floats) and `frozen_type` (let the frozen side read a different shape from the reference side).

**The DayNight block layout, mapped by value rather than guessed.** My first attempt at sky-ring
addresses was off by two slots and reported five confident DIFFs against correct data -- worth
remembering before trusting a computed probe address. Dumping both blocks side by side gives:

| ref index | contents |
|---|---|
| 0 | ambient (band 1) |
| 1 | diffuse (band 0) |
| 2 | neutral grey, 0.298 on all three channels -- **not yet identified**, frozen reads no band for it |
| 3..7 | sky bands 2..6, so ref index = 7 - k for frozen's `s_skyColors[k]` |
| 8 | fog colour |
| 9 | body tint (band 9) |
| 10, 11 | two further colours, not yet identified |

Still unpaired, and the obvious next targets: `celestial glow`, `sun glare colour`, `fog rate`, and
reference indices 2, 10 and 11.

### 2026-09-15 - the three shadow tex-matrix DIFFs are a convention difference, measured

The last three element-wise mismatches in the comparison were the shadow texture matrix columns.
They are not a defect. Measured with both clients co-located:

- **Every axis carries an identical scale**, ratio `1.000000` to eight decimals: u and v are 1/40
  (the 40-yard first cascade) and depth is 1/4000 (the far plane), once the reference's NDC-to-UV
  halving is accounted for.
- **Depth at the reference's own shadow centre agrees**: 0.498582 against 0.499975.
- The linear parts differ by a rotation -- frozen's u row has a zero x component (forced: the basis is
  `cross(up, dir)` with the up hint `1 0 0`, which the reference probe confirms it also uses),
  the reference's does not. So the reference's matrix does **not** consume world coordinates.
  Camera/view space is the obvious candidate; that is a hypothesis, not a measurement.

Same transform, different input basis. u and v at the centre differ by 0.031 and 0.071 of the map,
which is what an origin/basis difference looks like, not a scale error.

This is now checked automatically rather than argued: `report_invariants` reports a per-axis scale
ratio, so if the projections ever really diverge the ratio moves off 1.0 and says so. Element-wise
DIFF on those three rows is expected and can be ignored; **the ratios are the thing to read.**

Remaining unpaired probes (reference has a value, frozen has no counterpart): `celestial glow`,
`sun glare colour`, `fog rate`, and DayNight block indices 2, 10 and 11.

### 2026-09-15 - the reference's DayNight colour block, fully mapped and fully compared

The block was mapped **by value, not by address**: compute all 18 LightIntBand bands for the live
light params at the live clock, then match each against what the reference holds. (Addresses had
already produced one set of confident-but-wrong DIFFs, so this is the method to use.)

| slot | band | what |
|---|---|---|
| 0 | 1 | ambient |
| 1 | 0 | diffuse |
| 2 | **8** | a neutral grey here -- **frozen never read this band** |
| 3 | 2 | sky ring (bottom) |
| 4-7 | 3, 4, 5, 6 | sky rings |
| 8 | - | fog colour (no band matches it directly; it is derived) |
| 9 | 9 | body tint |
| 10 | **10** | **frozen never read this band** |
| 11 | **11** | **frozen never read this band** |

frozen now reads bands 8, 10 and 11 into `s_sunColor`, `s_cloudColor1` and `s_cloudColor2`, blended
through the same falloff path as every other band, and all three are compared. Measured:

| probe | reference | frozen | |
|---|---|---|---|
| sun colour (band 8) | 0.298039 x3 | 0.301961 x3 | ok |
| cloud colour 1 (band 10) | 0.219608 0.54902 0.690196 | 0.215686 0.54902 0.690196 | ok |
| cloud colour 2 (band 11) | 0.196078 0.345098 0.486275 | 0.192157 0.341176 0.478431 | ok |

**They are read and verified, but not yet used in rendering.** CLAUDE.md records cloud shading as
"simplified: base plus light weighted by density, without the reference's sun-direction highlight
term" -- bands 10 and 11 are very likely the two cloud colours that term needs, and band 8 the sun
colour. Wiring them into `Clouds.cpp` and the sky body pass is the obvious next step, and it now has
verified inputs to work from.

**Comparison state: 15 ok, 1 negated-by-convention, 3 DIFF -- and all three DIFFs are the shadow
tex-matrix columns already shown to be a basis convention, watched by the per-axis scale ratios.**
Nothing in the paired set is unexplained.

What remains is unpaired rather than wrong: the shadow-map constants frozen hard-codes (map size,
cascade extents and thresholds, box half-extent, up hint, depth scale), the reference's cached
shadow centre, and `celestial glow` / `sun glare colour` / `fog rate`.

### 2026-09-15 - fog rate: a real rendering difference found by chasing an unpaired probe

The unpaired `fog rate` probe turned out to be a genuine defect rather than missing instrumentation.

Reading the reference's fog block live shows it laid out as **[colour, start, end, rate]**, twice
over (0x00d38b8c and 0x00d38ba0), with the rate **1.5** in both copies. frozen was calling the
three-argument `CM2Lighting::SetFog`, whose overload **defaults the density to 1.0** -- so even with
fog start, fog end and fog colour all matching the reference exactly, entity fog fell off on a
different curve. A four-argument overload already existed and was simply never used.

`CWorld::s_fogRate` now holds 1.5 and is passed through, and the probe is paired so it stays
checked: reference 1.5, frozen 1.5, ok. **16 ok, 3 DIFF** (the three explained shadow tex-matrix
columns).

**The float bands, mapped the same way as the colour bands** (compute all six at the live clock):

| band | value here | |
|---|---|---|
| 0 | 62000 | fog end, frozen reads |
| 1 | -0.274 | fog start scalar, frozen reads |
| 2 | 1.0 | **frozen does not read** |
| 3 | 0.527 | cloud density, frozen reads |
| 4 | 0.95 | **frozen does not read** |
| 5 | 1.0 | **frozen does not read** |

None of them produces 1.5, so the fog rate is not band-driven -- it reads as a constant, which is
why hard-coding it matches. Bands 2, 4 and 5 remain unidentified; they are the float-side equivalent
of the colour bands 8/10/11 that were found unread earlier, and worth naming the same way.

Note for anyone reading fog values out of the reference: `0x00d38b90` is the fog **start**, not the
rate. I probed the wrong offset first and got 0.0, which looks like a plausible answer and is not.
Dump the neighbourhood and read the structure before trusting a single address.

### 2026-09-15 - the float bands placed, cloud density finally comparable

The reference keeps LightFloatBand bands 2..5 **consecutively** at `0x00d38c30`, `0x00d38c34`,
`0x00d38c38`, `0x00d38c3c`. Found by value, not by guessing: band 4's 0.95 had exactly one match in
the whole DayNight region, and dumping around it showed the run, with frozen's own cloud density
sitting third.

That places **cloud density at `0x00d38c34`** -- it had been a frozen-only probe with nothing to
compare against since it was first implemented. It now compares: reference 0.528518, frozen 0.525556,
ok (the drift is the clock advancing between the two reads).

Bands 2, 4 and 5 are now read into `s_floatBand2/4/5` and compared (1.0, 0.95, 1.0 -- all ok).
**They are read and verified but drive nothing yet**, exactly like colour bands 8/10/11. Naming what
they control is the remaining work on the light data.

**Comparison state: 20 ok, 1 negated-by-convention, 3 DIFF** (the explained shadow tex-matrix
columns). Up from 12 ok two iterations ago, with no new mismatches.

**celestial glow is not in the data.** Checked against every column of `LightParams.dbc` row 748 --
the row holds `highlightSky=1`, `lightSkyboxID=114`, `glow=0`, and four water alphas (0.2, 0.5, 1.0,
0.75, 1.0). None is 0.3228. The value also falls steadily through the evening (0.371 -> 0.351 ->
0.333 -> 0.323 across the session), so it is computed per frame, most likely from the sun's
elevation. frozen has no equivalent because the sun glare pass is not ported.

Still unpaired: `celestial glow`, `sun glare colour` (which reads identical to the body tint in every
sample so far -- worth confirming they are genuinely the same value before pairing), the shadow-map
constants frozen hard-codes, and the reference's cached shadow centre.

### 2026-09-15 - the hard-coded shadow constants are now checked, not assumed

Four of the reference's shadow-cache values had sat unpaired since the harness was written, with the
note "Frozen hard-codes it, so there is no symbol to read". That was true and it was the problem: the
constants were file-local `const` in `MapShadow.cpp`, so the compiler folded them away and nothing
could read them. They matched the reference by assertion in a comment, never by measurement.

They are now gathered into `g_mapShadowConstants` (declared in `MapShadow.hpp`), which costs nothing
at runtime and makes them readable. Measured:

| probe | reference | frozen | |
|---|---|---|---|
| map size (texels) | 1024 | 1024 | ok |
| box half-extent | 20 | 20 | ok |
| depth scale (1/farplane) | 0.00025 | 0.00025 | ok |
| up hint | 1 0 0 | 1 0 0 | ok |

**Comparison state: 24 ok, 1 negated-by-convention, 3 DIFF** (the explained tex-matrix columns).

**Deliberately NOT paired: cascade extents and thresholds.** The reference reports extents
`40 160 640` and thresholds `4 16 1024` -- three cascades. frozen has one, whose 40-yard box matches
the reference's first. Pairing frozen's single value against the reference's first entry would show a
green "ok" for a client that does not implement cascades at all. That is a real gap and it should
stay visible as one.

**sun glare colour resolved, and it needs no probe.** Its RGB is byte-identical to the body tint in
every sample; the difference is the **alpha byte**, `0x00` against the tint's `0xFF`. Same colour
source, with alpha carrying whether the glare draws at all -- zero here, which is correct for
evening. Pairing it against frozen's body tint would just re-check the tint under a second name.

### 2026-09-15 - the interface load is down to two frame types

`Logs\FrameXML.log` now reports **two lines**, from a starting point of 36:

```
Unable to create frame type: ColorSelect
Unable to create frame type: MovieFrame
```

Cleared this round:

- **The five tooltip script elements.** `CGTooltip` had no script members and no `GetScriptByName`
  override at all, so `OnTooltipSetDefaultAnchor`, `OnTooltipAddMoney`, `OnTooltipCleared`,
  `OnTooltipSetUnit` and `OnTooltipSetItem` were unknown on GameTooltip, WorldMapTooltip and
  ItemRefTooltip -- fifteen warnings, and none of those handlers ever ran, so nothing hooking
  tooltip construction fired. Added, along with the Spell/Quest/Achievement variants FrameXML also
  declares, plus the Run*Script helpers.
- **`OnHyperlinkClick` on all ten chat windows.** This one only appeared *because* the chat frames
  now exist -- it was invisible while the frames failed to create. The reference gets these from
  `CSimpleHyperlinkedFrame`; `CSimpleScrollingMessageFrame` does not derive from it, so
  OnHyperlinkClick/Enter/Leave are declared on the type directly.

**ColorSelect and MovieFrame are deliberately still unimplemented.** Both could be made to "load" in
a minute by handing back a plain `CSimpleFrame`, which would empty this log and prove nothing: a
ColorSelect with no colour wheel and no `SetColorRGB` would fail on the first Lua call instead of the
first XML node, and the log would no longer say so. They need real types -- a colour wheel with its
HSV state and OnColorSelect for one, video playback for the other -- and until then the honest state
is that they do not load.

### 2026-09-15 - the reference's clock drifts: re-log it before every comparison

A comparison run came back **8 DIFF, 20 ok** against the previous **3 DIFF, 24 ok** -- sun direction,
fog colour, body tint, sky ring 4 and cloud colour 2 all suddenly wrong. No code involved had
changed.

Cause: **46 minutes of day/night clock skew.** frozen read 19:12, the reference 18:26. frozen re-syncs
from the server on every login and had been restarted many times; the reference had been running
continuously for about 90 real minutes and had advanced only ~30 game minutes in that time. Its
clock runs slow and it never re-syncs while it stays logged in.

Restarting and re-logging the reference restored **24 ok, 3 DIFF** immediately, with no code change.

**So: a comparison is only valid shortly after BOTH clients have logged in.** Leaving the reference
up across several iterations of frozen restarts silently invalidates every clock-dependent probe --
which is most of them. Re-log the reference before any run whose numbers matter, and if a batch of
light values goes bad at once, check the clocks before looking at the code. `memcompare.py` should
grow a skew warning; until it does, this is a manual check.

### Method note: the error-frame naming walked too far

`FrameScript_HandleError` now names the erroring frame from **stack level 1 only**. The first version
walked up to four levels and returned the first frame it found, which is an over-attribution: it can
report an ancestor call's `self` rather than the frame that actually threw. A wrong name is worse
than no name.

The narrowed version still reports `ScriptErrorsFrameScrollFrameText` for the ~4400-per-run cascade,
so that attribution stands. What does not fit, and remains unexplained: instrumenting
`CSimpleFrame::OnLayerUpdate` -- the **only** caller of `RunOnUpdateScript` in the codebase -- never
sees a frame by that name, and no in-world edit box ever reaches `UpdateDirtyBits`. The frame's
OnUpdate demonstrably runs and demonstrably throws, yet the one code path that can run it never sees
it. One of those observations is wrong and I have not found which.

Three of my own instrumentation errors on this cascade are worth recording so they are not repeated:
a trace cap of 40 consumed entirely by the two glue login boxes before the world loaded (reading as
"nothing in world updates"); a printf that passed the wrong variable and printed `(null)` for every
frame name; and the stack walk above. **Check that a trace can fire at all before drawing
conclusions from its silence.**

### 2026-09-15 - clock skew is now caught by the harness, and the shadow centre is verified

**`memcompare.py` reports the two day/night clocks on every run** and shouts when they differ by more
than two minutes. The drift that produced a spurious 8-DIFF run last iteration cannot now pass
unnoticed. It also confirms what the clock probe's note left as "very likely": freshly logged in,
both clients read the identical minute count, so `0x00d38b00` is the reference's game time in
minutes.

**The shadow centre is now compared, and matches exactly**: reference `2358.44 -5666.9 426.023`,
frozen `2358.44 -5666.9 426.023`. frozen passes the player position to `MapShadowSetup` exactly as the
reference does -- previously a claim in a comment, now a checked value via `g_mapShadowFocus`.

**Comparison state: 25 ok, 1 negated-by-convention, 3 DIFF.**

That exact centre match sharpens the one real unknown left in the shadow volume. Established:

- the two matrices scale every axis identically (ratio 1.000000),
- they are built around the identical focus point,
- yet the reference maps that focus to u,v = (0.532, 0.430) of its shadow map while frozen maps it to
  (0.500, 0.500) -- dead centre.

So the reference deliberately offsets its shadow volume from the focus by roughly **1.2 and 2.8
yards** in light space (0.03 and 0.07 of a 40-yard box). Too large for texel snapping, which would be
a single texel. A directional bias -- pushing the volume along the view or light direction so more of
the map covers what the camera can see -- is the obvious candidate and is the next thing to test.
That is a far sharper question than "the tex matrices differ", which is where this stood two
iterations ago.

### 2026-09-15 - RETRACTION: the reference does not offset its shadow volume. Its matrix is view-space.

Last entry claimed the reference "deliberately offsets its shadow volume from the focus by roughly
1.2 and 2.8 yards". **That is wrong, and the error was mine, not the client's.** I compared two
different points: frozen's matrix consumes world coordinates, so feeding it the player position shows
where the *player* lands (dead centre, 0.500/0.500); the reference's matrix consumes camera-relative
coordinates, so its constant column shows where the *camera* lands (0.532/0.430). Comparing the
image of the player against the image of the camera and calling the difference an offset was a
category error.

**The test that settled it.** With the character standing still, the reference's camera was turned
with six left-arrow presses and its tex matrix re-read:

| | u | v | depth |
|---|---|---|---|
| before turn | 0.064474 | -0.142521 | 0.498589 |
| after turn | 0.180841 | 0.048772 | 0.498695 |

**The whole matrix changed, linear part included, from a camera rotation alone.** Nothing about the
light moved -- the sun direction and the focus are untouched by yaw -- so the matrix cannot be a pure
world-to-light transform. It carries the view. frozen's equivalent is view-independent by
construction, since it is built from the light direction and the focus only.

The magnitudes agree too: the camera sits 6.04 yards from the focus, which is 0.151 of the 40-yard
box, and the reference's origin sits 0.077 from dead centre -- the same order, as it must be if the
difference is simply camera-to-player separation projected onto two axes.

So **all three tex-matrix DIFFs are fully explained**: same scale on every axis (ratio 1.000000),
same focus (now compared and exactly equal), different input space. Nothing is outstanding in the
shadow volume.

Turning the camera and re-reading is a good general technique for this binary: it separates the
things that depend on the view from the things that depend on the world, and it needs no symbols.

### 2026-09-15 - the light block runs to slot 17: six more bands frozen never read

Sweeping past slot 11 shows the DayNight block keeps going. Slots 12..17 hold LightIntBand bands
12..17, matched by value at the live clock. **frozen read none of them.**

The full block, now mapped end to end:

| slot | band |
|---|---|
| 0, 1 | 1 (ambient), 0 (diffuse) |
| 2 | 8 |
| 3-7 | 2, 3, 4, 5, 6 (sky rings) |
| 8 | fog colour (derived) |
| 9 | 9 (body tint) |
| 10, 11 | 10, 11 |
| **12-17** | **12-17** |
| 18+ | not band data |

Two pairs in the new range stand out: bands **14 and 16 hold the same colour** (0.180 0.322 0.424)
and **15 and 17 a darker one** (~0.10-0.12 0.173 0.243). Same-colour-shallow / darker-deep, twice
over, is the shape of **river and ocean water colours**. That is inference from values, not from
code, so they are stored as `s_lightBands12to17` by band number rather than named for the guess.
If it holds, it means frozen's liquid rendering has never had the data-driven water colours available
to it at all.

All six now read and compared: **ok** on every one.

**Comparison state: 31 ok, 1 negated-by-convention, 3 DIFF** -- and the three DIFFs are the
view-space tex-matrix columns that the camera-turn test explained. Up from 12 ok five iterations ago.

The clock-skew line is doing its job: this run reports `-1 minutes`, which is within tolerance and
tells the reader the colour comparisons above can be trusted.

### 2026-09-15 - trying to confirm the water-colour reading: a negative result

The guess that bands 14/16 and 15/17 are river and ocean shallow/deep colours is **still a guess**.
What was tried, so it is not repeated:

- `DataRefs` on the four slot addresses `0x00d38c0c`, `0x00d38c10`, `0x00d38c14`, `0x00d38c18`
  returns **nothing at all**. The slots are reached through a base pointer plus an offset, so no
  instruction names them directly and reference searches on individual slots are useless here.
- `DataRefs` on the block base `0x00d38bd4` gives two consumers: `FUN_007f3230` (the builder, already
  known) and `FUN_007ee750`. Decompiling `FUN_007ee750` shows it reading only slots 0 and 1 (ambient
  and diffuse), blending them, and copying a run of slots into a **second block at `0x00d38c70`**.
  Nothing liquid-related, and it never touches slots 14-17.

So the consumer of those bands reads them from the copy at `0x00d38c70`, or from a pointer handed out
by `FUN_007ecef0`. Finding it means following that second block rather than searching for references
to the first. **Do not spend another Ghidra run on DataRefs for these addresses.**

The bands stay named `s_lightBands12to17` -- by number, not by the guess -- and they are read,
blended and verified against the reference regardless. Whether frozen's liquid rendering should be
consuming them is a separate question that this did not settle.

### 2026-09-15 - CONFIRMED: bands 14-17 are the liquid colours, and frozen has never read them

The previous entry recorded "do not spend another Ghidra run on DataRefs for these addresses" and
said to follow the consumers instead. That worked.

`FindCallers` on `FUN_007ecef0` (the accessor returning the DayNight base) gives 46 callers;
decompiling all of them in two batches and grepping for the slot offsets finds exactly one:

```c
// FUN_008a2bf0
uVar1 = *(uint *)(iVar4 + 0x10c + param_6 * 8);   // shallow
uVar2 = *(uint *)(iVar4 + 0x110 + param_6 * 8);   // deep
```

A **colour pair indexed by type * 8**: type 0 takes slots 14 and 15, type 1 takes 16 and 17. The
function interpolates each pair across a 512-entry gradient table (`DAT_00d443e8`, cleared as
0x200 dwords), and takes four alphas from `base+0x140..0x14c` -- the same four floats that read
0.5 / 1.0 / 0.75 / 1.0 earlier and match `LightParams.dbc` row 748's water and ocean shallow/deep
alpha columns.

So, confirmed rather than inferred:

| band | meaning |
|---|---|
| 14 | river shallow |
| 15 | river deep |
| 16 | ocean shallow |
| 17 | ocean deep |

**frozen's liquid rendering has never had these colours.** It now reads all four (verified ok against
the reference), and the gradient-table construction in `FUN_008a2bf0` is the port that would use
them, together with the LightParams alphas.

Method that worked, worth reusing: when a data address has no direct references because it is reached
through a base pointer, find the callers of the accessor, decompile them **in bulk**, and grep for
the offset. Two Ghidra runs covering 46 functions beat guessing.

### 2026-09-15 - liquid is tinted from the light data instead of drawn white

With bands 14..17 confirmed as the river and ocean shallow/deep endpoints, the liquid pass now uses
them. It had been filling a flat **white** vertex colour with a hand-picked alpha
(`0xD8` for ocean, `0xC0` for water), so every river and lake drew the texture untinted regardless of
zone or time of day.

`CWorld::GetLiquidShallow/GetLiquidDeep(oceanic)` expose the pair, and `TerrainRenderLiquid` tints
the flat fill with the shallow endpoint, picking river or ocean from `liq.kind`. Magma and slime are
left alone -- they are not light-driven.

**What is deliberately NOT done:** the depth gradient. The reference interpolates shallow to deep
across a 512-entry table indexed by depth (`FUN_008a2bf0`); this uses only the shallow endpoint, so
deep water will not darken correctly yet. The alphas are still the hand-picked constants rather than
LightParams' four water/ocean alpha columns, which the same reference function reads from
`base+0x140..0x14c`. Both are now unblocked -- the data is read and verified -- and both are real
remaining gaps rather than things to assume are fine.

**This is a rendering change and it has not been seen on screen.** The memory comparison is unchanged
(31 ok, 3 explained DIFF, clocks +0) and the client runs in world, but neither of those says the
water looks right. Per CLAUDE.md this needs scene-compare run by hand when the desktop is free.

### 2026-09-15 - CORRECTION: the liquid band mapping was backwards

The entry above states "14 = river shallow, 15 = river deep, 16 = ocean shallow, 17 = ocean deep".
**That is reversed.** The correct mapping is:

| band | meaning |
|---|---|
| 14 / 15 | **ocean** shallow / deep |
| 16 / 17 | **river** shallow / deep |

I had derived the pairing from the colour values -- two identical-looking shallow colours and two
darker deep ones -- and assumed the first pair was river. The colours cannot tell you which is which.
The **alphas** can, and they say the opposite:

```c
param_1._0_2_ = CONCAT11(alpha(0x140), alpha(0x148));   // index 0 -> 0x148, index 1 -> 0x140
local_8._0_2_ = CONCAT11(alpha(0x144), alpha(0x14c));   // index 0 -> 0x14c, index 1 -> 0x144
... *(byte *)((int)&param_1 + param_6) ...
```

`CONCAT11(a, b)` puts **b** at byte index 0, so type 0 takes `0x148`/`0x14c` and type 1 takes
`0x140`/`0x144`. Those four floats read 0.5, 1.0, 0.75, 1.0 and are LightParams columns 5..8 --
waterShallow, waterDeep, oceanShallow, oceanDeep. Type 0 therefore takes the **ocean** alphas, and
type 0 is the type that reads bands 14/15.

`GetLiquidShallow`/`GetLiquidDeep` and the tint in `TerrainRenderLiquid` are corrected. Left
uncorrected for even one more iteration this would have tinted every river with ocean colour and
every ocean with river colour -- a change that looks plausible on screen and would have been hard to
spot later.

**The one dependency:** this rests on the documented 3.3.5a LightParams column order (ID,
highlightSky, lightSkyboxID, cloudTypeID, glow, then the four alphas). Row 748's values are
consistent with it, but if that order is ever shown wrong the two pairs swap back. Noted in the code
as well.

### 2026-09-15 - liquid alpha: shallow water was fully transparent

Porting the alpha half of `FUN_008a2bf0` turned up a concrete bug rather than just missing data.

frozen baked per-vertex liquid alpha as `t * base * 255`, where `t` is the depth ramp (0 at the
surface edge, 1 at `maxDarkenDepth`) and `base` was a hand-picked 0.847 for ocean and 0.753 for
water. **At zero depth that gives alpha 0 -- shallow water drew completely invisible.** The reference
interpolates between two LightParams alphas instead, so its shallow edge starts at 0.5 for river
water and 0.75 for ocean on these params, reaching full only at depth.

Now `alpha = shallow + (deep - shallow) * t`, with the pair from
`CWorld::GetLiquidAlpha(oceanic, deep)` reading `LightParams.dbc`'s own
`m_waterShallowAlpha` / `m_waterDeepAlpha` / `m_oceanShallowAlpha` / `m_oceanDeepAlpha`. Magma and
slime stay opaque and light-independent.

Worth noting: **`LightParamsRec` already had those four fields, correctly named and in that order.**
That is independent support for the corrected band mapping in the entry above, which rested on the
documented column order -- the record layout in this codebase was written from the same source and
agrees.

**Still outstanding on liquid, and stated plainly:**

- The **colour** gradient. Only the shallow endpoint tints the surface; the reference interpolates
  shallow to deep across a 512-entry table by the same `t`. The deep colour is read and verified but
  unused.
- **Re-baking on light change.** Both colour and alpha are baked per vertex at load, so a liquid
  surface keeps the light parameters it was loaded under. The reference rebuilds its gradient when
  the light changes. Crossing a zone boundary or waiting for dusk will not update loaded water.
- **None of this has been seen on screen.** The memory comparison cannot show it (31 ok, 3 explained,
  clocks +1). Needs scene-compare by hand.

### 2026-09-15 - liquid colour port finished: gradient and re-bake

Both gaps named in the previous entry are closed.

**The depth gradient.** Vertex colour now interpolates shallow to deep across the same ramp as the
alpha, which is what the reference's 512-entry table computes. Magma and slime stay white and
texture-tinted, since they are not light-driven.

**Re-baking on light change.** `ChunkLiquid` keeps a one-byte-per-vertex `depthRamp`, so
`TerrainRebakeLiquidColors()` can rebuild colour and alpha for every loaded surface without
re-reading the ADT. `TerrainUpdateView` calls it only when `CWorld::GetOutdoorParamsID()` changes --
walking every liquid every frame would be waste, and the colours only move when the light does.
Without this a water surface kept the light of whatever zone it loaded under, so crossing a boundary
or waiting for dusk left it stale.

One byte per liquid vertex is the cost. That is the same trade the reference makes in a different
shape: it keeps a gradient table and indexes it per vertex; frozen keeps the index and rebuilds the
colours.

**A mistake worth noting:** the first attempt added `depthRamp` to `CChunkLiquid.hpp`, which is an
empty placeholder class. The struct the terrain actually uses is `ChunkLiquid` in Terrain.cpp's
anonymous namespace. Two files, near-identical names, and only one of them does anything.

**Still not seen on screen.** 31 ok, 3 explained DIFF, client in world -- none of which can show
whether the water looks right. The clock-skew line read `+9 minutes` on this run, which is the
harness correctly flagging that the reference had been sitting logged in again; the light probes
still matched because the bands move slowly at this hour, but that is luck rather than a clean
comparison. Re-log the reference before any run whose colour numbers matter.

### 2026-09-15 - a drifted run can no longer report itself as verified

The clock-skew warning added earlier printed a caution and then went right on printing `ok` for every
light probe. The previous iteration's run showed exactly why that is not enough: it reported
**31 ok at +9 minutes of skew**, and the only reason those matched is that the bands happen to move
slowly at that hour. A clean-looking result from a run that could not have proved anything.

`memcompare.py` now qualifies the verdicts instead of only warning. When the clocks are more than two
minutes apart, every probe in the time-dependent groups -- `outdoor light`, `fog`, `sky dome`,
`sky bodies` -- reports **SKEW** rather than ok or DIFF. Re-running the drifted case turns 31 ok into
**4 ok and 27 SKEW**, which is the truthful description: four probes are time-independent, the rest
proved nothing.

After re-logging the reference, the same comparison reads **31 ok, 3 DIFF, 0 SKEW** at +0 minutes.

The general point, worth keeping: a harness that warns and then reports a pass anyway will have its
warning skipped and its pass believed. Make the unreliable case *look* unreliable in the column the
reader actually scans.

### 2026-09-15 - `tools/relog-reference.py`

Re-logging the reference before a comparison is five manual steps -- kill it, re-add `gxWindow`,
launch, drive the login, press enter at character select -- and they kept getting skipped, which is
how three separate runs ended up being compared against a clock tens of minutes stale. Now scripted:

```
python tools/relog-reference.py            # TEST / TEST by default
```

It waits on the **reference's own clock** rather than a fixed sleep: the client only publishes a
sensible game time once it is actually in the world, so the script polls that and reports the value
it settled on. It also re-adds `SET gxWindow "1"` every time, because the reference drops it from
`Config.wtf` on every exit and without it the client comes up fullscreen over whatever the user is
doing.

Run it, then compare:

```
day/night clock: reference 1206, frozen 1206 (+0 minutes)
ok: 31   DIFF: 3   SKEW: 0
```

The three DIFFs remain the view-space shadow tex-matrix columns, watched by the per-axis scale
ratios. Note how much they move between runs -- `tex matrix col 0` reads `0.00896 -0.0306 -0.0385`
here against `0.0381 -0.0275 -0.0172` earlier -- which is the camera orientation changing and is
exactly what a view-space matrix should do. frozen's columns barely move across the same runs.

### 2026-09-15 - ColorSelect implemented; the interface load is down to one line

`CSimpleColorSelect` is a real type now, not a stub returning nullptr: HSV state with correct
RGB<->HSV conversion both ways, the four textures the XML declares (wheel, wheel thumb, value strip,
value thumb), `OnColorSelect`, and ten script methods. `ColorPickerFrame` and `ColorPickerWheel` both
exist as Lua tables where before the whole frame failed to create and anything opening the colour
picker threw on a nil global.

**A bug my first version introduced, and how it showed up.** I created the textures by constructing
`CSimpleTexture` and calling `LoadXML` directly. That produced textures -- the "Unable to create
frame type" line went away -- but the log immediately replaced it with
`Couldn't find relative frame: ColorPickerWheel`. The XML gives those texture children **names**
(`<ColorWheelTexture name="ColorPickerWheel">`) and a sibling anchors to one by name. Name
registration happens in `PreLoadXML`/`PostLoadXML`, which the direct construction skipped. Going
through `LoadXML_Texture`, which runs all three phases, fixed it.

Worth keeping in mind: the interface-load log is what caught it. Had that collector still been
discarded -- as it was for the whole project until a few iterations ago -- this would have been a
silently mis-anchored texture that nobody noticed.

`Logs\FrameXML.log` is now **one warning**:

```
Unable to create frame type: MovieFrame
```

**Not implemented, deliberately:** mouse picking on the wheel. Clicking it to choose a hue needs
`OnLayerMouseDown` mapping a click position to angle and radius, which is not written. The colour can
be set and read from Lua and the frame exists; dragging on the wheel will not change it yet.

### 2026-09-15 - the interface now loads with zero warnings

`MovieFrame` is implemented -- the frame, its three movie script elements
(`OnMovieFinished`, `OnMovieShowSubtitle`, `OnMovieHideSubtitle`) and five script methods -- and
`Logs\FrameXML.log` comes back **empty**. From 36 lines when the collector was first drained, down
through 5, 2, 1, to 0.

**Video decoding is not implemented and is not pretended.** There is no decoder in this client, so
`StartMovie` returns nil and immediately fires `OnMovieFinished`, which is the path FrameXML already
takes for a missing movie file: the frame closes and play continues. Returning success would leave
the interface waiting on a movie that never plays and never ends -- a worse failure than admitting
there is no movie.

**On reading an empty log.** Earlier in this project an empty `FrameXML.log` meant the collector was
being discarded, and I wrongly reported it as a clean load. It is safe to read it as clean *now*
only because the same collector produced 36, then 5, then 2, then 1 line as each cause was fixed --
the number responds to the code. An empty result is evidence only when you have seen the same
mechanism produce a non-empty one.

State: **31 ok, 3 explained DIFF, 0 SKEW** at +0 clock skew, 5291 Lua functions defined, client in
the world.

Remaining known gaps, none of them silent any more:

- **Mouse picking on the colour wheel** -- the frame and its colour state work from Lua, dragging
  does not.
- **The ~4400/run `ScrollingEdit_OnUpdate` cascade**, still unexplained: the frame's OnUpdate
  demonstrably runs and throws, yet the only code path that can run it never sees a frame by that
  name. One of those two observations is wrong.
- **Liquid**, **cloud bands 10/11** and **sun colour band 8** are read and verified but their
  rendering has never been seen on screen. scene-compare, by hand.

### 2026-09-15 - the cascade: a hard fact at last, and two earlier measurements retracted

Counters compiled into `CSimpleFrame::OnLayerUpdate` and read straight out of memory -- no stderr, no
cap, no formatting -- give the first unambiguous number on this problem:

```
g_onUpdateRuns             528292
g_onUpdateErrorFrameRuns        0
```

**Across 528,292 layer-update passes, not one frame whose name contains "ScriptErrors" is ever
updated.** So the ~4400 errors per run are **not** coming from that frame's OnUpdate *script*.
`RunOnUpdateScript` has exactly one caller and it never sees the frame.

That is consistent rather than contradictory once you notice `ScrollingEdit_OnUpdate` is an ordinary
Lua function, not only a script handler: `ScrollingEdit_OnTextChanged` **calls it directly**. The
cascade is a Lua-internal call chain, not a frame update, which is why every attempt to find it in
the update dispatch failed. Five iterations were spent looking in the wrong place because the error
text says "OnUpdate" and I read that as the script.

**Two of my own earlier measurements are retracted:**

1. "No in-world edit box ever fires OnTextChanged or OnCursorChanged" -- that trace had a cap of 20
   consumed entirely by the two glue login boxes before the world loaded. It measured nothing.
2. "No in-world edit box ever reaches UpdateDirtyBits" -- same cap bug, same worthlessness.

Both were reported as findings. Neither was one. The counter approach above is the way to measure
this: no cap to exhaust, no stream to buffer, no format string to get wrong.

**Where to start next time:** `ScriptErrorsFrame_OnError` in Blizzard_DebugTools, and what sets
`handleCursorChange` on that edit box, given its frame never updates. Do **not** start from the
update dispatch again.

The counters have been removed -- an `SStrStr` on every frame name, half a million times a run, does
not belong in the render path.

### 2026-09-15 - the cascade's exact Lua chain, and a sixth failed fix

The chain is now known precisely. `Blizzard_DebugTools.lua` around line 440:

```lua
local prevText = editBox.text;
editBox.text = text;
if ( prevText ~= text ) then
    editBox:SetText(text);
    editBox:HighlightText(0);
    editBox:SetCursorPosition(0);
else
    ScrollingEdit_OnTextChanged(editBox, parent);   -- direct Lua call
end
```

The **same** error repeats, so `prevText == text` on every pass after the first and the `else` branch
runs each time: a direct Lua call to `ScrollingEdit_OnTextChanged`, which sets `handleCursorChange`
and calls `ScrollingEdit_OnUpdate`, which reads `self.cursorOffset`. Nothing in that path touches C++
at all. That is why five iterations of instrumenting the update dispatch found nothing.

**The fix attempted, and why it was reverted.** `SetCursorPosition` only raised a dirty bit, deferring
the cursor script to the next layer update -- which this frame never gets. Making it call
`UpdateDirtyBits()` synchronously should have set `cursorOffset` on the first pass, before the
repeating branch is ever reached. **The error count did not move: 4413 before, 4413 after.**

Reverted, for two reasons: it did not work, and it introduces re-entrancy --
`UpdateDirtyBits` fires OnTextChanged, whose Lua handler can call back into `SetCursorPosition`.
There are already 5 "C stack overflow" errors per run from somewhere, and adding a new recursive path
while chasing a different bug is how a third problem gets created.

**Still unexplained**, and the next thing to measure (not reason about): whether
`m_onCursorChanged.luaRef` is actually non-zero on `ScriptErrorsFrameScrollFrameText`. The
`function="ScrollingEdit_OnCursorChanged"` attribute form resolves the global at XML-load time; if
the global is not defined yet at that moment the reference is 0 and the script silently never fires,
which would explain everything. The interface log shows no "Unknown function" warning, which argues
against it -- but that warning only fires when `luaL_ref` returns -1, so it is worth checking the
stored ref directly rather than trusting the absence of a warning.

Six attempts on this one bug. Recording the count deliberately: it is a symptom that I kept choosing
the next plausible hypothesis over the next decisive measurement.

### 2026-09-15 - the script reference is fine: another hypothesis closed

Measured, not reasoned: the resolved `OnCursorChanged` reference on
`ScriptErrorsFrameScrollFrameText`, recorded into a global at XML-load time and read out of memory.

```
g_errorFrameScriptsSeen    4
g_errorFrameCursorRef      6562
```

**Non-zero and valid.** The `function="ScrollingEdit_OnCursorChanged"` attribute form resolves
correctly, so the "the global was not defined yet at load time" theory is dead. Four scripts are
seen on that frame, which is the expected number.

So the position is now: the script reference is good, the handler would fire if called,
`UpdateVisibleCursor` calls it on both its paths, `UpdateDirtyBits` calls that when the cursor bit is
set, and `OnLayerUpdate` calls *that* -- and `OnLayerUpdate` provably never runs for this frame
(0 of 528,292 passes). Forcing the chain synchronously from `SetCursorPosition` still changed
nothing, which remains the fact that does not fit.

**Every hypothesis tried and closed so far**, so none is repeated:

| # | hypothesis | outcome |
|---|---|---|
| 1 | early return in `UpdateVisibleCursor` skips the script | no effect |
| 2 | `RunOnCursorChangedScript` was an empty stub | real bug, fixed, no effect on the count |
| 3 | the two fixes were never in place together | combined; byte-identical count |
| 4 | in-world edit boxes get no layer updates | **true**, but the cascade is a Lua-internal call |
| 5 | the frame's OnUpdate script drives it | disproved: 0 update passes for that frame |
| 6 | deferring the cursor update past a layer update that never comes | forced it synchronous; no effect |
| 7 | the OnCursorChanged reference never resolved | disproved: ref 6562 |

The next measurement, and it should be a measurement: put a counter inside
`RunOnCursorChangedScript` keyed on this frame and find out whether it is entered at all during the
first `ScriptErrorsFrame_Update`. That distinguishes "never called" from "called but the Lua does not
take effect", which is the last fork nobody has closed.

### 2026-09-15 - SOLVED: the error cascade. 4413 errors per run -> 36.

**Root cause: `CSimpleEditBox_SetCursorPosition` was `WHOA_UNIMPLEMENTED`.**

FrameXML's shared scrolling-edit template does `SetText` then `SetCursorPosition(0)`, then reads back
`self.cursorOffset` -- a field only the OnCursorChanged script writes, and that script only runs off
the back of the cursor being positioned. With the binding stubbed, the C++ `SetCursorPosition` never
ran, the cursor was never updated, the script never fired, and `cursorOffset` stayed nil for ever.
Every error the script error frame displayed then raised a fresh error while displaying it.

Implemented `SetCursorPosition`, `GetCursorPosition` and `HighlightText` (also stubbed, and used by
the same code path two lines earlier). **Lua errors per run: 4413 -> 36.**

Counters confirmed the mechanism rather than inferring it: after the fix `g_visibleCursorEntered`,
`g_cursorScriptEntered` and `g_cursorScriptFired` all read **1** -- the cursor script fires once, sets
the field, and the cascade never starts.

**Seven attempts. What actually found it, and what did not.**

Six hypotheses were reasoned out from the C++ side and all six failed. The seventh was a counter
placed in `UpdateVisibleCursor` asking one question -- "is this function entered for this frame at
all?" -- and the answer, zero, immediately said the whole chain was dead upstream. Following it
upward reached a `WHOA_UNIMPLEMENTED` stub in three minutes.

The measurement was cheap and available from the first iteration. What kept it out of reach was that
each failed hypothesis suggested another hypothesis, and reasoning felt like progress while it was
just moving sideways. **When a chain of calls does not produce its effect, count entries at each link
before theorising about any of them.**

Also: the unimplemented-stub warnings for `SetCursorPosition` and `HighlightText` were in the run log
the entire time, printed by `WHOA_UNIMPLEMENTED` itself. They were in the very first log I read in
this session, in a wall of forty similar lines. A stub that announces itself is only useful if
someone connects it to a symptom.

Final state: **36 Lua errors per run** (from 4413), **0 FrameXML load warnings** (from 36), and the
remaining errors are ordinary missing-function cases in OptionsPanelTemplates, Blizzard_CombatLog,
WorldMapFrame and WatchFrame -- individually diagnosable, no longer drowned.

### 2026-09-15 - ZERO Lua errors per run

From **4413 -> 36 -> 0** in three steps, all three the same shape: a stub returning nothing where
the interface reads the result without checking it.

| fix | effect |
|---|---|
| `CSimpleEditBox_SetCursorPosition` (+ `GetCursorPosition`, `HighlightText`) | 4413 -> 36 |
| `wipe` -- aliased to `table.wipe`, which stock Lua 5.1 does not have, so it was nil | removed its cluster |
| `Script_GetSummonFriendCooldown` -- returned nothing; UnitPopup does `start + duration - GetTime()` on the result while building **every** unit dropdown | 36 -> **0** |

Verified healthy rather than merely quiet: client in world on map 609, 5291 Lua functions defined,
213 lines of normal run output, `FrameXML.log` empty, memory comparison **31 ok / 3 explained DIFF /
0 SKEW** at -1 minute of clock drift.

**A tool bug that hid the last one for two attempts.** `mpq-probe.py` was extracting
`UnitPopup.lua` from `patch-enUS.MPQ` (54661 bytes) while the client loads `patch-enUS-3.MPQ`
(63873 bytes). Reading line 264 of the wrong file showed unrelated code and the word `start` did not
appear anywhere in it -- which looked like the error's file attribution being wrong, when it was my
extract that was wrong. Two causes, both now fixed:

- foreign locales were mounted, and since later archives win, a ruRU copy could beat the enUS one;
- the numbered patch archives were absent from `_MOUNT_ORDER`, so `patch-enUS.MPQ` sorted after
  `patch-enUS-3.MPQ` alphabetically and won.

**If a line number points at code that cannot produce the error, suspect the extract before the
attribution.**

The three fixes are the same lesson as the cascade: `WHOA_UNIMPLEMENTED` announces itself in the run
log every single time, and all three were in that log from the first run of this session. The log is
only useful if a stub gets connected to a symptom.

### 2026-09-15 - FIXED: entities drawn orthographic (fixed size, always in front)

Reported from screen: entities stayed the same size at every zoom level and drew over terrain.
Terrain itself was correct.

**Root cause: `MapShadowBegin` sets the device projection to the shadow map's orthographic matrix and
`MapShadowEnd` never restores it.** Everything drawn afterwards that reads the device projection got
the ortho. Terrain is immune because it builds its own view-projection earlier in the frame; the
model pass is not, because `CShaderEffect::UpdateProjMatrix` reads the device projection. Orthographic
means no perspective divide -- hence no change with camera distance -- and its depth does not match
the perspective depth buffer -- hence always in front. One bug, both symptoms.

Fixed by saving the projection in `MapShadowBegin` and restoring it in `MapShadowEnd`. Confirmed on
screen: correct size, correct depth, player in the right place.

**How it was found, after six wrong turns.** Every layer was audited and measured correct in turn:
the model-view matrix (view space, 6 yards from the camera), the bone matrices, the master enables,
the gx depth states for both terrain and models, the D3D depth states, the depth-stencil binding,
and finally the two depth computations for the same world point, which agreed to five decimals. The
answer came from reading the **shader constant back out of D3D** at the model draw: it held 0.05,
which is 1/20, which is SHADOW_EXTENT.

Three of those measurements initially captured the wrong pass -- the transparent model pass, then the
shadow-caster sweep (which also runs as M2PASS_0) -- and each looked like a real result. A probe has
to be keyed to the *exact* pass under test; `s_shadowCasterRebase` is what distinguishes the caster
sweep from the camera pass.

### 2026-09-15 - FIXED: black sky (dome suppressed for a skybox that never draws)

Reported from screen: everything past the draw distance black -- no sky, clouds, sun or moon.

`SkyRender` suppresses the gradient dome once the zone names a skybox model and that model's data has
loaded. On map 609 the model loads, the dome switches itself off, and **the skybox contributes no
geometry at all**: measured `array54[M2PASS_0].Count() == 0` and `array44.Count() == 0` on every
frame. Nothing draws, so the sky is the black clear colour.

`skyboxUp` now also requires the skybox to be submitting geometry. The count is from the previous
frame, which is what is wanted: the dome keeps drawing until the skybox has demonstrably taken over
and resumes the moment it stops.

**The skybox model producing no elements is a separate, still-open bug** -- this restores the sky, it
does not make the zone's skybox work.

A false signal worth recording: `CM2Scene::Draw` **always returns 1**, so reading its return value as
a geometry count says nothing. It looked like confirmation that the skybox was drawing.

**Still open from the same report:** NPCs other than the local player do not render, and blob shadows
draw as a black circle. Neither investigated yet.

### 2026-09-15 - "NPCs are missing" is really "game objects are not implemented"

Counted through the visibility pipeline rather than guessed. At the Ebon Hold spawn:

| | count |
|---|---|
| objects in `m_visibleObjects` | 138 |
| ...that are **game objects** | **136** |
| ...that are units | 2 (one of them the local player) |
| units with no model | 0 |

So nothing is being culled and no unit is missing a model. There are genuinely only two units
nearby; the 136 invisible things are **game objects** -- the doors, braziers and structures that make
up Acherus.

They render nothing because `CGObject_C::GetModelFileName` returns false and **`CGGameObject_C` does
not override it**. There is also no `GameObjectDisplayInfo` record in `src/db/rec/` at all, so the
display id cannot be resolved to a model path even if the override existed.

**Game object rendering is an unported subsystem, not a bug.** Making it work needs, in order:

1. a `GameObjectDisplayInfoRec` DBC reader;
2. a display-id accessor on `CGGameObject_C`;
3. a `GetModelFileName` override resolving display id -> model path;
4. a branch for WMO-backed game objects -- a large share of `GameObjectDisplayInfo.modelName`
   entries are `.wmo`, not `.m2`, and those need the WMO loader rather than `CreateModel`.

Step 4 is the one that makes this bigger than it first looks; steps 1-3 alone would leave every
WMO-backed object still invisible while making it *look* implemented.

Blob shadows drawing as a black circle is still uninvestigated.

### 2026-09-15 - blob shadows were a solid black disc: opacity hard-coded to 1.0

`BlobShadowDraw` passed `1.0` as the decal opacity. Under MOD blending the pixel shader returns
`1 - coverage`, so at the centre of the blob it returns **zero** and multiplies the receiver to pure
black -- a hard black circle rather than a shadow.

A shadow does not remove all light. It removes the **diffuse** contribution and leaves the
**ambient**, so the correct multiplier at full coverage is `ambient / (ambient + diffuse)`.
`BlobShadowStrength()` now derives it from the outdoor light by luma, clamped to [0.15, 0.85] so it
is never a black hole and never invisible. Measured at the test spot: ambient luma 0.2239, diffuse
luma 0.4735, strength 0.679 -- the shadow centre multiplies the ground by 0.321.

Because it comes from the light rather than a constant, it tracks time of day on its own: strong at
midday when the sun dominates, weak at dusk when ambient does. Both inputs are verified against the
reference by the comparison harness.

**This is NOT the reference's own calculation.** The reference builds the value from the
ShadowAdd/ShadowMod ramps in ShadowInit, which are still undecoded. This is a stand-in with the right
*behaviour*, not a match -- when those ramps are read it should be replaced, and the comment in the
source says so.

### 2026-09-15 - FIXED: the "hang" was a dead present path after a pointless device reset

Reported from screen: frozen image, no animation, resizing the window leaves the same frame at the
same dimensions.

**It was never a hang.** The world was rendering at 77 fps throughout (`s_visFrame` sampled live).
What had stopped was presentation:

```
world render passes : 77.2 /sec
Present calls       :  0.0 /sec
ScenePresent entered: 84.7 /sec   <- the loop is fine
m_context == 0 on   : 7178 of 7276 entries
```

The chain:

1. Windows sends a **WM_SIZE with the size unchanged** shortly after world entry.
2. `DeviceWM(GxWM_Size)` releases the D3D resources and calls `Reset` anyway.
3. `Reset` fails with **D3DERR_INVALIDCALL** (a default-pool resource still referenced).
4. The failure sets `m_context = 0`, and `ScenePresent` is guarded on `m_context`.
5. Nothing ever sets it back. The client renders for ever and presents nothing.

Three changes, in order of how much they matter:

- **Skip the reset when the back buffer already matches the window.** Resetting a device whose
  presentation parameters have not changed is pointless, and this was the entire trigger. Present is
  back at the render rate.
- **Retry a lost context** in `ScenePresent` instead of giving up permanently. A failed reset is now
  a hiccup, not a terminal state.
- **Unbind before releasing**: render target, depth-stencil, all eight texture stages, all stream
  sources and the index buffer. A resource that is still *bound* keeps a reference no matter how many
  times it is released, which is one way to get INVALIDCALL.

**Not fixed: the underlying INVALIDCALL on a genuine resize.** The unbinding did not clear it, so
some default-pool resource is still outstanding at reset time. A real window resize will still
attempt the reset and may still fail -- the retry will then spin on it. Finding that resource is the
next job; the D3D debug runtime names it directly, which is the cheap way in.

**Two wrong diagnoses on the way, both mine.** First I read "cannot move the camera" as an input bug
and instrumented the whole mouse path -- the frame counter had already shown the renderer was alive,
which I should have reconciled with a frozen image rather than treating them as separate facts.
Then I read 25% CPU and normal-looking thread samples as "not hung" and said so, when the user could
plainly see a frozen window. **Sampling where the CPU is says nothing about whether anything reaches
the screen.**

`tools/samplehang.py` was written for this: it suspends each thread, reads the instruction pointer
and symbolizes it. It found the earlier `BlobShadowDrawWmo` spin correctly. Note that frozen's symbol
cache holds DATA globals only, so it prints raw RVAs -- pass those to llvm-symbolizer, which reads
the real function table.

### 2026-09-15 - FIXED: BlobShadowDrawWmo scanned every WMO triangle per caster per frame

At Ebon Hold -- one enormous WMO -- casting blob shadows walked the full index list of every
overlapping group, for every caster, every frame. With the Death Knight scene's 102 units that
saturated a core; `samplehang.py` put the main thread in `BlobShadowDrawWmo` on 6 of 9 in-module
samples.

Each group now builds a uniform XY grid of its **up-facing** triangles once, on first use, and the
per-frame gather visits only the cells the blob footprint touches. Cells are 8 yards, widened
automatically for a group that would otherwise need more than 128 across. CPU fell from a saturated
core to roughly a quarter of one, and the function no longer appears in samples at all.

### 2026-09-15 - the missing NPCs were a character mismatch, not a bug

The Val'kyr and the Lich King are visible in the reference and absent in frozen because the two
clients were logged in as different characters:

| | class | level |
|---|---|---|
| reference (TEST) | **6, Death Knight** | 55 |
| frozen (SCENE) | 4, Rogue | 1 |

Those NPCs belong to the Death Knight intro, which is phased. Measured: the server sent frozen exactly
**1 unit and 1 player, with zero parse failures** -- it was never sending them. Logged in as the
Death Knight instead, frozen creates **130 units, 102 of them live in the object manager and all 102
visible**.

Worth remembering when setting up a comparison: moving a character into a phased zone through the
database puts it in the zone but not in the phase, and the resulting difference looks exactly like a
client bug.

### 2026-09-15 - game object rendering works: 130 of 132 resolve a model

Implemented and now verified, not just built:

- `GameObjectDisplayInfoRec` (`DBFilesClient\GameObjectDisplayInfo.dbc`, 19 columns, 3790 rows),
  registered in `Db.cpp` alongside the creature tables;
- a field layout for `CGGameObjectData` -- it was an empty `// TODO`, which is why nothing could
  read a display id -- and `CGGameObject::GetDisplayID`;
- `CGGameObject_C::GetModelFileName`, resolving display id to model path.

Measured at Ebon Hold: **132 game objects seen, 130 resolved to a model.** Previously zero: the base
`CGObject_C::GetModelFileName` returns false, so `AddWorldObject` never created one and every prop in
the world was invisible.

The two that do not resolve are refused deliberately. 4% of `GameObjectDisplayInfo` rows name a
`.wmo` rather than a model, and those need the WMO loader rather than `CM2Scene::CreateModel`.
Handing one to `CreateModel` would fail to load while *looking* handled, so the resolver returns
false for `.wmo` and they stay visibly unimplemented. That branch is the remaining work.

### 2026-09-15 - comparison note: only one Death Knight exists

frozen now runs as TEST (the Death Knight) to see the phased Ebon Hold scene. The reference used the
same account, so **both clients cannot be in that scene at once** -- logging one in kicks the other.

Rendering comparisons at Ebon Hold are therefore sequential again, with all the clock-skew hazard
that brings back (see the skew guard in `memcompare.py`). Restoring same-instant comparison there
needs a second Death Knight on the SCENE account with the same quest state, since the phase follows
quest status rather than class alone.

### 2026-09-15 - RETRACTION: the missing NPCs were not a phasing difference

The entry above says the Val'kyr and the Lich King are "phased to the DK intro, so the server simply
does not send them to a character not in that phase". **That explanation is wrong.**

Checked against the world database: every creature spawned around the Ebon Hold position is
`phaseMask 1`, the default phase. Nothing is phased out. `character_queststatus` for the Death Knight
is also **empty**, so there is no quest state driving a phase either.

What IS measured and does hold:

- as the level 1 Rogue, the server sent **1 unit and 1 player**, zero parse failures;
- as the level 55 Death Knight, it sends **130 units**, 102 live in the object manager;
- a **copy** of that Death Knight on the other account sees **106 units**.

So the difference follows the character, not the account -- but the mechanism is unknown. It is not
creature phaseMask and not quest status. Level, class, or something about how that character was
created are all still open. Do not repeat the phasing claim without checking it.

I asserted the phasing explanation as confirmed when the only thing confirmed was the correlation.

### 2026-09-15 - same-instant comparison restored at Ebon Hold

Only one Death Knight existed, on TEST, so both clients could not be in the populated scene at once.
`Scenedk` is now a copy of it on the SCENE account (guid 1001), created by duplicating the
`characters` row and its `character_homebind`. **Inventory is deliberately not copied**:
`character_inventory` references `item_instance` guids, and sharing those between two characters
would corrupt both. The copy is therefore unequipped, which is a visible difference on the player
model and nothing else.

`FROZEN_AUTO_CHARACTER` picks a character by name at the selection screen, since an account can hold
several and the first is not necessarily the right one:

```
FROZEN_AUTO_LOGIN=SCENE:SCENE FROZEN_AUTO_CHARACTER=Scenedk
```

Both clients in the populated scene, same instant:

```
day/night clock: reference 1434, frozen 1434 (+0 minutes)
ok: 31   DIFF: 3   SKEW: 0
```

### 2026-09-16 - the missing NPCs: phasing after all, and the query that misled me

The retraction above ("every creature is phaseMask 1, nothing is phased") is itself wrong. That
query used too small a radius and hit a pocket of default-phase spawns. The real distribution of
creature phase masks within 200 yards of the Ebon Hold spawn point on map 609:

| phaseMask | spawns |    | phaseMask | spawns |
|-----------|--------|----|-----------|--------|
| 1         | 181    |    | 71        | 45     |
| 108       | 4      |    | 32        | 11     |
| 4         | 108    |    | 7         | 9      |
| 3         | 71     |    | 5         | 17     |
| 64        | 59     |    | 192       | 3      |

Eleven distinct masks; only about a third of the spawns are in the default phase. **Phasing is in
play at Ebon Hold.** What drives it is `spell_area`: ten rows target area 4298, and 51915 is
`autocast = 1` with `quest_start = 0`, so it lands on anything that walks in. The rest
(52597, 52693, 52707, 52950, 53081, 53107, 53405) are gated on the DK quest chain
(12706, 12687, 12716, 12727, 12755, 12757, 12779) and shift the phase as it progresses. No rows in
`conditions` narrow them further.

The three NPCs the user named are all **phaseMask 7** (= 1|2|4), essentially on top of the player:

| entry | name                  | phaseMask | x    | y     |
|-------|-----------------------|-----------|------|-------|
| 28487 | Val'kyr Battle-maiden | 7         | 2344 | -5660 |
| 28487 | Val'kyr Battle-maiden | 7         | 2356 | -5678 |
| 25462 | The Lich King         | 7         | 2345 | -5672 |

Mask 7 includes phase 1, so a normally phased character sees them. The level 1 Rogue that was
standing in the same spot received **one** unit, so it was not in phase 1 either -- the autocast
aura had put it somewhere empty.

What this means for the parity harness: **"the NPCs are missing" was not a rendering defect.** The
server was not sending them to the character that was logged in. As the Death Knight, our client
holds **106 units in the object manager**. Whether they are *drawn* is still unconfirmed -- the user
was playing, so no client could be run to look.

Two lessons, both about my own method:

- I published the phasing explanation as confirmed, retracted it on a bad query, and have now
  reinstated it. Each step was stated with more confidence than the evidence carried. A spawn query
  is only as good as its radius; **check the row count against the zone before concluding from it.**
- Both the claim and its retraction would have been caught immediately by asking the server what
  phase the *player* is in, rather than inferring it from what the creatures are.

### 2026-09-16 - the client can hand over its own frame

Every "unverified on screen" row in this document exists because there was no way to look at a frame
without being at the machine. The desktop-capture route is not one: `CopyFromScreen` grabs a screen
rectangle, so when the client is behind another window -- or when `SetForegroundWindow` quietly fails,
which it does from a background process -- it returns whatever the user is actually looking at.

So the client now emits its own back buffer.

| layer | what landed |
|-------|-------------|
| `CGxDeviceD3d::IScreenShot` | `GetBackBuffer` -> `CreateOffscreenPlainSurface(D3DPOOL_SYSTEMMEM)` -> `GetRenderTargetData` -> lock -> 24-bit uncompressed TGA. `GetBackBuffer`, not `GetRenderTarget`, so a pass that left an offscreen target bound cannot redirect the capture. |
| `CGxDevice::ScreenShot` / `GxScreenShot` | the same four-layer chain `GxRenderTargetDump` already uses; non-D3D backends inherit a no-op |
| `Screen::s_capturePath` + the `s_captureScreen` block in `Screen.cpp` | the `// TODO` next to the capture flag is filled in: the grab happens at present time, so the image is a whole frame |
| `Script_Screenshot` | was `WHOA_UNIMPLEMENTED`. Names the file `Screenshots/WoWScrnShot_MMDDYY_HHMMSS.tga` as the reference does, sets the flag, and signals event 171 |
| `AutoScreenShot` in `CGWorldFrame.cpp` | `FROZEN_SCREENSHOT=<path>` captures once after `FROZEN_SCREENSHOT_FRAME` frames in world (default 300) |
| `tools/tga2png.py` | stdlib-only TGA -> PNG, since nothing in the viewing path reads TGA and Pillow is not installed |

The binding defers to the layer code rather than capturing inline: at the moment a Lua binding runs,
the interface is still being drawn over the world, so an inline grab catches a half-composed frame.

**Verified:** the converter, round-tripped against a synthetic 24-bit top-down BGR TGA of the exact
shape the client emits -- colours and orientation both come back correct. **Not verified:** the
capture itself, which needs a run. The user was playing, so no client could be started.

Next run, this is the command:

```
FROZEN_AUTO_LOGIN=SCENE:SCENE FROZEN_AUTO_CHARACTER=Scenedk FROZEN_SCREENSHOT=<path>.tga Frozen.exe
python tools/tga2png.py <path>.tga
```

That answers the three open visual rows in one shot: whether the 106 units draw, whether the clouds
are visible, and what the blob shadows actually look like.

### 2026-09-16 - measuring the Lua half: which stubs are actually hit

"Lua at 100%" had no number behind it. FrameXML loads with zero errors and zero warnings, but that
only means nothing *raised*; a binding that silently returns the wrong thing raises nothing, which is
exactly how `CSimpleEditBox_SetCursorPosition` hid behind a 4413-error cascade for six wrong
hypotheses.

`WHOA_UNIMPLEMENTED` prints one line per stub the first time it is reached, so a run log is a direct
measurement of which stubs the client actually touches. Three numbers, narrowing:

| measure | count |
|---------|-------|
| Lua bindings registered in a script table whose body is `WHOA_UNIMPLEMENTED` | **1029** |
| ...of those, referenced anywhere in the reference's 290 FrameXML files | **492** |
| ...of those, actually reached during a login -> character select -> world run | **58** (98 hits) |

The 58 is the list worth working from. The other 971 are battleground, LFG, guild and voice-chat
paths that a standing-in-the-world client never enters. FrameXML was pulled out of the reference
archives to get the middle number -- 290 files, newest patch wins.

Four implemented this pass, chosen because each returned *nothing* where script expected a value,
which is the silent-wrong-answer class rather than the missing-feature class:

- **`CScriptRegion_GetPoint`** -> point, relativeTo, relativePoint, offsetX, offsetY. The index is
  1-based over the *set* points, not a FRAMEPOINT value, because `m_points` is sparse and indexed by
  the point itself. `SetPoint` converts script offsets into DDC on the way in, so this undoes exactly
  that; `DDCToNDCWidth` and `NDCToDDCWidth` are a multiply and a divide by the same scalar, so the
  round trip is exact.
- **`CScriptRegion_GetNumPoints`** -> how many of the nine slots are occupied.
- **`CScriptRegion_GetSize`** -> width and height, resolving an unmeasured region the way `GetWidth`
  already does rather than reporting a zero that only means "not laid out yet".
- **`CSimpleFrame_GetAlpha`** -> `m_alpha / 255`, since `SetAlpha` stores a byte and script speaks
  0..1.

`FramePointToString` went into `src/ui/Util.cpp` beside `StringToFramePoint` so the two tables cannot
drift apart.

**Not implemented, deliberately:** `SetMovable` / `SetResizable` / `SetUserPlaced` and friends. The
flag bits are not defined on `CSimpleFrame` and the reference's bit values are not in `docs/ref/`;
inventing them would make `IsMovable` self-consistent while the drag machinery
(`StartMoving`, `StopMovingOrSizing`) stays absent -- half a feature that reports itself as whole.

**Verified:** it compiles and is installed. Nothing here has been run. Stopping the batch at four
rather than pressing on, because a long run of unverified changes on 2026-09-14 is what produced a
client that crashed on first launch.

### 2026-09-16 - reviewing the unverified batch found a crash in it

Three passes without a free desktop, so instead of adding more code that cannot be run, I read back
what this session had already written. That turned up a defect in my own `CScriptRegion_GetPoint`
from the previous pass.

`CFramePoint::GetRelative()` hands back a `CLayoutFrame*`, and I cast it straight to
`CScriptRegion*` to reach `lua_objectRef`. There are exactly two `CLayoutFrame` subclasses:

```
class CScriptRegion : public CScriptObject, public CLayoutFrame   // multiple inheritance
class CSimpleTop    : public CLayoutFrame                         // NOT a CScriptRegion
```

Because `CScriptRegion` inherits `CLayoutFrame` second, `static_cast<CScriptRegion*>` on a
`CLayoutFrame*` **subtracts the `CScriptObject` subobject's size** from the pointer. For a real
`CScriptRegion` that is correct. For a `CSimpleTop` there is no such base, so the result points
before the object, `lua_objectRef` is whatever heap memory sits there, and
`lua_rawgeti(L, LUA_REGISTRYINDEX, <garbage>)` follows.

`CSimpleTop::s_instance` is reachable as a relative -- `SetPoint` assigns it whenever the relative
argument is explicitly nil -- so this was not a theoretical path.

Fixed by checking identity against `CSimpleTop::s_instance` and pushing nil for it, which is also the
honest answer: `CSimpleTop` is the internal root above UIParent and is not exposed to script.
`dynamic_cast` would also have worked (`CLayoutFrame` is polymorphic and RTTI is on), but this
codebase uses it nowhere and the original client had no RTTI, so an identity check matches both the
surrounding style and what `SetPoint` already does.

The identical hazard was already present on `SetPoint`'s own error path, where it calls
`GetDisplayName()` through the same cast. Fixed the same way -- it would have crashed precisely when
something else had already gone wrong, which is the worst time to lose the error message.

Also re-derived the coordinate round trip rather than trusting the comment I had written:
`SetWidth` stores `s_x * w / aspect`, and `GetWidth` returns `DDCToNDCWidth(aspect * w)`. Those look
like different orderings but `DDCToNDCWidth(aspect * w)` and `DDCToNDCWidth(w) * aspect` are the same
expression, so `GetWidth` is an exact inverse and both `GetSize` and `GetPoint` match it.

**Verified:** the hierarchy, by reading the two class declarations; the round trip, by algebra.
**Not verified:** still nothing has been run.

### 2026-09-16 - what "31 ok" actually covers, and what it does not

Four passes reporting "ok: 31, DIFF: 3" without ever asking what those 31 rows *are*. They are a
slice, and naming the slice matters more than the count.

`tools/probes.json` holds 57 probes. They do not all compare:

| | count |
|---|---|
| probes in the file | 57 |
| frozen-only -- no reference address located yet | 10 |
| reference-only -- read there, no frozen equivalent read back | 12 |
| **actually compared pairs** | **34** |
| ...of which the explained shadow-matrix basis difference | 3 |
| ...**matching pairs** | **31** |

And by subsystem:

| group | probes |
|-------|--------|
| fog | 15 |
| sky dome | 14 |
| shadow-map constants | 8 |
| shadow-map per-frame | 7 |
| outdoor light | 3 |
| sky bodies | 3 |
| camera | 2 |
| time | 1 |

**Nothing else is probed at all.** There are zero probes for clouds, liquid, terrain, M2 entities,
blob shadows, particles, weather or world text. So "the memory comparison is clean" means the fog,
sky and shadow-map slice agrees -- it is not evidence about rendering as a whole, and it is
specifically silent on all three things the user last reported seeing wrong:

| user-reported symptom | probed? |
|-----------------------|---------|
| animated cloud textures not visible | no |
| blob shadows look like a black circle | no |
| NPCs not drawing | no (and this turned out to be server-side phasing) |

That is the honest boundary of the current measurement, and it is the gap that has to close before
"100 percent" means anything. Each new probe needs a reference address, which means Ghidra, not
guesswork -- the frozen side is already reachable (`BlobShadowStrength` and the liquid accessors in
`src/world/Terrain.cpp`, the density and sheet-flip state in `src/world/Clouds.cpp`).

Also restored `SET gxWindow "1"` to the reference install's `WTF/Config.wtf`. It was missing again --
the reference drops it on every exit -- and **both** clients read that file, so without it the next
launch of either one comes up fullscreen over whatever the user is doing.

### 2026-09-16 - first probes outside the fog/sky slice

Closing the coverage gap named in the previous entry. `docs/ref/` already held what was needed, so no
Ghidra run: `parity-sky-bodies.md` records `cloudStruct + 0x90 == 0x00d38e20`, which puts the
struct base at **0x00d38d90**, and gives its layout.

| field | address | probe |
|-------|---------|-------|
| `densityOverride` (float, 0 = use the DNInfo band) | 0x00d38d94 | reference-only; frozen has no override |
| `alphaThreshold` (uint8) | 0x00d38d98 | paired with frozen's `s_threshold` |

`alphaThreshold` is the useful one: it is `round((1 - density) * 255)`, so it encodes the effective
cloud density in a single byte that is rewritten whenever the density changes. Probes: 57 -> 59.

**A difference this should expose on the next run.** The write-up has the reference *rounding*;
`Clouds.cpp` casts, which truncates:

```cpp
uint8_t threshold = static_cast<uint8_t>((1.0f - clamp(density)) * 255.0f);
```

At the density measured in world (0.500) that is 127 against the reference's 128. I have **not**
changed the formula: the "round" is my own earlier summary of the decompile, not something re-read
from the disassembly, and this session has already produced two claims that were confidently written
down and wrong. The probe decides it with one run, which is what a probe is for.

**A hypothesis checked and discarded**, recorded so it is not re-run: 192 of the 1180 globals in the
PDB report RVAs around 0x08BExxxx, far past the 2.3 MB image, which looked like a symbol-resolution
bug silently feeding garbage addresses to the harness. It is not. `s_texMatrix` is one of the 192 and
it returned correct values in the live comparison, so those addresses are real -- the image simply
carries a large uninitialized data region. The 12 rows that print "-" on the frozen side are just
probes with no `frozen` field, exactly as the previous entry described. **No bug; nothing to fix.**

### 2026-09-16 - the new Lua bindings, checked against real call sites

The four bindings added on 2026-09-16 changed *arity*: `GetSize` 0 -> 2 returns, `GetPoint` 0 -> 5,
`GetNumPoints` 0 -> 1, `GetAlpha` 0 -> 1. Extra return values change what a caller sees, so the
extracted FrameXML was grepped for how each is actually used. This is verification of unverified
code without running it.

**`GetPoint` round-trips into `SetPoint`, including the case I was unsure about.** FrameXML captures
the whole return list and replays it:

```lua
ChannelFrame.lua:871   local point = { ChannelPulloutTab:GetPoint() };
FocusFrame.lua:436     frame.oldAnchors[i] = frame.oldAnchors[i] or {frame:GetPoint(i)};
```

The captured `relativeTo` goes back in as `SetPoint`'s third argument, which accepts a table
(`LUA_TTABLE`) -- and, critically, accepts **nil**, mapping it to `CSimpleTop::s_instance`. That is
exactly the value `GetPoint` now pushes for a `CSimpleTop` relative. So the nil is not a lossy
fallback: it is the value that reconstructs the original anchor. The safe choice and the correct
choice turned out to be the same one.

**`GetNumPoints` and `GetPoint(i)` are used as a matched pair**, so their index conventions have to
agree:

```lua
FocusFrame.lua:435   for i=1, frame:GetNumPoints() do
FocusFrame.lua:440       ... = equivFrame:GetPoint(i);
```

`GetNumPoints` counts occupied slots and `GetPoint(i)` indexes the occupied subset. Consistent. Had
`GetPoint` indexed `m_points` directly -- the obvious reading, since that array is what it walks --
the loop would have skipped real anchors and returned nothing for others.

**`GetPoint()` with no argument** appears in `ChannelFrame`, `Minimap` and `PVPFrame`; the
implementation defaults to index 1, which matches.

**`GetAlpha` was producing real Lua errors, not just wrong values.** It feeds arithmetic directly:

```lua
CastingBarFrame.lua   alpha = barFlash:GetAlpha() + CASTING_BAR_FLASH_STEP;
                      local alpha = self:GetAlpha() - CASTING_BAR_ALPHA_STEP;
```

A stub returning nothing yields `nil + number`. Every casting-bar flash and chat fade was raising.

**`GetSize` is never called by 3.3.5a FrameXML at all** -- zero call sites. Harmless, but it was not
worth the slot; it went in because it sat next to the other two in the same file.

**`IsMouseOver` is still a stub and its failure is silent**, which is why it is worth doing next: it
is used only in conditions (`if (frame:IsMouseOver())`, and with inset arguments such as
`IsMouseOver(45, -10, -5, 5)`), so returning nothing reads as false and the branch simply never
fires -- buff consolidation and chat fade quietly do nothing rather than erroring.

### 2026-09-16 - IsMouseOver, with its contract read off the call sites

The fifth binding from the runtime-hit list. Its failure was the silent kind: used only inside
conditions, a stub returning nothing reads as false, so buff consolidation and chat fade never fired
rather than erroring.

**No coordinate conversion was needed for the position**, which is the part worth recording.
`CSimpleTop::OnMouseMove` copies the mouse event straight into `m_mousePosition`, and
`CSimpleFrame::TestHitRect` compares that same event's `x`/`y` against `m_hitRect` without
converting. Region rects and the cursor are therefore already in one space, and the only values
needing conversion are the offsets arriving from script -- through exactly the transform `SetPoint`
applies to its own offsets, so an inset means the same thing as the offset that positioned the
region.

**The argument contract was read out of FrameXML, not assumed:**

```lua
FloatingChatFrame.lua:1074  chatFrame:IsMouseOver(topOffset, -2, -2, 2)
BuffFrame.lua:494           self:IsMouseOver(1, -1, -1, 1)
```

The first call names its own first argument `topOffset`, which fixes the order as
(top, bottom, left, right). Both use the pattern `(+, -, -, +)`, and both carry comments saying the
intent is a slightly *larger* area -- "1-pixel outer padding", "slightly larger than the hit rect
insets to give us some wiggle room". That is an outward expansion only if each offset is added to its
own edge, which fixes the sign. Two independent sites agreeing, each with a comment stating the
intent, is better evidence than the signature alone would have been.

Runtime-hit stubs: **58 -> 54** (`GetPoint`, `GetNumPoints`, `GetAlpha`, `IsMouseOver`). `GetSize`
was implemented alongside them but was never in the runtime list, and FrameXML never calls it.

### 2026-09-16 - CSimpleSlider:Disable, and picking targets by usage first

Started this pass on `CSimpleTexture:GetTexture` and abandoned it partway: `CSimpleTexture` stores
only an `HTEXTURE`, which is an opaque `HOBJECT`, and nothing in the codebase converts one back to
the `CTexture` that owns the `filename` field. Reaching the path would mean either a new handle
accessor or a new field on a decompiled class. Then I checked what FrameXML actually does with it --
**two call sites**. The investigation was worth more than the binding.

Counting first would have said so immediately, so that is what I did next, over the runtime-hit list:

| calls in FrameXML | binding |
|---|---|
| 294 | `Disable` |
| 7 | `GetCurrentMapZone` |
| 5 | `SetUserPlaced` |
| 4 | `SetMovable`, `GetDefaultLanguage` |
| 3 | `SetMaxBytes`, `GetLanguageByIndex` |
| 2 | `UpdateScrollChildRect`, `SetResizable`, **`GetTexture`** |

`Disable` by two orders of magnitude. Most of those calls land on buttons, which already have their
own implementation, but `CSimpleSlider_Disable` was in the measured runtime-hit list, so sliders do
reach the stub.

It needed no reference research at all -- its two siblings in the same file define it completely:

```cpp
CSimpleSlider_Enable     -> SetFrameFlag(0x400, 0); RunOnEnableScript();
CSimpleSlider_IsEnabled  -> enabled when !(m_flags & 0x400)
```

So `Disable` sets `0x400` and runs the matching script. `CSimpleFrame` had `m_onDisable` declared,
mapped from the XML attribute `"OnDisable"` by the script-slot lookup, and used by `CSimpleButton` --
but no `RunOnDisableScript` to fire it. Added, mirroring `RunOnEnableScript` including its
`m_loading` guard, and **verified the slot is really populated** rather than assuming: the name
lookup at `CSimpleFrame.cpp:709` returns `&m_onDisable` for `"OnDisable"`, so a frame's script does
get attached. A runner for a slot nothing fills would have looked correct and done nothing.

Runtime-hit stubs: **54 -> 53**.

### 2026-09-16 - tools/scene-run.py: the whole run as one command

Thirteen passes of assembling the run sequence by hand in a shell one-liner, and the desktop has not
been free once. When it is, the run has to work first time -- a wasted window costs another hour of
waiting. So the sequence is now a script with the safety rules compiled in rather than remembered:

```
python tools/scene-run.py                     # both clients, compare, screenshot, clean up
python tools/scene-run.py --keep              # leave them up to poke at
python tools/scene-run.py --character Scenedk # which character ours logs in as
```

1. **Refuses while `RunicWorldGame.exe` / `RunicWorldLauncher.exe` is running.** The documented guard,
   now enforced by the thing that does the launching instead of by me remembering to check.
2. Re-adds `SET gxWindow "1"`. The reference drops it on every exit and **both** clients read that
   file, so without it either one can come up fullscreen over the user.
3. Kills leftovers **by full path**.
4. Brings the reference to the world, waiting on its own clock rather than a fixed sleep.
5. Launches ours with auto-login and a one-shot screenshot.
6. Runs memcompare at the same instant and saves the full report.
7. Converts the TGA to PNG, and reports plainly if the capture never fired.
8. Shuts both down unless `--keep`, and counts the unique stubs the run hit.

**A real hazard fixed on the way.** `tools/relog-reference.py` opened with

```
Stop-Process -Name WoW -Force
```

which is precisely what CLAUDE.md and the launch memory forbid: the user's own game is also a
`WoW.exe`, so that line could have ended their session. It now enumerates `Win32_Process`, matches
`ExecutablePath` against the reference's, and kills only that. This tool has been run several times
this session; it happened not to fire on anything of theirs.

**Verified:** the guard, by running it -- it refused, naming both processes. That is the first path
through the script and the one that matters most. The rest waits on a free desktop like everything
else.

### 2026-09-16 - triage of the user's observed-bug list against the run log

First run of both clients in this session succeeded, and the user then listed eleven visible defects.
Each is mapped below to measured evidence from `build/scene-run.log` rather than to a guess, and
labelled with how well it is actually established.

| # | reported | status | evidence |
|---|----------|--------|----------|
| 1 | abilities not populated in hotbar | **CONFIRMED** | `Script_HasAction`, `Script_GetActionTexture`, `Script_GetActionText`, `Script_IsEquippedAction` all hit as stubs. `CGActionBar` stores no slot data, and `SMSG_UPDATE_ACTION_BUTTONS = 0x0129` is *declared* in `src/net/Types.hpp` with **no handler anywhere** -- the packet arrives and is dropped. Needs the packet stored before the bindings mean anything. |
| 2 | zone / subzone text missing | **CONFIRMED** | `Script_GetZoneText`, `GetRealZoneText`, `GetSubZoneText`, `GetMinimapZoneText` are all stubs, and nothing in the client tracks a current area id at all (no `s_areaID`, no AreaTable lookup outside the DBC record class). |
| 3 | buffs / debuffs missing | **CONFIRMED** | `Script_UnitAura` hit as a stub. |
| 4 | cast bar | **CONFIRMED** | `Script_UnitCastingInfo` and `Script_UnitChannelInfo` hit as stubs. |
| 5 | some objects missing entirely | **LIKELY, already known** | `CGGameObject_C::GetModelFileName` refuses `.wmo` paths, which is 4% of `GameObjectDisplayInfo` rows. |
| 6 | mouse tooltips | **UNDIAGNOSED** | Not `GetMouseFocus` -- see below. Tooltips come from each frame's own `OnEnter`, so the question is whether `OnEnter` fires and what `CGTooltip` does with it. `CGTooltip_SetPadding` is a stub but is cosmetic. |
| 7 | white chat box | **UNDIAGNOSED** | No stub hit points at it. Backdrop rendering. |
| 8 | sky colour not tinting player/NPC models | **UNDIAGNOSED** | Outdoor light values *are* computed -- the log carries `map 609 default params 56, 9 lights: ambient(0.25,0.26,0.33) diffuse(0.17,0.15,0.00)`. Whether the M2 pass consumes them is the open question. |
| 9 | NPCs stuck in idle, missing visual effects | **LIKELY** | CLAUDE.md already records unit movement as not ported, and `CM2Model::SetBoneSequence` leaves `0xFFFF` for a missing sequence. Server-driven emote state is not handled. |
| 10 | quest marker not overhead | **LIKELY** | No world-text / quest-giver-status system. The `CGQuestPOIFrame_*` stubs that did fire are the world *map* POIs, a different thing. |
| 11 | player names not overhead, skybox texture not drawn | **UNDIAGNOSED** | Skybox M2 machinery exists (`s_skyboxScene`, `s_skyboxModel`, `SkyboxLightingCallback`), so it is a draw or texture failure rather than a missing feature. |

**Implemented this pass:** `Script_GetMouseFocus`, from `CSimpleTop::m_mouseFocus` -- which the client
already maintains, since `OnMouseMove` stores the first frame whose hit test passes and fires
OnEnter/OnLeave off it.

**And immediately corrected.** The comment I wrote claimed this was why tooltips never appear. It is
not: 3.3.5a FrameXML calls `GetMouseFocus()` **once**, in `VehicleMenuBar.lua`. Tooltips come from
per-frame `OnEnter` scripts. The binding is correct and worth having, but it will not change anything
the user can see, and the comment now says so. Caught by checking call sites straight after writing
it -- the same check that has now paid off three times in this session.

**The pattern in this list worth acting on:** seven of the eleven are missing *data plumbing*
(action buttons, area id, auras, cast state, emote state), not missing rendering. The renderer is in
better shape than the symptom list suggests -- the run showed correct terrain, models, depth,
lighting and colour.

### 2026-09-16 - action buttons: the packet, two DBCs, and the four bindings

The confirmed cause of the empty hotbar. `SMSG_UPDATE_ACTION_BUTTONS = 0x0129` was declared in
`src/net/Types.hpp` with no handler, so the slot data arrived every login and was dropped; the four
bindings on top of it then had nothing to answer with.

**Wire format read from the server this client is developed against**, `Player::SendActionButtons`
in the local AzerothCore checkout, rather than from memory:

```
uint8  state                 // 2 = clear the bars, no payload; anything else = full replacement
uint32 packed[144]           // MAX_ACTION_BUTTONS
   action = packed & 0x00FFFFFF
   type   = (packed & 0xFF000000) >> 24
   types: 0x00 spell, 0x01 click, 0x20 equipment set, 0x40 macro, 0x41 click-macro, 0x80 item
```

`CGActionBar` gained `s_actions[144]` plus `GetAction`/`GetActionType`/`GetActionID`, and
`ReceiveActionButtons` is registered beside the weather handler in `Client.cpp`.

**Icons needed two DBCs that were not loaded.** Their layout was verified against the shipped files
rather than taken on trust -- `Spell.dbc` alone is 234 columns and the wrong index would have looked
plausible and produced nonsense:

| file | rows | columns | row size | checked |
|------|------|---------|----------|---------|
| `Spell.dbc` | 49839 | 234 | 936 | spell 133 reads name "Fireball" at column **136** and icon **185** at column **133**; 585 gives "Smite", 2050 "Lesser Heal" |
| `SpellIcon.dbc` | 3226 | 2 | 8 | icon 185 -> `Interface\Icons\Spell_Fire_FlameBolt` |

`SpellRec` reads the row as one 234-dword block and keeps three fields, rather than declaring 234
members for combat data nothing uses yet. Note `src/db/CMakeLists.txt` globs its sources, so adding a
record class needs a **cmake reconfigure** -- the first build failed with seven `LNK2019`s for
exactly that reason.

**Bindings implemented:** `HasAction`, `GetActionTexture`, `GetActionText`, `IsEquippedAction`.

Slot numbering verified from `ActionButton.lua` rather than assumed: `ActionButton_CalculateAction`
returns `self:GetID() + ((page - 1) * NUM_ACTIONBAR_BUTTONS)` with `GetID()` starting at 1, so script
slots are 1-based against a 0-based array. These are global functions, so the argument sits at Lua
index 1 (not 2 as for a method).

**Deliberately answering nil rather than guessing:** `GetActionText` for macros (macro storage is not
implemented, so the slot is known to hold one but its name cannot be produced) and
`IsEquippedAction` (item actions are not resolved, and a `false` would claim a check that never
happened). `GetActionTexture` resolves spell actions only; items and macros carry their art in the
item cache and macro icon list.

**Verified:** the wire format against the server source, both DBC layouts against the shipped files,
the slot convention against FrameXML, and a clean build. **Not verified:** that buttons actually
populate on screen -- the user is playing, so no run.

### 2026-09-16 - the player frame showed "Unknown" because entering the world deletes the name

Second item off the user's list, and a clean root cause.

`Script_UnitName` special-cased the active player to the glue's selected character:

```cpp
if (unit->GetGUID() == ClntObjMgrGetActivePlayer()) {
    auto selected = CCharacterSelection::GetSelectedCharacter();
    if (selected) { name = selected->m_info.name; }
}
```

That is correct at the character-select screen and wrong everywhere else. Entering the world runs
`CGlueMgr::Suspend()` -> `CCharacterSelection::Shutdown()`, which calls `s_characterList.Clear()`.
`GetSelectedCharacter()` then finds `s_selectionIndex` out of range and returns null, so the name
falls to the literal `"Unknown"` -- for the rest of the session, which is exactly what the
screenshot showed on the player frame while the reference showed the character's name.

The fix is to let the player resolve like everyone else. `NameCache::Find` already handles
`TYPE_PLAYER`, keys on GUID and issues `CMSG_QUERY_PLAYER_NAME` on a miss, and the server answers for
the player's own GUID like any other. The selection is still consulted first, since it is
authoritative and immediate while the glue is up, whereas the cache only fills when the reply lands.

Verified against FrameXML rather than assumed: `UnitFrame.lua` sets the label from `GetUnitName`,
which is a Lua wrapper doing `local name, server = UnitName(unit)` -- so it is this binding, not the
stubbed `UnitPVPName`, and the two-value return already matches.

**Verified:** the teardown path by reading it, the cache path by reading it, the caller by reading
FrameXML, and a clean build. **Not verified on screen:** the user is playing.

### 2026-09-16 - models not picking up the sky colour: a candidate, and a measurement rather than a fix

Third item off the user's list. The plumbing is all present, so this is not a missing feature:
`CGObject_C` sets `CWorld::LightingCallback` on world objects, and that callback applies
`s_outdoorAmbient` and `s_outdoorDiffuse` -- which the run log confirms are being computed
(`map 609 default params 56, 9 lights: ambient(0.25,0.26,0.33) diffuse(0.17,0.15,0.00)`).

But the callback has an **early return**:

```cpp
if (TerrainInteriorAmbientAt(pos, interior)) {
    lighting->AddAmbient(interior);
    return;                      // no diffuse, no direction
}
```

and `TerrainInteriorAmbientAt` decides "interior" with an **axis-aligned box test against the group
bounds**. A group's AABB is a strictly larger volume than the room it describes. Ebon Hold is one
WMO (`DEATHKNIGHTNECROPOLIS.WMO`), and the screenshot has the player out on an open deck under
visible sky -- well inside the AABB of some interior hall. Anything classified that way is lit flat
by interior ambient with no diffuse at all, while the terrain under its feet still takes the outdoor
path.

That matches the reported symptom exactly: terrain and sky correct, models not tinted. The reference
resolves containment through the group BSP and portals rather than bounds, and the data for that is
already loaded -- `WmoInstance` carries `portals`, `portalRefs` and `portalVerts`.

**Not fixed, deliberately.** It is a coherent story but an unverified one, and this session has
already produced several confident explanations that did not survive contact with evidence. Instead
the branch now logs its first six hits with position, group index, the ambient it applied, the
group's z range and the instance's portal count. One run says whether the branch is taken at Ebon
Hold at all; if it is, the fix is a containment test, and if it is not, the cause is elsewhere and
nothing was broken chasing it.

**A near-miss worth recording.** The build failed on this edit (`w.path` does not exist on
`WmoInstance`), but the `cp` in the same command ran anyway and installed the *previous* exe -- the
stale-install trap CLAUDE.md warns about, which exits 0 and looks fine. Caught by comparing
timestamps afterwards: `build/bin` 01:41:39 against `build/dist` 01:41:07. Chain the install behind
the build, and check the timestamp, rather than trusting that a command that printed "installed"
installed anything new.

### 2026-09-16 - bulk stub implementation, with the flag bits taken from the reference

Feedback taken: too much time on one visual at a time when there is a large body of stubs with
obvious implementations, and the decomp is right there. Switched to clearing them in batches.
`CSimpleFrameScript.cpp` alone was **53 of 85 stubbed**; it is now 32.

**The flag family needed real bit values, so they were recovered rather than invented.** The route,
which is reusable for any binding in the reference:

1. `strrefs.sh` on the binding names finds each string and the table entry that references it --
   `SetMovable` at `0x00ac16f0`, `IsMovable` at `0x00ac16f8`, and so on. The entries are 8 bytes
   apart, which is what a `{name, function}` table looks like on 32-bit.
2. Reading the dword *after* each name pointer straight out of the PE gives the function. Every
   name pointer read back matched the string address `strrefs` reported, which is what confirms the
   table layout before trusting the function column.
3. `decomp.sh` on those six addresses.

| binding | reference | flag |
|---------|-----------|------|
| `SetToplevel` | `FUN_0049fbb0` | **0x0001** |
| `SetMovable` | `FUN_004a0800` | **0x0100** |
| `IsMovable` | `FUN_004a0850` | reads `frame + 0xb4 & 0x100` |
| `SetResizable` | `FUN_004a0980` | **0x0200** |
| `SetUserPlaced` | `FUN_004a0c70` | **0x1000**, and it *errors* unless `flags & 0x300` |
| `SetClampedToScreen` | `FUN_004a0d70` | not a flag -- calls a dedicated setter, left alone |

Two independent confirmations: `IsMovable` reads the same field the setter writes (`piVar1[0x2d]`
is `+0xb4`), and this codebase's own XML loader already used 0x100/0x200 for the `movable` and
`resizable` attributes. The constants now live in `CSimpleFrame.hpp` with that provenance recorded,
instead of being three magic numbers.

`SetUserPlaced` reproduces the reference's refusal, message included: a frame that is neither movable
nor resizable raises "Frame %s is not movable or resizable" rather than silently accepting.

**Implemented this pass (21):** `SetMovable`, `IsMovable`, `SetResizable`, `IsResizable`,
`SetUserPlaced`, `SetToplevel`, `IsToplevel`, `IsMouseEnabled`, `IsKeyboardEnabled`,
`IsMouseWheelEnabled`, `IsJoystickEnabled`, `EnableDrawLayer`, `DisableDrawLayer`, `GetScale`,
`GetEffectiveScale`, `GetFrameStrata`, `GetNumChildren`, `GetChildren`, `GetNumRegions`,
`GetRegions`, `IsEventRegistered`.

None needed new state: the event mask that `EnableEvent`/`DisableEvent` already maintain answers the
input queries, `m_children`/`m_regions` answer the enumeration ones, and `s_scriptEventsHash` keeps
the per-event listener list that `IsEventRegistered` walks. `FrameStrataToString` went in beside
`StringToFrameStrata` for the same reason as `FramePointToString` -- so the two tables cannot drift.

**Whole-tree count: 1561 Lua binding functions, 1074 still stubbed.** That is the real size of the
job, and it is the number to drive down.

### 2026-09-16 - the tooltip core, and why no tooltip could ever appear

`CGTooltipScript.cpp` was **67 of 69 stubbed** -- only `IsOwned` and `GetOwner` were real. That is
the whole explanation for the user's missing tooltips, and the decisive one is `SetOwner`: every
`OnEnter` handler calls it first, so with it stubbed `IsOwned` answered false for every frame and
nothing downstream ever ran. The rest of the API could have been perfect and still shown nothing.

**How lines work, which decided the shape of this.** `GameTooltipTemplate.xml` declares the line
font strings as children named `<tooltip>TextLeft1..8` and `TextRight1..8`; FrameXML never calls
`AddFontStrings` in 3.3.5a, so the client owns line creation. The reference makes more on demand
when a tooltip outgrows the template. That is not ported, so **line 9 is dropped rather than
overwriting line 8** -- truncating a long tooltip is recoverable, silently corrupting one is not.

**Implemented (13):** `SetOwner`, `GetAnchorType`, `SetAnchorType`, `ClearLines`, `AddLine`,
`AddDoubleLine`, `SetText`, `NumLines`, `SetMinimumWidth`, `GetMinimumWidth`, `SetPadding`,
`GetPadding`, `IsUnit`.

`StringToTooltipAnchor` / `TooltipAnchorToString` went into `src/ui/Util.cpp` beside the frame-point
and strata mappings, sharing one table so the two directions cannot disagree.

**The multiple-inheritance trap, caught a second time.** `GetLayoutFrameByName` returns a
`CLayoutFrame*`, and `CScriptRegion` inherits `CLayoutFrame` *second* -- so casting one to
`CSimpleFontString*` adjusts the pointer past a base that a `CSimpleTop` does not have. Identical to
the `CScriptRegion_GetPoint` defect found earlier this session. The lookup now rejects
`CSimpleTop::s_instance` and confirms `IsA(CSimpleFontString::GetObjectType())` before casting.
Worth naming as a rule: **any cast from `CLayoutFrame*` in this codebase needs both checks.**

`IsUnit` answers false unconditionally and says so in a comment, because `SetUnit` is still a stub so
`m_unitGUID` is never set. A comparison against a field nothing writes would read like working code.

Whole-tree: **1561 bindings, 1061 stubbed** (from 1074).

### 2026-09-16 - the Unit* family, and only answering what can actually be answered

`ScriptEvents.cpp`: 109 of 169 stubbed. Eleven cleared, chosen on one rule -- **implement only where
the client can produce a correct answer**, and leave the rest stubbed rather than inventing a
plausible-looking one. A binding that returns a confident wrong value is worse than one that
announces itself missing, which is the whole lesson of the `SetCursorPosition` cascade.

**Implemented (11):** `IsLoggedIn`, `UnitPVPName`, `UnitIsSameServer`, `UnitIsInMyGuild`,
`IsInGuild`, `IsGuildLeader`, `IsInArenaTeam`, `IsArenaTeamCaptain`, `IsXPUserDisabled`,
`GetPlayerFacing`, `UnitSelectionColor`.

Each is answerable for a real reason, not by assumption:

- `IsLoggedIn` is `CGGameUI::IsInWorld()`. `s_inWorld` was private with a public `IsLoggingIn()`
  beside it, so a matching accessor went in rather than widening the field.
- `UnitPVPName` is the plain name. That is not a stand-in: the reference returns exactly that for a
  character with no PVP title, so the common case is already correct.
- The guild and arena queries answer false because **no guild or arena data is ever received** --
  there is no roster, no membership packet, nothing. False is the truth about this client's state,
  not a guess about the character's.
- `UnitIsSameServer` is true for anything visible, because this client talks to one realm.
- `UnitSelectionColor` derives from `UnitReaction`, so it will track real faction reactions for free
  when those land (today `UnitReaction` answers 5, neutral, for everything).

**Left stubbed on purpose:** `IsSwimming`, `IsFlying`, `IsMounted`, `IsStealthed`, `IsFalling`,
`IsIndoors`, `IsOutdoors`, the combat-rating and stat family, `UnitAura`, `UnitCastingInfo`. Each
needs unit fields or packets the client does not process yet; a `false` from any of them would claim
a check that never happened.

**Calling a sibling binding directly, not through the Lua global.** `UnitPVPName` and
`UnitSelectionColor` both reuse another binding. The first version did it with
`lua_getglobal("UnitName")`, which works but goes through a name any addon can replace; both now
call the C function in the same translation unit.

Whole-tree: **1561 bindings, 1050 stubbed** (from 1061).

### 2026-09-16 - auras: the packets, the cache, and UnitAura

`UnitAura` was a stub with nothing behind it, and the reason was upstream: **both aura opcodes were
declared and neither was handled**, so the server's aura state was read off the wire and dropped.
Same shape as the action buttons. `src/object/client/AuraCache.{hpp,cpp}` now handles them, modelled
on `NameCache`.

**Wire format, read from AzerothCore's `AuraApplication::BuildUpdatePacket`:**

```
SMSG_AURA_UPDATE     (0x496)  packed guid + one record
SMSG_AURA_UPDATE_ALL (0x495)  packed guid + records to the end of the packet

record:
  uint8  slot
  uint32 spellID                        0 = this slot was cleared
  uint8  flags, casterLevel, stacks
  packed casterGuid                     only when AURA_FLAG_CASTER (0x08) is clear
  uint32 maxDuration, duration          only when AURA_FLAG_DURATION (0x20) is set
```

Three details that are decisions, not transcription:

- **The record count is not on the wire** for the ALL variant, so records are read while
  `Tell() < Size()`, with a length check before each one so a truncated packet stops cleanly instead
  of reading past the end.
- **A full update erases the unit's slots first.** Otherwise an aura that expired while the unit was
  out of view would linger forever, since its removal is a slot the new packet simply does not list.
- **Slots are a `std::map`, not an array.** The slot is a wire-supplied byte; an array sized to
  today's slot count would silently drop anything past it, and a 256-entry array per unit would cost
  memory on every creature in view.

**Timestamps matter here.** `UnitAura` must return `expirationTime` on `GetTime()`'s clock, and
`GetTime()` is `OsGetAsyncTimeMs() / 1000`. The first version stamped records with the message
handler's `time` argument, which is not that clock; each record now takes `OsGetAsyncTimeMs()` at
receipt, so `(receivedMs + duration) / 1000` lines up with what FrameXML compares it against.

**`UnitAura` returns the full 11-value shape** `BuildFrame.lua` destructures -- name and texture now
resolve for real through `Spell.dbc` -> `SpellIcon.dbc`, which landed with the action buttons. Four
values are honest nils (`rank`, `debuffType`, `unitCaster`, and the two booleans): they need Spell.dbc
columns that are not read yet or a guid-to-token reverse lookup that does not exist, and a fabricated
value there would be worse than a nil FrameXML already handles.

**`UnitBuff` / `UnitDebuff`** were `// TODO auras; return 0`. They are `UnitAura` with the filter
pinned to HELPFUL / HARMFUL, so they force the third argument and defer rather than duplicating the
record-to-Lua conversion.

### 2026-09-16 - the casting bar: six opcodes and two bindings

Same story again: `UnitCastingInfo` / `UnitChannelInfo` were stubs, and nothing behind them was
handled. `src/object/client/CastCache.{hpp,cpp}` now tracks what every visible unit is casting.

`MSG_CHANNEL_START` (0x139) and `MSG_CHANNEL_UPDATE` (0x13A) were **missing from
`src/net/Types.hpp` entirely** -- the enum jumped 0x0138 to 0x013C -- so those had to be added
before they could be handled.

| opcode | effect |
|--------|--------|
| `SMSG_SPELL_START` | begins a cast bar |
| `SMSG_SPELL_GO` | the cast went off; ends the bar |
| `SMSG_SPELL_FAILURE`, `SMSG_SPELL_FAILED_OTHER` | interrupted or refused; ends the bar |
| `MSG_CHANNEL_START` | begins a channel |
| `MSG_CHANNEL_UPDATE` | moves the channel's end, or ends it at 0 |

Four decisions worth recording:

- **`SMSG_SPELL_START` is parsed as a prefix only.** Caster, spell and timer all sit before the
  variable-length `SpellCastTargets` block, and nothing after it is needed -- so the reader stops
  rather than porting target serialisation to reach nothing.
- **`SPELL_GO` only ends a cast, never a channel.** A channel's `SPELL_GO` arrives at the *start* of
  the channel, so treating it as an end would clear the bar the instant it appeared.
- **`CHANNEL_UPDATE` moves the end, not the start.** The server is authoritative on what remains; a
  channel shortened by haste should not rewrite when it began.
- **A cast whose end has passed is treated as finished** even with no completion packet. Those can be
  lost or late, and a bar that never empties is worse than one that clears a frame early.

**Units differ between these and the auras, and the difference is load-bearing.** `UnitAura` returns
seconds; `UnitCastingInfo` returns **milliseconds**. That is not a guess --
`CastingBarFrame.lua` computes `GetTime() - (startTime / 1000)`, which fixes it. Getting it wrong
would have put the bar off by three orders of magnitude rather than failing visibly.

Whole-tree: **1561 bindings, 1047 stubbed.** Three of the eleven reported defects now have their
data plumbing in place (action buttons, auras, casting), all three having been the same shape: a
declared opcode with no handler.

### 2026-09-16 - a run that answered three open questions at once

Ran our client alone with the screenshot hook. Three things the run settled:

**1. The action-button packet works.** `Action buttons: state 1, 0 of 144 slots filled`. The packet
is received and parsed; the character genuinely has no saved bar, because `Scenedk` was created by
copying the `characters` row in SQL and `character_action` was never copied. The plumbing is right
and the test character is empty -- worth populating before judging the hotbar by eye.

**2. The interior-lighting hypothesis is CONFIRMED, and now fixed.** The diagnostic fired at the
outdoor spawn:

```
InteriorAmbient: pos(2355.6 -5677.9 429.8) group 3 ambient(0.24 0.27 0.39)
                 groupBox z[414.1 544.1] portals 6
```

Standing out on the open deck, inside interior group 3's bounding box, so every model there was lit
by flat interior ambient with no diffuse while the terrain under it took the outdoor path. `WmoGroup`
now carries a second XY triangle grid and `WmoGroupContains` decides containment physically: a point
inside a room has geometry both above and below it, a ceiling and a floor; on a deck there is a floor
and open sky. The shadow grid could not be reused -- it keeps only up-facing triangles, and this test
needs the ceiling, which faces down.

**3. The white chat box is root-caused -- three faults stacked.** The run finally surfaced Lua
errors, and the chat one named the path:

```
FloatingChatFrame.lua:1000: bad argument #1 to 'max' (number expected, got nil)
```

- `FloatingChatFrame_Update` applies a window's colour and alpha **only** under
  `if ( onUpdateEvent )`, which comes from `UPDATE_CHAT_WINDOWS` / `UPDATE_FLOATING_CHAT_WINDOWS`.
  Both exist in `g_scriptEvents` (386, 529) and **nothing ever signalled either**, so no chat frame
  was ever styled and `frame.oldAlpha` was never set -- hence the nil in the fade path.
- `GetChatWindowInfo` returned `r,g,b,a = 1,1,1,1`: an opaque **white** panel. That is the white box
  itself. FrameXML's own defaults are black at `DEFAULT_CHATFRAME_ALPHA`, which
  `FloatingChatFrame.lua:21` defines as 0.25.

Both fixed: the event is signalled right after `PLAYER_ENTERING_WORLD`, and the defaults now match
FrameXML's.

**And the player name.** `UnitName` was fixed earlier, but the frame still read "Unknown" because the
name query is asynchronous: by the time the reply lands every frame has already drawn, and nothing
asks again. `NameCache` now signals `UNIT_NAME_UPDATE` (event 144) for the active player when the
reply arrives. The binding was right; the label was simply never refreshed.

**Also worth recording: 84 stub hits and a batch of real Lua errors are now visible in the run log**
(`SpellBookFrame` arithmetic on nil, `Blizzard_CombatLog` min on nil). Those are the next thread --
each names a binding returning nothing where a number is expected.

### 2026-09-16 - verification run, then two arity bugs the errors named

Ran with the fixes in. Confirmed by the user on screen: **model lighting is fixed** and the moon is
visible. Confirmed by the log: `InteriorAmbient` hits **0** (was 6), `FloatingChatFrame` errors
**0**, total Lua errors **0** (was several). The chat frame is no longer a white box, though not yet
finished.

**The draw-distance sphere is NOT a fog parameter problem.** Read live from the running client and
cross-checked against the last two-client comparison, every fog value matches the reference exactly:

| | reference | frozen | |
|---|---|---|---|
| fog end | 727 | 727 | ok |
| fog start | 0 | 0 | ok |
| fog colour | 0 0.188 0.235 | 0 0.192 0.239 | ok |
| fog rate | 1.5 | 1.5 | ok |
| far clip | 727 | 727 | ok |

`fogStart = 0` looks wrong but is a measured match -- the comment in `CWorld.cpp` records the
reference reporting 0 at the same spot and clock where the raw product is -190. Terrain culling is
frustum-based with no radial term, and the tile window is 5x5 (>=1066 yd) against a 727 yd clip, so
neither streaming nor culling explains a sphere. With the parameters identical, the difference has to
be in how fog is *applied*, and settling that needs a side-by-side rather than more reading.

**Two arity bugs, both named outright by the run log.** These are the `SetCursorPosition` class
again -- a binding that returns too few values, so the caller's later locals are nil:

- `GetSpellTabInfo` returned **4** values; FrameXML destructures **6** and then assigns
  `numSpells = highestRankNumSpells`. On the default `ShowAllSpellRanks` setting both became nil,
  giving `attempt to perform arithmetic on local 'numSpells'` and, through
  `SpellBookFrame.selectedSkillLineOffset`, the second spellbook error. Now returns 6.
- `CombatLogGetNumEntries` returned nothing and is consumed as
  `count = max(1, min(count, COMBATLOG_MESSAGE_LIMIT))`. Now returns 0 -- an empty log, which the
  interface handles, unlike nil.

Worth generalising: **the run log's Lua errors are a far better worklist than the stub count.** Each
one names a binding and the exact arithmetic that failed, where the 1047 remaining stubs give no
ordering at all.

### 2026-09-16 - the spinning cursor, and the resize "crash" that was a hang

**The cursor.** Two faults in four lines of `IWindowClassRegister` / `WindowProcD3d`:

```cpp
wc.hCursor = LoadCursor(instance, IDC_ARROW);   // wrong: a STANDARD cursor needs a null module
```

With `instance` passed, `LoadCursor` looks for a resource *named* IDC_ARROW inside the exe, finds
nothing and returns NULL. A window class with a null cursor leaves whatever the previous window set
-- at startup, the "app starting" arrow-and-spinner. The second half: `WM_SETCURSOR` returned 1
without ever calling `SetCursor`, and returning TRUE means "handled, do not change it", so the
spinner stayed for the life of the process. Now `LoadCursor(nullptr, IDC_ARROW)`, and WM_SETCURSOR
sets the class cursor for HTCLIENT while letting DefWindowProc keep the resize-border cursors.

**The resize.** Not a crash -- a hang, and the log said so outright: `IReleaseD3dResources call
4847`. Every WM_SIZE reset failed, `ScenePresent`'s retry ran the whole release-and-reset again on
the next frame, and after the first failure the default colour and depth surfaces are already
released, so no retry could ever succeed. The client kept rendering and never presented.

Logging the HRESULT instead of guessing named it in one run:

```
Reset FAILED 0x8876086C (INVALIDCALL - a default-pool resource is still alive)
```

And the resource was findable from there. `ITexCreate` sets `d3dPool = D3DPOOL_DEFAULT` whenever
`m_flags.m_renderTarget` is set, then branches by kind:

| branch | pool | tracked? |
|--------|------|----------|
| cube map | DEFAULT | **no** -- returns early |
| depth stencil | DEFAULT | **no** -- returns early |
| ordinary texture | DEFAULT | yes, `TrackRenderTarget` |

**The map shadow map's depth texture took the depth-stencil branch**, so it was never tracked,
`ForgetRenderTargets` never released it, and it blocked every Reset. Both early-return branches now
track when the pool is DEFAULT, and `ForgetRenderTargets` releases through `IUnknown*` rather than
casting cube and depth-stencil textures to `LPDIRECT3DTEXTURE9` and relying on the vtables lining up.

**Verified by re-running the same reproduction**, driving three programmatic resizes through
`SetWindowPos` so WM_SIZE arrives exactly as a drag delivers it:

| | before | after |
|---|--------|-------|
| `Reset FAILED` | every resize | **0** |
| `IReleaseD3dResources` calls | 4847 | **5** |
| client after three resizes | hung | rendering, captured |

The reproduction script is `scratchpad/resize.py` -- worth keeping, since this class of bug cannot be
reached without actually resizing the window.

### 2026-09-16 - the hard far cutoff: horizonFarclipScale was registered and never read

Confirmed fixed on screen by the user.

The symptom was a sudden cull of everything past a distance -- terrain, water, all of it -- with
faint geometry still visible beyond, which ruled fog out. I had chased fog and then the sky dome
first; both were wrong, and the user's insistence that it was **sudden, not fog** is what redirected
it.

**Cause.** `CWorldParam::cvar_horizonFarClip` ("horizonFarclipScale", default **4.0**) is registered
and **nothing ever read it** -- `HorizonFarClipScaleCallback` is a bare `// TODO`. The camera's far
plane was set from `CWorld::GetFarClip()` instead, so the whole world was frustum-clipped at
farclip:

```cpp
this->m_camera->SetFarZ(CWorld::GetFarClip());   // 727
```

The two values are not the same thing. **`farclip` is where fog ends and where doodads stop being
worth drawing; the world itself is drawn to `farclip * horizonFarclipScale`** -- 2908 yards here.
Clipping the world at farclip puts the frustum's far plane exactly where fog has not yet saturated,
so geometry is cut off while still visible. That is the hard edge.

**Fix, in two parts, because the first alone only moves the edge:**

- `CWorld::GetHorizonFarClip()` returns `s_farClip * horizonFarclipScale` (scale clamped to >= 1, since
  a value below 1 would pull the world inside the fog), and `CGWorldFrame` builds the camera from it.
- Terrain streaming sized itself from farclip too -- `radius = ceil(GetFarClip() / TILE_SIZE)`, capped
  at `MAX_TILE_RADIUS = 2`. With the far plane extended, the 5x5 tile window becomes the new hard
  edge. The radius now follows the horizon distance and the cap is **3** (a 7x7 window, >= 1600 yards),
  which puts the boundary well inside fog dense enough to hide it.

**The lesson worth keeping.** Two CVars that sound alike govern different things, and the one that
was never wired up is the one that decides how far the world is drawn. A registered CVar with a TODO
callback and no readers is a strong signal on its own -- worth grepping for as a class:

```
CWorldParam::cvar_horizonNearClip   // also registered, also never read
```

### 2026-09-16 - working the backlog from the run log, not the file list

With terrain and atmosphere confirmed 1:1 by the user, back to the stub backlog. The latest run
reports **zero Lua errors** -- the arity fixes held -- so the worklist is the **98 stubs actually
hit** rather than the whole 1030.

**Two clusters examined and set aside, with reasons:**

- `GetChatWindowSavedDimensions` / `GetChatWindowSavedPosition` look like gaps but are already
  correct. FrameXML guards both -- `if ( width and height )`, `if ( point )` -- so returning nothing
  *is* the "no saved layout" answer, and the interface falls back to its default placement. They are
  noisy, not broken.
- The `CSimpleModelFFX` light family (`AddLight`, `AddCharacterLight`, `AddPetLight`,
  `ResetLights`) has no caller anywhere in FrameXML, so its argument contract cannot be read off a
  call site. `CSimpleModel::LightingCallback` already applies `m_light` when visible, so the state is
  waiting for them -- but guessing the signature would be inventing.

**Seventeen implemented**, on the standing rule: only where the client can state a *correct* answer,
never where false is merely convenient.

| binding | answer | why it is correct, not a default |
|---------|--------|----------------------------------|
| `GetExpansionLevel`, `GetAccountExpansionLevel` | 2 | this client IS 3.3.5a, and Wrath is expansion 2 |
| `GetDungeonDifficulty`, `GetRaidDifficulty` | 1 | there is no difficulty system, so normal is the only state reachable |
| `NoPlayTime`, `PartialPlayTime` | false | a regional play-time limit the server never sends cannot be active |
| `IsPVPTimerRunning` | false | no PVP flag timer is tracked |
| `IsPartyLFG`, `GetAvailableRoles`, `GetLFGRandomCooldownExpiration`, `UnitGroupRolesAssigned` | false / 0 / "NONE" | all four are LFG state, and there is no LFG system |
| `GetOptOutOfLoot` | false | no loot system to opt out of |
| `IsPossessBarVisible`, `UnitIsPossessed` | false | possession is not implemented |
| `GetTargetTradeMoney` | 0 | no trade window, so never money in one |
| `UnitIsTalking` | false | voice chat is not implemented |
| `DungeonUsesTerrainMap` | false | |

Each is answerable because of something the client knows about *itself* -- the build it is, or a
subsystem that does not exist and therefore cannot be active. The ones left stubbed (`IsResting`,
`OffhandHasWeapon`, the honor and arena currencies, the PVP stat blocks) need unit fields or packets
that are not processed, and a zero from those would claim a reading that never happened.

Counts: **1566 bindings, 1030 `WHOA_UNIMPLEMENTED`, 1001 action stubs.**

### 2026-09-16 - tooling for the stub mountain, because hand-picking does not scale

Feedback, fairly: implementing 13 or 17 at a time against ~2000 stubs is not progress. The answer is
not to type faster -- it is to stop deciding one at a time. Three tools now do the deciding.

**`tools/stubtriage.py`** classifies every stubbed binding by **what the caller actually needs back**,
read from the FrameXML call site rather than from the name. 1955 stubs, 23 families:

| what the caller needs | count | what a stub costs |
|---|---|---|
| `uncalled` | 899 | nothing -- FrameXML never calls it |
| `ignored` | 598 | nothing -- the result is discarded |
| `value` | 325 | arity matters; needs the call site |
| `boolean` | 102 | silently reads as false |
| **`number`** | **31** | **hard error** -- every "arithmetic on a nil value" is one of these |

That ordering is the whole point: **930 of the 1955 cannot break anything**, and 31 break something
every time they are touched. The mountain is much smaller than its headcount.

**`tools/stubfill.py`** fills the two rule-determined classes in bulk -- 0 where the caller does
arithmetic on an absent subsystem, false where it branches on one. It applies the boolean rule only
to real predicates (`Is`/`Can`/`Has`/`In`/`Are`), never to a `Get*` that happens to sit in a
condition, because those return strings or numbers and false there would be a NEW bug. An exclusion
list holds the ones the rules would mishandle: `GetLocale`, `GetContainerFreeSlots`, `strlenutf8`,
`IsMouseButtonDown` (input state the client genuinely has), and the actions that are not predicates
at all. **79 filled in one pass.**

**`tools/arity.py` / `arityfill.py`** find the `GetSpellTabInfo` bug class wholesale: bindings that
return fewer values than FrameXML destructures. It found **29**, including one introduced an hour
earlier -- `UnitGroupRolesAssigned` returned the single role string the later API uses, while both
call sites in this FrameXML take three booleans (`PartyMemberFrame.lua:223`, `PlayerFrame.lua:250`).

Padding with nils would be pointless -- a value never pushed is already nil. What matters is the
TYPE at each position, and the caller names it:

```lua
local haveTotem, name, startTime, duration, icon = GetTotemInfo(slot)
      -- boolean  -- string  -- number  -- number  -- string
```

so `arityfill.py` types each position from the local it lands in and pushes 0, false or nil
accordingly. Mismatches: **29 -> 16**. The remaining 16 have real bodies and need reading.

**Net: 1952 stubbed, from 2031.** More usefully, the count that can actually break something is now
close to zero, and the next sweep is a tool run rather than a reading session.

### 2026-09-23 - the D3D device's state sync, and which of its empty bodies actually matter

`tools/livestubs.py` reports 52 empty-bodied functions with live call sites, and three of them sit
in the D3D backend right under every draw: `CGxDeviceD3d::IStateSyncEnables`, `IStateSyncLights`
and `IStateSyncMaterial`. That reads like a serious hole. Two of the three are not.

`CGxDeviceD3d::IStateSync` calls Lights, Material and Xforms **only when no vertex shader is
bound**. Terrain, map objects, detail doodads, blob shadows, models and the sky all bind one, so
on the world render that whole branch never executes and the two empty bodies cost nothing. The
render states themselves reach D3D through `CGxDevice::IRsSync`, which is fully implemented and
walks the dirty list into `IRsSendToHw`.

`IStateSyncEnables` is the one that runs unconditionally, and it is worse than a stub: it is one
empty function standing where the reference calls **four** helpers.

The reference side was found by searching the text dump for `0x738(%e..)` — app render state 77
(`GxRs_VertexShader`) at a 0x18 stride from the state-array pointer at `CGxDevice+0x28f4`, which is
the exact test frozen's `IStateSync` makes. Only two functions in the binary do it, one per D3D
device class:

| reference | what it is | how it was confirmed |
|---|---|---|
| `006a5940` | `CGxDeviceD3d::IStateSync` | the whole structure, call for call |
| `006a9860` | the D3D9Ex variant | identical but for four device-specific helpers |
| `006a9fe0` | `IShaderConstantsFlush` | flushes a dirty register range, `shl 4` = 16 bytes per constant register |
| `00685b50` | `CGxDevice::IRsSync` | matches frozen line for line |
| `00685a70` / `006859e0` | `IRsForceUpdate`, the two overloads | one takes no argument and loops; the other takes a state and appends it |
| `006a43d0` | `IStateSyncLights` | gates on app state `0x108 / 0x18` = 11 = `GxRs_Lighting` |
| `006a4700` | `IStateSyncMaterial` | position, plus the `+0x28a8 & 0x10` gate it shares with Lights |
| `006a4850` | `IStateSyncXforms` | a dirty byte guards one `SetTransform` through the device vtable |

**All four were read the next cycle**, and the picture is better than it looked. They are not four
mystery functions: one of them is `IStateSyncEnables` under its own correct name, one is a function
frozen already implements, and two are features frozen does not have at all.

| reference | what it is | frozen |
|---|---|---|
| `006a3810` | `IStateSyncEnables` | **ported 2026-09-23** |
| `006a3870` | the clip-plane sync | no state for it |
| `006a38d0` | the scissor-rect sync | no state for it |
| `006a5700` | `IStateSyncVertexPtrs` | already implemented |

The two adjacent fields `006a3810` compares turned out to be `m_appMasterEnables` and
`m_hwMasterEnables`, at the same offsets frozen uses, so `IStateSyncEnables` was the right name all
along -- "enables" means the master enables, not D3D's enable render states. It sends exactly one of
the nine to the device, and that is not an omission: `MasterEnableSet` routes Lighting, Fog,
DepthTest, DepthWrite, ColorWrite and Culling through `IRsForceUpdate`, so they travel the ordinary
render-state path. `GxMasterEnable_PolygonFill` has no `GxRs` of its own, so it is the only one left
to send directly, as `SetRenderState(D3DRS_FILLMODE, solid or wireframe)`.

Clip planes and the scissor rect are real gaps but small ones. The reference keeps six 16-byte
planes behind a dirty mask and pushes them with `SetClipPlane`; frozen has `GxRs_ClipPlaneMask` and
a GL path that calls `glClipPlane`, but the D3D backend handles neither the mask nor the planes and
nothing stores them. The scissor setter `00682e70` has **two callers in the whole binary**, so it
is a minor feature on the reference side too. Adding either means adding the state and its public
setter, not just the sync function.

### 2026-09-23 - the graphics CVars: ten do nothing, and one does the wrong thing

`tools/audit-ported.py`, widened to check the whole map rather than only the overrides, found that
every one of the twelve `CWorldParam` graphics CVar callbacks is an empty body counted as ported.
Checking what frozen does with each CVar afterwards splits them cleanly, and the split is more
interesting than the count:

**Ten are read nowhere at all.** `extShadowQuality`, `specular`, `baseMip`, `textureCacheSize`,
`footstepBias`, `violenceLevel`, `skyCloudLOD`, `terrainAlphaBitDepth`, `hwPCF` and `bspcache`
appear in `CWorldParam.cpp` twice each, once as the static and once in the registration, and
nowhere else. The settings exist in the console and change nothing. That is the gap
`docs/ref/parity-shadows.md` lists as item 10, and it is wider than shadows.

**Two take effect, but not the way the reference makes them.** `DetailDoodadRender` reads
`groundEffectDist` and `groundEffectDensity` directly every frame, so those sliders work. The
reference does not poll them: its callbacks validate the value, store it in engine state, and raise
a rebuild flag.

The density one is a real behavioural difference, not just a structural one:

| | reference | frozen |
|---|---|---|
| accepted range | 16 to 256 | any, divided by 16 and clamped to [0, 1] |
| what 16 means | the minimum | the maximum |
| what it controls | how many doodads are **placed** (raises a scatter-rebuild flag) | what fraction of the built scatter is **drawn** |

Both agree exactly at the default of 16, which is why nothing looked wrong. Move the slider up and
the reference adds doodads while frozen does nothing. Closing it means moving density into
`BuildDetailDoodads` and rebuilding the scatter when it changes, which is visible and wants a run in
the same cycle.

`groundEffectDist` is narrower: the reference clamps to `[0, 140]` on the way in and caches the
square; frozen reads the raw CVar and squares it per frame. Defaults match at 70.0.

**Worth carrying forward:** "the callback is empty" and "the setting does nothing" are not the same
claim. Nine of these were committed under the second before the CVar reads were checked, and two of
those nine were wrong.

### 2026-09-23 - which render states actually reach D3D, and the one that did not

Comparing the `GxRsSet` calls across `src/` against the cases `CGxDeviceD3d::IRsSendToHw` handles
gives a short list of states frozen sets that never reach the device:

| state | set from | consequence |
|---|---|---|
| `GxRs_ColorWrite` | `CM2SceneRender::SetupMaterial` | **wired up here** |
| `GxRs_Lighting` | 16 places | none: the world always binds a vertex shader, and fixed-function lighting is bypassed |
| `GxRs_MatDiffuse`, `GxRs_MatEmissive`, `GxRs_MatSpecularExp` | `CShaderEffect`, `CM2SceneRender` | same reason -- these are fixed-function material state |
| `GxRs_ClipPlaneMask` | `CM2SceneRender` | frozen stores no clip planes either; see the state-sync entry above |

`GxRs_ColorWrite` was the real one. `Ds_ColorWriteEnable` existed in the device-state enum and
**neither** switch had a case for it, so `SetupMaterial`'s request to turn colour writes off was
dropped twice over.

**The bit order is the part that would not have survived a guess.** Gx and D3D both use four bits,
but the reference's handler (inside the D3D `IRsSendToHw` at `0x006a5038`) remaps the middle two:
Gx `0x2` becomes D3D's BLUE and Gx `0x4` becomes D3D's GREEN, so Gx orders them **R, B, G, A**
against D3D's R, G, B, A. That matches the BGRA byte order used for colours elsewhere in this
codebase. The reference also gates the whole thing on `GxMasterEnable_ColorWrite`, the same pairing
frozen's depth-write and culling cases already use.

**It is inert today, and that is worth knowing.** `SetupMaterial` only asks for colour writes off
when the element carries flag `0x1`, and frozen's element gather in `CM2Scene` sets `0x2` and
`0x4` and never `0x1`. Reading what that flag does in `SetupMaterial` -- alpha-key blending with
colour writes off -- makes it a **depth prepass for alpha-tested geometry** such as hair and
foliage. So frozen does not do that prepass at all, and the missing piece is the gather condition,
not this plumbing. The plumbing is now correct for when it lands.
