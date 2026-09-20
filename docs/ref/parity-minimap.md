# Minimap parity: 3.3.5a reference vs frozen

Scope: the minimap frame and everything it draws (MinimapFrame.cpp region, ~0x57b-0x582).
Program: `RunicWorldGame.exe` (Win 3.3.5a 12340). Raw decompiles captured for this doc live in the
session scratchpad; the addresses below were each decompiled and read, and inferences are marked
*(uncertain)*.

**Status: not started.** `src/ui/game/CGMinimapFrame.cpp` is 75 lines and holds the script
metatable, the object type and the zoom accessors. Nothing draws. The player-arrow question raised
in 2a is answered in 2a-i: it is a texture, and the model attributes in Minimap.xml are inert. The Lua surface is ahead of the
renderer: 13 of the 15 bindings are implemented and tagged, and the two that are not
(`PingLocation`, `GetPingPosition`) are blocked on this, not on themselves.

---

## 1. What exists in frozen today

| piece | state |
|---|---|
| `CGMinimapFrame` script metatable, object type, method table | done, 13/15 bindings tagged |
| zoom: `GetZoomLevels` / `GetZoom` / `SetZoom` | done and tagged (0057bf50 / 0057bf90 / 0057bfd0) |
| `SetPlayerTexture` | done and tagged (0057e100) |
| separate indoor and outdoor zoom levels, 6 of them, clamped unsigned to 5 | done |
| the dirty bit and terrain refill `SetZoom` is supposed to raise | **absent** — recorded as `diverged` in overrides.json |
| any drawing at all | **absent** |
| `PingLocation` / `GetPingPosition` | stubs, blocked on the above |

---

## 2. How the reference does it

The cluster is five functions plus the tracking helpers:

```
0057bea0   construct / load: resolve the player arrow texture
0057c6a0   per-frame: fetch the active player and read its facing (vtable +0x34)
0057c720   the main update, 523 lines            <-- the bulk of the work
0057f7f0   per-object blip decision, 152 lines
00581e80   the largest, 742 lines                <-- blip and tracking update
0057f1b0   tracking icons: "Interface\Minimap\Tracking"
0057f4f0   tracking icons, second entry point
0051d9b0   CVars: portal traversal limit, and the saved tracking flags
0057bd90   texture teardown: releases every minimap texture global and nulls it
0057bd10   blip insertion: orders 256 blips into a list by a float key at +0x8c
0057dca0   the constructor, 802 bytes
```

### 2e. The constructor, and why the arrow region starts null

`0057dca0` is `CGMinimapFrame::CGMinimapFrame(parent)`. It chains to the base frame constructor,
installs both vtables, and then does three things worth knowing:

- **It sets `+0x2a0` to zero.** The player arrow region starts null in the reference too, exactly as
  frozen's `m_playerTexture` does. So nothing in the constructor creates it, and `LoadXML` as
  decompiled does not assign it either -- the assignment is either inside the base
  `CSimpleFrame::LoadXML`, through a named-region lookup like the compass, or folded away by the
  decompiler. That is where to look next; it is not in the constructor.
- **It records itself as a singleton** in `DAT_00beba88`, but only if that is still null, so the
  first minimap created wins. This is consistent with the texture set living at module scope rather
  than per frame.
- **It precomputes the zoom radius table** into `DAT_00beba44` through `DAT_00beba5c`. The base is
  `NDCToDDCHeight(1.0f) * 1.6666666` (5/3, at `00a0b634`), scaled by four constants read out of the
  image at `00a11f88` and the three words after it: **0.055, 0.045, 0.0375 and 0.0125**. Each radius
  is stored beside its own half, the halving constant being the same 0.5 at `009e2ec4` that
  `SetTextInsets` uses. Reading the four words on either side confirms the run is exactly four long:
  `00a11f84` holds -0.8 and `00a11f98` onward is unrelated data.

  This is the table frozen's header means by "which zoom radius table" the interior flag picks: one
  table of pairs, full outdoors and half indoors, rather than two tables.

  **Open question.** Four radii against six zoom levels does not divide, and `GetZoomLevels` really
  does return a literal 6. So a zoom level does not index this table directly and something between
  them is still unidentified. Do not wire `SetZoom` to these values on the strength of the
  arithmetic alone.

- **It initialises `+0x2a4` to -10000**, a sentinel rather than a coordinate *(uncertain: the field
  is not otherwise identified)*.

### 2d. The texture globals are wider than frozen's header records

`0057bd90` releases the whole set and is the cleanest inventory of it. Frozen's `CGMinimapFrame`
header lists seven statics, `DAT_00beba24` through `DAT_00beba3c`. The teardown also releases
**`DAT_00beba20`**, which the header does not mention, and an **array of seven more at
`DAT_00beba04`** (it loops a 0x1c byte span in 4-byte steps). So the real set is the seven named
ones, plus one unnamed neighbour below them, plus a seven-entry array below that. Whoever ports the
renderer should widen the header to match before wiring anything up.

`0057bd10` walks 256 entries of a 164-byte (0x29 dword) structure and links each into a list
ordered by a float at `+0x8c`, which is the blip draw order *(uncertain: the key is read but its
meaning -- distance, or screen depth -- is not established)*.

### 2a. The player arrow comes from an XML attribute, with a hardcoded fallback

`0057bea0` is `CGMinimapFrame::LoadXML`. It chains to the base `CSimpleFrame::LoadXML` first, then
reads the **XML attribute** `minimapPlayerTexture`, not a CVar despite reading like one, falling
back to `Interface\Minimap\MinimapArrow.tga` when the attribute is absent and handing it to
`CSimpleTexture::SetTexture`. The error it raises names the file: `Invalid minimapPlayerTexture in
Minimap.xml`. It then resolves a child region named `MinimapCompassTexture` by name and stores it
at `+0x2a8`, or null when there is none.

Note for whoever ports it: the decompilation shows **no assignment to `+0x2a0`**, the player arrow
region itself. Only the compass at `+0x2a8` is assigned here. So the arrow region is created
somewhere else, in the constructor or by the base LoadXML from a `<Texture>` child in Minimap.xml,
and this function only points it at a file. That makes it less self-contained than it first looks:
porting it without finding what creates `m_playerTexture` would call `SetTexture` on the null that
`CGMinimapFrame::m_playerTexture` already documents.

The constructor has since been found (2e) and it settles half the question: it sets `+0x2a0` to
zero, so the region genuinely starts null in the reference as well. Two neighbours were ruled out
on the way -- `0057bd90` and `0057bd10`, immediately before `LoadXML`, are the texture teardown and
the blip insertion (2d).

### 2a-i. The shipped Minimap.xml sets a model, and the client ignores it

Resolved 2026-09-19 from the data rather than the binary. `build/framexml/Minimap.xml` line 90:

```xml
<Minimap name="Minimap" enableMouse="true"
         minimapPlayerModel="Interface\Minimap\MinimapArrow.mdx"
         minimapArrowModel="Interface\Minimap\Rotating-MinimapArrow.mdl">
```

It does **not** set `minimapPlayerTexture`. It sets two *model* attributes instead, and reading
only the XML one would conclude the player arrow is an `.mdx` model.

It is not, in this build. Checking which of these names exist as strings in `WoW.exe`:

| name | in WoW.exe |
|---|---|
| `minimapPlayerTexture` | **yes** -- the attribute `LoadXML` reads |
| `minimapPlayerModel` | **no** |
| `minimapArrowModel` | **no** |
| `MinimapArrow.tga` | **yes** -- the hardcoded fallback |
| `MinimapArrow.mdx` | yes, but reached from somewhere other than these attributes |

An attribute the parser never names cannot be read, so **both model attributes in the shipped XML
are inert** in 3.3.5a -- leftovers the client of this build does not parse. Since the XML never
sets `minimapPlayerTexture` either, `LoadXML` always takes its fallback, and the player arrow in
this build is the texture `Interface\Minimap\MinimapArrow.tga`.

Two consequences for the port: wiring the arrow to a model because Minimap.xml names one would be
wrong, and the attribute path in `LoadXML`, while worth porting for fidelity, can never fire with
the shipped interface -- so a port that only ever produces the fallback is behaving correctly.

### 2a-ii. LoadXML uses the arrow region, it does not create it

Re-read `0057bea0` on 2026-09-19 with the map able to name its callees, which changes the reading.
The whole function is:

1. chain to the base `CSimpleFrame::LoadXML` (`FUN_00490410`, still unmapped in frozen);
2. `XMLNode::GetAttributeByName("minimapPlayerTexture")`, falling back to the literal
   `Interface\Minimap\MinimapArrow.tga`;
3. **`CSimpleTexture::SetTexture`** (`004859e0`) with that path -- a member call whose `this`
   Ghidra dropped, and the only candidate for it is the arrow region at `+0x2a0`;
4. on failure, the status error `Invalid minimapPlayerTexture in Minimap.xml`;
5. resolve the child region named `MinimapCompassTexture` and store it at `+0x2a8`, **less 0x20** --
   a base-subobject adjustment, so the stored pointer is not the region pointer; a port that stores
   the region directly will be off by that much.

So the earlier note stands that there is no assignment to `+0x2a0` here, but the conclusion drawn
from it was too weak. LoadXML *dereferences* the arrow region. For the reference not to fault, the
region already exists by the time the override runs -- so it is created by the base `LoadXML` at
step 1, which is therefore the thing to decompile next, not this function.

That also corrects a comment in frozen's `CGMinimapFrame_SetPlayerTexture`, which said the
`minimapPlayerTexture` attribute is what creates the region. It is not; it only names the file, and
in the shipped interface it is never even set (2a-i).

### 2a-iii. It is PostLoadXML, not LoadXML

Corrected 2026-09-19, and it invalidates the conclusion 2a-ii reached about what to decompile next.

`0057bea0` chains to `FUN_00490410`, which 2a-ii read as the base `LoadXML`. It is not.
`CSimpleFrame::LoadXML` is `004932c0` and has been mapped all along at 0.942 fidelity. `00490410`
calls `CSimpleFrame::PostLoadXML_Frames` (`0048f400`, already mapped) and then propagates a
visibility bit and a parent-multiplied alpha down the child regions -- that is
**`CSimpleFrame::PostLoadXML`**, now tagged in frozen.

So `0057bea0` is `CGMinimapFrame::PostLoadXML`, and everything in it reads correctly in that light:
pointing an existing region at a file and resolving a sibling region *by name* are both things that
can only be done after the XML pass has created them.

Which leaves the original question open again, and narrower. The `<Minimap>` element in
`build/framexml/Minimap.xml` has no `<Texture>` child at all -- only `<Size>`, `<Anchors>` and
`<Frames>` -- so the arrow region is not created from the shipped XML either. It is not the
constructor (2e), not `PostLoadXML`, and not the XML. `CGMinimapFrame::LoadXML` proper has not been
found yet; that is the remaining candidate, and it is a different address from `0057bea0`.

**Rename throughout when porting:** every reference to `0057bea0` as "LoadXML" in the sections
above should be read as `PostLoadXML`.

### 2b. Orientation comes from the player object, not the camera

`0057c6a0` resolves the active player through the object manager and calls a virtual at vtable
`+0x34` to get its facing. The minimap rotates with the player's facing rather than the camera's
*(uncertain: the virtual is not yet identified, but the surrounding use is a single float angle)*.

### 2c. Tracking state is persisted

`0051d9b0` registers two CVars: a limit named *Max Number of Portals to traverse for minimap*, and
one described as *Stores the minimap tracking that was active last session*. So tracking survives a
restart and the minimap has a portal-traversal budget, which implies the terrain it draws is
gathered by walking the world like the main scene rather than blitted from a prebaked tile.

### 2f. The tracking list has two halves, and only one of them needs the minimap

Recovered 2026-09-19 and ported the same day. A tracking id is 1-based and indexes two lists laid
end to end:

**The spell half** comes first: the tracking spells the player knows, kept in `DAT_00be8dec` with
its count in `DAT_00be8de8`. `SetTracking` on one of these just *casts the spell* (`FUN_0080da40`);
the active one is remembered in `DAT_00beba68` and its icon comes from `SpellIcon.dbc` (the active
icon if it is the one being tracked, the normal icon otherwise).

Ported 2026-09-19. What makes a spell a tracking spell is `FUN_007fdf60`: **any of its three
`EffectApplyAuraName` values is 44, 45 or 151** -- TRACK_CREATURES, TRACK_RESOURCES,
TRACK_STEALTHED. Nothing else about the spell is consulted.

Finding that took a chain worth recording, because none of it needed a name:

| step | how |
|---|---|
| the list globals | `DAT_00be8de8`/`dec` from `GetNumTrackingTypes` |
| who touches them | scan the image for those four bytes -- 21 sites, all consumers, the sort, and the teardown |
| the real writer | the array is a `{capacity, count, data}` triple, so scan for the **base** `00be8de4` instead: 4 sites, one of them new |
| the predicate | that site is the learn-spell path (`FUN_00542030`), which appends when `FUN_007fdf60` is true |

The reference maintains the list incrementally (append on learn, remove on unlearn). Frozen derives
it from the spellbook instead, which is the same answer without a second structure to keep in step.

`SpellRec` gained `m_activeIconID` (column 134) and `m_effectAura[3]` (columns 95-97) for this. The
95 is corroborated: the reference reads the record at `+0x17c`, and `0x17c / 4` is 95.

**Still missing:** nothing sets `s_trackingSpell`, so a cast tracking spell never reports back as
active. That needs the aura side -- `DAT_00beba68` is written when the tracking aura applies.

**The table half** is a static 15-row table at `DAT_00a11c50`, 0x14 bytes per row, read out of
.rdata rather than transcribed from FrameXML. Each row is `{kind, value, globalString, texture,
classMask}`:

| kind | meaning | value |
|---|---|---|
| 1 | match an NPC flag | the `UNIT_NPC_FLAGS` bit |
| 2 | match a game object type | `0x13`, the mailbox |
| 3 | trivial quests | unused -- no flag behind it |

`classMask` is 0 for everyone, else a mask of `1 << classId` **with no -1**: Poisons is `0x10`
(rogue, class 4), Ammunition `0x1a` (warrior, hunter, rogue) and StableMaster `0x08` (hunter). With
no active player the reference uses a mask of 0, which leaves only the unrestricted rows -- that is
behaviour, not a defensive guard. `GetNumTrackingTypes` and `GetTrackingInfo` index the *filtered*
list, so ids shift with the player's class.

`FUN_0057e070` sets the selection: it writes the row's **global string name** (not an index, which
would move) into the `minimapTrackedInfo` CVar from 2c, and signals `MINIMAP_UPDATE_TRACKING` (170).
When the trivial-quest row is switched on or off it also walks every object (`FUN_004d4b30` over
`FUN_0057e020`) to add or drop that marker -- **not ported**, there is no POI renderer to refresh.

`GetTrackingTexture` always returns a path, falling back to `Interface\Minimap\Tracking\None`
when nothing is tracked, so the minimap button never shows an empty square.

All four bindings register **globally**, not as frame methods, even though their function pointers
sit next to `CGMinimapFrameMethods` in .rdata: FrameXML calls each one bare (`Minimap.lua` 409, 421,
427, 430) and never as `Minimap:GetTrackingInfo()`.

---

### 2g. The zoom radius, and why four never divided into six

Resolved 2026-09-19, replacing a wrong reconstruction. `CGMinimapFrame` used to carry
`s_zoomRadius[4][2]` with a note that four radii against six zoom levels did not divide and that
`SetZoom` should not be wired to it on the strength of the arithmetic. The shape was the problem.

`FUN_007f3b90` returns the current view radius in yards, and there are **two tables of six** -- one
per zoom level -- with the interior flag (`DAT_00d39434`, frozen's `s_indoors`) choosing between
them, not between halves of a single entry:

| | table | values |
|---|---|---|
| outdoors | `UNK_00a41e04`, int32 | 14, 12, 10, 8, 6, 4 -- in **chunks** |
| indoors | `UNK_00a41e1c`, float | 150, 120, 90, 60, 40, 25 -- already **yards** |

The outdoor row is scaled by `0.5 * 33.33333` (`009e2ec4` and `00a3e554`), the second being yards
per ADT chunk. The halving is not a fudge factor: the stored number is a diameter in chunks. So
outdoors runs 233.3 yards at level 0 down to 66.7 at level 5.

The zoom level itself comes from `s_zoom[0]` outdoors and `s_zoom[1]` indoors, which frozen already
had right.

This unblocks `PingLocation` (`0057ed70`), which needs the radius to turn a click offset inside the
minimap frame into a world position: it converts the offset out of DDC, divides by the frame's own
width and height, and multiplies by twice this radius.

---

## 3. What porting it needs that frozen does not have

Ordered by how much is missing, not by draw order:

1. ~~**Minimap terrain imagery.**~~ **The index reader is DONE.** `src/world/Minimap.cpp` reads
   `md5translate.trs` (`MinimapLoadTranslate`, ref `FUN_007f6540`) and builds both key shapes:
   `MinimapWorldTile` for `<map>\map<x>_<y>.blp` and `MinimapWmoTexture` for the interior pieces.
   Measured while porting it: only 7534 of the 18644 entries are world tiles -- the other 11110 are
   buildings and dungeons carrying their own minimap imagery. What is still missing is the DRAW,
   and what that needs is section 5 below.
2. **The circular mask.** The frame is square and the art is round; the reference masks it.
3. **Blips.** `00581e80` and `0057f7f0` decide, per object, whether it appears and with which icon,
   which needs the object manager walk plus the tracking flags from 2c.
4. **The ping.** Only after the above, since both ping bindings are defined in minimap-local
   coordinates.

---

## 4. Suggested order

Step 1 carries a prerequisite of its own (see 2a). Even so, steps 1 and 2 are the cheapest in the
cluster and neither depends on the terrain imagery:

1. Find what creates `m_playerTexture` (see 2a), then port `0057bea0`: `LoadXML`, the attribute
   and its fallback, and the compass lookup. The first half is the prerequisite for the second.
   `SetPlayerTexture` is already ported and already guards against the null this would leave.
2. Port `0057c6a0` — player facing. Two calls, and it makes the arrow point somewhere.
3. ~~Build the `md5translate.trs` reader~~ (done), then the tile draw. This is the large one, and
   section 5 is what it actually costs.
4. Blips and tracking (`00581e80`, `0057f7f0`, `0057f1b0`), which unblock the tracking bindings.
5. Ping, which unblocks `PingLocation` and `GetPingPosition`.

Do **not** port `SetZoom`'s missing side effects until step 3 exists: the dirty bit and terrain
refill have nothing to invalidate while no tiles are drawn, which is why that divergence is recorded
rather than treated as a defect.

---

## 5. The tile updater, and why it is not a single cycle

`FUN_00581e80` is the minimap's render (4905 bytes). The part that decides WHICH tiles are on
screen and keeps their textures resident is separate: `FUN_007f5ba0`, in the same translation unit
as the `.trs` reader already ported.

What it does, read rather than guessed:

- Keeps a **256-entry tile table**, stride `0x29` dwords (`0xA4` bytes). Each slot holds a texture
  handle just below its flags word -- the flag word is at `+0x94` of the slot and the handle at
  `+0x90` -- and the update walks all 256 with that stride in four separate loops (invalidate,
  mark, resolve, release).
- Recomputes a visible block only when the camera leaves it. The block bounds live in six globals
  (`00d39460`..`00d39474`); the camera position that last defined them is `00d39454`..`0039445c`.
  The test at the end of the "did we move" branch is literally whether the camera is still inside
  those bounds.
- Snaps the block to the zoom radius: `radius = UNK_00a41e1c[zoom]`, tile index = `floor(pos /
  radius)`, bounds = `index * radius` to `+ radius`. **That confirms the zoom table already
  recorded in section 2g from the other side** -- it is the same array, indexed by the same
  `00af4e50`.
- Asks the map for the chunks inside the block: `FUN_0077f130(map, &bounds, &outCount, 0x100, ...)`,
  capped at the same 256. Then `FUN_007f3ce0` per chunk to test one, and `FUN_007f5070` to load it.
- Signals a script event (`FUN_0081b530`) when the "is there terrain here at all" answer flips,
  which is what drives the minimap going blank indoors.

**The blocker is the map query, not the minimap.** `FUN_0077f090`, `FUN_0077f130`, `FUN_0077f160`
and `FUN_0077f1b0` are the map-side calls this leans on, and none is ported -- frozen has other
functions from that module (`FUN_0077f490`/`4a0`/`4b0` are in `CWorld.cpp`) but not these. So the
tile draw is not one cycle: it is the map's chunk-bounds query first, then the 256-slot residency
table, then the draw.

Do not start it from the render end. `FUN_00581e80` has around 47 unlinked callees and mixes the
mask, the rotation, the tiles, the blips and the POI arrows in one body; porting it before the
query exists would be guesswork with nothing to check it against.

### 5a. Where the query actually lives (decompiled 2026-09-20)

All four "blocker" functions above turn out to be three-line forwarders. Each tests its `this` for
null and hands straight off to a different object:

| map-side | forwards to | shape |
|---|---|---|
| `FUN_0077f090` | `FUN_007a1480` | `bool HasTerrain()` -- no arguments |
| `FUN_0077f130` | `FUN_007a17e0` | the chunk-bounds query, 5 arguments forwarded |
| `FUN_0077f160` | `FUN_007a18d0` | fills three out-pointers |
| `FUN_0077f1b0` | `FUN_007a1640` | fills three out-pointers, returns 0 unless both lookups hit |

So **the four addresses to port are `007a1480`, `007a17e0`, `007a18d0` and `007a1640`**, not the
`0077f0xx` ones, which are worth a tag each and nothing more.

The four share one preamble, which is the thing to understand first: they walk an intrusive Storm
list rooted at `this + 0x20`, advanced by `FUN_004b6670`, with the usual sentinel test (`link & 1`
or `link == 0` means the end). Each node's object is at `+8`, is skipped when `*(obj + 8) & 4` is
set, and carries its own list at `+0x20` whose first object supplies a flags word at `+0xc` tested
against `0x400`. The loop is looking for the first entry whose `0x400` flag is set AND whose
`FUN_007ae7b0(obj + 0x50)` result has bit 3 set -- in other words the active, loaded one.

### 5b. The five callees, read (2026-09-20)

All five are short. What they are, not yet what their classes are called:

- `FUN_007ae7b0(owner, index)` -- `if (!owner[0x1e0]) return 0; return *(u32*)(owner[0x130] +
  index * 0x20);`. So `+0x130` is an array of **0x20-byte records** whose first dword is a flags
  word, `+0x1e0` is a "there is data at all" guard, and the caller's `+0x50` member is the index
  into it. The `& 8` the callers test, and the `& 0x40` in the query below, live in that word.
- `FUN_007aea80(owner, index, allowUnloaded)` -- same guard, then `obj = owner[0x1f8 + index*4]`,
  returning null unless `obj[0x198] & 1` (loaded) or the caller passed `allowUnloaded`. So `+0x1f8`
  is a **parallel array of pointers** on the same index, and `+0x198 & 1` is the residency bit.
- `FUN_007b00a0(owner, index, box, out, outCount, wantFlags, pointQuery)` -- the query proper.
  Bumps a global counter (`DAT_00d1c418`), copies the caller's **five** dwords of box into a local
  and fills the sixth from `obj[0x48]`, then splits: `pointQuery` goes to `FUN_007afe70(&box, out,
  outCount)`, otherwise `FUN_007afc70(index, index, &box, out, outCount, wantFlags, mask, 0)` where
  `mask` is 8 when `wantFlags` is zero and `record.flags & 0x40` otherwise. Returns 0 when the
  entry is not resident, which is what makes the minimap go blank indoors.
- `FUN_007f9430(transform, box, out)` -- writes `out[0..2]` and `out[3..5]` both from
  `transform[0x30..0x38]`, then calls `FUN_007f9320(transform + 0x20)`. That is an **AABB
  transform**: seed min and max at the translation, then expand by the rotated extents, with
  `+0x20` the rotation and `+0x30` the translation. Ghidra drops the box argument and the
  expansion, so do not port this one from the listing above -- decompile `FUN_007f9320` first.
- `FUN_00990560(a, b, c)` -- a `bsearch` over a global table (`_DAT_00ad4e48`, `DAT_00ad4e34`
  entries of 0x30 bytes) with comparator `FUN_00990530`, keyed on the triple, bracketed by what
  look like a critical section enter and leave. A cache lookup, not a computation.

### 5c. What is still missing

The shapes above are clear; the **identities** are not. Two objects appear throughout and neither
is named yet:

- the list node's object -- flags at `+0x8` (bit 2 = skip), a child list at `+0x20`, and the index
  at `+0x50` that feeds all of `007ae7b0`/`007aea80`/`007b00a0`
- its child's object -- flags at `+0xc` (bit 10 = active), and `+0xb0`, `+0xf4`, `+0x104`, `+0x120`

Worse, the `owner` that `007ae7b0` and friends are called *on* does not appear in the decompiled
callers at all: Ghidra shows those calls with one argument because `this` arrives in ECX and was
lost.

`callers.sh` narrows it (run 2026-09-20). `FUN_007ae7b0` has 13 call sites and `FUN_007aea80` has
about 24, and **every one lies between `0077f`.. and `007c2`..** -- one module, the map. Two of
them are worth opening first because they call straight through with no preamble at all, so the
`this` they pass is whatever their own caller handed them and the wrapper will name the type:

```
FUN_007a6b60 -> FUN_007aea80 at +0x0a
FUN_007a6d70 -> FUN_007aea80 at +0x0c
```

The shape the arrays imply is a map with a per-tile grid: `+0x1f8` an array of pointers indexed by
tile, `+0x130` a parallel array of 0x20-byte records on the same index, `+0x1e0` a "has data at
all" guard. That is consistent with `CMap`, but it is a reading of the arrays and not yet a fact.

### 5d. The owner, settled (2026-09-20)

The decompiler cannot show it, so read the instruction bytes instead. At the call site inside
`FUN_007a1480` (0x007a14c5):

```
8b 71 08                mov esi, [ecx+8]           ; the child object
8b 40 50                mov eax, [eax+0x50]        ; the index
8b 8e f4 00 00 00       mov ecx, [esi+0xf4]        ; <-- this, for FUN_007aea80
6a 00                   push 0
50                      push eax
e8 b6 d5 00 00          call FUN_007aea80
```

**The owner is `child + 0xf4`.** That is the same `+0xf4` that `FUN_007a18d0` and `FUN_007a1640`
dereference and then read `+0x120` and `+0x158` out of, and the same one `FUN_007a1150` fetches as
`*(DAT_00cd87a4 + 0xf4)` before its first query. So the chain is:

```
DAT_00cd87a4        the active map/area object (has a transform at +0xb0)
  +0xf4             the tile manager -- owns +0x130, +0x1f8, +0x1e0, +0x158, +0x160, +0x194
  node obj +0x50    the index into the manager's two parallel arrays
```

More of the manager, from the same pass:
- `+0x194 == 1` means "no data"; `FUN_007a1150` gives up on it before doing anything else
- `+0x158` is an array of 0x30-byte records, indexed by a byte out of the tile object's `+0x58`
  (four of them) -- the light/fog bands, with a position at +4..+0xc and a radius at +0x14
- `+0x160` is the base of a 0x40-stride array, indexed by a `u16` at the tile object's `+0x130`
  (`FUN_007a6d70` is the whole accessor: `tile->[0x130] * 0x40 + manager->[0x160]`)

**This is as far as reading gets it.** The remaining obstacle is not knowledge, it is structure:
frozen's `CMap`/`CWorld` are not laid out like this, so there is no `child + 0xf4` to port onto.
The query cannot be written faithfully until those classes exist in this shape, and inventing a
frozen-shaped equivalent would be the silent divergence the 1.0.0 rule exists to prevent.

Until those are named this cannot be written honestly -- a port built on guessed offsets would be
exactly the kind of "looks right" code the recomp cycle exists to stop.
