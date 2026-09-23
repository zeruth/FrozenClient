# Parity: the M2 particle system

frozen draws M2 particles through `src/world/ParticleFx.cpp`, which is a stand-in written from
what the screen should look like rather than from the reference. This is the map of the real
system, built by decompiling it on 2026-09-23, so the port can start from facts.

**Nothing here is ported yet.** The stand-in still drives the screen; it should stay until the
port is complete enough to swap in and verify in one step.

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
| `FUN_0097be80` | the simulation step, 5,350 bytes: the largest function in the system |
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

## What is still unknown

- The emitter's own field offsets past the copied block: the live particle list, the bounds at
  `+0x218`, and the flags at `+0x134` are known by use, not by layout.
- The simulation step `FUN_0097be80` has not been read.
- How particles reach the scene's transparent sort (`CSortableParticleRecord`) and
  `CM2SceneRender::DrawParticle` (`FUN_008214e0`, still a stub in frozen).

## Suggested order

1. Port `CParticleEmitter2`'s construction and the small setters above, which are all understood,
   with the definition copy driven by the confirmed `M2Particle` layout.
2. Read and port `FUN_0097be80`.
3. Only then replace `ParticleFx.cpp`, and verify it with a run in the same change.
