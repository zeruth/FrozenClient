# Parity: the M2 particle system

frozen draws M2 particles through `src/world/ParticleFx.cpp`, which is a stand-in written from
what the screen should look like rather than from the reference. This is the map of the real
system, built by decompiling it on 2026-09-23, so the port can start from facts.

**Status, 2026-09-24.** The header of this file used to say "nothing here is ported yet"; that has
not been true for some time.

* The **runtime** is ported: `src/model/CM2ParticleEmitter.*` carries `CParticleEmitter2`'s
  construction, pool sizing, emission, the substepper, the step loop, both concrete subclasses,
  placement and `Update`; `CM2Model` builds and configures one emitter per `M2Particle` and drives
  it every frame from `AnimateParticleEmitter` (`FUN_008309c0`, 100% recall).
* The **draw gate** is ported: `CM2SceneRender::DrawParticle` (`FUN_008214e0`, 91% recall).
* The **geometry** is not. `CM2ParticleEmitter::Draw` reaches its quad builder and prints an
  error instead. That is the whole remaining gap, and the section "The draw chain" below is its
  specification.

**The stand-in still drives the screen** and should stay until the geometry lands and a run
confirms it. Nothing in the ported half has been seen running.

## What the reference calls it

RTTI (`docs/ref/win-symbols-rtti.txt`) gives the class names, and the assert strings put them in
`ParticleSystem2.cpp`:

| class | what it is |
|---|---|
| `CParticleEmitter` / `CParticleEmitter2` | the emitter; `2` is the one M2 models use |
| `CParticle` / `CParticle2` | one live particle |
| `CParticle2_Model` | a particle that is itself a model (the recursion model) |
| `CSortableParticleRecord` | a particle in the scene's transparent sort |

Related strings worth knowing: `Particle`, `Particle_Unlit`, `ParticleBatch` (shader/effect
names), `M2BatchParticles` and `M2ForceAdditiveParticleSort` (CVars), `particleDensity`
(`DAT_00b2d678`), `DBFilesClient\ParticleColor.dbc`, and `Particle.wfx`.

The whole system lives in roughly `0x978000`-`0x984000`: 212 functions.

## The functions identified so far

| address | what it does |
|---|---:|
| `FUN_0097be80` | **the per-particle VERTEX WRITER**, 5,350 bytes: the largest function in the system. This table called it "the simulation step" until 2026-09-24, which was wrong and would send a reader looking for the step loop in the wrong place -- the step loop is `FUN_0097dd20`. See "The draw chain" below. |
| `FUN_0097eec0` | `CParticleEmitter2` built from an `M2Particle` definition, 1,600 bytes |
| `FUN_0097e150` | the `CParticleEmitter2` base constructor (vtable `PTR_FUN_00aa2cc0`) |
| `FUN_00981310` / `FUN_009813f0` / `FUN_009820f0` | the emitter subclass constructors, storing kind 1, 2 and 3 at `+0x20` |
| `FUN_009808a0` | emitter `Initialize`: tile counts, index buffer, the per-row/column reciprocals |
| `FUN_0097ea60` | per-frame pre-render: resets the bounds at `+0x218`, then fills through `FUN_0097e730` when the emitter has particles |
| `FUN_0097e730` | the fill: binds the emitter texture and walks the live particles |
| `FUN_0097b9e0` | "does this emitter or any child still have particles", recursive over the children at `+0x6c` |
| `FUN_0082d2f0` | `CM2Model`'s per-frame emitter animation (see below) |
| `FUN_00978c70` | `SetTextureTileSize`: both arguments must be powers of two, stores log2(rows) and the two reciprocals |
| `FUN_00978bf0` | `SetTexture`: closes the old handle, duplicates the new one into `+0x128` |
| `FUN_00978d00` | sets the head/tail flags `0x4`/`0x8` and `0x20000`, and the 4-vertex / 6-index counts they imply (doubled when both are on) |
| `FUN_00978e30` | sets flag `0x100000` when the tile grid has more than one cell |
| `FUN_00978da0` | stores a float at `+0xbc`, snapping it to zero below the epsilon |
| `FUN_00978dd0` | stores a linear ramp `(slope, intercept)` at `+0x180`/`+0x17c` from two points, zeroing both when the two x values coincide |
| `FUN_0097aeb0` | attaches the recursion model: creates it through `CM2Scene::CreateModel` and registers a callback |
| `FUN_0097f570` / `FUN_0097f630` / `FUN_0097f640` | small flag and counter accessors |

## How CM2Model drives it

`FUN_0082d2f0` is the per-frame animation, one iteration per `M2Particle`:

- the emitter objects live in an array at `model+0x2c4`, one pointer per definition, and a
  parallel array of `0x88`-byte per-emitter state blocks at `model+0x2c0`;
- the definitions are `M2Data+0x12c` with the count at `M2Data+0x128`, stride `0x1dc` (476 bytes,
  which is what frozen's `M2Particle` should measure);
- the enabled track is animated first (`FUN_0082b270`, the byte-track animator) into state `+0x78`;
- an emitter counts as active when its enabled byte is set *and* the emitter object's flag `0x2`
  is on; otherwise it stays alive only while `FUN_0097b9e0` still reports particles;
- the model's flag `0x400` is the running OR of "some emitter is still alive";
- then ten float tracks are animated through `FUN_0082b340` into the state block at `+0x00`,
  `+0x0c`, `+0x18`, `+0x24`, `+0x30`, `+0x3c`, `+0x48`, `+0x54`, `+0x60`, `+0x6c`, in the
  definition's own field order (speed, variation, latitude, longitude, gravity, life, emission
  rate, width, length, z-source).

Each track is only animated when its sequence-times array holds more than one entry, or holds one
whose key array is longer than the model's current sequence index: the same guard frozen's
`M2AnimateTrack` callers use.

## `M2Particle` is confirmed correct

frozen's `M2Particle` matches the reference field for field. Two independent checks:

- it measures exactly `0x1dc` bytes, the stride the reference walks the array with, and
  `src/model/M2Data.hpp` now carries a `static_assert` so a future edit cannot silently break it;
- every offset the animation reads lands on the field frozen declares there. The bone index at
  `+0x14`, the ten float tracks at `+0x34`, `+0x48`, `+0x5c`, `+0x70`, `+0x84`, `+0x98`, `+0xb0`,
  `+0xc8`, `+0xdc`, `+0xf0`, and the enabled track at `+0x1c8` with its sequence-times count and
  offset at `+0x1cc` and `+0x1d0`, are speed, variation, latitude, longitude, gravity, life,
  emission rate, width, length, z-source and visibility, in that order, exactly as declared.

So `FUN_0097eec0` is a field-by-field copy of the definition into the emitter: the 34-dword block
at `+0x98` is `lifeTrack` through the start of `alphaTrack`, and the individual dwords after it
are the remaining `M2Array` count/offset pairs the compiler did not merge. The emitter therefore
owns a copy of most of the definition, which is what its own layout has to mirror.

The one read that does not fit is `+0x1dc`, one dword past the end of the record. Resolve it
against the disassembly before trusting that field.

## The draw chain

Decompiled and disassembled end to end on 2026-09-24. Ghidra drops register arguments throughout
this chain, so every receiver below was read off the disassembly rather than the decompilation.

```
CM2SceneRender::DrawParticle        FUN_008214e0   PORTED, 91%
  CM2ParticleEmitter::Draw          FUN_0097ea60   ported
    <the quad builder>              FUN_0097e730   NOT PORTED
      SetupDrawBasis                FUN_0097a390   PORTED, 100%
      CGxDevice::BufStream          FUN_00684850   ported
      SetupVertexCursor             FUN_0097a2e0   PORTED, 100%
      <walk the live particles>     FUN_0097e580   NOT PORTED
        <per-particle vertices>     FUN_0097be80   NOT PORTED  <- the bulk of the work
          SampleAppearance          FUN_00979e90   PORTED, 100%
            SampleColor             FUN_009795d0   PORTED, 100%
            M2PartTrackEvalAlpha    FUN_009794f0   PORTED, 100%
            M2PartTrackEvalCell     FUN_00979560   PORTED, 100%
          SampleSpin                FUN_0097a130   PORTED, 100%
          C33Matrix::RotationAroundAxis  FUN_004c5820  PORTED
      <the draw call>               FUN_0097a580   NOT PORTED
        <shared index buffer fill>  FUN_0097a260   NOT PORTED

M2ParticleIndexBufferCreate         FUN_00979170   PORTED   (from CM2Cache::Initialize)
<the twinkle table fill>            inline 0x81c240  PORTED  (from CM2Cache::Initialize)
```

**Three functions left**: the walk (`FUN_0097e580`), the writer (`FUN_0097be80`), and the pair
that frames them (`FUN_0097e730` and `FUN_0097a580` with `FUN_0097a260`).

### FUN_0097e730 -- the fill

Receiver is the emitter; `relativeTo` and the caller's buffer come in as arguments.

1. Copy the current view matrix aside (`device + 0x6c0 + device[0x6be] * 0x10`).
2. `FUN_0097a390(relativeTo, <that matrix>)` -- the basis setup below.
3. Resolve the emitter's texture (`+0x128`); on failure clear draw bit 0, zero `+0x1c` and return.
4. Cap the particle count to what fits: `min(0x4000 / verticesPerParticle, liveCount)`, where
   `verticesPerParticle` is the emitter's `+0x8c`.
5. Pick the vertex format: `(matFlags & 0x1) ? GxVBF_PNCT : GxVBF_PCT`. In the reference this is
   the expression `(-(matFlags & 1 != 0) & 0xfffffffc) + 8`, which is 4 when lit and 8 when not --
   and 4 and 8 are exactly `GxVBF_PNCT` and `GxVBF_PCT` in frozen's own enum.
6. When the caller passed no buffer, take one: `BufStream(0, GxVertexAttribOffset-derived stride,
   stride * count)`, then lock it through the device virtual at `vtbl+0xd8`.
7. `FUN_0097a2e0` builds the four write cursors, `C44Matrix::AffineInverse` (`FUN_004c2fc0`) is
   taken, and `FUN_0097e580` writes the vertices.
8. Unlock (`vtbl+0xdc`), mark the buffer, and if anything was written call `FUN_0097a580` with the
   index count `emitter->+0x90 * emitter->+0x1c`, then restore the view.

### FUN_0097a390 -- the basis, and the globals every writer reads

This is where the camera-facing basis is computed once per emitter and parked in globals:

| global | what it holds |
|---|---|
| `DAT_00b2d540 .. 0b2d548` | the **vertex normal** every particle vertex is written with -- row 2 of the matrix handed in, i.e. the camera direction |
| `DAT_00b2d550 .. 0b2d58c` | the particle-space matrix (`local_50` built here), used by `FUN_004c21b0` to put a particle's position into view space |
| `DAT_00b2d590 .. 0b2d5a4` | the 2x3 **billboard basis** (right and up), only filled when flag `0x4000` is set |
| `DAT_00b2d5b4 / 0b2d5b8` | the four **corner offsets**, stride 8 |
| `DAT_00b2d5d4 / 0b2d5d8` | the four **corner UVs**, stride 8 |
| `emitter + 0x20c .. 0x214` | the normalised tumble axis, derived here when `0x4000` is set |

It negates `emitter + 0x1c4 .. 0x1cc` (the emitter's world offset) into the translation row, and
when flag `0x200` is set it pre-multiplies by the emitter's own placement matrix at `+0x184`
instead of by the caller's `relativeTo`.

### FUN_0097a2e0 -- the four write cursors

Builds an 9-dword cursor block from the mapped buffer base and the format. Each of the first four
entries is a pointer and each of entries 4..7 is its stride; entry 8 is the vertex counter.

| cursor | attribute | written as |
|---|---|---|
| `[0]` / `[4]` | `GxVA_Position` (0) | three floats |
| `[1]` / `[5]` | `GxVA_Normal` (3) | three floats, always the `DAT_00b2d540` triple |
| `[2]` / `[6]` | `GxVA_Color0` (4) | one packed dword |
| `[3]` / `[7]` | `GxVA_TexCoord0` (6) | two floats |

**When the emitter is UNLIT the normal cursor is redirected** at a static zeroed `C3Vector`
(`DAT_00dce8b4`) with **stride 0**, so every normal write lands harmlessly in the same scratch --
which is correct, because the unlit format `GxVBF_PCT` has no normal to write into. Frozen's
`GxVertexAttribOffset(format, attrib)` is `FUN_00681240` exactly; `FUN_00681230` is the format's
vertex size.

### FUN_0097e580 -- the walk

Indexes the live list at `+0x54`, resolving each index into the 0x20-byte pool at `+0x34` or the
0x40-byte pool at `+0x44` on `m_particleKind` (`+0x98`), and calls `FUN_0097be80` per particle.

When flag `0x20` (sorted) is set it first computes a view depth per particle
(`DAT_00b2d558/568/578/588` dotted with the position) through `FUN_007a0f50`, then drains the sort
through `FUN_0097e080` instead of walking in list order.

Afterwards it merges the accumulated `+0x218` bounds with the caller's box and offsets both
corners by the emitter's world position, and finally sets `+0x1c` to
`writtenVertices / verticesPerParticle`.

### FUN_0097be80 -- the per-particle vertices

**READ THIS IN ASSEMBLY, NOT FROM THE DECOMPILATION.** Ghidra's stack-frame numbering inside the
velocity-aligned branch does not reconcile with the disassembly -- it reports `local_14 = local_c`
where the instructions at 0x97c0e1 plainly do `[ebp-0xc] = [ebp-0x4]` -- so the locals it names
cannot be trusted to be the ones it says. The function is ~1,400 lines of disassembly and this is
the one place in the whole chain where transcribing the decompilation would silently produce
wrong geometry rather than a compile error.

The two tables it indexes are **static initialised data**, not computed at startup, and were read
straight out of the image:

| i | corner (0x00b2d5b4, stride 8) | UV (0x00b2d5d4, stride 8) |
|---|---|---|
| 0 | (-1,  1) | (0, 0) |
| 1 | (-1, -1) | (0, 1) |
| 2 | ( 1,  1) | (1, 0) |
| 3 | ( 1, -1) | (1, 1) |

which is +y up with v=0 at the top, and matches the index pattern (0, 1, 2, 3, 2, 1) the shared
index buffer holds: triangles (0,1,2) and (3,2,1), consistently wound.

The velocity-aligned branch's call sequence, read at 0x97c063..0x97c11b, since the decompilation
garbles it: negate the particle's velocity, put it through the **3x3 only** of the particle-space
matrix (`FUN_0057c2e0`, which is a direction transform and drops translation), copy its X and Y
into a 2-vector (`FUN_004c4df0`), and take `1/|xy|` guarded on 2^-22. The quad is then stretched
along that screen-space direction, with the width scaled by the ratio of the 3D and 2D lengths.

Five different quad shapes live in it. The flag word is `+0x134`:

| gate | shape |
|---|---|
| `0x4` clear | not a head quad; skip to the tail test |
| `0x200000` set and the particle has velocity | **velocity-aligned**: the quad is stretched along the screen-space velocity direction, width scaled by the ratio of the two lengths |
| spin at `+0xc8`/`+0xcc` both zero, `0x4000` clear | **plain billboard**: four corners from the corner table, in view space |
| spin zero, `0x4000` set | **basis billboard**: corners rotated through the `DAT_00b2d590` right/up pair |
| spin non-zero, `0x4000` clear | **spun billboard**: `FUN_006f7a60` gives sin/cos of the particle's angle and the four corners are written out longhand |
| spin non-zero, `0x4000` set | **tumbled**: `FUN_004c5820` builds a rotation about the `+0x20c` axis per corner |
| `0x8` set | **head-tail**: a second quad trailing along the particle's velocity, its length `min(age, +0xac)` when `0x20000` is set |

Shared by every path:

* **Twinkle.** When `+0x140 < 1` or `+0x148 != 0`, a 7-bit index is taken from the particle's age
  times `+0x13c` mixed with the particle's own address (`(addr >> 5) + round(age * fps)) & 0x7f`)
  into the random table at `DAT_00dce690`. A particle whose sample exceeds `+0x140` is **skipped
  entirely** -- that is the on/off blink. The surviving ones scale by
  `table[i] * +0x148 + +0x144`.
* **Colour and alpha** come from `FUN_00979e90`, or `FUN_00979d60` when flag `0x1000000` is set.
  The packed colour is **byte-swapped** when `FUN_00532af0()->+0x14 == 1`, which is the renderer's
  endianness/format flag.
* **Texture cell.** `cell & (+0x124 - 1)` gives the column and `cell >> +0xc` the row, times the
  `+0x10` / `+0x14` reciprocals; the negative-column fixup adds `DAT_009e23ac`.
* **Every vertex** updates the emitter's running bounds at `+0x218`/`+0x224`, writes the constant
  normal, writes the packed colour, writes the UV, then advances all four cursors and increments
  the counter.

### FUN_0097a580 -- the draw

`XformSetView(identity)`, then the shared index buffer at `DAT_00dce684` -- filled once by
`FUN_0097a260` with the repeating quad pattern **(0, 1, 2, 3, 2, 1)** per four vertices, 0x1fff8
shorts in all, i.e. 21,845 quads -- is bound, the stream is set, and a batch
`{primType 3, 0, indexBuffer, 0, count - 1}` goes to the device virtual at `vtbl+0xa8`.

## Suggested order

Steps 1 and 2 of the original list are done; what is left:

1. `FUN_0097be80`, **from the disassembly**, starting with the plain billboard path -- it draws
   the overwhelming majority of particles. **Do not fall back to the billboard for the other four
   shapes**; an unported shape should say so, not silently draw the wrong thing.
2. `FUN_0097e580` (the walk). Its unsorted path is short; the sorted one (flag `0x20`) needs
   `FUN_007a0f50` and `FUN_0097e080` and can wait.
3. `FUN_0097e730` and `FUN_0097a580` with `FUN_0097a260` to close the chain, then a run.
4. Only then replace `ParticleFx.cpp`, and verify that in its own change.

Two things are ported but **unreachable**, and neither is on this path: `SampleColor`'s flag
`0x10` colour override (its setter `FUN_0097a990` and the `model30` inheritance in
`CM2Model::InitializeLoaded` are not ported) and `FUN_00979d60`, the flag `0x1000000` fast path
that replaces the whole appearance sampler with two lookups into a precompiled ramp at emitter
`+0x11c`. Both hang off that same unported ramp, so they are one piece of work, not two.
