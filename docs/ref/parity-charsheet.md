# Parity: the character sheet's player stats

Each of these is the same shape in the reference: resolve the active player, read one field of
`CGPlayerData` by absolute offset, and push `0.0` rather than nil when there is no player, so the
sheet shows a zero instead of going blank.

## Landed

| binding | reference | field | offset |
|---|---|---|---|
| `GetBlockChance` | `FUN_0060df90` | `blockPercentage` | `0xdb0` |
| `GetDodgeChance` | `FUN_0060df30` | `dodgePercentage` | `0xdb4` |
| `GetParryChance` | `FUN_0060e070` | `parryPercentage` | `0xdb8` |
| `GetCritChance` | `FUN_0060e0d0` | `critPercentage` | `0xdc4` |
| `GetRangedCritChance` | `FUN_0060e230` | `rangedCritPercentage` | `0xdc8` |
| `GetSpellBonusHealing` | `FUN_0060e3b0` | `modHealingDonePos` | `0x1050` |
| `GetSpellPenetration` | `FUN_0060e470` | `modTargetResistance` (negated) | `0x105c` |
| `GetSpellCritChance` | `FUN_0060e290` | `spellCritPercentage[school - 1]` | `0xdd0` |
| `GetExpertisePercent` | `FUN_00612cb0` | `expertise`, `offhandExpertise`, each x 0.25 | `0xdbc`, `0xdc0` |
| `GetSpellBonusDamage` | `FUN_0060e310` | `modDamageDonePos + modDamageDoneNeg`, by school | `0xffc`, `0x1018` |

All six offsets are `static_assert`ed in `ScriptEvents.cpp`, so a struct that drifts fails the build
rather than quietly reporting one stat as another.

## How spell bonus damage is read

`GetSpellBonusDamage` does not read a field directly. It calls two `CGPlayer_C` methods, both
identified 2026-09-19 and now ported beside it:

| method | reference | field |
|---|---|---|
| `GetModDamageDonePos` | `FUN_00578210` | descriptor `0xffc + school * 4` |
| `GetModDamageDoneNeg` | `FUN_00578250` | descriptor `0x1018 + school * 4` |

The two offsets are `0x1c` apart, which is exactly seven `int32_t`, and that is what identified
them: `modDamageDonePos[7]` then `modDamageDoneNeg[7]`, matching frozen's struct already.

Both refuse for anyone but the active player -- the reference compares the object's GUID against
the active player's and returns 0 on a mismatch -- because these fields are only ever sent for
them. And the binding **adds** the two, so the half named Neg is expected to arrive already
signed; that is reproduced rather than second-guessed.

## Decompiled and blocked

**`GetPetSpellBonusDamage` (`FUN_0060e410`)** — reads `+0x1264`, which no named field in frozen's
struct has been shown to sit at, and gets none of the neighbouring-offset corroboration that made
the percentages safe.

**`IsMounted` (`FUN_006125a0`)** — reads the object rather than the descriptor: a count at `+0x9c0`
that must be positive and flag `0x10000000` at `+0xa30` that must be clear. Neither has a frozen
counterpart.

**`IsIndoors` / `IsOutdoors` (`FUN_00612300` / `FUN_00612360`)** — one predicate and its negation,
both resting on a `CGUnit_C` method at `0071b7f0` with no frozen counterpart.
