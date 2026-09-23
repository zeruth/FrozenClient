# M2 batch shader substitution

Written 2026-09-23 while triaging `tools/livestubs.py` output. Everything here was read out of
`WoW.exe` with `llvm-objdump`. **Nothing here has been ported or seen running.**

## The gap

`CM2Shared::InitializeSkinProfile` resolves one `CShaderEffect*` per skin-profile batch, and before
it does, it rewrites the batches' `shader` fields twice:

```
FUN_00836980   SubstituteSimpleShaders        implemented in frozen
FUN_00837680   SubstituteSpecializedShaders   EMPTY in frozen, one live call site
FUN_00836c90   GetEffect                      per batch, after both
```

So every batch that the second pass would have rewritten reaches `GetEffect` with an
unsubstituted shader id, and picks a different effect than the reference would. That is a model
material difference, not a crash, and it has **not** been confirmed on screen.

All three were identified from the call site, not from their bodies: the reference makes exactly
these three calls in exactly the order frozen's `InitializeSkinProfile` makes them, inside the
function at `FUN_00837a40`.

## What the specialised pass does

It merges adjacent batches that draw the same material into one multi-texture batch, and marks the
pair. Field offsets below are `M2Batch` as frozen already declares it, and they check out against
every access in the function:

| offset | field | how the reference uses it here |
|---|---|---|
| +0x02 | `shader` | read, masked, and overwritten with `0x8000` / `0x8001` |
| +0x0a | `materialIndex` | indexes `m_data->materials` (4 bytes each) |
| +0x0c | `materialLayer` | the whole pass is skipped unless some batch has this above zero |
| +0x0e | `textureCount` | compared against 1 and 2 |
| +0x10 | `textureComboIndex` | the two batches must agree |
| +0x12 | `textureCoordComboIndex` | indexes a `uint16` array at `m_data+0x8c` |
| +0x14 | `textureWeightComboIndex` | indexes a `uint16` array at `m_data+0x94`; the two batches' entries must be equal |

The entry test is cheap and worth keeping when this is ported: walk the batches, and if none has
`materialLayer > 0`, return immediately. Single-layer models skip the whole pass.

The **pair protocol** is the part to get right. When two batches merge, the reference writes
`shader = 0x8000` into the first and `shader = 0x8001` into the second. `SubstituteSimpleShaders`
already skips any batch whose shader has bit 15 set (frozen implements that check), so the marks
survive into `GetEffect`, which is what turns them into the combined effect.

Other observations from the same read, each one an access the port has to reproduce:

* `shader &= 0xFF8F` clears bits 4, 5 and 6 when the batch has `materialLayer == 0`,
  `textureCount >= 1` and the material's blending mode is 0.
* `shader & 7` is taken once at the top of each iteration and kept; values 4 and 6 gate one of the
  branches.
* A running "previous materialIndex" is held and compared, so the pass only ever considers
  consecutive batches sharing a material.
* `m_data+0x74` is the materials array (`M2Material`, 4 bytes: two `uint16`s, the second being the
  blending mode, which the function compares against 0, 1 and 2).

## Why it is not ported yet

It is roughly 960 bytes of branchy state machine with four nested conditions and a small local
state byte that selects between three merge shapes. A transcription slip would give models the
wrong blend or texture stage, which is exactly the class of bug this project was set up to stop
guessing at, and there is no way to check it without a run. It should be ported in its own cycle,
alone, and looked at on screen in the same change.

## Recorded links

| reference | frozen | evidence |
|---|---|---|
| `00837a40` | `CM2Shared::InitializeSkinProfile` | opens on the skin profile at `+0x170` and its index count, then makes the three calls below in order |
| `00836980` | `CM2Shared::SubstituteSimpleShaders` | first of the three |
| `00837680` | `CM2Shared::SubstituteSpecializedShaders` | second of the three, **stub** |
| `00836c90` | `CM2Shared::GetEffect` | already linked by string evidence |
