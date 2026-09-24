# Parity: M2 ribbons

Ribbons are the trail systems attached to M2 models — weapon trails, banner edges. frozen has
**nothing**: `CM2SceneRender::DrawRibbon` is an empty function, `CM2Model` has no ribbon array, and
no element of type 3 is ever built, so the empty draw is not even reached.

This is the map, decompiled 2026-09-24, so the port can start from facts rather than from what a
weapon trail looks like. It is the same method that worked for particles; `parity-particles.md` is
the finished example of where this ends up.

## What the reference calls it

RTTI (`docs/ref/win-symbols-rtti.txt`) gives two class names:

| class | what it is |
|---|---|
| `CRibbonEmitter` | one ribbon, owning a ring of segments and its own vertex and index arrays |
| `CRibbonMat` | one material pass over that geometry — an 8-byte record, and a ribbon has several |

and a debug string, `"Ribbon: model=%s index=%d"` at `0x00a454e8`, which is what identifies
`DrawRibbon`.

## The data is already here

`M2Ribbon` in `src/model/M2Data.hpp` is complete and correctly laid out — checked against the
reference's own arithmetic rather than assumed. The reference strides the ribbon array by **0xb0**
and reads `materialIndices`' data pointer at **+0x20**; frozen's field order puts `materialIndices`
at +0x1c (so its `offset` word lands on +0x20) and its last member `pad` closes the struct at
exactly 0xb0. `M2Data::ribbons` is at reference +0x124 and `M2Data::materials` at +0x74, which is
how `DrawRibbon` reaches a ribbon's material.

## CM2SceneRender::DrawRibbon — FUN_00820f40

Small, and a near-twin of `DrawParticle`, which is already ported and is the model to follow:

1. `SysMsgPrintf("Ribbon: model=%s index=%d", ...)` — `FUN_005eeb70` is a bare `retl`, nothing to
   port.
2. **The material is a real `M2Material`, not a scratch one.** It is
   `m_data->materials[m_data->ribbons[element->index].materialIndices[0]]`, assigned straight to
   `m_curMaterial`. This is where ribbons differ most from particles, which build one from emitter
   flags.
3. `m_particleUnlitEffect->SetCurrent()` — **always the unlit effect**, with no bit test. Ribbons
   are never lit.
4. `SetupTextures()` (`FUN_0081f450`, already linked) — particles skip this; ribbons need it.
5. `SetupLighting()`, `SetupMaterial()`.
6. `SetupParticleTransform({0, 0, 0})` — the ZERO vector, not the camera position that
   `DrawParticle` passes.
7. `CShaderEffect::SetTexMtx_Identity(0)`.
8. `emitter->Draw(m_curModel->m_particleRelative)` where the emitter is
   `m_curModel->m_ribbonEmitters[element->index]` — reference +0x2bc, the field beside the particle
   emitters at +0x2c4 that frozen already has.
9. `GxXformSetView(identity)` and `GxXformSet(GxXform_World, identity)`.

## CRibbonEmitter::Draw — FUN_00980b70

Gated on `0x00b2d658 != 0` and on the segment ring being non-empty. **That global is static
initialised data holding 1 with exactly one reader and no writer** — the same shape as the
particle system's `0x00b2d530`, and the same trap: it reads as a permanently-false branch in the
disassembly and is not one. Read it out of the image.

The ribbon owns its geometry on the CPU and uploads it whole, which is the opposite of the
particle path's write-through-cursors:

```
world = relativeTo or identity;  world.translation -= origin      (+0x2c..+0x34)
GxXformPush(GxXform_World) + GxXformSet(world)                    FUN_00616a30 does both
vbuf = BufStream(GxPoolTarget_Vertex, 0x18, vertexCount)          (+0x3c)
ibuf = BufStream(GxPoolTarget_Index,  2,    indexCount)           (+0x4c)
GxBufData(vbuf, vertexArray, vertexCount * 0x18, 0)               (+0x40)
GxPrimVertexPtr(vbuf, GxVBF_PCT)
GxBufData(ibuf, indexArray + tail * 4, indexCount * 2, 0)         (+0x50)
device->PrimIndexPtr(ibuf)
```

`0x18` is 24 bytes, which is `GxVBF_PCT` exactly — position, colour, one texcoord. The format
constant in the call is literally 8, and `GxVBF_PCT = 8`.

**The index count comes from the ring, and the ring wraps:**

```
span  = tail < head ? head - tail : head + capacity - tail        (+0x14 head, +0x18 tail, +0x08 cap)
count = span * 2 + 2
```

which is a triangle STRIP — two vertices per segment edge plus the closing pair. The batch
confirms it: `primType 4` is `GxPrim_TriangleStrip`, `maxIndex` is `vertexCount - 1`.

### The per-material loop

`+0x118` is a material count and `+0x11c` an array of 8-byte `CRibbonMat` records; `+0x12c` is a
parallel array of texture handles. For each:

| bit of `mat.flags` | what it drives |
|---|---|
| 0 | unlit: emissive is black when set, white when clear — and the same bit goes to `FUN_008731c0` |
| 1 | `CShaderEffect::SetFogEnabled` |
| 2 | `GxRs_DepthTest` (state 13) |
| 3 | `GxRs_DepthWrite` (state 15) |
| 4 | `GxRs_Culling` (state 17) |

and `mat.blend` (the dword at +4) goes to `GxRs_BlendingMode` (state 6). Every one of those five
numbers matches frozen's `EGxRenderState` on the nose, which is the check that the bit assignments
are right.

The body is wrapped in `GxRsPush` / `CGxDevice::RsPop` and ends with
`SetShadersForGeometry(0)` and `SetWorldViewConstants()` — **the two functions the particle submit
already uses**. They were ported for particles this cycle and are general, not particle-specific,
which is a useful independent confirmation that `FUN_00872b00` really is "the transform for
unskinned geometry" rather than something particle-shaped.

## What frozen already has

Most of the draw's callees are written and, in three cases, were not even linked until this map
was made:

| address | frozen | state |
|---|---|---|
| `FUN_00681a20` | `GxBufData` | linked here |
| `FUN_00681b00` | `GxPrimVertexPtr` | linked |
| `FUN_00682f10` | `CGxDevice::PrimIndexPtr` | linked |
| `FUN_00684850` | `CGxDevice::BufStream` | linked |
| `FUN_00409670` / `FUN_00685fb0` | `GxRsPush` / `CGxDevice::RsPop` | linked |
| `FUN_00681450` | `GxTexSetWrap` | linked |
| `FUN_004b6cb0` | `TextureGetGxTex` | linked |
| `FUN_00873390` / `FUN_00873a50` | `SetFogEnabled` / `SetEmissive` | linked |
| `FUN_00873160` / `FUN_00872b00` | `SetShadersForGeometry` / `SetWorldViewConstants` | linked |
| `FUN_0081f450` | `CM2SceneRender::SetupTextures` | linked |

Three are not, and each is small:

* `FUN_00616a30` — `XformPush` followed by `XformSet` on one transform. frozen has both halves
  (`CGxDevice::XformPush`, `GxXformSet`) and no combined form, so this is two calls here.
* `FUN_008731c0` — sets `s_lightEnabled` and, with shaders off, `GxRs_Lighting`. A strict subset of
  `SetLocalLighting`. **It matters beyond ribbons**: `s_lightEnabled` is the first term of the
  vertex shader permutation, so whatever calls this changes which shader everything downstream
  uses.
* `FUN_00873ee0` — reads the device's current `GxRs_BlendingMode`, indexes a float table at
  `0x00ad8b7c`, scales by `0x00a45564` and calls `CShaderEffect::SetAlphaRef`. Not the same as
  frozen's `GxRsSetAlphaRef`, which sets a render state instead.

## Still unknown

The **runtime**. Everything above is the draw; nothing here says how segments are born, aged or
retired, how the vertex and index arrays are filled, or what the fields between +0x60 and +0x118
hold. `FUN_009808a0` is the emitter's `Initialize` — ten arguments, and it sizes both arrays, fills
the index array with a `% (n * 2)` pattern, computes the texture-cell reciprocals from a rect at
+0x148 and a rows/cols pair at +0x158/+0x15c, and takes two texture slots. `FUN_0097f630` and
`FUN_0097f570` are one-line setters (+0x17c, and bit 2 of the +0x160 flags). The constructor, the
per-frame update and the segment walk have not been read.

`CM2Model::InitializeLoaded` is where ribbons are built, and its 35% recall is mostly this: the
missing calls include `FUN_009808a0`, `FUN_00980630`, `FUN_0097f630`, `FUN_0097f570` and the
`FUN_0097fbe0` walk at 0x824430 over `model->[0x120]` ribbons.

## Suggested order

1. The `CRibbonEmitter` layout and `Initialize` (`FUN_009808a0`), which pins most of the fields.
2. The constructor and the per-frame update — the segment ring is the part with real behaviour.
3. `CRibbonEmitter::Draw` (`FUN_00980b70`), which is fully mapped above.
4. `CM2SceneRender::DrawRibbon` (`FUN_00820f40`) and the type-3 element that reaches it.
5. A run. Ribbons need unit movement to show anything, and that is
   [not ported](../../CLAUDE.md#known-blockers) — so plan to verify on a weapon trail in an
   emote or attack animation rather than on a moving unit.
