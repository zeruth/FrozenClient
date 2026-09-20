# Item cache parity: 3.3.5a reference vs frozen

Scope: the client-side item record, how it arrives, and what blocks it in frozen.
Program: `RunicWorldGame.exe` (Win 3.3.5a 12340). Every address below was decompiled and read;
inferences are marked *(uncertain)*.

**Why this matters more than any single binding.** `GetItemInfo`, `GetItemIcon`, `GetItemCount`,
`GetItemCooldown`, `GetItemFamily`, `GetItemUniqueness` and most of the 44 remaining stubs in
`CGTooltipScript.cpp` are blocked on this one subsystem, not on themselves. It is the largest
single unlock left in the UI.

---

## 1. The chain, opcode to fields

```
SMSG_ITEM_QUERY_SINGLE_RESPONSE (0x58)
  registered at 00635b40
    -> handler 006354d0        one line: forwards into the generic cache parser
         -> FUN_0067cbd0       reads a packed id, finds or allocates the record
              -> FUN_0098d910  the per-cache record reader -- the item fields live HERE
```

There is no bespoke item parser to copy. The item response is read by the generic cache machinery,
which is why the search for one kept coming back to `FUN_0067ca30` (the cache lookup, 275 callers,
already recorded as absent in overrides.json).

`FUN_0098d910` names its own source file in its allocation calls: `.\ItemStats.cpp`.

---

## 2. The three CDataStore getters it uses

| reference | reads |
|---|---|
| `FUN_0047b3c0(dest)` | one dword |
| `FUN_0047b480(buf, size)` | a string into a caller buffer |
| `FUN_0047b440(dest)` | four bytes, used between dword reads and in the damage loop -- **float** *(uncertain: not decompiled, inferred from position)* |

Strings are copied out with `SStrDupA` when non-empty and stored as null when empty, so an absent
name is a null pointer rather than an empty string.

---

## 3. Field order as read

Offsets are into the record. Names are by behaviour and by what 3.3.5a's response is known to
carry; the **order and widths** below are read directly out of the decompilation, the **names** are
the inference.

**Superseded 2026-09-20.** The table that used to be here was a summary and it was wrong in two
places -- it put the stat block at `+0x4c` when `+0x4c..+0x60` are plain dwords, and it mislabelled
the `+0x100` block as damage and resistances when that is the spell block. The read order below is
transcribed instruction by instruction from the decompilation and then **verified** (section 3a).

```
+0x04  u32     class
+0x08  u32     subclass
+0x0c  u32     soundOverrideSubclass
+0x1f4 .. +0x200   four strings, SStrDup'd into a 400-byte buffer, null when empty
+0x10  u32     displayInfoID
+0x14  u32     quality
+0x18  u32     flags            \  read as a 2-iteration loop
+0x1c  u32     flags2           /
+0x20 .. +0x60  seventeen dwords, in order:
               buyPrice, sellPrice, inventoryType, allowableClass, allowableRace, itemLevel,
               requiredLevel, requiredSkill, requiredSkillRank, requiredSpell, requiredHonorRank,
               requiredCityRank, requiredReputationFaction, requiredReputationRank, maxCount,
               stackable, containerSlots
+0x64  u32     statsCount
               then statsCount pairs: statType[i] at +0x68+i*4, statValue[i] at +0x90+i*4
               afterwards the unused TYPES (only) are filled with -1; the values stay 0
+0xb8  u32     scalingStatDistribution
+0xbc  u32     scalingStatValue
               two damage bands, read interleaved (min, max, school) though stored as three arrays:
               damageMin[i] float at +0xc0+i*4, damageMax[i] float at +0xc8+i*4,
               damageType[i] u32 at +0xd0+i*4
+0xd8 .. +0xf0  seven dwords: armor, then holy, fire, nature, frost, shadow, arcane resistance
+0xf4  u32     delay
+0xf8  u32     ammoType
+0xfc  float   rangedModRange
               five spell slots, read one index across all SIX arrays before advancing:
               spellID +0x100, spellTrigger +0x114, spellCharges +0x128, spellCooldown +0x13c,
               spellCategory +0x150, spellCategoryCooldown +0x164 (each 5 dwords)
+0x178 u32     bonding
+0x17c string  description, into a 1024-byte buffer
+0x180 .. +0x1bc  sixteen dwords, in order:
               pageText, languageID, pageMaterial, startQuest, lockID, material, sheath,
               randomProperty, randomSuffix, block, itemSet, maxDurability, area, map, bagFamily,
               totemCategory
               three socket slots: socketColor[i] at +0x1c0+i*4, socketContent[i] at +0x1cc+i*4
+0x1d8 u32     socketBonus
+0x1dc u32     gemProperties
+0x1e0 u32     requiredDisenchantSkill
+0x1e4 float   armorDamageModifier
+0x1e8 u32     duration
+0x1ec u32     itemLimitCategory
+0x1f0 u32     holidayID        -- the last field read
```

**One reference bug, not reproduced.** The stat loop reads `statsCount` pairs with no bound, into
ten-slot arrays. A server sending eleven walks off the end of the record and into the fields below.
Frozen consumes every pair -- it has to, or the rest of the message desyncs -- but stores only the
first ten. Recorded here because a silent divergence is a bug.

### 3a. How the layout was verified

The reference writes these records back out verbatim, so its own cache file is a fixture. Decoding
`Cache/WDB/enUS/itemcache.wdb` with exactly the order above: **all 31 records consume exactly their
declared byte count**, with no slack and no overrun, and the walk lands on the terminator. A single
wrong width anywhere would have thrown the very first record's length off.

That is a check on the field ORDER and WIDTHS only. It says nothing about the field NAMES, which
remain inference -- though they agree with what 3.3.5a's item query response is known to carry.

---

## 4. What frozen would have to build

**Done, by route 2, as of 2026-09-20.** `src/object/client/ItemCache.cpp` holds the whole record:
it sent the query and read as far as `containerSlots` since 2026-09-19, and now reads every field
in section 3 through to `holidayID`. What remains is not the cache -- it is the consumers, chiefly
the tooltip filler `FUN_006277f0` that eight `CGTooltipScript` setters share.

The original assessment is kept below because the choice it records still stands.

---

Frozen has the opcodes (`CMSG_ITEM_QUERY_SINGLE` 0x56, response 0x58) and the item world objects
(`CGItem`, `CGItem_C`), but no cache of static item data by entry id.

Two routes, and the second is the one this codebase has already taken once:

1. **Port the generic `CDBCache`.** Faithful, and it would serve the name, creature, item and every
   other cache at once. It is a real subsystem: record store, completion flag, pending callback
   list, hashed buckets with a 12-byte stride.
2. **Write a bespoke item cache**, the way `src/object/client/NameCache.cpp` already substitutes for
   the name and creature caches. Its shape is the precedent to copy: a `std::map` keyed by id, a
   `pending` flag per entry, a query sender, a response handler registered at startup. Section 3 is
   then the whole of the parsing work.

Route 2 is smaller and matches what is already here. Route 1 is what a 1:1 recomp eventually wants.
Either way the field order in section 3 is the part that had to be recovered, and it now has been.

---

## 5. What `GetItemInfo` needs beyond the cache

`GetItemInfo` (00516c60) is the binding most of FrameXML reaches for, and the cache alone is not
enough for it. Decompiled and read; the return order is:

```
name          FUN_00706d70(buf, 0x400, id, ...)   a builder, not a field -- it folds in the suffix
link          FUN_0061e290(id, quality, ...)      a builder
quality       record +0x14
item level    record +0x34
required level record +0x38
class name    ItemClass lookup on record +0x04    (bounds 00ad3da4/a0, index 00ad3db4)
subclass name a second lookup of the same shape
stack count   } the rest follow from fields the cache already holds
equip slot    }
texture       }
sell price    }
```

So five of the eleven returns are already in `ItemInfo`. The four that are not:

1. **The name builder.** Not the raw name field: it composes the displayed name, which is why the
   reference calls a 1 KB-buffered builder rather than pushing the string.
2. **The link builder**, which needs the quality for the colour code.
3. **`ItemClass`**, which frozen does not load -- there is no `ItemClassRec` in `src/db/rec/`.
4. **`ItemSubClass`**, same.

A partial port would return nil for the link and both type names, which are exactly the values
FrameXML selects out of it most often, so it is not worth landing until at least the two tables are
loaded. Adding those two DBCs is the smallest step that makes the rest worth writing.

**Also settled while reading this:** the binding returns *no values at all* when the item is not in
the cache, rather than nils. Anything counting returns has to expect zero.
