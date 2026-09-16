# Depth-buffer / z-fighting parity: 3.3.5a reference vs whoa

Program: **RunicWorldGame.exe** (Win 3.3.5a build 12340, stripped, image base 0x400000) in the
Ghidra project `C:\Users\tyler\tools\ghidra-projects\RunicWorld`. Raw dumps captured for this doc:
`win-strrefs-depth.txt`, `win-decomp-depth1.txt`, `win-decomp-depth2.txt`, `win-decomp-depth3.txt`,
`win-decomp-depthbias.txt`, `win-callers-rsdirty.txt`. Existing context reused rather than re-run:
`win-decomp-scene-leaves.txt`, `win-decomp-render-callees.txt`, `win-decomp-cmap-render2.txt`,
`win-decomp-chunkrender.txt`, `docs/ref/parity-shadows.md`, `docs/world-render-inventory.md`.

Everything marked *(uncertain)* is an inference; everything else was read in the decompile or in
the binary's data.

---

## 0. Executive summary

* The **render-state machinery is already at parity**. Whoa's `EGxRenderState` numbering, the
  depth-compare table, the cull table and the master-enable gating all match the reference
  bit-for-bit (verified against the reference's `IRsSendToHw` switch and its D3D lookup tables).
* The **depth buffer is D24S8 in both**. This is not a precision problem from the buffer format.
* The **near/far planes match** (`nearclip` 0.2, `farclip` 350 in both). With n=0.2/f=350 a D24
  buffer resolves ~0.7 mm at 50 yd and ~3 mm at 100 yd - far better than the errors below.
* `GxRs_PolygonOffset` really is a **no-op in every whoa backend**, but the reference only uses it
  for *decals* (footprints, post-liquid decals, projected-texture batches) and explicitly sets it to
  **0** for the M2 scene. It is therefore **not** the cause of model z-fighting. It is still a real
  gap, and the reference's exact mapping is recovered below.
* The actual parity break that produces z-fighting is **coordinate space**: the reference draws
  terrain chunks, WMO groups and ground doodads with a **per-chunk / per-instance world transform**
  over *local* vertex data, while whoa **bakes absolute world coordinates** (up to +-17066) into the
  vertex streams and folds the camera translation into a single matrix. The per-vertex `dp4` then
  cancels two ~10^4 magnitudes, leaving 2-8 mm of view-space error that *changes as the camera
  moves*. That is 3-10x the D24 depth quantum at close range, and it is why anything coplanar with
  the ground flickers.
* Compounding it, three world passes draw **the same or coplanar geometry through a different vertex
  shader** than the pass that wrote the depth (blob shadows, detail doodads, the fallback terrain
  overlays). Two different vertex programs on the same positions differ by ULPs - with
  `DepthFunc = LESSEQUAL` that is textbook z-fighting. `docs/ref/parity-shadows.md` already
  documents this for blob shadows and its conclusion is confirmed here.

---

## 1. Depth bias / polygon offset

### 1a. Whoa: declared, initialised, handled by nobody

| Where | What |
|---|---|
| `src/gx/Types.hpp:118` | `GxRs_PolygonOffset = 0` (first render state) |
| `src/gx/CGxDevice.cpp:639` | `m_appRenderStates[GxRs_PolygonOffset].m_value = 0` |
| `src/gx/d3d/CGxDeviceD3d.cpp:1059-1210` | `CGxDeviceD3d::IRsSendToHw` - switch has **no `case GxRs_PolygonOffset`** (cases present: BlendingMode, AlphaRef, DepthTest/DepthFunc, DepthWrite, Culling, ScissorTest, Fog, FogColor, FogStart, FogEnd, Texture0-15, VertexShader, PixelShader) |
| `src/gx/gll/CGxDeviceGLL.cpp:455-651` | same switch, **no `case GxRs_PolygonOffset`** |
| `src/gx/gles/CGxDeviceGLES.cpp` | no reference at all |
| `src/gx/gll/GLDevice.cpp:450` | `glPolygonOffset(states.rasterizer.slopeScaledDepthBias * 2.0, units)` exists in the *lower* GLL layer, but nothing ever writes `rasterizer.slopeScaledDepthBias` from the RS path |
| Writers in whoa | `src/model/CM2SceneRender.cpp:88` (`GxRsSet(GxRs_PolygonOffset, 0)`) and `src/console/Screen.cpp:106` (`0.0f`) - both write 0, so nothing is lost today |

### 1b. Reference: `D3DRS_DEPTHBIAS` only, negated, caps-gated

The render-state array lives at `device + 0x28f4` -> `base + index * 0x18`, value at `+0`; whoa's
enum numbering is confirmed identical by the offsets seen in the decompiles
(`6 -> +0x90` BlendingMode, `7 -> +0xa8` AlphaRef, `0xb -> +0x108` Lighting, `0xc -> +0x120` Fog,
`0xf -> +0x168` DepthWrite, `0x11 -> +0x198` Culling). `FUN_00685970(index)` is the inlined
"mark dirty" half of `GxRsSet`; `FUN_00408bf0(rs, int)` / `FUN_00763c70(rs, float)` are the
out-of-line setters.

The applier is **`FUN_006a4c30`** (`CGxDeviceD3d9Ex::IRsSendToHw`; an identical body also exists at
`FUN_006a8bd0`) - `win-decomp-depthbias.txt`:

```c
case 0:                                             // GxRs_PolygonOffset
  if (*(int *)(this + 0x304) != 0) {                // caps: D3DPRASTERCAPS_DEPTHBIAS
    d3dDevice->SetRenderState(0xc3 /* D3DRS_DEPTHBIAS */, -*polygonOffsetFloat);
    return;
  }
  break;                                            // no caps -> silently ignored
```

* **`D3DRS_DEPTHBIAS` (195) only. `D3DRS_SLOPESCALEDEPTHBIAS` (175) is never used** - a byte scan of
  `.text` finds `push 175` nowhere in the gx region. (This corrects the aside in
  `docs/ref/parity-shadows.md` P4, which suggested both.)
* The value is passed as the raw float bits, **negated**. D3D9 adds `D3DRS_DEPTHBIAS` to the
  post-projection depth in [0,1]; negating pulls the fragment *toward* the viewer. So
  `GxRs_PolygonOffset` is a **positive "pull toward camera" amount in normalised depth units**.
* Gated on the caps bit at `this + 0x304`, which whoa already computes as
  `m_caps.m_depthBias` (`src/gx/d3d/CGxDeviceD3d.cpp:1284`).

The same function confirms the rest of the depth path matches whoa exactly:

| RS | Reference | Whoa |
|---|---|---|
| `0xd`/`0xe` DepthTest/DepthFunc | if master-enable bit 4 clear **or** `appRs[DepthTest] == 0` -> `D3DRS_ZFUNC = 8` (`D3DCMP_ALWAYS`), else `DAT_00a2fa14[DepthFunc]` = `{4,3,7,2}` = `{LESSEQUAL, EQUAL, GREATEREQUAL, LESS}` | identical (`s_cmpFunc`, `CGxDeviceD3d.cpp:13-18, 1093-1108`) |
| `0xf` DepthWrite | master-enable bit 8 gate, then `D3DRS_ZWRITEENABLE` (14) | identical (`:1111-1120`) |
| `0x11` Culling | `DAT_00a2fa24` = `{1,2,3}` = `{NONE, CW, CCW}` | identical (`s_cullMode`) |

Note the reference never touches `D3DRS_ZENABLE`; the depth test is disabled by setting
`ZFUNC = ALWAYS`. Whoa does the same. Both rely on `EnableAutoDepthStencil` leaving
`ZENABLE = D3DZB_TRUE`.

### 1c. Which reference draws actually use it

Found by `FindCallers` on `FUN_00685970` (132 distinct callers, `win-callers-rsdirty.txt`) plus
grep for `FUN_00685970(0)` across every decompile in `docs/ref/`:

| Caller | Meaning | Value written |
|---|---|---|
| `FUN_006865b0` (`CGxDevice` RS defaults) | initial state | `0.0` |
| `FUN_00823130` (`CM2SceneRender::Draw`) | **the M2 scene explicitly zeroes it** | `0.0` (whoa mirrors this at `src/model/CM2SceneRender.cpp:88`) |
| `FUN_0079fcc0` (footprints, `Map.cpp`, CVar `showfootprints`) | footprint decals | `footstepBias * _DAT_00a3fd64` = `0.125 * 2^-8` = **4.883e-4** |
| `FUN_0079d5e0` (post-liquid decal list `DAT_00adfb60`) | ripple/splash decals | a local; its source was not traced *(uncertain)* |
| `FUN_007e3aa0` (projected-texture decal draw, `Shadow.cpp`; called from `FUN_007e3e80` @ `007e3e63`) | projected decals, when its bias arg != 0 | saves the old value, sets `(bias + bias) * _DAT_00a41170` = `2 * bias * 2^-15`, restores afterwards |
| `FUN_007d5610` (terrain debug overlay, gated on `DAT_00cf493c`) | debug wireframe | `1.0` |

CVar defaults read straight out of the PE: `footstepBias` = `"0.125"`
("Unit footstep depth bias", registered in `FUN_0078e400`), `_DAT_00a3fd64` = `0.00390631` (2^-8),
`_DAT_00a41170` = `3.05180e-05` (2^-15).

**Conclusion for item 1:** in the reference, polygon offset is a decal-only feature. Models, terrain,
WMOs and liquids all draw with offset 0. Whoa draws none of those decals yet (no footprints, no
post-liquid decals, no projected-texture batches - `CM2SceneRender::DrawBatchProj` is a TODO at
`src/model/CM2SceneRender.cpp:262` and `CM2Scene::uint104` is permanently 0, so element type 1 is
never produced). **The missing backend cannot be causing model z-fighting.** It should still be
implemented (task G1), because it is the tool the reference reaches for as soon as those decal
passes land.

Also note the blob-shadow path (`parity-shadows.md`): the reference solves *its* coplanarity with
`DepthFunc = EQUAL` + `DepthWrite = 0` and **no** depth bias. Do not paper over that one with bias.

---

## 2. Projection near / far

### 2a. Reference

`FUN_0078e400` (WorldParam.cpp CVar registration) - default strings read out of the PE:

| CVar | Default | Notes |
|---|---|---|
| `nearclip` | **`"0.2"`** (`DAT_00a3f6dc`) | callback `FUN_0078d7a0` |
| `farclip` | **`"350"`** (`DAT_00a3f6f8`) | callback `FUN_0078d780`; blended per-area in `CWorld::Update` |
| `farClipOverride` | `"0"` | raises the clamp ceiling |
| `horizonNearclipScale` | `"0.7"` (`DAT_00a3f0a4`) | sky/horizon pass only |
| `horizonFarclipScale` | `"4.0"` (`DAT_00a1e228`) | sky/horizon pass only |

The sky pass additionally renders through a viewport with the depth range squeezed to
`[DAT_00adeef0, DAT_00adeef4]` = **`[0.9990234375, 1.0]`** (verified in the binary; exactly
`1 - 2^-10`).

### 2b. Whoa

| Item | Value | Where |
|---|---|---|
| `nearclip` CVar | `"0.2"` | `src/world/CWorldParam.cpp:182-191` |
| `farclip` CVar | `"350"` | `src/world/CWorldParam.cpp:170-180` |
| `CWorld::s_nearClip` | `0.1f` at static init, **`0.2f`** after `LoadMap`/`SetFarClip` | `src/world/CWorld.cpp:27, 532, 568` |
| `CWorld::s_farClip` | `AdjustFarClip(cvar, mapID)`, clamped to `[183.33, 791.67]` (or 1583.33 with `farClipOverride`) | `src/world/CWorld.cpp:390-403, 531` |
| Projection | `GxuXformCreateProjection_Exact(FOV*0.6, aspect, m_nearZ, m_farZ)` | `src/ui/simple/CSimpleCamera.cpp:117` |
| Remap to D3D [0,1] z | `CGxDeviceD3d::IXformSetProjection` | `src/gx/d3d/CGxDeviceD3d.cpp:1963-1997` |
| Sky viewport | `SKY_VIEWPORT_MIN_Z = 0.9990234375f`, `MAX_Z = 1.0f` | `src/world/Terrain.cpp:574-575, 4513` |

**The near and far planes are at parity.** The sky depth range is bit-identical to the reference's.

### 2c. Precision arithmetic (so this can be ruled in or out for good)

For the standard projection, the world-space depth resolution at view distance `z` is

```
dz  =  z^2 * (f - n) / (n * f) / 2^bits
```

With n = 0.2 the factor `(f-n)/(n*f)` is **4.997** at f=350 and **4.999** at f=727 - i.e. the far
plane is irrelevant once `n << f`; only the near plane matters.

| z (yd) | D24, n=0.2 | D24, n=0.5 | D16, n=0.2 |
|---|---|---|---|
| 10 | 0.03 mm | 0.012 mm | 7.6 mm |
| 50 | 0.74 mm | 0.30 mm | 0.19 yd |
| 100 | 3.0 mm | 1.2 mm | 0.76 yd |
| 300 | 27 mm | 11 mm | 6.8 yd |

So a D24 buffer at n=0.2 is comfortable and matches the reference exactly. **A too-small near plane
is not the cause here.** (If whoa were falling back to D16 it would be catastrophic - item 4 rules
that out.)

### 2d. Two real whoa bugs found in this area (neither is the main cause)

1. **`CGCamera` latches near/far at construction.**
   `src/ui/game/CGCamera.cpp:90`:
   `CGCamera::CGCamera() : CSimpleCamera(CWorld::GetNearClip(), CWorld::GetFarClip(), 90 * DEG2RAD)`.
   Nothing ever calls `SetNearZ`/`SetFarZ` afterwards (`grep SetFarZ src/` -> only the definition in
   `src/ui/simple/CSimpleCamera.cpp:103`). Consequences:
   * Changing the `farclip` CVar at runtime does nothing to the projection (only to fog and
     culling), so the camera and `CWorld::s_farClip` drift apart.
   * On a map change (`SMSG_NEW_WORLD` -> `src/client/ClientHandlers.cpp:27` -> `CWorld::LoadMap`)
     the existing `CGWorldFrame`'s camera keeps the previous map's far clip.
   * If a `CGWorldFrame` is ever constructed before the first `LoadMap`, it gets
     `near = 0.1, far = 0.0` - `GxuXformCreateProjection_Exact` asserts `minZ < maxZ` and produces a
     degenerate matrix. Today the login path happens to run `LoadMap` (`src/client/Client.cpp:169`)
     before the world UI loads, so this is latent rather than live *(uncertain - the FrameXML
     ordering was not traced)*.
2. `gxDepthBits` is registered from `s_defaults.format` (`src/console/Device.cpp:103-104, 159-171`),
   which is zero-initialised -> `Fmt_Rgb565` -> the CVar's *registered default* is `"16"`. It is then
   corrected to `"24"` by `SetGxCVars(s_requestedFormat)` at `:409`. Cosmetic only, but it makes the
   CVar a misleading diagnostic.

---

## 3. Depth-state audit of whoa's world passes

All of these run inside `CGWorldFrame::OnWorldRender` (`src/ui/game/CGWorldFrame.cpp:150-350`).
`GxRs_DepthFunc = 0` is `LESSEQUAL` everywhere.

| Pass | File:line | Test | Func | Write | Verdict |
|---|---|---|---|---|---|
| `GxSceneClear(0x3, ...)` | `CGWorldFrame.cpp:171` | - | - | - | correct: `D3DCLEAR_TARGET\|D3DCLEAR_ZBUFFER`, Z=1.0 (`CGxDeviceD3d.cpp:1024-1037`) |
| `RenderShaded` (terrain, D3D9) | `Terrain.cpp:2115-2117` | 1 | LE | 1 | correct; own `s_terrainVS` |
| `RenderFallback` (terrain, non-D3D) | `Terrain.cpp:2189-2226` | 1 | LE | 1 base / 0 overlays | **draws the same 145 verts up to 4x with LEQUAL**. Safe only because all four passes use the same shader and the same vertex data, so depth is bit-identical. Fragile by construction; the reference layers inside one shader. |
| `RenderWmos` opaque | `Terrain.cpp:2518-2547` | 1 | LE | 1 | ok. Positions are **baked to world space** - see 3a |
| `RenderWmos` blended | `Terrain.cpp:2677` | 1 | LE | 0 | ok (sorted back-to-front); leaves `DepthWrite = 0` on exit, but `TerrainRender`'s `GxRsPop` restores it |
| `LiquidRender(0/1)` | `Terrain.cpp:4123-4125` | 1 | LE | opaque?1:0 | ok |
| `SkyRender` | `Terrain.cpp:4516-4518` | 1 | LE | 0 | **correct and safe.** Viewport z-range `[0.999,1.0]` is set at `:4513` and restored at `:4656`; `GxRsPush/Pop` brackets the state and depth write is off, so the squeezed range never pollutes the buffer for later passes. No bad interaction with anything else was found. |
| `BlobShadowsBegin/Draw/End` | `Terrain.cpp:4283-4285, 4299-4352` | 1 | LE | 0 | **BROKEN - redraws terrain chunk triangles through `s_uiVertexShader[0]` while `RenderShaded` wrote the depth with `s_terrainVS`.** Different vertex programs -> ULP-different depth -> LEQUAL accepts/rejects per pixel. Fully analysed in `docs/ref/parity-shadows.md` (its T1). |
| `DetailDoodadRender` | `Terrain.cpp:3537-3539` | 1 | LE | 1 | **suspect.** Vertices sit at exactly `ChunkHeightAt(...)` (`Terrain.cpp:773, 3501`), i.e. coplanar with the terrain surface, and are drawn with `s_uiVertexShader[0]` + `s_viewProjT` while the terrain used `s_terrainVS`. Same ULP failure class as blob shadows, on a pass that covers the ground everywhere within `groundEffectDist`. |
| `WeatherRender` | `Terrain.cpp:3177-3179` | 1 | LE | 0 | ok |
| `ParticleFxRender` | `ParticleFx.cpp:433-436` | 1 | LE | 0 | ok |
| `UnderwaterOverlayRender` | `Terrain.cpp:4229-4230` | 0 | - | 0 | ok |
| M2 `Draw(M2PASS_0/2/1)` | `CM2SceneRender.cpp:87, 403-410` | material flag `0x8` | LE (set once at `:87`) | material flag `0x10` | **matches the reference exactly** (`SetupMaterial`: `depthTest = !(flags & 0x8)`, `depthWrite = !(flags & 0x10)`) |

### 3a. The coordinate-space break (the substantive finding)

**Reference.** `FUN_007d0050` (per-chunk draw setup, `win-decomp-chunkrender.txt`) calls
`FUN_00790440(chunk + 0x24)` -> `FUN_00834900(0, chunk + 0x24)` before every chunk, i.e. it installs
a **per-chunk transform** and the chunk's vertex data is chunk-local. This is consistent with the
ADT format itself: MCVT heights are stored relative to the MCNK's `position` (`+0x68..0x70`), so the
natural vertex range is 0..33.33 yd. WMOs are likewise drawn with a per-`CMapObjDef` placement
matrix (`FUN_007964a0` -> `FUN_007a8320` / `FUN_00790440` / `FUN_0081e400`), and detail doodads with
a per-chunk transform (`FUN_007984a0` -> `FUN_00790440` + `FUN_007b10e0`, 0x17 VS constants). *(The
exact semantics of `FUN_00834900` were not confirmed - marked uncertain - but the per-chunk /
per-instance transform call pattern is unambiguous.)*

**Whoa.** Everything is pre-baked into absolute world coordinates and drawn with one global matrix:

* `Terrain.cpp:831-833` - `chunk.position[k] = { posX - fx*UNIT_SIZE, posY - fy*UNIT_SIZE, posZ + heights[k] }`, absolute, up to +-17066.
* `Terrain.cpp:3919-3935` - `view = GxXformView(); view.Translate(-cameraPos); viewProjT = (view*proj)^T`, uploaded to `c0..c3`.
* `RenderWmos` - group geometry "baked to world space" (`docs/world-render-inventory.md`), same matrix.
* `BuildDetailBatch` (`Terrain.cpp:3501`) - absolute world positions, same matrix.

The terrain VS (`src/world/TerrainShadersD3d9.hpp`) is `mad r0, v0.xyzx, c4.xxxy, c4.yyyx`
(= `(x,y,z,1)`) then four `dp4 oPos.{x,y,z,w}, r0, c{0,1,2,3}`. Each `dp4` sums
`x*m0 + y*m1 + z*m2 + m3`, where `m3` carries `-cameraPos` projected - so two terms of magnitude
~10^4 cancel down to a view-space value of ~10^1. Float32 ULP at |world| = 9000 is 9.8e-4 yd and at
17066 it is 2.0e-3 yd; a four-term `dp4` accumulates a few ULPs, so **oPos.z / oPos.w carry roughly
2-8 mm of error, and the residue changes every time the camera moves**.

Compare that with the D24 quantum from 2c: 0.74 mm at 50 yd, 3.0 mm at 100 yd. The cancellation
error is **3-10x the depth quantum at the distances where z-fighting is most visible**, and it is
view-dependent, i.e. it flickers.

M2 models do *not* suffer this per-vertex: `CM2Scene::Animate` builds
`m_view = GxXformView(); m_view.Translate(-cameraPos)` (`src/model/CM2Scene.cpp:389-392`), so the
cancellation happens **once per model, on the CPU**, inside the model's matrix; the per-vertex maths
then runs on model-local coordinates. `CM2Model::SetWorldTransform` (`src/model/CM2Model.cpp:2151`)
stores the absolute world position, and `CM2SceneRender::Draw` sets view and world to identity
(`CM2SceneRender.cpp:83-84`) because the scene has already folded the camera in.

**Net effect:** model geometry is accurate to ~1 mm and rigid; terrain / WMO / detail-doodad geometry
wobbles by several millimetres per vertex as the camera moves. Anything sitting *on* the ground - a
model's feet, a detail doodad's base, a blob shadow, a liquid surface at ground level - therefore
intersects inconsistently from frame to frame.

### 3b. Things explicitly checked and found clean

* No pass draws the same geometry twice at equal depth except `RenderFallback` (non-D3D only) and
  `BlobShadowDraw` (intentional, see above).
* `CM2Scene::Draw` uses per-pass index lists `array54[pass]` (`src/model/CM2Scene.cpp:694-711`), so
  no batch is drawn in two passes. The one path that *could* double-list an element
  (`CM2Scene.cpp:625-641`: `array54[1]` **and** `array54[2]` when lighting flags `0x20` and `0x40`
  are both set - the reference's above-water / below-water clip-plane split) cannot fire today:
  `CM2Lighting` sets `0x20` unconditionally (`src/model/CM2Lighting.cpp:74`) and **never sets
  `0x40`**, so `array54[2]` is always empty and `Draw(M2PASS_2)` is a no-op. Worth remembering: the
  day the liquid clip planes land (`CM2SceneRender.cpp:361` `// TODO enable clip plane mask`), this
  *will* start drawing the same transparent batch twice with no clip plane to separate the halves.
* The depth-compare, cull and master-enable tables match the reference exactly (1b);
  `m_appMasterEnables` is initialised to 511 (`src/gx/CGxDevice.cpp:459`), so nothing is forcing
  `ZFUNC = ALWAYS` or `ZWRITEENABLE = 0` behind the app's back.
* `GxSceneClear` clears Z to 1.0 with the correct flag mapping.
* The sky's `[0.999, 1.0]` viewport is saved and restored and writes no depth; it cannot affect any
  other pass.

---

## 4. Depth buffer format

**Whoa requests D24S8 and gets it.**

* `src/console/Device.cpp:395-396` - `s_requestedFormat.colorFormat = Fmt_Argb8888;
  s_requestedFormat.depthFormat = CGxFormat::Fmt_Ds248;`
* `src/gx/d3d/CGxDeviceD3d.cpp:1349-1350` - `ISetPresentParms` `memset`s the whole
  `D3DPRESENT_PARAMETERS` to 0 first, so no stale fields.
* `src/gx/d3d/CGxDeviceD3d.cpp:1386-1387` -
  `d3dpp.EnableAutoDepthStencil = true; d3dpp.AutoDepthStencilFormat = s_GxFormatToD3dFormat[format.depthFormat];`
* `src/gx/d3d/CGxDeviceD3d.cpp:114-123` - `s_GxFormatToD3dFormat[Fmt_Ds248] = D3DFMT_D24S8`.
* `multisampleCount` is 0 (zero-initialised static), so the `<= 1` branch is taken and
  `MultiSampleType` stays `D3DMULTISAMPLE_NONE`.

Reference: `gxDepthBits` is `"24"` in `Config.wtf`; the CVar is read in `FUN_0076a630`,
`FUN_0054f1b0`, `FUN_0054f980` and `FUN_0054f8b0` (`win-strrefs-depth.txt`), and the D3D device code
lives at `FUN_00689ef0`+ (`.\CGxDeviceD3d\CGxDeviceD3d.cpp`) / `FUN_006a0aa0`+
(`.\CGxDeviceD3d9Ex\...`). The reference's format table is the same shape as whoa's (whoa's gx is a
decompilation of it), so both land on `D3DFMT_D24S8`. *(The reference's exact
`AutoDepthStencilFormat` line was not decompiled - marked uncertain - but the CVar value and the
shared table make it certain enough.)*

**Caveat worth guarding:** a zero-initialised `CGxFormat` has `depthFormat == Fmt_Rgb565 == 0`
(`src/gx/CGxFormat.hpp:11-19`), which maps to `D3DFMT_R5G6B5` as an `AutoDepthStencilFormat` - an
invalid depth format that would fail `CreateDevice`. Any future path that builds a `CGxFormat`
without explicitly setting `depthFormat` (the Windows adapter enumeration in
`src/gx/CGxDevice.cpp:106-160` never sets it; only the Android and Mac branches do) will hit that.
See task C5.

---

## 5. Ranked causes of the observed model z-fighting

1. **Detail doodads coplanar with the terrain, drawn through a different vertex shader.**
   `DetailDoodadRender` (`Terrain.cpp:3523-3615`) draws grass/rock props whose base vertices are
   placed at exactly the terrain height (`Terrain.cpp:773, 3501`) using `s_uiVertexShader[0]`, while
   the terrain under them was drawn with `s_terrainVS`. `DepthWrite = 1`, `DepthFunc = LESSEQUAL`.
   Two different vertex programs on the same world position give ULP-different depth, so the base
   ring of every ground doodad fights with the ground. This is the pass that most literally looks
   like "z-fighting on models", it is on-screen everywhere within `groundEffectDist` (70 yd), and it
   is recent (added 2026-09-14).
2. **Absolute world coordinates in the terrain / WMO / doodad vertex streams (section 3a).**
   2-8 mm of view-dependent depth error against models that are accurate to ~1 mm, i.e. 3-10x the
   D24 quantum at 50-100 yd. Makes every ground-contact surface unstable and amplifies cause 1.
   This is the deepest parity break: the reference draws chunk-local and WMO-local geometry with a
   per-chunk / per-instance transform.
3. **Blob shadows redrawing terrain through the UI shader with `LESSEQUAL`.**
   Already diagnosed in `docs/ref/parity-shadows.md`; reproduced independently here. Appears as a
   speckled patch directly under every unit, which reads as "the model is z-fighting".
4. **`CGCamera` latching near/far at construction** (2d). Not a fighting cause by itself, but it can
   silently leave the projection out of sync with `CWorld::s_farClip` after a map change, and it
   makes any near-plane tuning ineffective.
5. **M2 pass 1/2 double-listing, latent** (3b). Harmless today; becomes a guaranteed same-geometry
   double draw the moment `CM2Lighting` starts setting flag `0x40`.
6. **`GxRs_PolygonOffset` unimplemented.** Cannot be the cause (the reference zeroes it for models,
   and whoa draws none of the decals that use it), but it is a real gap that blocks footprints,
   post-liquid decals and projected-texture batches.

---

## 6. Task list

### Cheap correct fixes (no gx feature needed)

* **C1 - `DetailDoodadRender`: use the same vertex program as the terrain.**
  `src/world/Terrain.cpp:3523-3615`. Either (a) draw the detail batches with `s_terrainVS` plus a
  pixel shader that ignores the alpha map, or (b) lift the doodad bases off the ground by a small
  constant in `BuildDetailBatch` (`Terrain.cpp:3501`, add ~0.02 yd to `lz`). (a) is the parity fix;
  (b) is a one-line stopgap. Do **not** reach for depth bias here - the reference does not.
* **C2 - Make terrain vertex data chunk-local (parity with `FUN_007d0050`).**
  `src/world/Terrain.cpp:831-833` (`ParseChunk`): store `chunk.position` relative to the chunk origin
  (`posX/posY/posZ`), keep the absolute origin in the chunk struct, and in `RenderShaded` /
  `RenderFallback` / `BlobShadowDraw` / `DetailDoodadRender` upload
  `translate(chunkOrigin - cameraPos) * viewProj` per chunk instead of one global `s_viewProjT`.
  Everything that reads `chunk.position` absolutely must be updated:
  `ChunkHeightAt` (`:642-668`), `BuildDetailDoodads` (`:770-771`), bounds (`:879-884`),
  `ParseLiquid` (`:2744-2745`), `LiquidAt` (`:2876-2877`), `BuildDetailBatch` (`:3382`).
  This is the single change with the largest depth-stability payoff.
* **C3 - Same treatment for WMOs.** Keep `WmoGroup` positions group-local and upload the placement
  matrix per instance (reference `FUN_007964a0` -> `FUN_007a8320`), instead of baking to world space
  in `LoadWmoInstance` (`src/world/Terrain.cpp:1128+`).
* **C4 - Stop latching the camera's clip planes.**
  `src/ui/game/CGCamera.cpp:90` and `:203` (`SetupWorldProjection`): read `CWorld::GetNearClip()` /
  `CWorld::GetFarClip()` every frame, or have `CWorld::SetFarClip` push into
  `CGWorldFrame::s_currentWorldFrame->m_camera->SetFarZ`. Also guard
  `GxuXformCreateProjection_Exact` against `farZ <= nearZ`.
* **C5 - Make an unset depth format impossible.** `src/gx/CGxFormat.hpp` / `src/gx/CGxDevice.cpp:106`:
  either reorder the enum so 0 is not a colour format, or set `format.depthFormat = Fmt_Ds248` in the
  Windows adapter-format enumeration the way the Android and Mac branches already do. Cheap insurance
  against silently dropping to an invalid (or, on some future fallback, 16-bit) depth buffer.
* **C6 - Fix the `gxDepthBits` registration default.** `src/console/Device.cpp:103-104` -
  `s_defaults.format` is zero, so the CVar registers as `"16"` before `SetGxCVars` corrects it.
  Reporting-only, but it is the first thing anyone will check when chasing depth problems.
* **C7 - Blob shadows.** Follow `docs/ref/parity-shadows.md` T1 (identical vertex transform +
  `DepthFunc = EQUAL` + `DepthWrite = 0`). Listed here only for completeness; that doc owns it.
* **C8 - Guard the latent M2 pass-1/2 double-list.** `src/model/CM2Scene.cpp:625-641`: when the
  liquid clip planes are eventually implemented, the `v21 && v22` case must set the complementary
  clip-plane masks in `CM2SceneRender::SetupMaterial` (`src/model/CM2SceneRender.cpp:355-364`).
  Until then, add a comment or an assert so nobody sets `CM2Lighting` flag `0x40` without it.

### Needs a gx feature

* **G1 - Implement `GxRs_PolygonOffset` in the D3D backend.**
  `src/gx/d3d/CGxDeviceD3d.cpp`, `CGxDeviceD3d::IRsSendToHw` (`:1059`), add as the first case:

  ```cpp
  case GxRs_PolygonOffset: {
      if (this->m_caps.m_depthBias) {                  // already computed at :1284
          float offset = static_cast<float>(state->m_value);
          float bias = -offset;                        // the reference negates
          this->m_d3dDevice->SetRenderState(D3DRS_DEPTHBIAS, *reinterpret_cast<DWORD*>(&bias));
      }
      break;
  }
  ```

  Exactly one render state, negated, caps-gated. **Do not add `D3DRS_SLOPESCALEDEPTHBIAS`** - the
  reference never sets it (byte scan of `.text`).
* **G2 - Implement `GxRs_PolygonOffset` in `CGxDeviceGLL`.** `src/gx/gll/CGxDeviceGLL.cpp` RS switch
  (~`:455-651`): map the value onto `rasterizer.depthBias` / `slopeScaledDepthBias`, which
  `GLDevice.cpp:430-450` already consumes. GL's sign convention is the opposite of D3D's, so the
  value goes in un-negated; the units differ too (GL's `units` is in depth-buffer LSBs, D3D's
  `DEPTHBIAS` is normalised), so scale by `2^24` for a 24-bit buffer.
* **G3 - Decal passes that will need G1.** Footprints (`FUN_0079fcc0`, offset `footstepBias * 2^-8`),
  post-liquid decals (`FUN_0079d5e0`), projected-texture M2 batches
  (`CM2SceneRender::DrawBatchProj`, `src/model/CM2SceneRender.cpp:262`, plus `CM2Scene::uint104`
  which is currently hard-zero). Each is its own port; none is on the critical path for the reported
  z-fighting.
