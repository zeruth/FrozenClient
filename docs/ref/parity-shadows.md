# Shadow rendering parity: 3.3.5a reference vs frozen

Scope: unit/doodad **blob shadows** (Shadow.cpp region, ~0x7e2-0x7e4) and the **map shadow map**
(MapShadow.cpp ~0x7bb + CShadowCache ~0x874-0x875). Program: `RunicWorldGame.exe` (Win 3.3.5a 12340).
Raw decompiles captured for this doc: `win-decomp-shadow-blob.txt`, `-blob2.txt`, `-blob3.txt`,
`-blob4.txt`. Earlier context: `win-decomp-cmap-render.txt`, `-cmap-render2.txt`, `win-decomp-leafdraws.txt`.

Every address below was decompiled and read. Inferences are marked *(uncertain)*.

---

## 1. How the reference does it

### 1a. Blob shadows are a projected-texture pass over re-drawn receiver geometry

Not a decal mesh, not a quad. The reference collects the triangles of everything that can receive the
shadow, copies them into a dynamic vertex buffer, and re-draws them with a projective texture and
**depth compare EQUAL**. The pipeline:

```
CMap::Render FUN_0079a870  step 11
  -> FUN_00793980            (WorldScene.cpp:0xe35) walk the scene-entity list
       -> FUN_0082ced0       current animation bounding box  (non-unit path)
       -> FUN_0071ed80       unit shadow box, floor-clamped   (unit path)
       -> FUN_007e49e0       gate
            -> FUN_007e4480  build projector + set render states   <-- the interesting one
                 -> FUN_007e4370  set GxXform_Tex0/Tex1 from the projector
                      -> FUN_007e2d60  build the two texture matrices
                      -> FUN_007e3e80  gather receivers, re-draw them
                           -> FUN_007e35f0  gather
                                -> FUN_0077f350 -> FUN_007a2aa0  up to 10 M2 receivers (DAT_00d38014[])
                                -> FUN_0077f340 -> FUN_007a6af0  world geometry batches (DAT_00cd8080[], stride 9 dwords)
                           -> FUN_007e32f0 / FUN_007e2fd0  fill a dynamic VB with the receiver triangles
                           -> FUN_007e3580                 trivial 0,1,2,... index buffer, draw
                           -> FUN_00829aa0                 M2 receivers: draw all opaque batches (== DrawBatchProj)
```

**Per-entity selection (FUN_00793980).** For each scene entity with a model (`entity+0x34`):

* entity flag `+0x7c & 0x800` must be set to cast a shadow; `+0x7c & 4` skips the entity.
* `FUN_004d4db0(entity+0x98, entity+0x9c, 8, ".\WorldScene.cpp", 0xe35)` resolves the entity GUID
  with type mask 8 *(inferred: TYPEMASK_UNIT)*.
  * resolved (a unit): footprint box from `FUN_0071ed80` - the unit model box with its Z re-based
    onto the floor/transport it is standing on - gated by `*(char*)(unit[0x34]+0x110) != 9`
    *(uncertain what 9 is)*; then `FUN_007e49e0`. Afterwards the unit vtable `+0x74` is called
    unconditionally *(uncertain; some per-unit render extra)*.
  * unresolved (a doodad / non-unit M2): `FUN_0052e570` clears a scratch struct, `FUN_0082ced0` fills
    a 60-byte block whose dwords [3..8] are the **current animation** bounding box (read out of the
    M2 sequence-bounds table at `m2data+0x150`, stride 0x40, box at `+0x20`), and that box is passed
    to `FUN_007e49e0`.
* So **doodads cast blob shadows too**, and the footprint is the *animated* box, not a fixed radius.

**Gate (FUN_007e49e0).** All of: model non-null; `FUN_00824fc0` textures resident; `model+0x10 & 0x4000`
clear; the box is non-degenerate (`FUN_0070bd20` returns 0); `*(int*)(DAT_00d38048+0x30) < 1`, i.e.
CVar **`extShadowQuality` < 1**; and `DAT_00af3e08 == 1`, i.e. CVar **`shadowLOD` == 1**.
Blob shadows are the *low* quality tier - at `extShadowQuality >= 1` the engine uses the per-unit
shadow-map path instead.

**Projector construction (FUN_007e4480).** Inputs: the 6-float box, the model, and a float
(`_DAT_009f98d8`, 0 in the blob path).

* half extents `hx = (maxX-minX)*0.5`, `hy = (maxY-minY)*0.5`; reject if either is below epsilon
  (`_DAT_009ea27c`) or if the Z extent is degenerate.
* copies the model 4x4 world matrix from `model+0xb4`, un-scales it (`FUN_004c51b0` load,
  `FUN_004c52e0` by `1/len(row0)` when the scale is not 1 - `FUN_00482890` is the float compare).
* transforms the four corners `(+-hx, +-hy, 0)` by the model 3x3 (`FUN_005fed20` x4), takes their AABB
  (`FUN_00984930`), and extends Z by `-DAT_00af3e14 * halfHeight` / `+DAT_00af3e18 * halfHeight`.
  **The projection axis is the model own local Z, not the sun direction** - blob shadows do not
  track the light.
* builds the projector matrix by concatenating scale `(1/|2hy|, 1/|2hx|, 1)`, the model 3x3 rotation
  (`FUN_004c1d40`) and the box fit (`FUN_004c5340` x2, `FUN_004c1d80`), so the quad is **oriented by
  the model facing**.
* colour = `0x00FFFFFF | (alpha<<24)` where `alpha = round(clamp(model[0x178],0,1) * 255 + 0.5)` - a
  white diffuse whose **alpha carries the model shadow opacity**.

**Render states (FUN_007e4480; `FUN_00408bf0` = GxRsSet(int), `FUN_00409670`/`FUN_00685fb0` = GxRsPush/Pop,
`FUN_00685f50` = GxRsSet(texture), `FUN_00408c30` = set AlphaRef to the default for the current blend mode).**

| state | id | value | meaning in frozen enum |
|---|---|---|---|
| GxRs_BlendingMode | 6 | **4** | `GxBlend_Mod` - src=ZERO, dst=SRCCOLOR, i.e. `dest *= shadowColor` |
| GxRs_AlphaRef | 7 | default-for-blend | `FUN_00408c30(7)` |
| GxRs_Lighting | 11 | 0 | off |
| GxRs_Fog | 12 | 0 | **off** |
| GxRs_DepthWrite | 15 | 0 | off |
| GxRs_FogColor | 10 | 0xffffffff | white (a no-op under modulate) - `FUN_0058eb20(10, -1)` |
| GxRs_Texture0 | 0x15 | `Textures\ShadowBlob.blp` (`DAT_00d38044`) | |
| GxRs_ColorOp0 | 0x25 | 5 | fixed-function combiner *(exact op semantics uncertain)* |
| GxRs_AlphaOp0 | 0x2d | 3 | *(uncertain)* |
| GxRs_DepthFunc | 0x0e | **1** | per frozen `CGxDeviceD3d::s_cmpFunc` = `{LESSEQUAL, EQUAL, GREATEREQUAL, LESS}` -> **D3DCMP_EQUAL** |

**There is no `GxRs_PolygonOffset` (state 0) anywhere in this path, and no depth bias of any kind.**
Coplanarity is handled by `DepthFunc = EQUAL` + `DepthWrite = 0`: only fragments whose depth exactly
matches what the base pass already wrote are shaded. That is only sound because the shadow pass feeds
the rasteriser the *same world-space vertices* through the *same transform* as the pass that wrote
the depth (`FUN_007e32f0` copies the receiver vertices verbatim; `FUN_0057c450(8, m)` sets
`GxXform_World` to the receiver own matrix). This is the mechanism frozen is missing.

Only the `DepthFunc = 1` line is conditional (`if (param_3 == 0.0)`) - the ground-marker caller passes a
non-zero value and therefore keeps the default LESSEQUAL.

**Second texture stage = a distance fade (FUN_007e3e80).** Stage 1 (`0x16` = `GxRs_Texture1`) is bound to
one of two 64x8 procedural ramps created in ShadowInit:

* `"ShadowAdd"` (`DAT_00d38040`, generator `FUN_007e36e0`) - RGB white, A = trapezoid ramp (0->1, flat,
  1->0 along u). Used when the device is on the fixed-function path, with `ColorOp1 = 0`, `AlphaOp1 = 0`.
* `"ShadowMod"` (`DAT_00d3803c`, generator `FUN_007e3820`) - grey `1-ramp` in RGB. Used when the pixel
  shader class is 4 (`*(int*)(DAT_00c5df88+0x28f4)+0x90 == 4`), with `ColorOp1`/`AlphaOp1` = 2.

`GxRs_TexGen0`/`TexGen1` (0x35/0x36) are set to 2 and states 0x3d/0x3e to 1, i.e. hardware texgen from
position through the two `GxXform_Tex0`/`Tex1` matrices set by `FUN_00616a30` in `FUN_007e4370`.
Stage 0 gives the blob UV; stage 1 gives the fade coordinate along the projection axis.

**Receiver geometry re-draw (FUN_007e32f0 / FUN_007e2fd0 / FUN_007e3580).**

* Allocates a dynamic VB via `FUN_00684850(0, stride, triCount)`; stride 0x10 (pos + packed colour) or
  0x18 (pos + 3 floats).
* **Un-indexes** the receiver: each triangle 3 vertices are copied out through the receiver index
  buffer, then `FUN_007e3580` writes a throwaway `0,1,2,...` index buffer.
* **CPU backface cull in XY**: a triangle is kept only when
  `(p2-p1).x*(p3-p1).y - (p3-p1).x*(p2-p1).y >= 0`, i.e. only **up-facing** triangles receive the
  shadow - ceilings and undersides never do. Skipped when the caller passes bit `4` in the flags.
* If the receiver batch has an alternate height array (`+0xc != 0`), each vertex **Z is replaced**
  from it *(uncertain: a flattened/low-detail shadow receiver height)*.
* Vertex colour = the projector colour; for the 0x18 stride form, three floats from
  `DAT_00af4644/48/4c`.
* The world matrix per batch is `receiverMatrix * translate(-cameraOffset)` (`FUN_004f6650` gives the
  world-rebase offset, `FUN_004c1f00` multiplies, `FUN_0057c3a0(8)`/`FUN_0057c450(8, m)` push/set
  `GxXform_World`).

**M2 receivers.** The second loop of `FUN_007e3e80` walks `(&DAT_00d38014)[0 .. DAT_00d38054)` (max 10,
gathered by `FUN_007a2aa0`), pushes each model world matrix onto the device matrix stack, sets the
diffuse colour, and calls `FUN_00829aa0` - which iterates the model render batches (skipping ones with
`+0xc != 0` or flag `0x20`) and issues a triangles draw per batch with the projector states still bound.
**`FUN_00829aa0` is exactly the reference for the `CM2SceneRender::DrawBatchProj` stub.**

**ShadowInit (FUN_007e4a40).** Creates `Textures\ShadowBlob.blp` (`FUN_004b9760`, filter 8), the two
64x8 ramps above, registers CVar `shadowLOD` ("Unit shadow LOD", handler `FUN_007e3a20`, values 0/1 only)
into `DAT_00af3e08`, and looks up CVar `extShadowQuality` into `DAT_00d38048`.

**Shared with the ground marker.** `FUN_004f8a40(0x200122)` / `(0x20000)` uses the same
`FUN_007e4370` projector helper. Only the low byte of the flags reaches the draw
(`& 2` -> set diffuse directly rather than via `FUN_006c42f0`; `& 4` -> skip the CPU cull); the high bits
select which receiver classes `FUN_007e35f0` gathers *(uncertain which bit is which)*.

**Draw order.** Blob shadows are step 11 of `CMap::Render` - after terrain chunks (8), WMO groups (9)
and sky (10), before `CM2Scene::Draw(pass 0)` which is step 7 of `OnWorldRender`. All opaque receivers
have already written depth. Frozen current placement (after TerrainRender, before the M2 passes) is
therefore already correct; only the sky position differs, which is a separate known gap.

### 1b. Map shadow map (separate system, CVars `mapShadows` / `shadowLevel`)

* `FUN_007bb670(playerPos)` (MapShadow.cpp) - reads the current view matrix stack
  (`DAT_00c5df88+0x1af8`, `+0xf88`), builds the light basis (`FUN_004c1f00`), normalises it and stores a
  world-space **plane** into `_DAT_00d4319c..a8` (normal + d) plus a height `_DAT_00d25304 = z + _DAT_00a4040c`.
  Called at step 5 of `CMap::Render`, before any geometry.
* `FUN_007bb570` (step 7) - builds the light direction from the day/night light block
  (`DAT_00ce04a8 + 0x7c/0x80/0x84`, the Z component scaled and floored at `_DAT_00a400fc`), normalises it,
  then:
  * `FUN_00875c10(dir)` - stores and re-normalises the light direction into `_DAT_00d43180..94`.
  * `FUN_007bb3e0(&focus)` - focus point: the camera position (`FUN_007ecef0`), overridden by the active
    player position through `FUN_004d4db0(guid, 1, ".\MapShadow.cpp", 0x133)` + vtable `+0x2c`.
  * `FUN_00874010(1.0f, flag)` - sets `_DAT_00b1d518` / `_DAT_00d43168`.
  * `FUN_00875f80(&focus, bool)` - **renders the shadow map**. Uses three registered callbacks
    (`DAT_00d4315c/60/64`, all must be non-null), a snapped focus (`FUN_0088ce30` = floor, quantised to
    the texel grid `DAT_00d43150`), disables colour writes (`FUN_00408bf0(0x10, 0)`), and drives the
    scene render into an off-screen target.
  * `FUN_008750b0` - builds the shadow texture matrix and binds the result.
* Shaders: `ShadowMapRenderSL` (`FUN_007bb460`), `ShadowMap.wfx` (`FUN_00780f50`), and the terrain
  `Terrain2_pcf` / `Terrain3_pcf` permutations sample it.
* Requires **render-to-texture**, which frozen gx does not have.

---

## 2. Why the current frozen stand-in is wrong

`BlobShadowsBegin` / `BlobShadowDraw` / `BlobShadowsEnd`, `src/world/Terrain.cpp:4260-4370`,
called from `CGWorldFrame::OnWorldRender`, `src/ui/game/CGWorldFrame.cpp:243-274`.

The *structure* is right - re-drawing receiver geometry with the blob texture is what the reference
does. What is wrong:

1. **The z-fighting: the decal pass uses a different vertex shader than the pass that wrote the depth.**
   `RenderShaded` (Terrain.cpp:2114) binds `s_terrainVS` (the fork `vs_2_0` bytecode in
   `TerrainShadersD3d9.hpp`); `BlobShadowsBegin` binds `s_uiVertexShader[0]` (`Shaders\Vertex\UI` from
   the MPQ). Two different vertex programs transforming the same positions produce depths that differ
   by ULPs, so with `GxRs_DepthFunc = 0` (LESSEQUAL) a large fraction of fragments fail the test ->
   per-pixel speckle. The reference avoids this by feeding the *same* vertices through the *same*
   transform and asking for `DepthFunc = EQUAL`.
2. **No depth bias exists to fall back on.** `GxRs_PolygonOffset` is declared (`src/gx/Types.hpp:118`),
   initialised (`src/gx/CGxDevice.cpp:639`) and written by two callers
   (`src/model/CM2SceneRender.cpp:88`, `src/console/Screen.cpp:106`), but **no backend has a
   `case GxRs_PolygonOffset`** - verified in `src/gx/d3d/CGxDeviceD3d.cpp`, `src/gx/CGxDevice.cpp` and
   `src/gx/gll/CGxDeviceGLL.cpp`. `GLDevice.cpp:430-450` has `glPolygonOffset` plumbing but nothing
   routes the render state to it. So the state is silently a no-op everywhere. (This matters for the
   fallback plan, not for parity - the reference never uses it here.)
3. **Wrong blend mode and colour model.** Frozen uses `GxBlend_Alpha` with a hard-coded opaque-black
   vertex colour. The reference uses `GxBlend_Mod` (`dest *= shadowColor`) with a **white** diffuse
   whose alpha is the model shadow opacity (`model+0x178`). Consequence: frozen shadows are a flat
   black wash with no per-model opacity and no fade-out.
4. **Fog is left on.** Frozen sets `GxRs_Fog, s_fogActive`; the reference sets `Fog = 0` and
   `FogColor = 0xffffffff`. Distant frozen shadows get tinted by the fog colour.
5. **No stage-1 distance ramp.** The `ShadowAdd`/`ShadowMod` 64x8 ramp on `GxRs_Texture1`, driven by
   `GxXform_Tex1`, is what makes the shadow fade out along the projection axis. Frozen has neither, so
   the decal ends with a hard texture edge.
6. **Receiver coverage is terrain only.** Frozen loops `s_tiles`/`chunks`. The reference gathers world
   geometry batches *and* up to 10 M2 receivers, so shadows land on **WMO floors**, bridges, and other
   models. Frozen shadows vanish the moment a unit steps onto a WMO floor or a doodad.
7. **Casters are units only.** Frozen filters `object->IsA(TYPE_UNIT)`; the reference walks every scene
   entity with the `0x800` flag, so **doodads cast blob shadows too**.
8. **Footprint is a fixed axis-aligned circle.** Frozen uses `object->GetPosition()` and a clamped
   `max(ex,ey)*scale*0.9` radius. The reference uses the **current animation** bounding box
   (`FUN_0082ced0`), oriented by the model rotation, projected along the model local Z, and for
   units re-bases Z onto the floor (`FUN_0071ed80`).
9. **No CPU up-facing cull, and `Culling` is forced to 0.** Harmless for a terrain heightfield, wrong
   the moment WMO or M2 receivers are added - the shadow would be painted on ceilings.
10. **No gating CVars.** `shadowLOD` and `extShadowQuality` are not read; there is no way to turn blob
    shadows off, and no tier that switches to the shadow-map path.
11. **`ShadowInit()` is never called** (`src/client/Client.cpp:679`, commented out), so the texture is
    created lazily inside `BlobShadowsBegin` instead, and the two ramps do not exist at all.
12. Map shadow map: entirely absent. `CM2SceneRender::DrawBatchProj` is an empty stub
    (`src/model/CM2SceneRender.cpp:262`) and `CShadowCache::SetShadowMapGenericGlobal` is commented out
    (`src/model/CM2SceneRender.cpp:101`).

---

## 3. Ordered task list to reach parity

**Status checked against the source on 2026-09-23** and written into each heading below.
Four of the ten are done (T1, T2, T6, T8), none of them seen on screen. The doc's own
re-check section further down already said T1 and T2 were fixed, but this list did not, which
is the same drift the render inventory had. Keep the headings current when a task lands.

### T1 - Kill the z-fighting: identical vertex transform + `DepthFunc = EQUAL` --- **DONE** (unverified on screen)
*Change*: `BlobShadowsBegin`/`BlobShadowDraw`, `src/world/Terrain.cpp`.
*Ports*: `FUN_007e4480` (the render-state block).
*Prereq*: none.

Set `GxRs_DepthFunc, 1` (EQUAL) and `GxRs_DepthWrite, 0`, and make the decal pass transform positions
with **byte-identical** vertex code to the pass that wrote the depth. Two ways:

* **(a), cheapest and safest** - reuse `s_terrainVS` unchanged for the decal pass and change only the
  pixel shader. The terrain VS already emits a world-XY-derived texcoord (`v0.xy * 0.2`), so a small
  decal PS can recover world XY (`t * 5`) and compute the blob UV from a `(centerX, centerY, invRadius)`
  pixel-shader constant. Identical VS binary -> bit-identical depth -> EQUAL passes everywhere. This
  also removes the per-vertex `s_blobUv` rebuild in the inner loop.
* (b) - author `decal_vs.hlsl` whose `oPos` expression is textually identical to `terrain_vs.hlsl` and
  compile it with the same fxc. Works in practice but depends on the compiler emitting the same
  instruction sequence; (a) removes that risk.

For `RenderFallback` (non-D3D), the base pass already uses `s_uiVertexShader[0]`, so the current shader
choice is correct there - keep a per-path selection.

*Do not* add polygon offset here: the reference does not, and depth bias on a coplanar re-draw
re-introduces peter-panning at grazing angles.

### T2 - Correct blend / colour / fog --- **DONE** (unverified on screen)
*Change*: `BlobShadowsBegin`, `src/world/Terrain.cpp`. *Ports*: `FUN_007e4480`. *Prereq*: T1.
`GxRs_BlendingMode = GxBlend_Mod`; vertex colour white with alpha = the model shadow opacity;
`GxRs_Fog = 0`; `GxRs_FogColor = 0xffffffff`; `GxRs_Lighting = 0`. Under Mod the decal PS must output
white outside the blob footprint (multiply by 1 = no change), which the clamped ShadowBlob sampler
already gives.

### T3 - Oriented, animation-driven footprint --- **not started**: the caster radius comes from the cull extent and the blob stays axis-aligned
*Change*: `BlobShadowDraw` signature + its caller in `CGWorldFrame::OnWorldRender`.
*Ports*: `FUN_0082ced0` (animated box), the corner transform + AABB in `FUN_007e4480`.
*Prereq*: T1, P5.
Pass the model world matrix and the current sequence bounding box instead of `(position, radius)`;
build the quad from `(+-hx, +-hy, 0)` rotated by the model 3x3, and derive the Z extent from
`halfHeight` scaled by the two tunables. Frozen already reads `M2Bounds` in `CGWorldFrame.cpp:216`; the
per-sequence bounds table is the part that is missing.

### T4 - Restore `ShadowInit` --- **not started**: still commented out at `src/client/Client.cpp:834`
*Change*: `src/client/Client.cpp:679`, plus a `ShadowInit` in the Shadow module.
*Ports*: `FUN_007e4a40`. *Prereq*: none (independent of T1-T3).
Create `Textures\ShadowBlob.blp` up front, generate the two 64x8 ramps (`FUN_007e36e0` / `FUN_007e3820`
are complete and directly portable), register CVar `shadowLOD` and look up `extShadowQuality`, and gate
`BlobShadowsBegin` on `shadowLOD == 1 && extShadowQuality < 1` (`FUN_007e49e0`).

### T5 - Stage-1 distance fade --- **not started**, but decoded: the ramps are a fade along the projection axis (see the 2026-09-16 section below)
*Change*: the decal pixel shader + `BlobShadowsBegin`. *Ports*: the stage-1 setup in `FUN_007e3e80`,
the second texture matrix in `FUN_007e2d60`. *Prereq*: T1, T4 (the ramps).
Bind the `ShadowMod` ramp to `GxRs_Texture1` and sample it with a fade coordinate computed in the PS
from the projector Z range. Do **not** try to use `GxRs_TexGen1`/`GxXform_Tex1` - see P2.

### T6 - Doodads cast shadows --- **DONE** (unverified on screen): `CGWorldFrame` calls both blob draws from the doodad walk
*Change*: the caster loop in `CGWorldFrame::OnWorldRender`. *Ports*: the entity walk in `FUN_00793980`.
*Prereq*: T3.
Drop the `IsA(TYPE_UNIT)` filter; iterate every visible model-bearing scene entity and use the
non-unit path (animated box) for doodads, the unit path (floor-clamped box) for units.

### T7 - Generic receiver gather + CPU up-facing cull --- **not started** as a refactor: `BlobShadowDraw` still walks `s_tiles` itself, and T8 was done alongside it rather than through it
*Change*: new `ShadowReceiverGather` helper; `BlobShadowDraw` consumes a batch list instead of walking
`s_tiles` directly. *Ports*: `FUN_007e35f0` -> `FUN_007a6af0`, `FUN_007e32f0` (the XY cross-product cull
and the un-indexing into a dynamic VB). *Prereq*: T1, P6.
Keep the terrain chunk path as one producer. This is the refactor that makes T8/T9 possible; the cull
is `(p2-p1).x*(p3-p1).y - (p3-p1).x*(p2-p1).y >= 0`, and note the reference un-indexes to 3 verts per
triangle rather than re-using the receiver index buffer.

Caveat: an un-indexed re-draw keeps T1(a) bit-identity only because the *positions* are unchanged.
Once WMO receivers are added, their base pass must also share the decal pass vertex transform, or
those surfaces will z-fight again. Budget a per-receiver-class decal shader.

### T8 - Shadows on WMO floors --- **DONE** (unverified on screen) via `BlobShadowDrawWmo`, without the T7 gather
*Change*: add a WMO group producer to the T7 gather; `src/world/Terrain.cpp` (`RenderWmos` owns the
group geometry). *Ports*: the world-geometry half of `FUN_007a6af0`. *Prereq*: T7, and a WMO base pass
that shares the decal pass vertex transform.

### T9 - Shadows on models (`DrawBatchProj`) --- **not started**: the stub is also unreachable behind its own gate, see the note at the dispatch in `CM2SceneRender::Draw`
*Change*: `CM2SceneRender::DrawBatchProj`, `src/model/CM2SceneRender.cpp:262`; add the M2-receiver
producer to T7. *Ports*: `FUN_00829aa0` (+ `FUN_007a2aa0` for the max-10 receiver list).
*Prereq*: T7. `FUN_00829aa0` is short and complete in `win-decomp-shadow-blob3.txt`: walk the model
batches, skip `+0xc != 0` and flag `0x20`, issue one triangles draw per batch with the projector states
already bound.

### T10 - Map shadow map --- **not started**
*Change*: new `MapShadow` module + `CShadowCache`; hook at `CMap::Render` steps 5 and 7.
*Ports*: `FUN_007bb670`, `FUN_007bb570`, `FUN_007bb3e0`, `FUN_00874010`, `FUN_00875c10`, `FUN_00875f80`,
`FUN_008750b0`; shaders `ShadowMapRenderSL`, `Terrain2_pcf`/`Terrain3_pcf`.
*Prereq*: **render-to-texture in gx (P1)**, plus the terrain shader-level permutations. Defer until P1
lands; nothing in T1-T9 depends on it.

---

## 4. Prerequisites owned by other subsystems

* **P1 - render-to-texture in gx.** Blocks T10 entirely (and FFX glow). No off-screen colour/depth
  target exists in `src/gx`.
* **P2 - `GxRs_TexGen*` / `GxXform_Tex*` are D3D-unimplemented.** `CGxDeviceGLL.cpp:631-638` handles
  `GxRs_TexGen0..7`; `src/gx/d3d/CGxDeviceD3d.cpp` has no `TexGen` case at all. A literal port of the
  reference fixed-function projector is therefore impossible on D3D. **Recommendation: do not
  implement it** - compute the projector UVs in the decal shader instead (T1a/T5). Implement it only if
  the GLL/GLES backends are meant to run the same code path.
* **P3 - fixed-function `GxRs_ColorOp*` / `GxRs_AlphaOp*` / `GxRs_MatDiffuse` are D3D-unimplemented**
  (no cases in `CGxDeviceD3d.cpp`). Same conclusion: fold the combiner into the decal pixel shader.
* **P4 - `GxRs_PolygonOffset` is a no-op in every backend.** Not needed for shadow parity, but
  `CM2SceneRender.cpp:88` and `Screen.cpp:106` already set it expecting it to work, and the GL path in
  `GLDevice.cpp:430-450` is already written. A ~10-line `case GxRs_PolygonOffset` in `CGxDeviceD3d`
  (`D3DRS_DEPTHBIAS` + `D3DRS_SLOPESCALEDEPTHBIAS`) would close the gap. Treat as an independent gx fix,
  not as the z-fighting fix.
* **P5 - M2 per-sequence bounding boxes.** T3 needs the sequence-bounds table
  (`m2data+0x150`, stride 0x40, box at `+0x20`); frozen currently exposes only the global
  `M2Bounds`. Owner: `src/model`.
* **P6 - a shared receiver-geometry abstraction.** T7-T9 need terrain chunks, WMO groups and M2 batches
  to be enumerable through one interface with a world matrix per batch. Today each lives in its own
  renderer. Owner: `src/world` + `src/model`.
* **P7 - CVar registration for `shadowLOD`, `extShadowQuality`, `mapShadows`, `shadowLevel`**
  (`FUN_0078e400` registers the last two). Owner: the CVar layer.

## 2026-09-16 - root cause of the "ugly black circle", found offline

The user reported blob shadows rendering as a hard black disc. Cause found without running anything,
and each step is checkable.

**1. The asset has no gradient.** `Textures\ShadowBlob.blp`, extracted from `common.MPQ`, is a 32x32
palettised BLP2 with **`alphaDepth = 1`**. Decoding its alpha plane gives distinct values of exactly
`{0, 255}` -- every texel is fully opaque or fully transparent:

```
row 16: ..############################..
row  4: .......##################.......
```

**2. Our shader takes its coverage only from that alpha.** `src/world/shaders/blob_decal_ps.hlsl`:

```hlsl
float coverage = saturate(texel.a * decal.w);
return float4(1 - coverage, 1 - coverage, 1 - coverage, 1);
```

So coverage is binary: `0` or `decal.w`, with one texel of bilinear smoothing across a footprint
several yards wide. A hard black circle is the correct output for this input. The shader is not
wrong; it is being fed a mask and asked for a gradient.

**3. The reference does not get its falloff from the BLP either -- it generates one.** `ShadowInit`
(`FUN_007e4a40`) installs two texture-fill callbacks that write a procedural ramp to `DAT_00af3e24`:

| | |
|---|---|
| `FUN_007e36e0` "ShadowAdd" | RGB white, **alpha** = `round(v * 255)` |
| `FUN_007e3820` "ShadowMod" | **greyscale** `round((1 - v) * 255)` in all three channels, alpha `0` where `v == 0` else `255` -- built for `GxBlend_Mod` |

Both share one profile. The constants were read straight out of `.rdata` in the reference exe rather
than guessed:

| decompiler name | address | value |
|-----------------|---------|-------|
| `_DAT_00a1047c` | 0x00A1047C | **12** (x scale) |
| `_DAT_00a4040c` | 0x00A4040C | **2** (rise ends) |
| `_DAT_009e30cc` | 0x009E30CC | **10** (plateau ends) |
| `_DAT_009e2ec4` | 0x009E2EC4 | **0.5** (slope) |
| `DAT_009e30c0`  | 0x009E30C0 | **255** (alpha scale) |

giving, for texel `i` of a row of width `W`:

```
x = (i / (W - 1)) * 12
x <  2  ->  v = x * 0.5              // 0 -> 1 over the first sixth
x < 10  ->  v = 1                    // flat across the middle two thirds
else    ->  v = max((12 - x) * 0.5, 0)   // 1 -> 0 over the last sixth
```

A trapezoid with a one-sixth soft margin at each end. It is continuous at both knees (`x = 2` gives
`1.0`; `x = 10` gives `1.0`), which is what makes the reading trustworthy -- a misread constant would
almost certainly break that.

Every row of the texture is the same ramp, so this is a 1-D profile; sampling it by radial distance
from the blob centre is what turns it into a soft round shadow.

**Not implemented yet.** The constants above fully specify it, but four passes of unverified work are
already queued behind a single run, and the last review pass found a crash in exactly that kind of
"obviously fine" unverified change. This one goes in when it can be looked at.

### 2026-09-16, same day - correction: that was not the root cause

The entry above says the reference gets its blob falloff from the generated ramp while frozen is stuck
with a binary mask. **That is not what `ShadowInit` does.** Reading `FUN_007e4a40` itself rather than
inferring from the two ramp callbacks:

```
DAT_00d38044 = FUN_004b9760("Textures\ShadowBlob.blp", ...)              // the BLP, loaded
DAT_00d38040 = FUN_004b9200(0x40, 8, 2, 2, ..., FUN_007e36e0, "ShadowAdd")  // generated, 64x8
DAT_00d3803c = FUN_004b9200(0x40, 8, 2, 2, ..., FUN_007e3820, "ShadowMod")  // generated, 64x8
```

Three textures, not one. The reference loads **the same binary-alpha ShadowBlob.blp we do**, so the
1-bit alpha cannot by itself be what separates its shadows from ours. And the ramps are **64x8** --
a 1-D profile over 64 texels repeated on 8 rows, which is not a radial blob shape and is not a
drop-in replacement for the blob texture. Where they are bound is still unknown; `FUN_007e4480`
projects planar over the caster's bounding box (`FUN_004c5280(1/extentX, 1/extentY, 1)`), which does
not tell us which of the three texture handles is in which stage.

What survives from the entry above, because it was measured rather than inferred:

- `Textures\ShadowBlob.blp` is 32x32 palettised with `alphaDepth = 1`, and its decoded alpha plane
  holds exactly `{0, 255}`. **Both clients load this.**
- frozen's `blob_decal_ps.hlsl` derives its entire coverage from that alpha, so frozen's blob edge is as
  hard as the mask.
- The trapezoid profile and its five constants, read from `.rdata` at the addresses listed above,
  are correct as a description of what those two ramp callbacks write. Only the claim about *what
  the ramps are for* was wrong.

**Answered, and it was already in this file.** The "Second texture stage = a distance fade" section
above -- written earlier in this project, roughly 300 lines up -- says exactly which handle goes
where:

| stage | handle | role |
|-------|--------|------|
| 0 (`GxRs_Texture0`) | `DAT_00d38044` `ShadowBlob.blp` | the blob UV -- the round mask |
| 1 (`GxRs_Texture1`) | `DAT_00d38040` ShadowAdd (fixed-function) or `DAT_00d3803c` ShadowMod (pixel-shader class 4) | **a fade along the projection axis** |

So the 64x8 trapezoid is a **distance fade**, not the blob's edge. Both coordinates come from hardware
texgen through `GxXform_Tex0` / `GxXform_Tex1`, set by `FUN_00616a30` inside `FUN_007e4370`.

Three consequences, and the first one matters most:

1. **The hard circular edge is not a defect.** The reference binds the same binary-alpha
   `ShadowBlob.blp` at stage 0 that frozen does. Whatever makes the reference's shadows look better, it
   is not a softer mask -- so "make the blob edge soft" would be inventing a difference, not closing
   one.
2. **frozen has no stage 1 at all**, so its shadows never fade with distance along the projection axis.
   They sit at full strength wherever they land. That is a real, fully specified gap: a 64x8 texture
   whose profile and five constants are recorded above, bound to stage 1, modulated in
   (`ColorOp1`/`AlphaOp1 = 2` on the shader path).
3. `BlobShadowStrength()` in `src/world/Terrain.cpp` is explicitly *not* the reference calculation --
   it derives darkness from `diffuseLuma / (ambientLuma + diffuseLuma)`. That remains the other
   candidate for shadows reading too dark.

Porting (2) is not a shader one-liner: frozen's blob pass runs a pixel shader rather than the
fixed-function combiner, so the fade needs a second sampler, a constant carrying the fade range, and
an `fxc` rebuild. Specified, not yet written.

**CLAUDE.md says to read `docs/ref/` before reaching for Ghidra. I appended two entries to this very
file without reading the part of it that already held the answer**, and generated a wrong theory in
between.

I wrote "root cause found" on an inference drawn from two callbacks without reading the initialiser
that installs them. The measurements were sound; the conclusion built on them was not.

### 2026-09-16 - the section 2 gap list, re-checked against current source

That list of eleven gaps was written before several days of work and is now partly stale. Checked
item by item against the code as it stands, rather than re-quoting it:

| # | gap as written | state today |
|---|----------------|-------------|
| 1 | decal pass uses a different vertex shader than the depth-writing pass | **fixed** - `GxRs_VertexShader, s_terrainVS` with `GxRs_DepthFunc, 1` (EQUAL) |
| 2 | no depth bias to fall back on | **fixed** as a gx feature (and was never the cause) |
| 3 | wrong blend mode and colour model | **fixed** - `GxRs_BlendingMode, GxBlend_Mod` |
| 4 | fog left on | **fixed** - `GxRs_Fog, 0` |
| 5 | no stage-1 distance ramp | **still open** - frozen binds no stage 1 at all |
| 6 | receiver coverage is terrain only | **partly** - WMO floors covered via `BlobShadowDrawWmo` |
| 7 | casters are units only | **still open** |
| 8 | footprint is a fixed axis-aligned circle | **still open** |
| 9 | no CPU up-facing cull | **partly** - `BuildWmoShadowGrid` keeps only up-facing triangles |
| 10 | no gating CVars (`shadowLOD`, `extShadowQuality`) | **still open** |
| 11 | `ShadowInit()` never called | still commented out at `src/client/Client.cpp:679`, but deliberately: the frozen blob path loads its own texture and does not need it |

So six of eleven are closed or partly closed. The live ones are the stage-1 fade (5), caster and
footprint fidelity (7, 8), and the quality CVars (10).

**A suspected bug that turned out not to be one.** `BlobShadowStrength()` returns
`diffuseLuma / (ambient + diffuse)` while its own comment says the correct multiplier is
`ambient / (ambient + diffuse)` -- the complement, which looked like a sign error large enough to
explain a too-dark shadow. It is not a bug: the value is the amount of light *removed*, and
`blob_decal_ps.hlsl` emits `1 - coverage`, so what reaches the framebuffer is exactly
`ambient / (ambient + diffuse)`. Code and comment agree; only the naming hides it. Added a note to
the function saying which sense the return value is in, so the next reader does not spend the same
time on it.

The comment above that function also claimed the reference derives this strength from the
ShadowAdd/ShadowMod ramps and that those were undecoded. Both halves are wrong as of today: the ramps
are decoded, and they are the stage-1 distance fade, not a strength. Corrected in place.
