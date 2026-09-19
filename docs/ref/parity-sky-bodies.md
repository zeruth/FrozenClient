# Sun / moon discs and the cloud texture — the two sky blockers, solved

Reference program: `RunicWorldGame.exe` (Ghidra project `RunicWorld`, image base 0x400000). This
closes the two items listed as **Known blockers** in `CLAUDE.md` and as *uncertain* in
`docs/ref/parity-sky.md` sections 3 and 5.

Raw dumps produced for this pass (all in `docs/ref/`):

| dump | contents |
|---|---|
| `win-dump-bodies1.txt` | **disasm** of `FUN_007eecc0` and `FUN_007f09b0` (recovers the register-passed band tables and the three disc struct pointers), decompile+disasm of `FUN_005fe800` / `FUN_007ed3b0`, decompiles of `FUN_007f2790`, `FUN_007f1010`, `FUN_007efae0`, `FUN_007edbe0`, `FUN_007edee0`, `FUN_009abb60`, `FUN_007f1cd0`, `FUN_004b6cb0` |
| `win-dump-bodies2.txt` | `FUN_007ece10` (cloud texel callback), `FUN_004b9200`, disasm of `FUN_007f2790` and `FUN_007efd00` and `FUN_009ac660`, `FUN_007edb50`, `FUN_009ad0b0`, `FUN_007f1220`, `FUN_007ef6e0` |
| `win-dump-bodies3.txt` | `FUN_007ee150` / `FUN_007ee230` (sun/moon **glare** object init), `FUN_007ee0d0` (star field), refs to the noise LUTs, `FUN_0088d0c0` (= `pow`) |
| `win-dump-bodies4.txt` | refs to the cloud texture slots |
| `win-dump-bodies5.txt` | `FUN_007f1d30` (DayNight teardown), **`FUN_007ed250`** (builds both noise LUTs), disasm of `FUN_007edb50` |
| `win-dump-bodies6.txt` | `FUN_007ee460` (cloud dtor) |
| `win-dump-bodies7.txt` | raw disasm of the un-named code after `FUN_007f1010` |
| `win-dump-bodies8.txt` | refs to the cloud dtor thunk, disasm of `FUN_007f20e0` |
| `win-dump-bodies9.txt` | **`FUN_007f04b0`** — the cloud struct's static constructor (octaves, anim speed) |

New tooling: `C:\Users\tyler\tools\dump.sh` + `ghidra-scripts\Dump.java`. One headless run now mixes
`d:<addr>` (decompile), `a:<addr>` (disassemble a function), `x:<addr>` (raw disassembly from an
address, for code Ghidra has not made a function), `r:<addr>` (references to a data address). The
**disassembly mode is what unblocked this**: `FUN_007ed3b0` takes its band table in `EDI` and its key
count in `ESI`, so the decompiler drops both and every band call looks identical.

All float constants below were read straight out of the PE `.rdata`/`.data` by mapping RVA -> file
offset through the section headers. They are exact, not inferred.

---

## 0. The three shared primitives

### `FUN_005fe800` @ 0x005fe800 — `SplitFloor(float x, float* frac, int* floorInt)`

Not spherical-to-cartesian. It is a two-output `floor`/`frac` split:

```c
void SplitFloor(float x, float* frac, int* n) {
    // FPU rounding control forced to truncate-toward-zero (FLDCW |= 0x0C00)
    int i = (int)x;                 // trunc
    if (x <= 0.0f) i -= 1;          // the x > 0 test picks the branch
    *n    = i;
    *frac = x - (float)i;           // in [0, 1)
}
```

It exists only to feed the client's **polynomial cosine**, which is the actual "angle pair ->
direction" helper. Every trig call in the DayNight code is this shape:

```c
// cos(pi * x), for any real x
static float CosPi(float x) {
    float f; int n;
    SplitFloor(x, &f, &n);
    float r = 1.0f - (6.0f - 4.0f * f) * f * f;   // 0x009e8cf8 = 6.0, 0x009e8d2c = 4.0
    return (n & 1) ? -r : r;
}
#define ONE_OVER_PI 0.31830987f                   // 0x00a1e8d4

float Cos(float a) { return CosPi(a * ONE_OVER_PI); }
float Sin(float a) { return CosPi(a * ONE_OVER_PI - 0.5f); }
```

`h(f) = 1 - 6f^2 + 4f^3` is a cubic Hermite fit to `cos(pi f)` on `[0,1]`: exact and flat at both
ends, worst error ~0.020 near `f = 0.25` (0.6875 vs 0.7071, about 2.8%). Using real `cosf`/`sinf`
instead is a visible-scale difference of well under a pixel for a disc at radius 12 — **use `cosf`/
`sinf`**, the approximation is not worth porting.

### `FUN_007ed3b0` @ 0x007ed3b0 — `InterpBand(float t)` with `ESI = keyCount`, `EDI = float(*)[2]`

Confirmed by disassembly. Wrap-around linear interpolation over `(time, value)` pairs:

* `t` is clamped to `[0, 1]` first.
* Scan for the first key with `key[i].time > t`; call it `hi`. `lo = hi - 1`, except that
  `hi == count` wraps to `hi = 0, lo = count - 1`, and `hi == 0` also takes `lo = count - 1`.
* `span = key[hi].time - key[lo].time`; if `|span| < 0.001` (0x009e1134) return `key[lo].value`
  unchanged; if `span < 0` add 1.
* `f = t - key[lo].time`; if `f < 0` add 1.
* return `lerp(key[lo].value, key[hi].value, f / span)`.

frozen's existing `InterpFloatBand` loop is the same shape.

### The disc/cloud draw path

`FUN_007f09b0` @ 0x007f09b0 is the sky pass. The **disassembly** gives the three previously unknown
`ECX` arguments:

```
007f0b43  MOV ECX, 0x00d38ae4 ; CALL FUN_009abd50   // stars
007f0b4d  MOV ECX, 0x00d38e28 ; CALL FUN_009ac660   // SUN disc
007f0b57  MOV ECX, 0x00d38e48 ; CALL FUN_009ac660   // MOON 1 disc
007f0b61  MOV ECX, 0x00d38e68 ; CALL FUN_009ac660   // MOON 2 disc
007f0b6b  MOV ECX, 0x00d38d4c ; CALL FUN_009acb00   // gradient dome
007f0b75  MOV ECX, 0x00d38d90 ; CALL FUN_009acd40   // CLOUDS  <-- the cloud struct base
```

---

# Task 1 — sun and moon disc positions: **SOLVED, implementable**

Producer: **`FUN_007eecc0` @ 0x007eecc0**, called once per frame from the DayNight update. It writes
three 8-dword celestial-body records:

```c
struct CelestialBody {           // 0x20 bytes
/* +0x00 */ float    x, y, z;    // WORLD position (camera + 12 * direction)
/* +0x0c */ CImVector color;     // BGRA tint
/* +0x10 */ CTexture* texture;   // set once by FUN_009ad0b0(this, "...blp")
/* +0x14 */ float    size;       // = sizeBand(t) * baseSize, per frame
/* +0x18 */ float    baseSize;   // constant
/* +0x1c */ float    period;     // orbital period in days (moon 2 only)
};
```

| body | base | texture | `baseSize` | `period` |
|---|---|---|---|---|
| sun | `0x00d38e28` | `Textures\sunCenter.blp` | **1.0** | 1.0 (unused) |
| moon 1 | `0x00d38e48` | `Textures\moon.blp` | **1.75** (0x00a1ea74) | 1.0 (unused) |
| moon 2 | `0x00d38e68` | `Textures\moon02.blp` | **1.0** | **1.7 days** (0x00a241a0) |

(`FUN_009ad0b0` @ 0x009ad0b0 is `this->texture = TextureCreate(name, ...)`; the assignments are
visible in the `FUN_007f2790` disassembly at 0x007f295d / 0x007f2974 / 0x007f2995.)

## 1.1 The eight lazy-init band tables, with real values

`FUN_007eecc0`'s first ~100 lines are eight one-shot table fills guarded by the bits of
`DAT_00d39208`. The disassembly pairs each table with the `ESI`/`EDI` at its call site
unambiguously:

| table @ | keys | used as | call site |
|---|---|---|---|
| `0x00d391e0` | 5 | sun **zenith** theta | 0x007ef002 |
| `0x00d391c8` | 3 | sun **azimuth** phi | 0x007ef01d |
| `0x00d39128` | 4 | sun **size** scale | 0x007ef192 |
| `0x00d391a0` | 5 | moon 1 **zenith** theta | 0x007ef1d8 |
| `0x00d39188` | 3 | moon 1 **azimuth** phi | 0x007ef1f3 |
| `0x00d39108` | 4 | moon **size** scale (both moons) | 0x007ef364, 0x007ef5e0 |
| `0x00d39160` | 5 | moon 2 **zenith** theta | 0x007ef458 |
| `0x00d39148` | 3 | moon 2 **azimuth** phi | 0x007ef470 |

Values (radians; the clock times are `time * 24`):

```
SUN theta   @0xd391e0 (5):  (0.2291667, 1.7453293)   05:30  100 deg
                            (0.4965278, 0.0872665)   11:55    5 deg
                            (0.5000000, 0.0872665)   12:00    5 deg
                            (0.5034722, 0.0872665)   12:05    5 deg
                            (0.8958333, 1.7453293)   21:30  100 deg
SUN phi     @0xd391c8 (3):  (0.2291667, 0.7853982)   (0.5, 0.7853982)  (0.8958333, 0.7853982)
                            -> constant 45 deg
SUN size    @0xd39128 (4):  (0.25,     2.0)  06:00
                            (0.28125,  1.0)  06:45
                            (0.84375,  1.0)  20:15
                            (0.875,    2.0)  21:00      (wraps 2.0 across the whole night)

MOON1 theta @0xd391a0 (5):  (0.0000000, 0.6108652)   00:00   35 deg
                            (0.0034722, 0.6108652)   00:05   35 deg
                            (0.1666667, 1.7453293)   04:00  100 deg
                            (0.9166667, 1.7453293)   22:00  100 deg
                            (0.9965278, 0.6108652)   23:55   35 deg
MOON1 phi   @0xd39188 (3):  (0.0, 0.7853982) (0.1666667, 0.7853982) (0.9166667, 0.7853982)
                            -> constant 45 deg
MOON size   @0xd39108 (4):  (0.0416667, 1.0)  01:00
                            (0.1666667, 1.5)  04:00
                            (0.9166667, 1.5)  22:00
                            (0.9993056, 1.0)  23:58

MOON2 theta @0xd39160 (5):  identical to MOON1 theta
MOON2 phi   @0xd39148 (3):  (0.0000000, 2.3561945)  135 deg
                            (0.1666667, 2.6179938)  150 deg
                            (0.9166667, 2.8797934)  165 deg
```

`0.0872665 = 5 deg`, `0.6108652 = 35 deg`, `1.7453293 = 100 deg`, `0.7853982 = pi/4`,
`2.3561945 = 3pi/4`, `2.6179938 = 5pi/6`, `2.8797934 = 11pi/12`.

## 1.2 The position formula

For each body, with `t` = `DayNight+0x04` = the 0..1 day fraction (frozen: `CWorld::GetDayProgress()`):

```c
float theta = InterpBand(thetaTable, t);      // zenith angle, radians
float phi   = InterpBand(phiTable,   t);      // azimuth,      radians

float st = sinf(theta), ct = cosf(theta);
C3Vector d = { cosf(phi) * st, sinf(phi) * st, ct };

float k = 12.0f / sqrtf(d.x*d.x + d.y*d.y + d.z*d.z);   // 0x00a1047c = 12.0
body.x = cameraPos.x + d.x * k;
body.y = cameraPos.y + d.y * k;
body.z = cameraPos.z + d.z * k;

body.size = InterpBand(sizeTable, t) * body.baseSize;
```

Coordinate space: **world space, but the body is pinned 12 world units from the camera**
(`DayNight+0x18..0x20` = camera position). It is a direction, not an orbit; the length is a constant
12. The explicit `12/|d|` normalise exists because the polynomial cos/sin does not produce a unit
vector — with real `sinf`/`cosf` it collapses to `* 12.0f`.

`z` is up (WoW convention), so `theta` is measured from the zenith: `elevation = 90 - theta`.

**Moon 2 uses a phase time, not the time of day.** Before evaluating its three bands,
`FUN_007eecc0` computes (0x007ef3a1..0x007ef43b, 16.16 fixed point so it survives large day counts):

```c
// P = body.period = 1.7 days ; day = DayNight+0x08 (day counter), t = DayNight+0x04
int   acc   = (int)(day * 65536.0f - 0.5f) + (int)(t * 65536.0f - 0.5f);
int   whole = (int)(floorf((day + t) / P) * P * 65536.0f - 0.5f);   // FUN_0088ce30 = floor
int   rem   = acc - (unsigned)whole < (unsigned)acc ? acc - whole : 0;
float phase = ((float)(rem < 0 ? rem + 4294967296.0f : rem) * (1.0f/65536.0f)) / P;   // 0..1
```
i.e. **`phase = frac((dayNumber + dayFraction) / 1.7)`**, and moon 2's theta/phi/size bands are all
evaluated at `phase`. So `Textures\moon02.blp` is a second moon on its own 1.7-day cycle, drifting in
azimuth from 135 to 165 degrees — it is *not* locked to the clock like the sun and moon 1.

### What the day actually looks like

Both the sun and moon 1 keep a **constant azimuth of 45 degrees** all day: they rise and set in the
same compass direction, swinging up and back down like a pendulum rather than tracing an arc. That is
what the tables say, and it agrees with the light direction from `parity-sky.md` section 1: the
lighting azimuth is a constant 225 degrees and the stored vector points *away* from the light, so
`-lightDir` also points at azimuth 45 degrees. Sun disc and sun light share an azimuth; only their
elevations differ (the disc reaches 85 degrees at noon, the light only 37).

```
          sun                      moon 1
 00:00   -10 deg  size 2.00      +55 deg  size 1.75
 06:00    -2.6    size 2.00      -10      (below)
 06:10   horizon crossing
 07:00   +12.2    size 1.00
 12:00   +85.0    size 1.00
 18:00   +25.3    size 1.00
 20:30   horizon crossing
 21:00    -5.0    size 2.00
 23:00   -10      (below)        +23.9    size 2.18
```

## 1.3 From the body record to the drawn quad — `FUN_009ac660` @ 0x009ac660

Mesh (built per draw by `FUN_007edbe0` @ 0x007edbe0, then clipped by `FUN_007edee0` @ 0x007edee0):

* **4 vertices**, one triangle strip, index list `{0, 1, 2, 3}` (`DAT_00af4dac`).
  Positions are `unitQuad[i] * body.size`, in a *local* frame where **x = 0** (the quad is flat):

  ```
  v0 = ( 0, -0.5,  0.5 ) * size    uv0 = (0, 0)
  v1 = ( 0,  0.5,  0.5 ) * size    uv1 = (1, 0)
  v2 = ( 0, -0.5, -0.5 ) * size    uv2 = (0, 1)
  v3 = ( 0,  0.5, -0.5 ) * size    uv3 = (1, 1)
  ```
  (unit table at `0x00d39078`, UV table at `0x00d39048`, both lazily filled in `FUN_007edbe0`.)
* Two spare vertices `v4/v5` exist at local z = 99.0, uv.y = 99.0. They are only used by the split
  path below; the 6-vertex strip is `{0, 1, 4, 5, 2, 3}` (`DAT_00af4db4`).
* Every vertex takes `body.color`.
* Primitive type 4 = `GxPrim_TriangleStrip`.

Billboard (the matrix built in `FUN_009ac660`, basis from `FUN_009abb60` @ 0x009abb60):

```c
C3Vector f = column2(currentWorldMatrix);   // the camera forward direction in world space
row0 = normalize(f);                                       // local +X  -> view direction
row1 = normalize( C3Vector(-row0.y, row0.x, 0) );          // local +Y  -> horizontal right
       // if |row1.x * row0.x| <= 1e-5 the degenerate case substitutes (0, 1, 0)
row2 = cross(row0, row1);                                  // local +Z  -> screen up
row3 = bodyPos - cameraPos;                                // translation
GxXformSetWorld( billboard * currentWorldMatrix );
```

So the disc is a screen-aligned billboard, translated to the body's camera-relative offset. Local
`+Y` is horizontal in world space, which is what makes the horizon work on the *local* coordinates:

Horizon clip + fade (`FUN_007edee0`, operating on the local positions before the matrix, with
`h = body.z - camera.z`):

```c
// 1. hard clip against the camera's horizontal plane
float a = pos[0].z + h, b = pos[2].z + h;           // top edge, bottom edge
if (a < 0 && b < 0) { vertexCount = 0; return; }    // fully below the horizon: draw nothing
if (a <= 0 || b <= 0) {                             // straddles it: pull the bottom edge up
    float u = a / (a - b);
    pos[2].z = pos[3].z = lerp(pos[0].z, pos[2].z, u);
    uv[2].y  = uv[3].y  = u;
}
vertexCount = 4; indexCount = 4;

// 2. optional horizontal split at T = 0.4 (0x009f98d8) world units above the camera
if ((pos[0].z + h) - T > 0.001f && (pos[2].z + h) - T < 0.001f) {
    vertexCount = 6; indexCount = 6;  indices = {0,1,4,5,2,3};
    float u = ((pos[0].z + h) - T) / (((pos[0].z + h) - T) - ((pos[2].z + h) - T));
    pos[4].z = pos[5].z = lerp(pos[0].z, pos[2].z, u);
    uv[4].y  = uv[5].y  = lerp(uv[0].y,  uv[2].y,  u);
}

// 3. per-vertex alpha fade into the horizon
for (i = 0; i < vertexCount; i++) {
    float zr = pos[i].z + h;
    if (zr - T < 0.001f)                                    // 2.5 = 1/T  (0x009edce8)
        color[i].a = (uint8)round( clamp(zr * 2.5f, 0, 1) * 255.0f );
}
```

Net effect: the disc is cut off exactly at the camera's eye-level plane and its bottom 0.4 world
units (about 1.9 degrees of the 12-unit sphere) fade out, so the sun does not pop when it sets.

Render state (`FUN_009ac660`). The device's render-state array is at `dev + 0x90 + rs * 0x18`, which
decodes exactly onto frozen's `EGxRenderState`:

| dev offset | RS | set to |
|---|---|---|
| +0x90 | 6 `GxRs_BlendingMode` | **2 = `GxBlend_Alpha`** |
| +0xa8 | 7 `GxRs_AlphaRef` | `DAT_00ad8b7c[blendMode]` (the per-mode table) |
| +0x108 | 11 `GxRs_Lighting` | 0 |
| +0x120 | 12 `GxRs_Fog` | 0 |
| +0x168 | 15 `GxRs_DepthWrite` | 0 |
| +0x378 | 37 `GxRs_ColorOp0` | 0 |
| +0x438 | 45 `GxRs_AlphaOp0` | 0 |
| — | 21 `GxRs_Texture0` | the disc texture (`FUN_00685f50(0x15, tex)`) |

It does **not** touch `GxRs_Culling` or the depth test; it inherits whatever the sky pass has, and
draws inside the far-depth viewport `FUN_007f09b0` installs (`DAT_00adeef0/f4`, the
`[0.9990234375, 1.0]` range frozen already uses).

**Draw order inside the pass, and a correction to `parity-sky.md`:** stars, sun, moon 1, moon 2,
gradient dome, clouds. That only works because **the gradient dome is `GxBlend_Add` (3), not
opaque** — `FUN_009acb00` writes `*(dev + 0x90) = 3`, and RS 6 value 3 is `GxBlend_Add`. The dome is
additive over a cleared frame buffer, so it produces the sky colour on empty pixels *and* brightens
the stars and discs already drawn beneath it. The clouds then alpha-blend on top (mode 2), and the
cloud drawer additionally clears RS 17 `GxRs_Culling`. frozen's `SkyRender` currently sets
`GxRs_BlendingMode, GxBlend_Opaque` for the dome; that must become `GxBlend_Add` before the discs can
be drawn under it.

## 1.4 Colour, and the day/night rule

`FUN_007f3230` @ 0x007f3230 (the DayNight per-frame light blend) writes the tints:

```c
DAT_00d38e34 = DNInfo[9];   // sun disc colour       (0x00d38bf8 = DayNight+0xd4 + 0x24)
DAT_00d38e54 = DNInfo[9];   // moon 1 disc colour
DAT_00d38ec0 = DNInfo[9];   // sun glare colour
DAT_00d38f70 = DNInfo[9];   // moon glare colour
// then, while the loading fade DAT_00d38b88 > 0, the alpha byte of all four is
//   (uint8)round((1 - fade) * 255), and DAT_00d38e77 (moon 2's alpha) too.
```

By `parity-sky.md`'s Light.dbc -> DNInfo map, `DNInfo[9]` is **LightIntBand band 9**.

> *Uncertain:* moon 2's RGB (`0x00d38e74`) is never assigned by `FUN_007f3230` — only its alpha byte
> is. Either another writer sets it or the reference leaves it at whatever the .bss holds. Port it as
> `DNInfo[9]` like the other two.

**Visibility.** There is no explicit "hide the sun at night" flag. Three mechanisms together do it:

1. The theta bands park the body at 100 degrees zenith (10 degrees below the horizon) when it should
   not be seen, and `FUN_007edee0` culls anything entirely below the eye plane.
2. `FUN_007eecc0`'s tail writes `DAT_00d38cd8`, the **celestial glow scalar**, from four hard-coded
   ramps (0x007ef5f7 onward):

   ```
   t in [0.2291667, 0.5)   -> (t - 0.2291667) * 3.6923077      // 05:30 -> 12:00, 0 -> 1
   t in [0.5,       0.8958333) -> 1 - (t - 0.5) * 2.5263159    // 12:00 -> 21:30, 1 -> 0
   t in (0.9166667, 1.0)   -> (t - 0.9166667) * 12.0000029     // 22:00 -> 24:00, 0 -> 1
   t in (0.0,       0.1666667) -> 1 - t * 6.0                  // 00:00 -> 04:00, 1 -> 0
   otherwise               -> 0
   ```
   (the four multipliers are exactly `1/0.2708333`, `1/0.3958333`, `1/0.0833333`, `1/0.1666667`).
3. The glare pass picks its source by clock: `FUN_007efae0` @ 0x007efae0 uses the **moon** when
   `t < 0.2013889` (04:50) **or** `t >= 0.9236111` (22:10), else the **sun** — confirming
   `parity-sky.md`. That threshold governs the glare/sky-highlight only, not the discs.

## 1.5 Correction and new data for the glare pass (`parity-sky.md` section 3)

Two things there were wrong or missing.

**The glare textures are not the disc textures.** `FUN_007ee150` @ 0x007ee150 loads
`Textures\sunGlare.blp` into the sun glare object at `0x00d38ea8`; `FUN_007ee230` @ 0x007ee230 loads
`Textures\moonGlare.blp` into the moon glare object at `0x00d38f58`. `sunCenter.blp` / `moon.blp` /
`moon02.blp` are the *disc* textures only. There is no glare for moon 2.

Also: `parity-sky.md` lists the glare colour/texture as `obj[0x18]`/`obj[0x1c]` — those are **byte
offsets**, not dword indices (`0x00d38ec0` and `0x00d38ec4`). The `FUN_007ef6e0` fields it lists
(`[3..5]`, `[8]`, `[0xa]`, `[0xb]`, `[0x24]..[0x28]`) *are* dword indices and check out.

Real constants, now recovered:

| field (dword index from object base) | sun @0xd38ea8 | moon @0xd38f58 |
|---|---|---|
| `[2]` enabled (`SkySunGlare`) | 1 | 1 |
| `[8]` base size | **1.0** | **2.0** |
| `[0xa]` fade-in rate /s | **4.0** | **3.0303030** |
| `[0xb]` fade-out rate /s | **1.5151515** | **1.5151515** |
| `[0xe]` | 1 | 1 |
| `[0x1c..0x23]` visibility band, 4 keys | (0.2708333, 0) (0.3125, 1) (0.8125, 1) (0.875, 0) | (0.0833333, 1) (0.1354167, 0) (0.9479167, 0) (0.9993056, 1) |
| `[0x24]` size scale at cone edge | **3.0** | **1.0** |
| `[0x25]` size scale on axis | **20.0** | **1.0** |
| `[0x26]` cos(cone half-angle) | **0.7** (45.6 deg) | **0.7** |
| `[0x27]` alpha scale at cone edge | **0.5** | **0.1** |
| `[0x28]` alpha scale on axis | **1.0** | **1.0** |
| `[0x2a]` body pointer | `&0x00d38e28` (sun) | `&0x00d38e48` (moon 1) |

So the sun glare band ramps up 06:30 -> 07:30 and down 19:30 -> 21:00; the moon glare band is on from
22:45 through midnight to 03:15. The glare object's position `[3..5]` is copied from the body by
`FUN_007eecc0` every frame (0x007ef1c7, 0x007ef38f).

### Bonus: the star field (`FUN_009abd50` @ 0x009abd50, struct at 0x00d38ae4)

`FUN_007ee0d0` @ 0x007ee0d0 (reached via `FUN_007eea80`) sets `stars.pos = cameraPos` and
`stars.alpha = (uint8)(InterpBand(0x00af4c20, 4 keys, t) * 254.0f + 1.0f)` with the band
`(0.125, 1) (0.1875, 0) (0.9375, 0) (1.0, 1)` — stars fade out 03:00 -> 04:30 and back in
22:30 -> 24:00.

## 1.6 What is still uncertain in task 1

* Moon 2's RGB tint (above).
* The exact semantics of `DayNight+0x08` — `FUN_007eecc0` uses it as an integer **day counter** in
  the moon-phase maths (`frac((day + t) / 1.7)`), while `parity-sky.md` section 0 labels it "time of
  day override". The phase maths only makes sense as a day counter; frozen can drive it from the
  calendar day.
* `FUN_009abb60`'s input is `column2` of the matrix in the Gx transform slot at `+0x1b00`
  (`DAT_00c5df88 + DAT_00c5df88[0x6be] * 0x40 + 0x1b00`). It is the current world/view transform's
  forward axis; the read is unambiguous in the disassembly, but which Gx slot that is has not been
  cross-checked against frozen's `GxXform_*` enum. A plain "face the camera" billboard is equivalent.

---

# Task 2 — the cloud texture source: **there is no texture file**

`DNClouds0` / `DNClouds1` are neither CVars nor file paths. They are the **debug names of two
runtime-generated textures**. The clouds are lit, animated, procedurally-generated 4-octave value
noise, rasterised on the CPU into a pair of N x N BGRA buffers and double-buffered.

## 2.1 Where the texture array comes from

`FUN_007f1b10` @ 0x007f1b10 is `CloudSetLOD(byte lod, int rowsPerFrame)` — `this` is the cloud struct
`0x00d38d90` (confirmed at 0x007f28cb). It releases the two old textures and creates two new ones:

```c
this->dim     = kCloudDim[lod];      // DAT_00a41aac[4] = { 128, 256, 512, 1024 }
this->dimLog2 = kCloudLog2[lod];     // DAT_00a41ac0[4] = {   7,   8,   9,   10 }
this->mask    = this->dim - 1;
this->rowsPerFrame = rowsPerFrame ? rowsPerFrame : 8;
ArrayResize(this->texels /*+0x30*/, dim * dim);  memset(texels, 0xFF, dim*dim*4);
ArrayResize(this->height /*+0x48*/, dim * dim);  memset(height, 0x00, dim*dim);

this->tex[0] /*+0x90*/ = FUN_004b9200(dim, dim, this->format, 2, this, this->texels,
                                      FUN_007ece10, "DNClouds0", 0);
this->tex[1] /*+0x94*/ = FUN_004b9200(dim, dim, this->format, 2, this, this->texels,
                                      FUN_007ece10, "DNClouds1", 0);
```

`FUN_004b9200` -> `FUN_004b8c80` is the **callback-backed** `TextureCreate` (the same overload frozen
already calls for `s_skyWhite` in `SkyRender`). The last string is the texture's debug name. The
callback is:

```c
// FUN_007ece10 @ 0x007ece10 -- the GetTexels callback
void CloudTexCallback(int op, int width, ..., int mipLevel, void* userData,
                      int* outPitch, void** outTexels) {
    if (op == 1 && mipLevel == 0) { *outPitch = width * 4; *outTexels = userData; }
}
```

So the pixels are the cloud struct's own `+0x38` buffer. **`cloudStruct[0x90 + cloudStruct[0xb]*4]`
in the drawer `FUN_009acd40` is simply `tex[frontIndex]`, the completed one of the two.**

`SkyCloudLOD` (`FUN_007f1cd0` @ 0x007f1cd0, default `"0"` at `0x009e14a0`) clamps its value to
`[0, 3]` and calls `CloudSetLOD(lod, 0)`, so the default texture is **128 x 128**.

`FUN_007f1d30` @ 0x007f1d30 (DayNight teardown) releases `DAT_00d38e20` / `DAT_00d38e24` — the same
two slots, confirming `cloudStruct + 0x90 == 0x00d38e20`.

## 2.2 The cloud struct at `0x00d38d90`

```
+0x00  float   rampBase        = 0.96      (0x00a41d10, passed to FUN_007edb50)
+0x04  float   densityOverride (0 = use the DNInfo band)
+0x08  uint8   alphaThreshold  = round((1 - density) * 255)
+0x09  uint8   lod             (SkyCloudLOD, 0..3)
+0x0a  uint8   forceFullPass   (regenerate every row this frame)
+0x0b  uint8   frontIndex      (0/1 -- which of tex[0]/tex[1] the drawer samples)
+0x0c  float   animSpeed       = 2.0
+0x10  int     rowsPerFrame    = 8
+0x14  int     rowCursor
+0x18  int     texFormat       = 2 = GxTex_Argb8888
+0x1c  int     dim             (128 / 256 / 512 / 1024)
+0x20  int     dimLog2         (7 / 8 / 9 / 10)
+0x24  int     mask            = dim - 1
+0x28  int     octaves         = 4
+0x2c  int     built           (set to 1 by FUN_007f2790 -- the drawer's gate)
+0x30..+0x3c  CImVector[dim*dim]  texels     (the pixels the callback hands out)
+0x44..+0x50  uint8[dim*dim]      heightMap  (the density byte per texel)
+0x5c  C3Vector*  positions      (mesh, built by FUN_007f20e0)
+0x68  C2Vector*  texCoords
+0x74  CImVector* colors         (0xC0 = 192 allocated)
+0x80  uint16*    indices        (0x176 = 374)
+0x84  uint16     indexCount  = 374
+0x86  uint16     vertexCount = 177
+0x88  uint16     phase          (16-bit animation phase; high byte = noise Z, low byte = Z fraction)
+0x8c  float      timeAccum
+0x90  CTexture*  tex[2]         ("DNClouds0", "DNClouds1")
```

### The constructor — `FUN_007f04b0` @ 0x007f04b0

Registered as a CRT static initializer (the thunk at 0x009d0740 does
`MOV ECX, 0x00d38d90; CALL FUN_007f04b0`; the matching destructor thunk is 0x009dccd0 ->
`FUN_007ee460`). It is where the fields with no resolvable writer come from:

```c
memset(this + 0x30, 0, 0x54);      // every array pointer/size
FUN_007ed250();                    // build the two noise LUTs (section 2.3)
this->timeAccum    /*+0x8c*/ = 0;
this->frontIndex   /*+0x0b*/ = 0;
this->forceFullPass/*+0x0a*/ = 0;
this->animSpeed    /*+0x0c*/ = 2.0f;             // 0x00a4040c
this->octaves      /*+0x28*/ = 4;                // <-- FOUR octaves, not five
this->texFormat    /*+0x18*/ = 2;                // GxTex_Argb8888
this->rowsPerFrame /*+0x10*/ = 8;
```

So only the first **4** of the 5 per-LOD frequency steps at `DAT_00af4dc4` are used.

## 2.3 The noise

Two 256-entry lookup tables, both built once by **`FUN_007ed250` @ 0x007ed250** (from the ctor
above):

```c
for (i = 0; i < 256; i++)
    DAT_00d38688[i] = 1.0f - 2.0f * (rand() * (1.0f/32767.0f));   // value table, in [-1, 1]

for (i = 0; i < 256; i++)
    DAT_00d38188[i] = (1.0f - cosf(i * (float)M_PI / 256.0f)) * 0.5f;   // == sin^2(i*pi/512)
```

`DAT_00d38188` is the cosine interpolation weight indexed by an 8-bit fractional coordinate;
`DAT_00d38688` is the per-lattice-point random value. The permutation table
**`DAT_00af4a70` is a static 256-byte table baked into `.data`** (starts
`225, 155, 210, 108, 175, 199, 221, 144, 203, 116, 70, 213, 69, 158, 33, 252, ...`) — it is a real
Perlin permutation and can be lifted verbatim if bit-for-bit parity matters; any good permutation
works otherwise.

A third table, the **density response ramp** `DAT_00d38588[256]`, is built by
`FUN_007edb50` @ 0x007edb50 (`this` = the cloud struct, confirmed at 0x007f290a):

```c
void CloudBuildRamp(CloudState* c, float g /* = 0.96 */) {
    c->rampBase = g;
    float step = (255 - c->alphaThreshold) * (1.0f/256.0f);   // 0x00a3fdc8
    float x = 0.0f;
    for (int k = 0; k < 256; k++) { DAT_00d38588[k] = (uint8)roundf(255.0f - 255.0f * powf(g, x));
                                    x += step; }
}
```

Per-octave frequency steps, in 8.8 fixed point, at `DAT_00af4dc4` (5 `uint16` per LOD):

```
lod 0 (128px):  16, 32, 64, 128, 256
lod 1 (256px):   8, 16, 32,  64, 128
lod 2 (512px):   4,  8, 16,  32,  64
lod 3 (1024px):  2,  4,  8,  16,  32
```

Five entries are stored per LOD but the constructor sets `octaves = 4`, so only the first four are
used (at LOD 0: steps 16, 32, 64, 128). Octave `i` contributes amplitude `1 / (1 << i)`, i.e.
`1, 1/2, 1/4, 1/8`.

## 2.4 The per-frame regeneration — `FUN_007efd00` @ 0x007efd00 (via `FUN_007f1010`)

```
0. Bail (and clear forceFullPass) if a skybox model is weighted in -- when DAT_00d38b5c or any of
   DAT_00d38b64.. has weight > 0.99, the clouds are not regenerated at all.
1. timeAccum += DayNight[+0x48] (frame delta).
2. density   = cloudStruct.densityOverride ? that : DNInfo[0x18]   // LightFloatBand 3, "cloud density"
   alphaThreshold = (uint8)round((1 - density) * 255)
3. Query the lighting: FUN_007efae0(&sunColor, &ambient, &base, &sunScreenDir, &intensity)
   -- ambient/base/highlight come from DNInfo bytes at DayNight+0xbfc..+0xc06, intensity from a band.
4. For rowsPerFrame rows starting at rowCursor (or every row when forceFullPass):
     for x in 0..dim-1:
        n = sum over 4 octaves of  cosine-interpolated 3D value noise
              ( lattice from DAT_00af4a70 permutation, values DAT_00d38688,
                fade weights DAT_00d38188, z from the 16-bit phase )
        b = (uint8)round(n * 64.0f + 128.0f)                    // 0x009e8dd8 = 64, 0x009e8c6c = 128
        d = (b - alphaThreshold) >= 0 ? DAT_00d38588[b - alphaThreshold] : 0
        heightMap[row*dim + x] = d
        if (d == 0)  texel = previous texel's RGB, alpha 0
        else {
            // shade: base + ambient * ((~d >> 1) + 64)/255, plus a highlight term
            // from dot(normalize(gradient of the height field), sunScreenDir) * intensity
            texel.bgra = saturate(...) , alpha = d
        }
5. Upload rows [rowCursor, rowCursor + rowsPerFrame) into the BACK texture
     tex[(frontIndex - 1) & 1]   via FUN_00681f20 (GxTexUpdate).
6. rowCursor += rowsPerFrame; when it reaches dim, recompute the 16-bit phase
     phase = (int16)round(animSpeed * timeAccum)
   and flip frontIndex ^= 1, rowCursor = 0.
```

So a full cloud sheet takes `dim / rowsPerFrame` frames (128/8 = **16 frames** at the default LOD),
and the animation advances one 16-bit phase step per completed sheet.

## 2.5 What is still uncertain in task 2

* The exact shading expression in step 4 (the `(~d >> 1) + 64` weighting and the fast-inverse-sqrt
  normal, magic constant `0x5f3997bb`) is transcribed from the decompile but has not been re-derived
  from disassembly. The structure — flat base colour, plus an ambient term scaled by the density
  byte, plus a highlight term proportional to `dot(cloudNormal, sunDirection)` clamped at 0 — is
  certain; the exact weights are not.
* `FUN_007efae0`'s five outputs (@0x007efae0) are named by use, not by a symbol. It also drives the
  sky highlight, so a port should reuse it for both.
* The precise lattice indexing of the 3D noise (which of the four `DAT_00af4a70` lookups is the
  `(x,y)`, `(x+1,y)`, `(x,y+1)`, `(x+1,y+1)` corner) is readable but was not transcribed
  instruction-by-instruction; any standard 3D value-noise arrangement over these tables will look
  right, and only a bit-exact port needs it.

---

# Implementation task list (ordered)

These slot in after the existing `parity-sky.md` tasks 1-4. `SkyRender` is
`src/world/Terrain.cpp:4805`; light/time state is `src/world/CWorld.cpp`;
`CWorld::GetDayProgress()` already returns the 0..1 day fraction.

1. **Add a reusable wrap-around band evaluator.** `src/world/CWorld.cpp`: generalise the ad-hoc loop
   now inlined in `CWorld::UpdateOutdoorLight` (lines ~337-362) into
   `static float InterpBand(const float (*keys)[2], int32_t count, float t)` porting
   `FUN_007ed3b0` @ 0x007ed3b0 exactly (clamp `t`, wrap `hi`/`lo`, the `|span| < 0.001` early-out,
   `+1` on negative span and offset). Rewrite the existing sun-direction block to call it. Everything
   below depends on this one function.

2. **Add the celestial-body state to `CWorld`.** `src/world/CWorld.cpp` / `.hpp`: a
   `struct CelestialBody { C3Vector pos; CImVector color; HTEXTURE tex; float size; float baseSize;
   float period; }` and `static CelestialBody s_bodies[3]`, updated from
   `CWorld::UpdateOutdoorLight` by a new `CWorld::UpdateCelestialBodies()` porting `FUN_007eecc0`
   @ 0x007eecc0: the eight band tables from section 1.1 verbatim, the moon-2 phase
   `frac((dayNumber + dayProgress) / 1.7f)`, and
   `pos = cameraPos + 12.0f * (cos(phi)*sin(theta), sin(phi)*sin(theta), cos(theta))`,
   `size = sizeBand(t) * baseSize`. Base sizes 1.0 / 1.75 / 1.0. Tint = `GetSkyColor`'s band-9 slot
   (see task 5). Also store the glow scalar `s_celestialGlow` from the four ramps in section 1.4.
   Use real `sinf`/`cosf`, not the polynomial.

3. **Make the gradient dome additive first.** `src/world/Terrain.cpp` `SkyRender`: the dome's
   `GxRsSet(GxRs_BlendingMode, GxBlend_Opaque)` must become `GxBlend_Add` (the reference writes
   render state 6 = 3 in `FUN_009acb00`). Verify on its own with a run — over a cleared frame buffer
   an additive dome looks identical, so this is a safe standalone change, and it is a prerequisite
   for the discs being visible.

4. **Draw the discs in `SkyRender`.** `src/world/Terrain.cpp`: a new `SkyBodiesRender()` porting
   `FUN_009ac660` @ 0x009ac660 + `FUN_007edbe0` @ 0x007edbe0 + `FUN_007edee0` @ 0x007edee0, called
   from `SkyRender` **before** the gradient dome (reference order: stars, sun, moon 1, moon 2, dome,
   clouds). Per body: the 4-vertex local quad `(0, +-0.5, +-0.5) * size` with UVs
   `(0,0)(1,0)(0,1)(1,1)`, the screen-aligned billboard basis from `FUN_009abb60`, translation
   `pos - cameraPos`, the horizon clip and the `T = 0.4` / `1/T = 2.5` alpha fade, then the render
   states in section 1.3 (alpha blend, lighting off, fog off, depth write off, colour/alpha op 0
   cleared; culling and the depth test are *inherited*, not set), inside the existing
   `SKY_VIEWPORT_MIN_Z/MAX_Z` viewport.
   Textures: `Textures\sunCenter.blp`, `Textures\moon.blp`, `Textures\moon02.blp`, loaded once and
   cached beside `s_skyWhite`.

5. **Widen the sky band mapping to carry band 9.** `src/world/CWorld.cpp`
   (`ComputeLightColors` / `s_skyColors`): `parity-sky.md` task 2 already widens the stack to bands
   2..7; also capture **LightIntBand band 9** (`DNInfo[9]`) as the celestial tint used by both the
   discs and the glare. Without it the discs will draw untinted white.

6. **Clouds: generate the texture, do not load one.** `src/world/Terrain.cpp`, on top of
   `parity-sky.md` task 4 (which already has the 12 x 16 mesh): a `CloudTexture` state block porting
   `FUN_007f1b10` @ 0x007f1b10 and `FUN_007efd00` @ 0x007efd00 —
   (a) two `TextureCreate(dim, dim, ..., callback, ...)` textures over one `CImVector[dim*dim]`
   buffer, exactly like the existing `s_skyWhite` callback path, default `dim = 128`;
   (b) the two LUTs from `FUN_007ed250` @ 0x007ed250 (`value[i] = 1 - 2*rand01()`,
   `fade[i] = (1 - cos(i*pi/256)) * 0.5`) and the `DAT_00af4a70` permutation (lift the 256 bytes
   from the reference `.data` if exact parity is wanted);
   (c) the ramp `DAT_00d38588[k] = 255 - 255*pow(0.96, k*(255 - threshold)/256)` from
   `FUN_007edb50` @ 0x007edb50;
   (d) **4** octaves with the first four per-LOD steps at `DAT_00af4dc4`, amplitude `1/(1<<i)`,
   `b = round(noise*64 + 128)`, `density = ramp[b - threshold]` with the `< 0` guard, where
   `threshold = round((1 - LightFloatBand3) * 255)`;
   (e) 8 rows per frame into the back texture, flip the front index when a sheet completes, and skip
   the whole update while a skybox model is fully weighted in.
   Then bind `tex[frontIndex]` in the cloud draw, alpha blend, fog off.

7. **Glare pass** (`parity-sky.md` task 5, now unblocked): the body positions it needed are
   `CWorld::s_bodies[0].pos` (sun) and `s_bodies[1].pos` (moon 1). Use
   `Textures\sunGlare.blp` / `Textures\moonGlare.blp` (**not** the disc textures), base sizes
   1.0 / 2.0, fade rates 4.0 / 3.0303 in and 1.5152 out, cone `cos = 0.7`, size scale
   `lerp(3.0, 20.0, c)` for the sun and a flat 1.0 for the moon, alpha scale `lerp(0.5, 1.0, c)` /
   `lerp(0.1, 1.0, c)`, and the two 4-key visibility bands in section 1.5.

8. **Star field** (optional, cheap): `FUN_009abd50` @ 0x009abd50 with the struct at `0x00d38ae4`,
   positioned at the camera, alpha `band(t) * 254 + 1` from the 4-key band at `0x00af4c20`.

---

## Appendix - `DAT_00af4a70`, the cloud noise permutation (verbatim, 256 bytes from `.data`)

It is a true permutation of 0..255 (verified). Copy it if bit-for-bit cloud parity is wanted.

```c
static const uint8_t kCloudPerm[256] = {
    225, 155, 210, 108, 175, 199, 221, 144, 203, 116,  70, 213,  69, 158,  33, 252,
      5,  82, 173, 133, 222, 139, 174,  27,   9,  71,  90, 246,  75, 130,  91, 191,
    169, 138,   2, 151, 194, 235,  81,   7,  25, 113, 228, 159, 205, 253, 134, 142,
    248,  65, 224, 217,  22, 121, 229,  63,  89, 103,  96, 104, 156,  17, 201, 129,
     36,   8, 165, 110, 237, 117, 231,  56, 132, 211, 152,  20, 181, 111, 239, 218,
    170, 163,  51, 172, 157,  47,  80, 212, 176, 250,  87,  49,  99, 242, 136, 189,
    162, 115,  44,  43, 124,  94, 150,  16, 141, 247,  32,  10, 198, 223, 255,  72,
     53, 131,  84,  57, 220, 197,  58,  50, 208,  11, 241,  28,   3, 192,  62, 202,
     18, 215, 153,  24,  76,  41,  15, 179,  39,  46,  55,   6, 128, 167,  23, 188,
    106,  34, 187, 140, 164,  73, 112, 182, 244, 195, 227,  13,  35,  77, 196, 185,
     26, 200, 226, 119,  31, 123, 168, 125, 249,  68, 183, 230, 177, 135, 160, 180,
     12,   1, 243, 148, 102, 166,  38, 238, 251,  37, 240, 126,  64,  74, 161,  40,
    184, 149, 171, 178, 101,  66,  29,  59, 146,  61, 254, 107,  42,  86, 154,   4,
    236, 232, 120,  21, 233, 209,  45,  98, 193, 114,  78,  19, 206,  14, 118, 127,
     48,  79, 147,  85,  30, 207, 219,  54,  88, 234, 190, 122,  95,  67, 143, 109,
    137, 214, 145,  93,  92, 100, 245,   0, 216, 186,  60,  83, 105,  97, 204,  52,
};
```
