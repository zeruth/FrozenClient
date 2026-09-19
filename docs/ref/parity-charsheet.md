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

All six offsets are `static_assert`ed in `ScriptEvents.cpp`, so a struct that drifts fails the build
rather than quietly reporting one stat as another.

## Recovered but not yet landed

Decompiled 2026-09-19 and then held back: implementing them cost 14 unrelated callgraph links, and
the cycle was reverted under the report gate. See `tools/recomp/README.md` under **callgraph** for
the mechanism. The facts below are the expensive part and are recorded so the re-land needs no
further Ghidra.

**`GetSpellPenetration` (`FUN_0060e470`)** — `modTargetResistance` at `0x105c`, **negated on the way
out**. Penetration is held as a negative modifier to the target's resistance and reported as a
positive number; forwarding the field unchanged would show every value with the wrong sign.

**`GetSpellCritChance` (`FUN_0060e290`)** — `spellCritPercentage[school - 1]` at `0xdd0`, a
seven-entry array. School arrives 1-based and the reference tests the converted index **as
unsigned**, so school 0 and anything negative wrap past the end and take the usage error
`Usage: GetSpellCritChance(school)` rather than reading in front of the array.

**`GetExpertisePercent` (`FUN_00612cb0`)** — two returns, `expertise` at `0xdbc` and
`offhandExpertise` at `0xdc0`, each scaled by the float **0.25** at `00a1f6f4` (four points of
expertise to one percent). Both are pushed even with no player, as zeros, so the sheet keeps two
return values.

## Decompiled and blocked

**`GetSpellBonusDamage` (`FUN_0060e310`)** — same 1-based school and usage string as the crit one,
but it does not read a field: it combines two unidentified helpers at `00578210` and `00578250`.

**`GetPetSpellBonusDamage` (`FUN_0060e410`)** — reads `+0x1264`, which no named field in frozen's
struct has been shown to sit at, and gets none of the neighbouring-offset corroboration that made
the percentages safe.

**`IsMounted` (`FUN_006125a0`)** — reads the object rather than the descriptor: a count at `+0x9c0`
that must be positive and flag `0x10000000` at `+0xa30` that must be clear. Neither has a frozen
counterpart.

**`IsIndoors` / `IsOutdoors` (`FUN_00612300` / `FUN_00612360`)** — one predicate and its negation,
both resting on a `CGUnit_C` method at `0071b7f0` with no frozen counterpart.
