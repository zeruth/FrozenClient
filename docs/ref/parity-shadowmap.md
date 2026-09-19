# Map shadow map parity: 3.3.5a reference vs frozen

Scope: the **map shadow map** — the quality tier *above* the blob shadows covered by
`parity-shadows.md`. Modules: `MapShadow.cpp` (~0x7ba-0x7bd) and `CShadowCache` (~0x874-0x876).
Program: `RunicWorldGame.exe` (Win 3.3.5a 12340).

Raw dumps captured for this doc: `win-dump-shadowmap1.txt` .. `win-dump-shadowmap4.txt`.
Earlier context: `win-decomp-shadow-blob2.txt` (first pass on `CShadowCache`),
`win-decomp-cmap-render.txt` (`CMap::Render`), `win-decomp-depth1.txt` (CVar registration).

Every address below was decompiled or disassembled and read. Inferences are marked *(uncertain)*.
Float constants were read out of the PE by mapping RVA -> file offset.

---

## 0. The one-paragraph summary

`extShadowQuality` (0-5) selects a tier. At 0 the only shadows are the baked MCSH terrain shadow and
the blob decals. At **1+** the engine renders one or two **orthographic depth maps** from the sun's
direction into 1024x1024 or 2048x2048 render targets, centred on the player, and the terrain / WMO /
M2 pixel shaders sample them with an **8-tap PCF kernel**. At **3+** it additionally keeps three
**cascade slots** (half-extents 40 / 160 / 640) that are refreshed progressively, one cell of a 3x3
(or 5x5) atlas per frame. `hwPCF` decides whether the sampled texture is a **D24X8 depth texture**
(hardware comparison filtering) or an **R32F colour texture**; that choice, not the presence of
shadows, is what `Terrain2_pcf` / `Terrain3_pcf` mean.

---

## 1. Where it sits in the frame

`CMap::Render` FUN_0079a870 (inventory "Draw order" steps 5 and 7, both confirmed):

```
CMap::Render FUN_0079a870
  ...
  4. GxSceneClear(3, skyColour)
  5. FUN_007bb670(playerPos)          MapShadow: build the "interior" plane constant  (see 6c)
  6. CM2Scene::AdvanceTime FUN_0081c9c0, CM2Scene::Animate FUN_00821a20
  7. FUN_006fda20, ** FUN_007bb570 **  MapShadow: render + bind the shadow map(s)
     CShaderEffect::UpdateProjMatrix FUN_00872c10, fog FUN_00781610
  8. terrain chunks FUN_00798da0      -> calls FUN_00874660 to bind the shadow constants
  9. WMO groups FUN_007964a0          -> calls FUN_008744e0
  ...
```

Two things matter for the port:

* The shadow map is rendered **after** `CM2Scene::Animate`, so the caster model matrices are current.
  Frozen's `CGWorldFrame::OnWorldRender` runs `scene->AdvanceTime`/`Animate` *after* `TerrainRender`,
  so the shadow-map render cannot simply be dropped where `TerrainRender` is today — **Animate has to
  move before it** (or the shadow pass has to run before the terrain pass but after an early Animate).
* The constants are bound **inside each receiver's own draw routine**, not once per frame. Terrain
  binds its own set, WMO/M2 bind a different set at different registers.

---

## 2. CVars and quality tiers

Registered in `FUN_0078e400` (WorldParam) unless noted.

| CVar | default | handler | effect |
|---|---|---|---|
| `mapShadows` | `"1"` | FUN_0078d660 | sets/clears bit `0x40` of `DAT_00cd774c`. Messages "Terrain shadows enabled/disabled." |
| `shadowLevel` | `"1"` | FUN_0078d6f0 | "Terrain shadow map mip level", range 0-1, **takes effect on restart** |
| `extShadowQuality` | `"0"` | FUN_0078dcb0 | the real switch. -> `FUN_008740d0` (caps validation) -> `FUN_00874210` (`DAT_00d43154 = q`, `DAT_00b1d51c = 1` = realloc next frame) |
| `hwPCF` | `"1"` | FUN_0078e070 | requires caps `+0xac`; -> `FUN_00872ad0(on)` sets `DAT_00d43014`, then `FUN_00873fe0` (realloc) and `FUN_0079e4f0` (reload the terrain pixel shaders) |
| `shadowCull` | `"0"` | - | FUN_007bd3a0; tightens the light ortho to the caster bounds in FUN_007bafd0 |
| `shadowScissor` | `"0"` | - | FUN_007bd3a0; scissor the shadow-map draw to the caster bounds |
| `shadowInstancing` | `"0"` | - | FUN_007bd3a0; instanced M2 caster draw |

`extShadowQuality` descriptions, from the table at `PTR_..._00b1d538` (7 entries, `FUN_00873f60`):

| q | string | shadow map size | cascades |
|---|---|---|---|
| 0 | `[LOWEST]Precomputed terrain and no dynamic shadows.` | none allocated | - |
| 1 | `[LOW]Precomputed terrain and dynamic PC/NPC shadows (low-res).` | **1024** | - |
| 2 | `[MEDIUM]Precomputed terrain and dynamic PC/NPC shadows (high-res).` | **2048** | - |
| 3 | `[MED-HIGH]Full environmental and PC/NPC shadows, low-res, lg-dist.` | **1024** | 3 |
| 4 | `[HIGH]Full environmental and PC/NPC shadows, hi-res, lg-dist.` | **2048** | 3 |
| 5 | `[VERY HIGH]Cascaded shadow maps.` | **2048** | 3, all refreshed every frame |
| 6+ | `[INVALID]Unsupported quality level.` | - | - |

Size rule verbatim from `FUN_00875d30`:
`DAT_00d43150 = 0x400; if (q > 3 || q == 2) DAT_00d43150 = 0x800;`

`FUN_00873ff0()` returns `DAT_00b1d554[q_effective]` = `{0,1,1,2,2,3,3}` — the **shader permutation
index** (stored in `DAT_00d43010`); `FUN_00872de0` combines it with `hwPCF` as
`DAT_00d43010 + DAT_00d43014*4` (or `+ (DAT_00d43014+2)*4` when `caps+0x130` and `DAT_00d43010`).
`q_effective` is `FUN_00873f80()` = `DAT_00d43154` masked to 0 while the realloc flag
`DAT_00b1d51c` is set — i.e. **zero shadows for the frame on which the settings change**.

`shadowLevel` is *not* read anywhere in the shadow-map path that was walked. Its handler only prints
"changed upon restart", and no reader of `DAT_00cd8578` turned up in the shadow region. *(uncertain:
it is probably consumed at texture-creation time in a path not walked; treat it as a no-op for the
port.)*

---

## 3. The light view / projection  (`FUN_007bac10`, the answer to question 1)

`FUN_007bac10` is registered as callback slot 0 (`DAT_00d43158`) by `FUN_007bd3a0`. It is the
authoritative construction. Signature:

```
FUN_007bac10(const C3Vector* centre, float extent, C44Matrix* out, const C3Vector* up, int slot)
```

### 3a. The direction

**Not** taken raw from the DayNight block. `FUN_007bb570` builds it:

```
d = ( light->x, light->y, light->z * 5.0 )          light = DAT_00ce04a8 + 0x7c/0x80/0x84
if (d.z < -1.2)  d.z = -1.2                          _DAT_00a400fc = -1.2, _DAT_009ebf34 = 5.0
d = normalize(d)
FUN_00875c10(d, cameraPos)                           cameraPos = DAT_00cd8f74/78/7c
```

`DAT_00ce04a8 + 0x7c..0x84` is the light block's direction (`parity-sky`: the outdoor sun direction
solved in `FUN_007eea90`, zenith 110-127 deg, azimuth fixed at 225 deg, pointing *away* from the
light). With that zenith band `z` is about -0.34 .. -0.60, so `z*5` is -1.7 .. -3.0 and the clamp at
-1.2 **always bites**: in practice the shadow light is

```
dir ~= normalize( sin(theta)*cos(225deg), sin(theta)*sin(225deg), -1.2 )
     ~= (-0.43, -0.43, -0.79)          ~52 deg elevation, from the south-west
```

so the shadow direction tracks the sun's wobble only weakly and never gets shallower than ~50 deg.
That clamp is what stops shadows stretching to the horizon at dawn/dusk.

`FUN_00875c10(dir, pos)` stores:
* `_DAT_00d43180..88` = the **world-space** light direction (its `pos` argument is dropped — the
  disassembly shows only the direction being used).
* `_DAT_00d4318c..98` = the same direction transformed by the **world matrix stack top**
  (`device+0x1b00 + device[0x1af8]*0x40`) and re-normalised, `w = 0`. During `CMap::Render` that
  matrix is identity, so this equals the world direction. Bound as **PS c4** by the M2/WMO binders.

### 3b. The focus point

`FUN_007bb3e0(&focus)` (`.\MapShadow.cpp:0x133`): starts from `FUN_007ecef0()+0x18` (the DayNight
camera position) and **overrides it with the active player's position** via
`FUN_004d4db0(guid_lo, guid_hi, 1, ".\MapShadow.cpp", 0x133)` + vtable `+0x2c`. So the volume is
**centred on the player**, falling back to the camera if the player object is gone.

`FUN_00875f80` then snaps it:

```
size = (float)DAT_00d43150                  // 1024 or 2048
inv  = 1.0f / size
DAT_00d43260 = floorf(focus.x * size + 0.5f) * inv
DAT_00d43264 = floorf(focus.y * size + 0.5f) * inv
DAT_00d43268 = focus.z                                    // z is NOT snapped
```

This is the standard "snap the shadow centre to the texel grid so it does not shimmer", but **as
compiled the grid is `1/size` world units (~0.001 yd), not the texel size `2*extent/size`
(~0.039 yd)** — so it is effectively a no-op. Port it verbatim for bit parity, or use
`2*extent/size` if shimmering shows up; note the deviation either way.

### 3c. The volume

```
worldOffset = FUN_004f6650()                        // the camera world-rebase offset
eye    = centre - lightDirWorld * 2000.0 - worldOffset      _DAT_00a400e4 = 2000.0
target = centre - worldOffset
up     = DAT_00d43278 = (1, 0, 0)                   // world +X; +Z is unusable, the light is steep

view = LookAt(eye, target, up)                      FUN_006c0050
       zv = normalize(target-eye); xv = normalize(cross(up, zv)); yv = normalize(cross(zv, xv))
       columns (xv, yv, zv), then translate(-eye)   -> left-handed, row-vector

proj = OrthoOffCenter(-extent, +extent, -extent, +extent, near = 1.0, far = 4000.0)
                                                    FUN_006bf4c0, _DAT_00a3fa3c = 4000.0

S    = inverse(worldMatrixStackTop)                 // identity during CMap::Render
out  = S * view * proj
out.column2 = (S * view).column2                    // linear depth along the light, NOT projected z
out.m32    -= (k * 0.2f + (hwPCF ? 0.5f : 0.0f))    // depth bias, world units along the light
```

The bias multiplier `k` (`_DAT_009e8d84 = 0.2`):

| slot | k | bias (no hwPCF) | bias (hwPCF) |
|---|---|---|---|
| -1 (the main map) | 0.5 | **0.10** | 0.60 |
| 0 (cascade 40) | 2.0 | 0.40 | 0.90 |
| 1 (cascade 160) | 4.0 | 0.80 | 1.30 |
| 2 (cascade 640) | 8.0 | 1.60 | 2.10 |

**Extents.** The main map uses `_DAT_00d43258 = _DAT_009f281c = 20.0` — a **40x40 yard** box around
the player. The three cascade slots use `DAT_00b1d520[] = {40, 160, 640}` (so 80 / 320 / 1280 yards)
with re-centre distance thresholds `DAT_00b1d52c[] = {4, 16, 1024}` (squared) and snap grids
`{2.0, 4.0, 16.0}` yards.

**Depth encoding.** The eye sits 2000 yd back and the far plane is 4000 yd, so the light's z range
is exactly `[1, 4000]` in world units, linear (column 2 comes from the *view* matrix). The shadow
texture matrix then scales it by `_DAT_00d431c8 = 0.00025 = 1/4000` (set every frame by
`FUN_00874030(_DAT_00a40100)` inside the render callback), which maps it into `[0,1]`. The shadow-map
pixel shader is handed the same `0.00025` as **PS c0.w**, so writer and reader agree.

### 3d. Refresh cadence

The main map is rebuilt **every frame** (`FUN_007bb570` is called unconditionally from
`CMap::Render`). The three cascade slots are amortised: `FUN_00874890` re-renders a slot only when
its cached centre has moved further than its threshold (or its counter is live), and when it does it
renders into **one cell of a 3x3 atlas** (slots 0/1, cell size `2/3`, `_DAT_009fc830`) or a **5x5
atlas** (slot 2, cell size `2/5`, `_DAT_009f98d8`), advancing one cell per frame and ping-ponging
between two textures when the counter wraps (>8 for slots 0/1, >24 for slot 2).

---

## 4. The target: what `CShadowCache` allocates  (question 2)

`FUN_00875d30` (allocate) / `FUN_00874240` (free). Textures are created with
`FUN_004b8c80(GxTex_2d, size, size, 0, fmt, fmt, flags, 0, errFn, "ShadowCache", 0)` where
`flags = (base & ~0x1f) | 0x80` (`| 0x81` with hwPCF) — the `0x80` bit is **render target**.

| global | hwPCF off | hwPCF on | role |
|---|---|---|---|
| `DAT_00d43250` | `GxTex_R32F` (0xB) | `GxTex_Argb8888` (0x2) | pass-B target. Off: the map itself. On: a **dummy colour buffer** D3D9 needs alongside a depth target |
| `DAT_00d43254` | *(not created)* | `GxTex_D24X8` (0xC) | pass-B **depth** texture = the sampled map |
| `DAT_00d43148` | `GxTex_R32F` | `GxTex_D24X8` | pass-A target / depth |
| `DAT_00d43290 + i*0x3c` (+0x00,+0x04) | `GxTex_R32F` x2 | `GxTex_D24X8` x2 | cascade slot *i*, ping-pong pair (the second only when `q < 5`) |

All are `size x size` with `size = DAT_00d43150` (1024 or 2048), mip count 1.

The per-slot struct is 0x3c bytes and the **global entry uses the same layout** at base `0xd43250`:

```
+0x00 texture A        (0xd43250 / 0xd43290 + i*0x3c)
+0x04 texture B        (0xd43254)
+0x08 float extent     (0xd43258 = 20.0; cascades 40/160/640)
+0x0c float threshold2 (0xd4325c; cascades 4/16/1024)
+0x10 C3Vector centre, cached   (0xd43260..68)
+0x1c C3Vector centre, current  (0xd4326c..)
+0x28 C3Vector up hint (0xd43278 = (1,0,0))
+0x34 uint  atlas cell counter
+0x38 uint  ping-pong toggle
```

**Realloc** is deferred: `FUN_00873fe0` / `FUN_00874210` only set `DAT_00b1d51c = 1`; the next
`FUN_00875f80` does `FUN_00874240(); FUN_00875d30();` and clears the flag. `FUN_00875d30` also
registers `FUN_00873fe0` as a device-lost callback (`device vtable +0x7c`) and `FUN_00874240`
unregisters it (`+0x80`) — so a device reset re-creates the `D3DPOOL_DEFAULT` targets. Frozen needs
the same hook for a windowed resize.

**What frozen must bind.** Per pass:

```
GxRenderTargetSet(GxBuffers_Depth, depthTex, 0)   // only on the hwPCF path
GxRenderTargetSet(GxBuffers_Color, colourTex, 0)
GxSceneClear(3, CImVector{0xFFFFFFFF})            // colour AND depth, cleared to white / 1.0
... draw casters ...
GxRenderTargetSet(GxBuffers_Color, savedColour, 0)
GxRenderTargetSet(GxBuffers_Depth, savedDepth, 0) // restore, saved with GxRenderTargetGet
```

`CGxDeviceD3d::IRenderTargetSet` (`src/gx/d3d/CGxDeviceD3d.cpp:1843`) already does exactly this, and
`ITexCreate` already special-cases `GxTex_D24X8` -> `D3DUSAGE_DEPTHSTENCIL`. Two caveats that are
**not** handled yet:

* `IDirect3DDevice9::SetRenderTarget` **resets the viewport and scissor** to the full surface. The
  reference re-sets the viewport itself (`FUN_00681f60` with the slot's sub-rect); frozen's
  `GxXformSetViewport` must be called after the bind, not before.
* On the non-hwPCF path the reference binds **only** a colour target and keeps the default
  depth-stencil. D3D9 requires the depth surface to be at least as large as the colour surface, so a
  1024/2048 shadow map with a smaller back buffer will fail the `SetRenderTarget`. Either always
  allocate a matching `GxTex_D24X8` (i.e. take the hwPCF layout unconditionally) or clamp the map to
  the back-buffer size.

---

## 5. What is rendered into it  (question 3)

### 5a. Which passes

`FUN_00875f80(focus, bRenderPassB)` drives up to two passes plus the cascades. The scratch struct
`P` (~0xb90 bytes, constructed by `FUN_008753f0`) carries a **mode** per slot in `P[0..2]`:

| call | mode | target (hwPCF off / on) | bound by |
|---|---|---|---|
| pass A, always | **3** | colour `DAT_00d43148` / colour `DAT_00d43250` + depth `DAT_00d43148` | `FUN_008745d0(1)` -> PS s4 |
| pass B, if `bRenderPassB` | **1** (q<=2), **5** (q 3-4), **0xd** (q>=5) | colour `DAT_00d43250` / colour `DAT_00d43250` + depth `DAT_00d43254` | `FUN_00874660` (terrain) -> PS **s5**, `FUN_008745d0(0)` / `FUN_00874760` -> s4 |
| cascades, if q>2 | **8** (or **0xd** when dirty) | the slot's ping-pong texture | `FUN_00874660` -> s6/s7/s8 |

`bRenderPassB = (0.0f <= _DAT_00adf59c) || (DAT_00cd8778 != 0)` — both written in `CMap::Render`
just before the visibility traversal; the pair means **exterior geometry is visible / the camera is
outdoors**. So indoors the terrain shadow map is simply not rendered.

The caster gather `FUN_007bd200` ORs the per-slot modes and gates the *environment* gather on
`mode & 0xC`:

* `FUN_007bb9d0(P, 0, slotCount)` runs **always** — the dynamic (M2 / unit) caster gather. It reads
  `_DAT_00d25304` (= playerZ + 2.0, written by `FUN_007bb670`) and clamps the caster box with
  `10000.0` / `25.0` / `0.25` / `10.0` constants.
* `FUN_007cd910`, `FUN_007bc890`, `FUN_007bcf20` (or `FUN_007bc490` / `FUN_007bcc00` for the
  multi-slot path) run only when `mode & 0xC` — i.e. modes 5, 8, 0xd, i.e. **`extShadowQuality >= 3`**.
  These are the terrain + WMO gathers.

That is exactly the CVar description: q1/q2 cast **PC/NPC only**; q3+ adds **terrain and buildings**.

### 5b. The draw itself (`FUN_007bbc50`, callback slot 3)

```
list = &DAT_00d25320 + slot*9                     // the gathered caster lists
if (list[0] == 0 && list[4] == 0 && list[7] == 0)
    FUN_007bb830(colour, depth, P+0x994+slot*0x10)   // nothing to cast: just clear the sub-rect to white
    return

if (depth) GxRenderTargetSet(GxBuffers_Depth, depth, 0)
GxRsPush()
GxRsSet(GxRs_Culling, 0)                          // RS 0x11 = 0  -> NO backface culling
GxRsSet(GxRs_Fog, 0)                              // RS 0x0c = 0
FUN_00873480(0)
save the current colour target (GxRenderTargetGet(0, &saved))
FUN_00876530("ShadowMapRenderSL")                 // -> FUN_0055f4d0, select the named shader effect
worldOffset = FUN_004f6650()
eye    = P.centre[slot] - lightDirWorld*2000 - worldOffset
target = P.centre[slot] - worldOffset
save the current view (device+0xf88) and projection (device+0x6c0 + device[0x6be]*0x40)
GxXformSetViewport(P[+0x994 + slot*0x10] .. [+0x9a0], near 0, far 1)   // the atlas sub-rect
GxShaderConstantsSet(GxSh_Pixel, 0, {0,0,0, 0.00025f}, 1)              // PS c0.w = 1/4000
FUN_00874030(0.00025f)                            // -> _DAT_00d431c8, the same scale in the tex matrix
DAT_00d43010 = FUN_00873ff0()                     // shader permutation index
SetProjection(P + 0x9c4 + slot*0x40)              // the ortho built by FUN_007bafd0
CShaderEffect::UpdateProjMatrix FUN_00872c10
GxRenderTargetSet(GxBuffers_Color, colour, 0)
GxSceneClear(3, 0xFFFFFFFF)                       // white / depth 1.0
if (shadowScissor) GxSetScissor( (v + 1) * 0.5 of P[+0x900 + slot*0x10] )
view = LookAt(eye, target, up)
SetView(view)
GxShaderConstantsSet(GxSh_Vertex, 14, <3 float4>, 3)                   // VS c14..c16
if (list[0]) FUN_007ab760(list[1], list[0], list[2], translate(-worldOffset), P+0x6c+slot*0xf4)
if (list[4] || list[7]) FUN_0082da40(list+3, list+6)                   // M2 caster batches
GxRsPop(); restore viewport / view / colour target / projection
```

**There is no depth bias render state anywhere in this pass.** The bias is entirely the `k*0.2`
subtraction baked into the shadow *texture* matrix (section 3c) — consistent with the finding in
`parity-shadows.md` that the reference never drives `GxRs_PolygonOffset` outside decals.

`FUN_007ab760` (map-object / terrain caster batches) and `FUN_0082da40` (M2 caster batches) were
**not decompiled** — they are named but their batch selection and vertex format are unrecovered.
`ShadowMapRenderSL` is an MPQ shader effect, loaded by name; its source is not in the binary.
`ShadowMap.wfx` (`FUN_00780f50`, string at `0x00a3e750`) is the post-effect file and is a separate,
unrelated asset.

---

## 6. How it is sampled  (question 4)

### 6a. The shadow texture matrix

Built once per frame by `FUN_008750b0`, called last in `FUN_007bb570`:

```
M = BuildLightMatrix(centre = DAT_00d43260, extent = DAT_00d43258, slot = -1)   // section 3c
M = M * Scale(1.0f, -1.0f, _DAT_00d431c8)          // y flip for texture space, z -> [0,1] via 1/4000
M = M * Translate(0.5f/size, 0.5f/size, 0.0f)      // half-texel-ish offset, _DAT_00d431bc = (float)size
store COLUMNS 0,1,2 of M as three float4:
   DAT_00d43348 = (m00, m10, m20, m30)
   DAT_00d43358 = (m01, m11, m21, m31)
   DAT_00d43368 = (m02, m12, m22, m32)
then the same for the 3 cascade slots at 0xd43378, 0xd433a8, 0xd433d8 (12 float4 total)
```

Column-major storage of three columns is exactly "three `dp4`s in the vertex shader":
`uvw = float3( dot(pos4, c0), dot(pos4, c1), dot(pos4, c2) )` with no `w` divide — it is
orthographic, so there is no perspective divide anywhere in this system.

**Important range note.** The ortho produces `x,y` in **NDC [-1, 1]**, and the only post-multiplies
are a y flip and the `0.5/size` translate. So the reference's `uv` arriving at the pixel shader is
still in `[-1, 1]`; the `*0.5 + 0.5` remap must happen inside `Terrain2/Terrain3` (or a VS
permutation). *(uncertain: which stage does the remap — the MPQ shader source is not recoverable.)*
Also note `0.5/size` in NDC is a **quarter** texel, not a half texel; D3D9's texel-centre rule wants
`1.0/size` here. Both are flagged rather than silently corrected.

**For frozen, fold the remap into the matrix on the CPU** and drop the `inverse(worldStackTop)` factor
(identity at that point in the frame):

```
shadowTex = lightView * lightOrtho                      // with column 2 replaced + bias, per 3c
          * Scale(0.5f, -0.5f, 1.0f/4000.0f)
          * Translate(0.5f + 0.5f/size, 0.5f + 0.5f/size, 0.0f)
```

Then `uvw = mul(float4(worldPos, 1), shadowTex).xyz` lands directly in `[0,1]^2` plus a linear
`[0,1]` depth.

### 6b. What terrain binds (`FUN_00874660`, called from `FUN_00798da0` @ 0x00799139)

```
GxShaderConstantsSet(GxSh_Vertex, 0x25 /* c37 */, &DAT_00d43348, 12)   // main + 3 cascade matrices
GxShaderConstantsSet(GxSh_Pixel,  3   /* c3  */, &DAT_00d431d0,  8)    // the PCF kernel
GxRsSet(GxRs_Texture5, shadowMap)                                      // (&DAT_00d43250)[hwPCF]
if (q > 2) {
    GxRsSet(GxRs_Texture6, cascade0[toggle]);
    GxRsSet(GxRs_Texture7, cascade1[toggle]);
    GxRsSet(GxRs_Texture8, cascade2[toggle]);
}
```

Guarded by `DAT_00b1d51c == 0 && DAT_00d43154 > 0`. The whole call is additionally gated at the call
site by device caps `+0x138`. **Terrain does not get the light-direction constant** — only the
matrices, the taps and the samplers. (The M2/WMO binder `FUN_008744e0` uses VS c224 and PS c4 = the
light direction, PS c5..c12 = the taps, samplers s5-s7 for the cascades only; the interior binder
`FUN_008745d0` uses PS c3 = a plane and sampler s4; `FUN_00874760` uses VS c23, PS c3 = a plane,
c4 = direction, c5..c12 = taps, sampler s4.)

Frozen's `EGxRenderState` numbering matches the reference bit for bit here: `0x19 = GxRs_Texture4`,
`0x1a = GxRs_Texture5`, `0x10 = GxRs_ColorWrite`, `0x11 = GxRs_Culling`, `0x0c = GxRs_Fog`.

### 6c. The plane constant from `FUN_007bb670`

`FUN_007bb670(playerPos)` — the step-5 call — writes only two things:

```
M   = worldMatrixStackTop * viewMatrix                      // device+0x1b00+idx*0x40, device+0xf88
n   = normalize(worldMatrixStackTop.row2)                   // == (0,0,1) while world is identity
p   = transform(playerPos - DAT_00cd8f5c/60/64, M)          // the world-rebase origin
_DAT_00d4319c..a8 = (n.x, n.y, n.z, -dot(n, p))             // a 4-component plane
_DAT_00d25304     = playerPos.z + 2.0                        // _DAT_00a4040c = 2.0
```

The plane is consumed **only** by `FUN_008745d0` (`SetShadowMapGenericInterior`, PS c3) — the
MapObj/interior shaders — and the height by the caster-box builder `FUN_007bb9d0`. **The terrain path
never reads it**, so a terrain-only port can skip `FUN_007bb670` entirely. *(uncertain: the plane
mixes a world-space normal with a view-space point, which only makes sense because the world matrix
is identity there; its exact intended space was not resolved.)*

### 6d. The PCF kernel (`FUN_008742e0`)

Eight `float4` at `DAT_00d431d0`, `.zw` always 0. The `.xy` are offsets **in texels**, pre-multiplied
by `1/size`:

| c | offset (x, y) * 1/size |
|---|---|
| 0 | ( 0.8, -1.0) |
| 1 | (-0.2, -0.8) |
| 2 | ( 0.2, -0.6) |
| 3 | ( 1.0, -0.4) |
| 4 | (-0.6, -0.2) |
| 5 | ( 0.6,  0.2) |
| 6 | (-1.0, -0.4) |
| 7 | (-0.4, -0.6) |

`FUN_008742e0` also caches `_DAT_00d431bc = _DAT_00d431c0 = (float)size` and
`_DAT_00d43200 = 1/size` (which doubles as tap 3's `.x`).

### 6e. In frozen's terms — a terrain pixel shader that could implement it

Frozen's terrain VS (`g_terrainVsD3d9`, disassembled interface) uses `c0..c3` for the chunk matrix and
`c4 = (1, 0, 0.2, 0)`, inputs `v0` position, `v1` colour, `v2` blend UV, outputs `oPos`, `oT0`, `oT1`,
`oD0`. `oT2` and `c5+` are free. The minimal port:

```hlsl
// terrain_vs.hlsl (to be authored -- see task S0)
//   c5,c6,c7 = the per-chunk local -> shadow-texture matrix (chunkModel * shadowTex, columns 0..2)
oT2.xyz = float3( dot(pos4, c5), dot(pos4, c6), dot(pos4, c7) );
```

```hlsl
// terrain_ps.hlsl additions
sampler2D shadowMap : register(s5);      // GxRs_Texture5
float4 pcf[8] : register(c3);            // the table above, already divided by size

float MapShadow(float3 uvz) {
    // outside the volume -> fully lit; the volume is only 40x40 yd around the player
    float2 edge = saturate(uvz.xy) - uvz.xy;
    if (dot(edge, edge) > 0.0f || uvz.z <= 0.0f || uvz.z >= 1.0f) return 1.0f;

    float lit = 0.0f;
    [unroll] for (int i = 0; i < 8; i++)
        lit += (tex2D(shadowMap, uvz.xy + pcf[i].xy).r >= uvz.z) ? 1.0f : 0.0f;
    return lit * 0.125f;
}
```

and combine with the existing baked MCSH shadow by taking the **minimum of the two lit factors**
before the existing ambient lerp:

```hlsl
float bakedLit   = blend.a;                       // MCSH, 1 = sun reaches
float dynamicLit = MapShadow(t2);
float shadow     = lerp(v0.a, 1.0f, min(bakedLit, dynamicLit));
return float4(colour * v0.rgb * shadow, 1.0f);
```

That is the correct combination semantically (both are occlusion of the *same* sun, so a pixel is lit
only if neither occludes it) and it reuses the ambient-ratio machinery frozen already has in `v0.a`.
*(uncertain: the reference's exact combine is inside `Terrain2`/`Terrain3`, which is not recoverable —
`min` is the reasoned choice, not a recovered one.)*

Notes for the frozen implementation:
* With `hwPCF` **off** the map is `R32F` holding linear `depth/4000`, so a plain `tex2D(...).r >= z`
  comparison is right and ps_2_0 can do all 8 taps (8 `texld` + 8 compares fits easily).
* With `hwPCF` **on** the map is a `D24X8` depth texture; on D3D9 the hardware does the compare and
  the 2x2 bilinear blend inside `tex2D`, and the PS just averages the 8 returned values. Do **not**
  implement that first — it needs a caps check (`D3DFMT_D24X8` usable as a texture) and it is the
  only reason `Terrain2_pcf` exists.
* Frozen's terrain draws per-chunk with `ChunkMatrixT` at VS c0..c3; building `chunkModel * shadowTex`
  on the CPU per chunk and uploading it at c5..c7 costs three more `GxShaderConstantsSet` float4s per
  chunk and keeps the shader at 3 `dp4`s.

---

## 7. Ordered implementation task list

Each task names the frozen file/function and the reference function it ports.

### S0 - prerequisite: author `terrain_vs.hlsl`
*Change*: new `src/world/shaders/terrain_vs.hlsl`, regenerate `src/world/TerrainShadersD3d9.hpp`.
*Ports*: nothing; recovers a lost source. *Prereq*: none.

The terrain vertex shader exists only as bytecode: `src/world/shaders/` has `terrain_ps.hlsl`,
`blob_decal_ps.hlsl` and `detail_ps.hlsl` but **no** `terrain_vs.hlsl`. Nothing else in this list can
add `oT2` until it exists. Re-author it to the disassembled interface (`oPos` from `c0..c3`,
`oT0 = v2.xy`, `oT1 = v0.xy * 0.2`, `oD0 = v1`) and **diff the recompiled bytecode against the
existing array** before adding anything — the blob-shadow pass relies on this program's `oPos` being
bit-identical to the pass that wrote depth (`parity-shadows.md` T1, CLAUDE.md priority 1). Once the
recompile is byte-identical, adding `oT2` is safe; re-run the blob-shadow check anyway.

### S1 - `CShadowCache` allocation + CVars
*Change*: new `src/world/MapShadow.cpp`/`.hpp`; register the CVars beside the other WorldParam CVars.
*Ports*: `FUN_00875d30`, `FUN_00874240`, `FUN_008742e0`, `FUN_00874210`, `FUN_008740d0`,
`FUN_00873f60`, `FUN_00873f80`, `FUN_00873ff0`, `FUN_0078dcb0`, `FUN_0078e070`.
*Prereq*: none.

Allocate one `GxTex_R32F` (or `GxTex_D24X8` under hwPCF) render target of `1024`/`2048` per the size
rule, the dummy `GxTex_Argb8888` colour buffer for the hwPCF path, the deferred-realloc flag, the
device-lost hook, and the 8-entry PCF table. **Start with `extShadowQuality = 1` and hwPCF off** —
one map, R32F, no cascades. Register `extShadowQuality`, `hwPCF`, `shadowCull`, `shadowScissor`,
`shadowInstancing`; `mapShadows` and `shadowLevel` are already registered.

### S2 - the light camera and the shadow texture matrix
*Change*: `MapShadow.cpp`: `MapShadowSetup()` / `MapShadowBuildMatrix()`.
*Ports*: `FUN_007bb570` (direction), `FUN_007bb3e0` (focus), `FUN_007bac10` (view/proj),
`FUN_008750b0` (texture matrix). *Prereq*: S1.

Direction from the DayNight light block with the `*5, clamp -1.2` rule; focus = the player position
snapped; `LookAt(centre - dir*2000, centre, (1,0,0))`; `Ortho(-20, 20, -20, 20, 1, 4000)`; column 2
from the view matrix; bias `-0.1`; then the `Scale(0.5,-0.5,1/4000)` + `Translate(0.5+0.5/size, ...)`
remap (section 6a). Unit-testable without any rendering: feed it the player position and assert that
a point at the player's feet maps to `uv ~= (0.5, 0.5)` and `z ~= 2000/4000`.

### S3 - render the map (units only)
*Change*: `MapShadow.cpp`: `MapShadowRender()`; call it from `CGWorldFrame::OnWorldRender`
(`src/ui/game/CGWorldFrame.cpp`) **after** `scene->AdvanceTime`/`Animate` and **before**
`TerrainRender`. This requires moving the Animate call up — see section 1.
*Ports*: `FUN_00875f80` (the pass driver), `FUN_007bbc50` (the draw), `FUN_0082da40` (M2 casters).
*Prereq*: S2, plus render-to-texture (landed 2026-09-15).

Bind the target, `GxSceneClear(3, white)`, viewport = the full map, culling off, fog off, then draw
every visible `CM2Model` in the scene with a minimal depth-only vertex+pixel program that writes
`lightDepth/4000` to `.r`. Frozen has no `ShadowMapRenderSL`; write a `shadowmap_vs.hlsl` /
`shadowmap_ps.hlsl` pair beside the terrain shaders (VS: `oPos` from a per-model
`model * lightView * lightProj`, `oT0.x` = the linear light depth; PS: `return oT0.xxxx * c0.w`).
Restore the target, viewport and view matrix afterwards. **Verify by dumping the render target to a
file before wiring the terrain read** — a white square with dark blobs where units stand is the
checkpoint.

### S4 - sample it in the terrain shader
*Change*: `src/world/shaders/terrain_vs.hlsl` (add `oT2`, `c5..c7`),
`src/world/shaders/terrain_ps.hlsl` (add `s5`, `c3..c10`, the `MapShadow()` helper and the `min`
combine), `RenderShaded` in `src/world/Terrain.cpp:2114` (upload `chunkModel * shadowTex` at c5..c7
per chunk, the tap table at PS c3, `GxRsSet(GxRs_Texture5, shadowMap)`).
*Ports*: `FUN_00874660`. *Prereq*: S0, S3.

Gate the whole thing on `extShadowQuality > 0` and on the map having been rendered this frame;
otherwise leave `terrain_ps` on its current path so nothing regresses. This is the step that first
puts a dynamic shadow on the ground.

### S5 - shadows on WMO floors and models
*Change*: the WMO pass and the detail-doodad pass in `src/world/Terrain.cpp` (both already use
`s_terrainVS`), and `CM2SceneRender::Draw` in `src/model/CM2SceneRender.cpp:101` where
`CShadowCache::SetShadowMapGenericGlobal` is commented out.
*Ports*: `FUN_008744e0` (M2/WMO binder, VS c224 / PS c4 / PS c5..c12). *Prereq*: S4.

Because frozen already drives terrain, detail doodads and WMO groups through one vertex program, WMO
floors come nearly free once S4 lands — same `oT2`, same sampler. M2 receivers need the same three
constants in the model vertex program.

### S6 - environmental casters (`extShadowQuality >= 3`)
*Change*: `MapShadowRender()` gains a terrain + WMO caster pass.
*Ports*: `FUN_007bd200`'s `mode & 0xC` branch, `FUN_007ab760`. *Prereq*: S3.

Draw the visible terrain chunks and WMO groups into the map with the same depth-only program. This
is what turns "units cast shadows" into "the world casts shadows"; it is also where the 40-yard
volume starts to feel small, which is why the reference pairs it with the cascades.

### S7 - hardware PCF (`Terrain2_pcf` / `Terrain3_pcf`)
*Change*: `CShadowCache` allocation (D24X8 + dummy colour), a caps probe for `D3DFMT_D24X8` as a
texture, a second terrain PS permutation.
*Ports*: `FUN_0078e070`, `FUN_00872ad0`, `FUN_0079e4f0`. *Prereq*: S4. Pure performance; defer.

### S8 - cascades (the 40/160/640 slots, `extShadowQuality >= 3`)
*Change*: the slot array in `CShadowCache`, the progressive atlas refresh, three more samplers.
*Ports*: `FUN_00874890`, `FUN_00874fb0`, `FUN_00875760`. *Prereq*: S6.

The most code for the least visible gain at frozen's current state. Defer until S4-S6 are verified on
screen.

### Relationship to `parity-shadows.md`
`parity-shadows.md` T10 ("Map shadow map") is this whole document; it can be replaced by a pointer.
The two systems are mutually exclusive by design: `FUN_007e49e0` gates blob shadows on
`extShadowQuality < 1`, so once S4 lands the blob path should be gated the same way.

---

## 8. What could not be recovered

* **`ShadowMapRenderSL`** — an MPQ shader effect selected by name (`FUN_00876530` -> `FUN_0055f4d0`).
  Its HLSL/asm is not in the executable. The port has to invent an equivalent depth-only program;
  the only recovered facts about its interface are **PS c0 = (0,0,0, 1/4000)** and **VS c14..c16 = 3
  float4** set by `FUN_007bbc50`.
* **`Terrain2` / `Terrain3` (and their `_pcf` variants)** — likewise MPQ assets. The register layout
  is fully recovered (VS c37..c48, PS c3..c10, samplers s5..s8) but the sampling code, the
  `[-1,1] -> [0,1]` remap stage, and the exact combine with the MCSH alpha are not. Section 6e is a
  reasoned reconstruction, not a decompilation.
* **`FUN_007ab760` / `FUN_0082da40`** — the caster batch draws. Named and located, not decompiled.
* **`shadowLevel`** — registered, validated (0-1), announced as restart-only, but no reader was found
  anywhere in the shadow path. Treat as a no-op.
* **`_DAT_00d4319c` plane space** — the mix of a world-space normal with a view-space point in
  `FUN_007bb670` was not resolved. It only feeds the interior/MapObj shaders, so it does not block
  the terrain work.
* **Mode bit meanings** — the per-slot modes 1 / 3 / 5 / 8 / 0xd are a bitmask; only `& 0xC`
  ("include environmental casters") was decoded. What distinguishes mode 3 (pass A) from mode 1
  (pass B) inside `FUN_007bb9d0` is unread.
* **The texel snap in `FUN_00875f80`** quantises to `1/size` world units rather than the texel size
  `2*extent/size`. Reported as compiled; it looks like a bug in the original.
* **The half-texel offset** is `0.5/size` in NDC = a quarter texel, where D3D9's rule wants
  `1.0/size`. Reported as compiled.

---

## 9. Constant reference

| symbol | value | meaning |
|---|---|---|
| `_DAT_009ebf34` | 5.0 | light-direction z multiplier |
| `_DAT_00a400fc` | -1.2 | light-direction z floor (clamps the elevation) |
| `_DAT_00a400e4` | 2000.0 | eye distance back along the light |
| `_DAT_00a3fa3c` | 4000.0 | ortho far plane (near = 1.0) |
| `_DAT_00a40100` | 0.00025 | 1/4000, the depth scale (PS c0.w and `_DAT_00d431c8`) |
| `_DAT_009f281c` | 20.0 | main-map ortho half-extent (`_DAT_00d43258`) |
| `DAT_00b1d520[]` | 40, 160, 640 | cascade half-extents |
| `DAT_00b1d52c[]` | 4, 16, 1024 | cascade re-centre thresholds (squared distance) |
| cascade snap grids | 2.0, 4.0, 16.0 | `_DAT_00a4040c`, `_DAT_009e8d2c`, `_DAT_009e8ccc` |
| `_DAT_009e8d84` | 0.2 | depth-bias unit |
| bias multipliers | 0.5 / 2.0 / 4.0 / 8.0 | main map / cascade 0 / 1 / 2 |
| hwPCF extra bias | 0.5 | added when `DAT_00d43014` |
| `_DAT_009fc830` | 0.6666667 | 2/3, cascade 0/1 atlas cell size |
| `_DAT_009f98d8` | 0.4 | 2/5, cascade 2 atlas cell size |
| `_DAT_00a4040c` | 2.0 | also `_DAT_00d25304 = playerZ + 2.0` |
| `_DAT_009e2ec4` | 0.5 | rounding / half-texel numerator |
| `_DAT_009e23ac` | 4294967296.0 | the uint -> float fixup (so `DAT_00d43150` is unsigned) |
| `DAT_00b1d554[]` | 0,1,1,2,2,3,3 | shader permutation index per `extShadowQuality` |
| up vector | (1, 0, 0) | `DAT_00d43278`, the LookAt up hint |

## 10. Address index

| address | role |
|---|---|
| FUN_007bb570 | MapShadow: per-frame entry (direction, focus, render, bind) |
| FUN_007bb670 | MapShadow: the interior plane constant + `playerZ + 2` |
| FUN_007bb3e0 | MapShadow: the focus point (player, else camera) |
| FUN_007bd3a0 | MapShadow: registers the four `CShadowCache` callbacks + 3 CVars |
| FUN_007bac10 | callback 0: build the light view * ortho + depth bias |
| FUN_007bafd0 | callback 1: build the light camera + frustum (and `shadowCull` tightening) |
| FUN_007bd200 | callback 2: gather casters (`mode & 0xC` gates the environment gather) |
| FUN_007bbc50 | callback 3: render one shadow map (`ShadowMapRenderSL`) |
| FUN_007bb830 | clear a shadow-map sub-rect when nothing casts |
| FUN_007bb9d0 | dynamic (M2) caster gather |
| FUN_00875f80 | `CShadowCache`: the pass driver (A, B, cascades) |
| FUN_00875d30 / FUN_00874240 | allocate / free the targets |
| FUN_008742e0 | build the 8-tap PCF table |
| FUN_008750b0 | build the shadow texture matrices (12 float4) |
| FUN_008753f0 | construct the ~0xb90 render-params scratch struct |
| FUN_00874890 / FUN_00874fb0 / FUN_00875760 | cascade slot refresh / bind / clear |
| FUN_00874660 | bind for **terrain** (VS c37, PS c3, s5..s8) |
| FUN_008744e0 | bind for **M2 / WMO** (VS c224, PS c4 + c5, s5..s7) |
| FUN_008745d0 | `SetShadowMapGenericInterior` (PS c3 plane, s4) |
| FUN_00874760 | bind variant (VS c23, PS c3/c4/c5, s4..s7) |
| FUN_00874210 / FUN_008740d0 / FUN_00873f60 | `extShadowQuality` set / validate / describe |
| FUN_00873f80 / FUN_00873ff0 | effective quality / shader permutation index |
| FUN_00872ad0 / FUN_0079e4f0 | hwPCF toggle / terrain pixel-shader reload |
| FUN_006c0050 / FUN_006bf4c0 | LookAt / OrthoOffCenter |
| FUN_004c1930 / FUN_004c2f90 / FUN_004c1f00 / FUN_004c23d0 | determinant / inverse / multiply / transpose |

---

## 11. MEASURED (2026-09-15): section 3a is CORRECT, and frozen was applying it to the wrong vector

**Retraction.** An earlier version of this section claimed section 3a's `z *= 5`, clamp `>= -1.2`
rule did not hold, on the evidence that the reference's shadow texture matrix column 2 was nearly
perpendicular to what the rule predicts. That conclusion was wrong. The rule is right, it is
implemented, and the reference stores its result at **`DAT_00d43180`**, read live as
`-0.3956 -0.3956 -0.8288` against `-0.3954 -0.3954 -0.8291` computed from the same client's own
outdoor light. The mistake was assuming column 2 of the texture matrix is the light direction; the
direction the matrix column encodes is a separate vector stored at `DAT_00d4318c`.

**The real bug, and it was frozen's.** The rule operates on the direction the light TRAVELS, which
points downward. frozen stores the direction TOWARD the light, whose z is positive. The clamp is
one-sided, so applied to a positive z **it never engages**, and the clamp is precisely what pins the
light near 52 degrees of elevation.

| | z of the shadow light |
|---|---|
| reference | -0.829 |
| frozen, before | +0.963 (clamp never reached) |
| frozen, after | -0.826 |

Every shadow was therefore cast from a far steeper angle than the reference's, which is wrong length
and wrong direction on every surface. Fixed in `MapShadowSetup` by flipping into the reference's
convention BEFORE applying the rule. Verified live: frozen now reports
`light(-0.398 -0.398 -0.826)` against the reference's `-0.3956 -0.3956 -0.8288`, the residual being
time-of-day drift between the two readings.

This also settles the earlier sign change, which was made on single-instant evidence and flagged as
unjustified: with the direction now derived in the right convention, the eye sits back along the
travel direction and therefore above the ground, matching `eye = centre - lightDirWorld * 2000`.

**Still open:** what `DAT_00d4318c` is. It is what the texture matrix's column 2 carries, it is not
the light direction, and nothing yet explains how it is derived.

Confirmed live against the running reference and unchanged: box half-extent 20, far plane 4000,
depth scale 0.00025, up hint (1, 0, 0), cascade extents 40/160/640, cascade thresholds 4/16/1024,
and the first two PCF taps at (0.8, -1.0)/1024 and (-0.2, -0.8)/1024.
