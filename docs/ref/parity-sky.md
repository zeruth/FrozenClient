# Sky / atmosphere (DayNight) parity

Reference program: `RunicWorldGame.exe` (Ghidra project `RunicWorld`). Raw dumps produced for this
pass: `win-decomp-sky-lightblock.txt`, `win-decomp-sky-dome.txt`, `win-decomp-dnbase-callers.txt`,
`win-decomp-lighting-api.txt`, `win-datarefs-lightblock.txt`, `win-datarefs-lightdir.txt`,
`win-datarefs-fog.txt`, `win-callers-daynightbase.txt` (plus the earlier `win-decomp-daynight-*.txt`
and `win-datarefs-daynight-*.txt`). Float constants were read straight out of the PE `.rdata`
(image base 0x400000), so the numbers below are exact, not inferred.

---

## 0. The DayNight singleton, corrected

`FUN_007ecef0` @ 0x007ecef0 returns `&DAT_00d38b00` - the DayNight block is a plain global struct,
not a heap object. Field map established this pass (offsets from 0xd38b00):

| off | global | meaning | writer |
|---|---|---|---|
| +0x00 | DAT_00d38b00 | calendar day / date id | `FUN_004f8410`, `FUN_007e2730` |
| +0x04 | DAT_00d38b04 | **time of day, normalised 0..1** (band evaluator input) | `FUN_004f8410` |
| +0x08 | | time-of-day override value | `FUN_004f8410` |
| +0x0c..0x14 | | active player position | `FUN_004f8410` |
| +0x18..0x20 | DAT_00d38b18.. | **camera position** | `FUN_004f8410`; also `FUN_007816f0` in the override branch |
| +0x24..0x2c | DAT_00d38b24.. | light-query position (camera target) | `FUN_004f8410` |
| +0x30..0x38 | DAT_00d38b30.. | **normalised camera FORWARD vector** - *not* the sun | `FUN_004f8410` |
| +0x3c | DAT_00d38b3c | camera yaw = `atan2(fwd.y, fwd.x)` wrapped to [0,2pi) | `FUN_007f3920` |
| +0x40 | DAT_00d38b40 | far clip | |
| +0x44 / +0x48 | | frame time (s) / frame delta | `FUN_004f8410` |
| +0x4c | DAT_00d38b4c | death/fade scalar source -> `DAT_00d38b88` | `FUN_004f8410` |
| +0x5c..+0x84 | | LightSkybox slots (model, weight, flags) x4 | `FUN_007f31c0`, `FUN_007f3230` |
| +0x8c/+0x90/+0x94/+0x98 | | **fog colour / start / end / rate** | `FUN_007f16f0` |
| +0xa0..+0xac | | interior (WMO-override) fog set | `FUN_007f16f0` |
| +0xd4..+0x170 | DAT_00d38bd4.. | **DNInfo**: the blended 0x9c-byte light record (39 dwords) | `FUN_007f3230` |
| +0x19c..+0x1a4 | DAT_00d38c9c.. | **outdoor light DIRECTION** | **`FUN_007eea90`** |
| +0x1a8 | DAT_00d38ca8 | outdoor **diffuse** colour (BGRA) | `FUN_007ee750` |
| +0x1ac | DAT_00d38cac | outdoor **ambient** colour (BGRA) | `FUN_007ee750` |

The two earlier hunts failed because they assumed `+0x30` was the sun. It is the **camera facing**:
`FUN_007ef6e0` @ 0x007ef6e0 dots it with `normalize(glareSourcePos - cameraPos)` to decide glare
strength, and `FUN_007f3920` turns it into a yaw that rotates the sky-dome / cloud UVs. Its writer is
`FUN_004f8410` @ 0x004f8410 (in `WorldFrame.cpp`, string refs at lines 0x805/0x810), which fills
`DayNight[0x0c..0x4c]` from the camera every frame - exactly the data frozen already keeps in
`CWorld::s_cameraDir` / `CWorld::GetCameraPos`.

---

## 1. The outdoor light direction - SOLVED

**Writer: `FUN_007eea90` @ 0x007eea90**, called from `FUN_007f3920` @ 0x007f3920 (itself called from
the DayNight update `FUN_007816f0` @ 0x007816f0, once per frame).

It lazily initialises two hard-coded 4-key wrap-around bands (guarded by the bits of `DAT_00d39104`)
and evaluates both at the current time of day with the standard band evaluator
`FUN_007ed3b0` @ 0x007ed3b0 (count in ESI = 4, key array in EDI; the table/register pairing was
verified by disassembling the two call sites at 0x7eeb6e and 0x7eeb7c):

```
theta band (table @ 0x00d390e4):   (0.00, 2.2165682)  (0.25, 1.9198622)
                                   (0.50, 2.2165682)  (0.75, 1.9198622)   radians = 127 deg / 110 deg
phi   band (table @ 0x00d390c4):   (0.00, 3.9269910)  (0.25, 3.9269910)
                                   (0.50, 3.9269910)  (0.75, 3.9269910)   radians = 225 deg, constant
```

The trig is the client's polynomial cosine: `g(x) = 1 - (6 - 4*frac(x))*frac(x)^2`, sign-flipped when
`floor(x)` is odd, i.e. `g(x) ~= cos(pi*x)`; the code feeds it `angle * (1/pi)` (0x00a1e8d4 =
0.3183099) and `angle*(1/pi) - 0.5` to get cos and sin respectively. So:

```
theta = Band(t)          // 127 deg at t=0 and t=0.5, 110 deg at t=0.25 and t=0.75, wrap-lerped
phi   = 3.9269910        // 5*pi/4, constant

DayNight[0x19c] = cos(phi) * sin(theta)
DayNight[0x1a0] = sin(phi) * sin(theta)
DayNight[0x1a4] = cos(theta)
```

Sample values: `t = 0 or 0.5` -> `(-0.564718, -0.564718, -0.601815)`; `t = 0.25 or 0.75` ->
`(-0.664463, -0.664463, -0.342020)`. Unit length by construction.

**Sense of the vector.** It points *away* from the light (z < 0, i.e. downward). Consumers negate it:
`FUN_007cfbe0` (terrain shader constants) computes `fVar1 = -local_20; ...` before transforming it
into the vertex-shader constant, and `FUN_007bb570` (shadow-map view) normalises
`(dir.x, dir.y, dir.z*5)` to aim the shadow camera. So frozen's `s_outdoorDirection`, which is used as
"direction **to** the light" and dotted straight into N.L, must hold the **negation**:

```
CWorld::s_outdoorDirection = ( -cos(phi)*sin(theta), -sin(phi)*sin(theta), -cos(theta) )
                           = ( 0.564718, 0.564718, 0.601815 ) at midnight / noon
                           = ( 0.664463, 0.664463, 0.342020 ) at 06:00 / 18:00
```

frozen's current placeholder `{-0.402096, -0.301572, 0.864504}` is roughly 180 deg wrong in azimuth
(it lights from -X/-Y instead of +X/+Y) and about 23 deg too high. Those bytes do **not** appear
anywhere in `RunicWorldGame.exe` (the whole image was searched for all three floats), so the constant
was never taken from this reference.

**So: the sun barely moves.** Only the elevation wobbles, twice a day, between 37 deg (t=0, t=0.5)
and 20 deg (t=0.25, t=0.75) above the horizon; the azimuth is pinned at 225 deg for every zone and
every map. This is a hard-coded table, not a Light.dbc field. The *visible* sun/moon disc does move
(`FUN_007eecc0` @ 0x007eecc0) and is completely independent of the lighting direction.

**Propagation chain** (all confirmed):

```
FUN_007eea90  -> DayNight[0x19c..0x1a4]
FUN_007816f0  -> FUN_00834ae0(&g_light[0x58], &DayNight[0x19c])   // CLight::SetDirection, normalises
                 => DAT_00ce04a8 + 0x7c..0x84
              -> g_light[0x58]+0x30 = DayNight ambient   (bytes at +0x1ae/+0x1ad/+0x1ac)
              -> g_light[0x58]+0x3c = DayNight diffuse   (bytes at +0x1aa/+0x1a9/+0x1a8)
              -> g_light[0x58]+0x48 = DNInfo-derived colour (DayNight +0xf8..0xfa)
FUN_00834f60(ctx, &g_light[0x58])   // CLighting::AddLight: ctx+0x54 += ambient;
                                    //   FUN_00834dc0(diffuse, dir); ctx+0x6c += third colour
FUN_008355d0(ctx, &dir, &c0, &c1, &c2)   // read back: ctx+0x78 = accumulated direction
```

`FUN_00834dc0` @ 0x00834dc0 is byte-for-byte what frozen already has as `CM2Lighting::AddDiffuse`
(same three colour-weighted accumulators plus the 0.212671 / 0.71516 / 0.072169 luminance term), and
`FUN_00834d90` @ 0x00834d90 is `AddAmbient`. `FUN_00780cd0` @ 0x00780cd0 is the reference's
`CWorld::LightingCallback`: outdoors it calls `AddAmbient(DayNight+0x1ac)` then
`AddDiffuse(DayNight+0x1a8, DayNight+0x19c)`; indoors it instead sets fog from
`DayNight+0x8c/0x90/0x94/0x98` and defers to the interior light provider.

### Light.dbc -> DNInfo slot mapping (needed for sections 2 and 4)

`FUN_007ebff0` @ 0x007ebff0 is the Light.dbc -> DNInfo loader; it is a literal list of
`FUN_007ebf30(ctx, params, row, band)` calls, so the mapping is unambiguous:

```
LightIntBand band 0     -> DNInfo[1]        LightIntBand band 8 -> DNInfo[2]
LightIntBand band 1     -> DNInfo[0]        bands 9..17         -> DNInfo[9..0x11]
LightIntBand bands 2..7 -> DNInfo[3..8]
LightFloatBand 0 -> DNInfo[0x12]   (fog end)
LightFloatBand 1 -> DNInfo[0x13]   (fog start scalar, clamped to [-1, 1] on load)
                    DNInfo[0x14] = 1.0 (fog rate)
LightFloatBand 2..5 -> DNInfo[0x17..0x1a]
Light.dbc row: +8/+0xc/+0x10 (x/y/z) -> DNInfo[0x1f]/[0x25]/[0x16];
               +0x14/+0x18 (falloff start/end) -> DNInfo[0x1b]/[0x1c]
```

`FUN_007ee750` @ 0x007ee750 then does `DayNight[0x1a8] (diffuse) = DNInfo[1]` and
`DayNight[0x1ac] (ambient) = DNInfo[0]`, i.e. **LightIntBand band 0 = diffuse, band 1 = ambient** -
which is what frozen's `ComputeLightColors` already assumes. No change needed there. What *is* wrong is
the sky-band mapping (section 2).

---

## 2. Sky dome construction - SOLVED

Builder: **`FUN_007f2470` @ 0x007f2470** (`BuildSkyDome(scale)`, called once from the DayNight init
`FUN_007f2790` with scale 1.0). Drawer: **`FUN_009acb00` @ 0x009acb00**. Per-frame colour update:
**`FUN_007f0530` @ 0x007f0530** (called from `FUN_007f3230`).

Geometry (all constants read from `.rdata`):

* **24 segments** per ring (`this[0x24] = 0x18`; azimuth step 0x00a41cec = 1/24 turn).
* **7 rings**, zenith angles = `table[0x00a41a90][i] * pi`:

  | ring | table value | zenith angle | elevation | colour source |
  |---|---|---|---|---|
  | 0 | 0.00 | 0 deg (pole, 1 vertex) | 90 deg | DNInfo[3] = **LightIntBand band 2** (sky top) |
  | 1 | 0.17 | 30.6 deg | 59.4 deg | DNInfo[4] = band 3 |
  | 2 | 0.20 | 36 deg | 54 deg | DNInfo[5] = band 4 |
  | 3 | 0.23 | 41.4 deg | 48.6 deg | DNInfo[6] = band 5 |
  | 4 | 0.24 | 43.2 deg | 46.8 deg | DNInfo[7] = band 6 (horizon) |
  | 5 | 0.25 | 45 deg | 45 deg | DNInfo[8] = **band 7 (fog colour)** |
  | 6 | 1.00 | 180 deg (pole, 1 vertex) | -90 deg | DNInfo[8] = band 7 (fog colour) |

* Vertex: `x = sin(az)*sin(theta)*scale`, `y = cos(az)*sin(theta)*scale`, `z = cos(theta)*scale`
  (plus a constant z offset that Ghidra renders as `-fcos(DAT_009eaf48)`; the operand decodes as an
  x87 register constant, so the offset is almost certainly 0 - *uncertain*).
  `FUN_004c1cf0(angle, 0 or pi)` detects the degenerate pole rings and emits a single vertex for them.
* **122 vertices** (1 + 5*24 + 1), **300 indices**, one triangle strip (`this[0x28]` / `this[0x2a]`),
  positions stride 0xc and a per-vertex BGRA colour stride 4, **no texture and no texcoords**.
* Draw state (`FUN_009acb00`): blend mode 3, fog **off** (RS 0x11 cleared), no lighting, no alpha
  test, drawn inside the far-depth viewport that `FUN_007f09b0` installs (`DAT_00adeef0/f4`).
* Vertex-count evidence from the colour writer `FUN_007f0530`: 1 zenith colour, then 4 rings x 24 from
  successive bands, then 24 + 1 of `DAT_00d38bf4` (= DNInfo[8]) = 122 exactly.

**How the horizon blends to fog:** it does not "blend toward" the fog colour - the bottom two rings
*are* LightIntBand band 7, the very colour the distance fog uses (`FUN_007f16f0` copies DNInfo[8] into
`DayNight+0x8c`). Because fog is disabled while the dome draws, the fogged terrain horizon and the
dome's lowest ring converge on the same RGB automatically. Everything below 45 deg elevation is one
flat-shaded band of fog colour running down to the nadir.

**Azimuthal variation.** `FUN_007f0530` does not paint each ring one flat colour: for the four middle
rings it walks the 24 segments stepping a band parameter by `-1/24` starting from
`DayNight[0x3c] * (1/2pi) + 0.25` (the camera yaw) and evaluates a band per vertex, so the dome colour
rotates with the camera / sun azimuth. The exact per-vertex band pointers are passed in registers and
did not survive decompilation - *uncertain*, and not needed for a first-cut port.

### frozen gaps

`SkyRender` (`src/world/Terrain.cpp:4452`) uses a procedural 12x24 dome coloured by `z / SKY_RADIUS`
across 5 colours. Two concrete errors:

1. Wrong ring distribution. The reference puts **five** rings inside the 45..90 deg elevation cap and
   nothing between 45 deg and the nadir; frozen spreads 12 rings evenly, so the gradient sits far too
   low and the horizon band is far too thin.
2. Wrong band mapping. `ComputeLightColors` (`src/world/CWorld.cpp`) fills `sky[0..4]` from bands
   6,5,4,3,2 and `fog` from band 7, and `SkyRender` lerps `GetSkyColor(0..4)` bottom-to-top. The
   reference's stack is **band 2 (top), 3, 4, 5, 6, then band 7 twice** - six colours, not five, and
   the last one is the fog colour. frozen's dome never reaches the fog colour at the horizon, which is
   why its horizon and its fogged terrain do not meet.

---

## 3. Clouds and sun/moon glare

> **Superseded in part by `parity-sky-bodies.md`.** The cloud *texture* and the sun/moon *disc
> positions* are both fully recovered there: the clouds have no texture file (procedural 4-octave
> value noise regenerated into two callback-backed textures), and `FUN_007eecc0` is decoded with
> every band table. The glare textures are `Textures\sunGlare.blp` / `Textures\moonGlare.blp`, not
> the disc textures, and all its cone/fade constants are listed there.

### Clouds

Builder: **`FUN_007f20e0` @ 0x007f20e0** (`BuildCloudMesh(scale)`, from the init `FUN_007f2790`).
Per-frame vertex/UV update: **`FUN_007efd00` @ 0x007efd00** (reached via `FUN_007f1010`, called from
`FUN_007816f0`). Drawer: **`FUN_009acd40` @ 0x009acd40**.

* **12 rings x 16 segments** (azimuth step 0x00a02d7c = 0.3926991 = 2pi/16).
* Ring zenith angles = `table[0x00a41ad4][i] * pi` =
  `{0, .025, .05, .075, .1, .125, .15, .175, .205, .23, .245, .25} * pi`
  -> 0, 4.5, 9, 13.5, 18, 22.5, 27, 31.5, 36.9, 41.4, 44.1, 45 deg. A much finer dome than the sky
  dome, and it also stops at 45 deg from the zenith.
* Per-ring alpha byte table at `0x00a41b04` = `{255 x9, 128, 0, 0}` - the cloud sheet fades out over
  its last three rings so it never reaches the horizon line.
* UVs are a planar projection: `u = sin(az)*r + 0.5`, `v = cos(az)*r + 0.5`,
  `r = ringIndex * (1/11) * 0.5` (0x00a41ce8 = 0.0909091), so the radius runs 0..0.5 of the texture.
* 177 vertices used of 192 allocated, **374 indices** (0x176), one triangle strip.
* `FUN_009acd40` draws it textured from `cloudStruct[0x90 + cloudStruct[0xb]*4]` (a small texture
  array indexed by the current cloud type; the `SkyCloudLOD` cvar handler is `FUN_007f1cd0`,
  registered in `FUN_007f2790`), blend mode 2 (alpha), fog **off**, per-vertex BGRA colour at `+0x74`
  (replaced by flat white plus a fade alpha while the loading fade `DAT_00d38184 & 1` is set). Cloud
  tint comes from LightIntBand bands 9..12 via the DNInfo *(band indices uncertain)*.

### Sun / moon glare

Post pass: **`FUN_007f0870` @ 0x007f0870**, which runs only when `DAT_00d38ccc != 0` and does
`FUN_007ef6e0(glare, dt)` + `FUN_009ac400(glare)` twice - once for the sun glare object, once for the
moon's. The `SkySunGlare` cvar handler `FUN_007ece40` @ 0x007ece40 flips `DAT_00d38eb0` /
`DAT_00d38f60` (and prints "SunGlare enabled.  Don't look directly at it.").

`FUN_007ef6e0` (the intensity solver), per glare object (dword indices):

```
[3..5]  world position of the celestial body
[8]     base size,            [9]    computed size out
[0xa]   fade-in rate/s,       [0xb]  fade-out rate/s
[0xc]   smoothed alpha,       [0xd]  target alpha
[0x24], [0x25]  size scale at threshold / on-axis
[0x26]  cos threshold (the cone half-angle inside which the glare starts)
[0x27], [0x28]  alpha scale at threshold / on-axis
```

```
target = v_visible() * v_weather() * (1 - maxSkyboxWeight) * band(time) * v_occluded()
alpha  = target slewed toward by the fade rates * dt
c      = max(dot(cameraForward, normalize(bodyPos - cameraPos)), threshold)
c      = (c - threshold) / (1 - threshold)
size   = lerp([0x24], [0x25], c) * baseSize
alphaB = alphaB * lerp([0x27], [0x28], c) * smoothedAlpha
```

`FUN_009ac400` @ 0x009ac400 then draws one screen-facing quad (4 verts from `DAT_00af4cb0`, UVs
`DAT_00af4ce0`, indices `DAT_00af4b70`) at `bodyPos - cameraPos` scaled by `obj[0x24]`, texture from
`obj[0x1c]`, flat colour from `obj[0x18]`, **additive** blend (`FUN_00408bf0(6, 3)`), fog off, inside
its own far-depth viewport.

Which body is the glare source is picked in `FUN_007efae0` @ 0x007efae0 by the time of day:
`t < 0.2013889` (04:50) **or** `t >= 0.9236111` (22:10) -> the night body (`DAT_00d38e48..0x50`),
otherwise the sun (`DAT_00d38e28..0x30`). Textures loaded by the init: `Textures\sunCenter.blp`,
`Textures\moon.blp`, `Textures\moon02.blp`. The body positions themselves are computed by
`FUN_007eecc0` @ 0x007eecc0 (orbit maths not decoded - *uncertain*).

Note also that the three `FUN_009ac660` @ 0x009ac660 calls inside `FUN_007f09b0` are **not** dome
layers: `FUN_009ac660` draws a single camera-relative textured mesh built by
`FUN_007edbe0`/`FUN_007edee0` at `param_1[0..2]` with the texture at `param_1[4]`, i.e. the sun disc
and the two moon discs. The gradient dome is `FUN_009acb00` alone. (The inventory's "dome/glare
layers x3" row should be corrected.)

---

## 4. Fog - CONFIRMED, with formula corrections

Producer: **`FUN_007f16f0` @ 0x007f16f0** (the function the inventory labelled "DNClouds"; the
DataRefs in `win-datarefs-fog.txt` show it is the sole writer of every fog field). Applier:
`FUN_00781610` @ 0x00781610 -> `GxRs_FogStart = DayNight+0x90`, `GxRs_FogEnd = DayNight+0x94`,
`GxRs_FogColor = DayNight+0x8c` (start/end are only pushed when the camera mode `*(cam+0xb4) == 0`;
the colour always is).

```
// clear-weather / above-water path (DAT_00d38ad0 == 0)
fogEnd   = min(farClip /* DayNight+0x40 */, DNInfo[0x12] /* LightFloatBand 0 */)
fogStart = fogEnd * DNInfo[0x13]        // LightFloatBand 1, already clamped to [-1,1] on load
fogColor = DNInfo[8]                    // LightIntBand band 7
fogRate  = DNInfo[0x14]                 // = 1.0

// underwater / override path (DAT_00d38ad0 != 0)
fogEnd = min(farClip, DAT_00d38ab0); fogStart = fogEnd * DAT_00d38aac;
fogColor = DAT_00d38d18; fogRate = DAT_00d38aa8
```

A WMO/zone interior fog set is then cross-faded in by `DAT_00d38b9c` (from the volume query
`FUN_0077fb90`) and the result written both back to `+0x90/+0x94/+0x98` and to the mirror at
`+0xa0..+0xac` that the interior lighting path (`FUN_007b3f30` @ 0x007b3f30) reads.

Corrections to frozen (`ComputeLightColors` / `UpdateOutdoorLight` in `src/world/CWorld.cpp`):

* `s_fogEnd` must be **clamped to the far clip**: `min(CWorld::s_farClip, band)`. frozen does not clamp.
* `s_fogStart = fogEnd * scalar`, **not** `fogEnd * (1 + scalar)`.
* The scalar is clamped to `[-1, 1]` at load time (`FUN_007ebff0`), so a negative value legitimately
  puts the fog start behind the camera.
* **MEASURED CORRECTION (2026-09-15): the fog start scalar is NOT LightFloatBand band 1.** Read from
  the running reference, standing in the world on the same character as frozen:
  `_DAT_00d38c20` (the value the outdoor path multiplies fog end by) is **0.0**, while band 1 of
  this zone's light parameter set (748) is negative at every one of its seven keys across the whole
  day, from -0.500 at midnight to -0.125 at 09:00. No interpolation of that band yields 0.
  The reference is confirmed to be on the DATA path and not the defaults path here
  (`DAT_00d39008` = 9, non-zero; the default constants are 1e10 and 0.5, neither of which is live).
  Its colours are confirmed to come from params 748, so it is not simply using a different set.
  **RESOLVED**: it comes from a fog OVERRIDE, not from the light bands. `FUN_007ed820` copies an
  override set into the live fog globals when `DAT_00d38ad0` is set, and then clears the flag:
  `_DAT_00d38c1c = _DAT_00d38ac0` and `_DAT_00d38c20 = _DAT_00d38abc`. Only that path can produce
  the observed 0, because the three alternatives are excluded by measurement: the defaults would
  give 0.5 (`_DAT_009e2ec4`), the light band is negative at every hour, and the flag plus its source
  values now read 0, i.e. the override has already been consumed.

  So this is a **missing feature in frozen, not a wrong formula**: frozen has no zone or weather fog
  override, so it falls through to the band, which it reads correctly. Fog END and fog COLOUR match
  the reference exactly; only the start differs, and only while an override is active on the
  reference side.
* frozen divides the LightFloatBand end by 36. The reference does not scale it at all - it only clamps
  it against the far clip, which is what actually bounds it in practice. *(Worth one sanity check
  against the DBC, but either way the clamp is the real limiter.)*

**Is fog applied to the sky?** No. `FUN_009acb00` (dome) and `FUN_009acd40` (clouds) both explicitly
clear render state 0x11 (`*(dev+0x198) = 0`) and `FUN_009ac400` (glare) calls `FUN_00408bf0(0x11, 0)`.
The sky is never fogged; the horizon matches the fog because the bottom dome rings are literally the
fog colour. frozen already sets `GxRs_Fog, 0` in `SkyRender`, so this is correct today.

---

## Task list (ordered)

1. **Port the sun direction.** `src/world/CWorld.cpp`: replace the constant `s_outdoorDirection` with
   a per-frame computation in `CWorld::UpdateOutdoorLight` porting `FUN_007eea90` @ 0x007eea90:
   `theta = wrap-lerp over {(0, 2.2165682), (0.25, 1.9198622), (0.5, 2.2165682), (0.75, 1.9198622)}`
   at `GetDayProgress()`, `phi = 3.9269910`, then
   `s_outdoorDirection = -(cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta))`
   (negated because frozen uses it as "direction to the light"). Reuse the existing wrap-around band
   lerp; `InterpFloatBand`'s loop is the same shape as `FUN_007ed3b0`. Everything downstream
   (`LightingCallback`, the terrain ndotl bake at `Terrain.cpp:843`, the WMO bake at
   `Terrain.cpp:1475`) then tracks it - but note both bakes run at load time, so either they must be
   re-baked when the direction moves or the N.L must move into the shader for the wobble to show.
2. **Fix the sky band mapping and the fog formulas.** `src/world/CWorld.cpp`: make the sky stack six
   entries, `sky[0..5] = LightIntBand bands 2,3,4,5,6,7` ordered top-to-horizon (today it is five
   entries from bands 6,5,4,3,2 in the opposite order); set
   `s_fogEnd = min(s_farClip, InterpFloatBand(P,0,t))` and `s_fogStart = s_fogEnd * scalar`. Widen
   `s_skyColors` / `CWorld::GetSkyColor` accordingly and fix `SkyRender`'s clear-colour call site.
3. **Rebuild the dome to the reference geometry.** `BuildSkyDome` / `SkyRender`
   (`src/world/Terrain.cpp:4452`), porting `FUN_007f2470` + `FUN_007f0530`: 24 segments, 7 rings at
   zenith angles `{0, .17, .20, .23, .24, .25, 1.0} * pi`, 122 vertices, one 300-index triangle strip,
   per-vertex colour = the ring's band colour (rings 5 and 6 both take the fog colour). Replace the
   `z / SKY_RADIUS` gradient with a straight per-ring colour assignment.
4. **Clouds.** New mesh + draw in `src/world/Terrain.cpp` porting `FUN_007f20e0` / `FUN_009acd40`:
   12 rings x 16 segments at `{0,.025,.05,.075,.1,.125,.15,.175,.205,.23,.245,.25} * pi`, per-ring
   alpha `{255 x9, 128, 0, 0}`, planar UVs `r = ring/11 * 0.5`, 374-index strip, alpha blend, fog off,
   tint from the DNInfo cloud bands. Draw between the dome and the skybox M2 in `SkyRender`.
5. **Sun/moon glare post pass.** New `SkyGlareRender()` in `src/world/Terrain.cpp`, called last in
   `CGWorldFrame::OnWorldRender` (reference draw-order step 14), porting `FUN_007f0870` ->
   `FUN_007ef6e0` + `FUN_009ac400`: one additive screen-facing quad per body, size and alpha from
   `dot(CWorld::GetCameraDir(), normalize(bodyPos - cameraPos))` remapped through the cone threshold,
   `Textures\sunGlare.blp` / `Textures\moonGlare.blp` (**not** the disc textures), sun between 04:50
   and 22:10 else the moon. **No longer blocked** - the celestial-body positions and every glare
   constant are in `parity-sky-bodies.md`, which also supersedes the cloud task (4) with the
   procedural texture generator.
6. **Dome azimuthal colour variation** (lowest value, highest uncertainty): `FUN_007f0530` varies the
   middle rings' colour per segment using the camera yaw. Only worth doing after 1-5, and only after
   re-dumping `FUN_007f0530`'s band arguments from disassembly rather than decompilation.

## 2026-09-16 - the bright ring at the fog boundary: the sky dome was being added over terrain

User report, and a correction to an earlier one: geometry IS drawn beyond the fog, faintly. The real
artifact is a **bright ring where the sky meets ground geometry**, at a distance suspiciously close
to the view cutoff.

**Mechanism.** The sky pass squeezes its viewport depth range into
`[SKY_VIEWPORT_MIN_Z, SKY_VIEWPORT_MAX_Z]` = `[0.9990234375, 1.0]` -- the reference's constant -- so
that every sky pixel lands at the far end of the buffer and a less-equal test admits it only where
nothing else has drawn. The dome is then drawn with `GxBlend_Add` over a black clear, so the dome's
colour IS the sky.

That reasoning holds only if terrain depth never reaches the sky's minimum. With this client's
planes it does, easily. For a standard projection:

```
z_buf = (f / (f - n)) * (1 - n / z)      n = 0.2, f = 727
z_buf = 0.9990234375   ->   z = 159 yards
```

**Every terrain pixel beyond ~159 yards has a larger depth than the dome's nearest ring**, so the
dome passed the depth test across most of the view and was ADDED on top of ground that was already
fogged to the fog colour. Fog colour plus fog colour is a bright band, and it begins at the onset
distance -- the ring.

This is the trio flagged in CLAUDE.md as "unverified on screen -- if the sky comes out black or blown
out, this trio is the first place to look". It came out blown out, at the horizon.

**Fix:** both ends of the sky depth range are now 1.0. Every sky pixel lands at exactly the cleared
depth, so less-equal admits it only where the buffer is untouched -- which is the property the
squeezed range was reaching for, without depending on where the near and far planes happen to put
terrain. Recorded as a deliberate divergence from the reference's 0.9990234375, not a port: the
constant is right for the reference's own depth setup and wrong for ours.

**Not yet confirmed on screen.** The capture after the change is angled at the ground rather than the
horizon, and distant structures in it look faded rather than brightened, which is consistent but not
proof. The ring is obvious from a horizon view, so this needs one look.
