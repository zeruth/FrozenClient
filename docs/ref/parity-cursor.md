# Parity: the game cursor

What is on the mouse cursor — a spell being dragged to the hotbar, an item being moved between
bags, a stack of money — is one small global state block in the reference, read by a dozen Lua
bindings and written by about twenty more. Frozen has none of it, which is why the whole
`Pickup*` / `Place*` / `Cursor*` family is stubbed.

Recovered 2026-09-19 by decompiling the readers, the setters and the clear path. This is the map
that makes implementing it a porting job rather than a hunt.

**Status.** The state block and all five pure readers in section 3 are landed -- `CursorHasItem`,
`CursorHasSpell`, `CursorHasMacro`, `CursorHasMoney` and `GetCursorMoney` read `CGGameUI`'s cursor
statics and answer truthfully, which today means "the cursor is empty" because nothing sets it.

The next step in the suggested order, `FUN_00520960`, is **blocked twice over** and that was
confirmed rather than assumed:

- Frozen has no game cursor image. The only `SetCursor`-shaped thing in the tree is the edit box's
  caret, so step 5 of the setter shape below has nothing to call.
- `SpellRec` carries `m_ID`, `m_spellIconID`, `m_name`, `m_rank`, `m_spellVisualID` and
  `m_attributes` -- not the spell category, and not the override-icon flags. Step 2 picks the icon
  from exactly those, and it *bails when there is no icon*, so guessing the icon would not be a
  cosmetic shortcut: it decides whether the pickup happens at all.

So `GetCursorInfo` and `ClearCursor` are left stubbed on purpose. Implementing them against an
empty cursor would add a switch whose every arm is unreachable, and would read as working code.

## 1. The state block

All at fixed addresses in the reference's data segment. Frozen would keep these as statics on a
`CGCursor` (name unknown; `GameUI.cpp` and `ActionBarFrame.cpp` both touch it).

| address | meaning |
|---|---|
| `00bd0748` | **kind** — the discriminant, see the table below. 0 = empty cursor |
| `00bd074c` | set to 1 when something is picked up; the "cursor is holding something" flag |
| `00bd0750` | index — merchant slot, guild bank slot |
| `00bd0754` | a reference-counted handle, released on clear (`FUN_004d0090`) |
| `00bd0758` | item entry id |
| `00bd075c`, `00bd0760`, `00bd0764` | item-object state, cleared together with the guid |
| `00bd0768` / `00bd076c` | the item object's guid, as two dwords |
| `00bd079c` | money, in copper. Shared by kind 2 and kind 0xc |
| `00bd07e0` | spell id |
| `00bd07e4` | kind 4 payload (kind not yet identified) |
| `00bd07e8` | macro |
| `00bd07ec` | kind 10 payload (kind not yet identified) |

### Kinds

Taken from the `switch` in `GetCursorInfo` (`FUN_00515200`) and the one in the clear path
(`FUN_00519280`), which agree.

| kind | meaning | payload | GetCursorInfo pushes |
|---|---|---|---|
| 1 | item **object** (has a guid) | `bd0768`/`bd076c` | `"item"`, entry, link — 3 values |
| 2 | money | `bd079c` | `"money"`, amount — 2 values |
| 3 | spell, pet spell or companion | `bd07e0` | `"spell"` or `"companion"`, index+1, book — 3 values |
| 4 | not identified | `bd07e4` | — |
| 5 | merchant item | `bd0750` | `"merchant"`, index+1 — 2 values |
| 6 | not identified (clears like an item) | | — |
| 7, 9, 0xb | item **by entry** (7 and 9 also count as "has item") | `bd0758` | `"item"`, entry, link — 3 values |
| 8 | macro | `bd07e8` | `"macro"`, index+1 — 2 values |
| 10 | not identified | `bd07ec` | — |
| 0xc | guild bank money | `bd079c` | `"guildbankmoney"`, amount — 2 values |
| 0xd | equipment set | — | `"equipmentset"`, name — 2 values |

Note kind 3 covers three different things and `GetCursorInfo` disambiguates at read time: it asks
whether the spell is a companion (`FUN_008009b0`), and if not, whether it resolves in the player's
book (`FUN_0053b4e0(spell, 0)`) or only in the pet's (`…, 1`), pushing `"spell"` or `"pet"`
accordingly. A companion pushes `"CRITTER"` or `"MOUNT"` as its third value.

## 2. The primitives

| reference | what it does |
|---|---|
| `FUN_005136c0` | returns the cursor spell (`bd07e0`) — a one-line getter |
| `FUN_005136d0` | returns the cursor item entry |
| `FUN_00513660(2, file, line)` | returns the cursor item guid |
| `FUN_00520960(spell)` | **set the cursor to a spell** — the model for every setter, see below |
| `FUN_00520a80` | set to a pet spell |
| `FUN_00520be0(macro)` | set to a macro |
| `FUN_00520d30`, `FUN_00520dc0` | set to a companion / an equipment set |
| `FUN_00519280(a, b)` | **clear the cursor**, switching on kind |

### The setter shape (`FUN_00520960`)

Every setter follows this, and it matters that the state write is *last*:

1. Resolve the spell record; bail if there is none.
2. Find an icon for it — a pet-bar icon when the spell's category is 0x4e, an override icon when
   two flag bits are set, otherwise the icon from the spell icon table. **Bail if there is no
   icon string.** A spell with no icon never reaches the cursor at all.
3. Clear whatever was there (`FUN_00519280(1, 1)`).
4. Write `bd07e0 = spell`, `bd0748 = 3`.
5. Play `INTERFACESOUND_CURSORGRABOBJECT`, set the cursor image to the icon (`FUN_006165b0`),
   and set the holding flag `bd074c = 1`.

### The clear path (`FUN_00519280(returnToOrigin, notifyServer)`)

Clears the payload for the current kind, and does more than zero memory:

- kind 1 — with `returnToOrigin`, puts the item back where it came from (`FUN_00513770`), then
  releases the object and zeroes the guid and its three companions.
- kind 2 and 0xc — zero the money, play `LOOTWINDOWCOINSOUND`, and with `notifyServer` send
  opcode **0xa3** (kind 2) or **0x21d** (kind 0xc).
- kinds 5/6/7/9 — release the handle, zero entry/handle/index, play
  `INTERFACESOUND_CURSORDROPOBJECT`.
- kind 0xb — as above, plus with `returnToOrigin` a guild bank return and opcode **0x21b**.
- kinds 3/4/8/10 — zero the one payload word.

## 3. What the bindings need

Pure state reads, implementable the moment the block exists:

| binding | reference | rule |
|---|---|---|
| `CursorHasItem` | `FUN_00515100` | kind == 1 or 9 |
| `CursorHasSpell` | `FUN_00515140` | kind == 3 |
| `CursorHasMacro` | `FUN_00515180` | kind == 8 |
| `CursorHasMoney` | `FUN_005151c0` | kind == 2 |
| `GetCursorMoney` | `FUN_00515a50` | pushes `bd079c` |

Each of the four `CursorHas*` pushes 1 or **nil** — never false — and always returns 1.

`GetCursorInfo` (`FUN_00515200`, 808 bytes) is the big reader and needs the item link builder
(`FUN_0061e360` / `FUN_0061e3a0`) that `docs/ref/parity-itemcache.md` section 5 also wants.

## 4. Why the hotbar is blocked on this

`PickupAction` (`FUN_005ac090`) and `PlaceAction` (`FUN_005ab840`) are thin argument wrappers —
both take a 1-based slot, subtract one, and call a worker. Their argument handling is fully
recovered, including that `PlaceAction` **silently ignores slots 0x78..0x83** (the pet bar range)
rather than erroring.

The workers are not thin. `FUN_005abe70` (pickup) tries the slot as an equipment set, then an
item, then a macro, then a companion, each with its own resolver, and falls through to the place
worker when the cursor is already holding something. `FUN_005ab120` (place) is larger still and
sends the action-bar update opcodes. Porting only the spell branch would give a hotbar that picks
up spells and silently drops everything else, which is worse than one that does nothing.

**Suggested order:** the state block and the five pure readers first (they have no dependencies
beyond the block), then `FUN_00520960` as the first setter, then `PickupAction`'s spell branch
once `CGActionBar` can hand back an action's contents, then `GetCursorInfo`.
