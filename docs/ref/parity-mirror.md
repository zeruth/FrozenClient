# Parity: object field updates and the mirror handlers

Why the player's health bar never moves. Recovered 2026-09-19, starting from the runtime trace
rather than from reading code: the report's trace section records that the reference calls
`Script_UnitHealth`, `Script_UnitMana`, `CSimpleFontString::SetText`, `CScriptRegion_GetWidth` and
`CScriptRegion_GetHeight` on every traced frame and frozen calls them **zero** times.

That is not a missing binding. All of those are implemented. Nothing asks for them.

## 1. The chain, and where it stops

`SMSG_UPDATE_OBJECT` is read twice. `ObjectUpdateHandler` runs the first pass, seeks the data store
back to where it started, and runs the second pass over the same bytes.

| stage | reference | frozen | state |
|---|---|---|---|
| handler | — | `ObjectUpdateHandler` | |
| first pass | `FUN_004d7050` | `ObjectUpdateFirstPass` | |
| ... UPDATE_PARTIAL | `FUN_004d6e80` | `UpdateObject` | applies the values |
| ... apply | — | `FillInPartialObjectData` | `object->SetBlock(block, value)` |
| second pass | `FUN_004d7100` | `ObjectUpdateSecondPass` | |
| ... UPDATE_PARTIAL | **`FUN_004d5550`** | **`CallMirrorHandlers`** | **reads and discards** |
| ... UPDATE_MOVEMENT | `FUN_004d6da0` | `PostMovementUpdate` | |
| ... UPDATE_FULL / _3 | `FUN_004d63b0` | `PostInitObject` | |
| ... IN_RANGE / OUT_OF_RANGE | `FUN_004d41c0` | `SkipSetOfObjects` | |

**The field values do arrive.** `FillInPartialObjectData` writes every changed dword into the
object in the first pass, so `CGUnitData::health` and the rest are live and correct. A binding that
reads one returns the right number.

**What never happens is the notification.** `CallMirrorHandlers` walks the same change mask, reads
each changed dword, and drops it on the floor:

```c
if (IsMaskBitSet(changeMasks, block)) {
    uint32_t blockValue = 0;
    msg->Get(blockValue);          // and nothing else
}
```

The two `// TODO` markers around that `if` are where the per-field handlers belong. Discarding the
*value* there is correct -- the first pass already stored it, and re-applying would be redundant.
What is missing is the dispatch.

> Do not "fix" this by adding `object->SetBlock(...)` to `CallMirrorHandlers`. It looks like the
> obvious one-line repair next to its sibling `FillInPartialObjectData`, which does exactly that,
> and it is wrong: the second pass re-reads bytes the first pass has already applied.

## 2. Why that makes the interface static

FrameXML does not poll. `UnitFrameHealthBar_Update` runs when `UNIT_HEALTH` fires, and that event
comes from a mirror handler on the health field. With no dispatch:

- no `UNIT_HEALTH`, `UNIT_MANA`, `UNIT_MAXHEALTH`, `UNIT_DISPLAYPOWER`,
- so no `UnitFrame_Update`, so no `UnitHealth` call, no `SetText` on the health string, and no
  `GetWidth`/`GetHeight` while sizing the bar,

which is exactly the set of functions the trace caught the reference calling and frozen not. The
trace agrees with this reading on every row, which is the strongest evidence available short of a
run.

Frozen signals only three events from the whole object layer (`CGPlayer_C`, `NameCache`,
`SpellBook`). The glue screens signal many more, which is why character select behaves and the
world interface does not.

## 3. The dispatcher is a watcher registry, not a table

`FUN_004d5550` (frozen's `CallMirrorHandlers`) was decompiled after the section above was written,
and it corrects an assumption in it. There is **no block-index-to-handler table**. The reference
keeps an intrusive linked list of registered watchers, builds it on the stack for the duration of
the call, walks it once per descriptor block through `FUN_004d5150`, and flushes it at the end
(`FUN_007cecd0`).

Each watcher node, by offset:

| offset | meaning |
|---|---|
| `+0x10` | the callback, invoked as `(object, guid, fieldOffset)` |
| `+0x14` | passed to `FUN_004d3bf0` alongside the length to recover the field offset |
| `+0x1c` | where the watcher's copy of the **old** bytes live |
| `+0x20` | where the **new** bytes live |
| `+0x24` | the watched length, in bytes -- watchers cover *ranges*, not single dwords |
| `+0x2c` | set to 1 when the block is touched |
| `+0x2e` | when zero, memcmp old against new and fire only on a difference; when set, fire always |

Two consequences for any port:

* A watcher covers a byte range, so one registration can cover, say, all the power fields at once
  and recover which one moved from the offset handed to its callback.
* The old-versus-new comparison is the watcher's own, against its private copy -- which is why
  `FillInPartialObjectData`'s unused `if (!forFullUpdate)` branch matters. Frozen's first pass has
  already written the new value by the time the second pass runs, so comparing the object against
  the message in `CallMirrorHandlers` compares a value with itself and can never report a change.
  The snapshot has to be taken in the first pass, before the write.

## 4. What implementing it needs

1. The watcher list and `FUN_004d5150`'s walk, or a deliberate divergence from it.
2. The registration sites -- who registers a watcher on the health field, and what their callback
   does. Not yet recovered; that is the next thing to find.
3. The old-value snapshot, per the note above.

A shortcut exists and should be taken knowingly if at all: hard-code "health block changed ->
signal `UNIT_HEALTH`" without the registry. That reaches the right behaviour for the handful of
fields the player and target frames need (health, maxhealth, power, maxpower, level, faction) and
diverges from the reference in mechanism rather than in effect. It would have to be recorded as
`diverged`, with the registry named as the end state, or it will read later as a finished port.

## 5. What frozen signals today

Implemented 2026-09-19 as a deliberate divergence (`overrides.json` 004d5550). The first pass
records the blocks whose value actually changed; the second pass turns those into events.

| field | event | token |
|---|---|---|
| `CGUnitData::health` | `UNIT_HEALTH` | player, target |
| `CGUnitData::maxHealth` | `UNIT_MAXHEALTH` | player, target |
| `CGUnitData::power[0..6]` | `UNIT_MANA` | player, target |
| `CGUnitData::maxPower[0..6]` | `UNIT_MAXMANA` | player, target |
| `CGUnitData::level` | `UNIT_LEVEL` | player, target |
| `CGUnitData::factionTemplate` | `UNIT_FACTION` | player, target |
| `CGUnitData::pad1` -- the packed byte field | `UNIT_DISPLAYPOWER` | player, target |
| `CGUnitData::displayID`, `nativeDisplayID`, `mountDisplayID` (3 blocks) | `UNIT_MODEL_CHANGED` | player, target |
| `CGUnitData::target` (2 blocks) | `UNIT_TARGET`, and `PLAYER_TARGET_CHANGED` for the player | player, target |
| `CGPlayerData::xp`, `nextLevelXP` | `PLAYER_XP_UPDATE` | player only |
| `CGPlayerData::coinage` | `PLAYER_MONEY` | player only |

Not signalled, and each for a stated reason:

* `UNIT_AURA` -- auras do not live in the descriptors. `CGUnitData::auraState` is a bitfield of
  *categories* for spell requirements, not the aura list, which arrives in its own packet and is
  already parsed into the aura cache. Wiring `auraState` to `UNIT_AURA` would fire on the wrong
  thing at the wrong times.
* Every other field -- there is no watcher registry, so each one is a hand-written case. The six
  plus three above are the ones the player and target frames read on the first frame in the world.

Still narrow in the same two ways: no registry, and only two tokens.
