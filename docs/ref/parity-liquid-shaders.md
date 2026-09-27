# The liquid shaders, read out of the archives

The four liquid materials do not use shaders frozen compiled — they use the **reference's own**,
shipped in `patch.MPQ` under `Shaders\Vertex\vs_2_0\*.bls` and `Shaders\Pixel\ps_2_0\*.bls`. That
means frozen's constant block is a **contract with a binary it did not write**, and the only way to
check it is to read the shader. This file is that reading, done 2026-09-27.

## How to get at them

`tools/mpq-probe.py` already extracts:

```bash
python tools/mpq-probe.py --extract 'Shaders\Vertex\vs_2_0\vsLiquidWater.bls' out.bls
```

A `.bls` is a container: magic `HSXG`, version at +4, **permutation count at +8**, then per-permutation
sizes, and the D3D9 token streams begin at +0x1c. Each stream starts with its version token
(`00 02 fe ff` = vs_2_0, `00 02 ff ff` = ps_2_0) and ends with `ff ff 00 00`. Carve one out, write it
to a `.cso`, and disassemble:

```bash
FXC="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe"
MSYS_NO_PATHCONV=1 "$FXC" -nologo -dumpbin vsLiquidWater_0.cso
```

`vsLiquidWater` carries **4 permutations** (712, 936, 1160, 1384 bytes — they grow with the local
light count, which is why every water material loads four vertex programs and one pixel program).
`vsLiquidMagma` carries **1**. `vsLiquidProcWater` carries 4, the first alone being 2640 bytes.

## The constant contract, confirmed

Every register below was read out of the disassembly, not inferred. frozen's layout in
`LiquidMaterialSettings.cpp` matches it exactly.

| registers | contents | frozen |
|---|---|---|
| c0..c3 | projection, as ROWS | `VS_PROJECTION = 0` |
| c4 | fog: `(-scale, scale*fogEnd, density, 0)` | `VS_FOG = 4` |
| c5..c8 | world-view, as ROWS | `VS_WORLD_VIEW = 5` |
| c9, c13, c17, c21, c25, c29 | up to six texture matrices, 0x40 apart | `VS_TEX_MATRIX = 9`; `VS_SCALE`/`VS_ROT_SCALE` are numbers 5 and 6 of the same series |
| c33 | sun direction (view space) | `VS_SUN_DIR = 33` |
| c34, c35, c36 | sun ambient, diffuse, specular (`c36.w` = the exponent) | `VS_SUN_DIR + 1..3` |
| c46, c48[i], c51 | wave phase, per-wave, reciprocals — procedural only | `VS_WAVE_PHASE_A`, `VS_WAVE_A`, `VS_WAVE_RECIP_A` |

The transform is unambiguous, and it is ROW-vector:

```
mul r0, v0.y, c6 / mad r0, v0.x, c5, r0 / mad r0, v0.z, c7, r0 / add r0, r0, c8   ; view space
mul r1, r0.y, c1 / mad r1, r0.x, c0, r1 / mad r1, r0.z, c2, r1 / mad oPos, r0.w, c3, r1
```

So `oPos.z = z*c2.z + c3.z` and `oPos.w = z*c2.w`. With frozen's measured projection
(`c2 = (0,0,1.0001,1)`, `c3 = (0,0,-0.4,0)`) that is the standard D3D mapping for near 0.4 / far 4000,
and **the depth it produces is correct**. Negating the third row — the divergence `SetupTransforms`
still records as "the one knob" — would make `oPos.z` negative for everything in front of the camera.
It is right to skip it; that note should stop being offered as a suspect.

`r0.w` works out to 1 because position is declared FLOAT3 (D3D9 fills `.w` with 1) and an affine
world-view has `(0,0,0,1)` down its fourth column.

## Vertex declarations — which is what pins each material's format

| shader | declares | format |
|---|---|---|
| vsLiquidWater / NoSpec | position, **normal**, **color**, texcoord, texcoord1 | `GxVBF_PNCT2` (6) |
| vsLiquidMagma | position, **color**, texcoord | `GxVBF_PCT` (8) |
| vsLiquidProcWater | position, texcoord, texcoord1 | `GxVBF_PT2` (11) |

This is the independent confirmation that the three materials really do want three different formats,
and that `GxVBF_PT2` — which frozen once used for all of them — is correct for the procedural one
*only*. Under PT2 the water shader's `dcl_normal` and `dcl_color` read undefined data.

## The texcoords are crossed, and the shader says so

```
mul r1.xy, v3.y, c10 / mad r1.xy, v3.x, c9, r1 / add oT1.xy, r1, c12
mul r1.xy, v4.y, c14 / mad r1.xy, v4.x, c13, r1 / add oT0.xy, r1, c16
```

`oT1` comes from **v3 = TexCoord0** through the matrix at **c9**; `oT0` comes from
**v4 = TexCoord1** through the matrix at **c13**. Combined with `FUN_007d4ab0` passing its two
cursors crossed, that is why `WriteLayerVertices` puts the depth ramp in TexCoord1 and the tiling
coordinate in TexCoord0.

`vsLiquidMagma` has one matrix, at **c9**, with its translation read from **c12** — which is where
`MagmaScrollMatrix`'s `d0`/`d1` land, confirming the scroll belongs there.

## psLiquidWater

Two samplers only, and the alpha comes from the ramp:

```
dcl_2d s0 / dcl_2d s1
texld r0, t1, s1        ; s1 = Texture1 = the lake_a animation
texld r1, t0, s0        ; s0 = Texture0 = the procedural depth ramp
mul r3.w, r1.w, v0.w    ; OUTPUT ALPHA = ramp.a * vertex colour .a
mad r3.xyz, v0, r1, r0
add r2.xyz, v1, c0.x    ; specular + 0.25
mul r2.xyz, r0.w, r2    ; gated by the lake texture's alpha
```

So the water's transparency IS the depth ramp's alpha channel, which is what
`DepthGradientGenerate` writes into each pixel's high byte, multiplied by the vertex colour's alpha —
`0xffffffff` from the writers. The pixel shader writes no depth and has no texkill.

## What this rules out

Taken with the runtime measurements (D3D reports ZENABLE 1, ZFUNC LESSEQUAL, ZWRITE 1 and the same
depth surface for the WMO and liquid draws), the shader contract closes the structural search for the
**water-drawn-over-everything** defect: it is not the constant layout, not the vertex format, not the
transform convention, and not a depth-writing pixel shader. Whatever is left is not in this file's
subject matter.
