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

## 3. What implementing it needs

1. `FUN_004d5550` decompiled -- it is the dispatcher, and will name the handler table.
2. The table itself: descriptor block index to handler. The reference registers these per object
   type, which is what `s_objMirrorBlocks` and `IncTypeID` in frozen's `Mirror.cpp` already shadow.
3. The old value. `FillInPartialObjectData` has an unused `if (!forFullUpdate)` branch before the
   write, which is where the reference captures the previous value so a handler can compare; a
   handler that fires on every write rather than on every *change* would signal far too often.

Suggested order: dispatcher first with a handful of unit fields wired by hand (health, maxhealth,
power, maxpower, level, faction), since those alone bring the player and target frames to life, and
they are the fields whose absence is visible on the first frame in the world.
