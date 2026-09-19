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

```
+0x04, +0x08, +0x0c    three dwords          class, subclass, sound override subclass
+0x1f4 .. +0x200       four strings          the four name fields, SStrDup'd, null when empty
+0x10, +0x14           two dwords            display info id, quality
+0x18, +0x1c           two dwords (loop x2)  flags, flags2
+0x20 .. +0x48         eleven dwords         buy/sell price, inventory type, allowable class and
                                             race, item level, required level, skill, skill rank,
                                             required spell
+0x4c .. +0xf0         a counted loop        the stat block: each entry a float and a dword 8 apart
+0xf4, +0xf8           two dwords
+0xfc                  one float
+0x100 .. +0x174       a loop of six reads   stride 0x14, six parallel arrays -- the damage and
                                             resistance block
+0x178                 one dword
+0x17c                 one string            the description, SStrDup'd
+0x180 .. +0x1bc       sixteen dwords        spell triggers and their charges, cooldowns, categories
+0x1c0 .. +0x1d4       a loop of two reads   stride 0xc
+0x1d8, +0x1dc, +0x1e0 three dwords
+0x1e4                 one float
+0x1e8, +0x1ec, +0x1f0 three dwords
```

---

## 4. What frozen would have to build

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
