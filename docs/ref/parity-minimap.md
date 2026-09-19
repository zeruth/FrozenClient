# Minimap parity: 3.3.5a reference vs frozen

Scope: the minimap frame and everything it draws (MinimapFrame.cpp region, ~0x57b-0x582).
Program: `RunicWorldGame.exe` (Win 3.3.5a 12340). Raw decompiles captured for this doc live in the
session scratchpad; the addresses below were each decompiled and read, and inferences are marked
*(uncertain)*.

**Status: not started.** `src/ui/game/CGMinimapFrame.cpp` is 75 lines and holds the script
metatable, the object type and the zoom accessors. Nothing draws. The Lua surface is ahead of the
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
- **It precomputes the zoom radius table** into `DAT_00beba44` through `DAT_00beba5c`: four radii,
  each also halved into the slot below it, scaled from a base and three constants at `00a11f88`,
  `00a11f90` and `00a11f94`. This is the table frozen's header refers to when it says the interior
  flag selects "which zoom radius table" -- it is one table of pairs, not two tables.

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

### 2b. Orientation comes from the player object, not the camera

`0057c6a0` resolves the active player through the object manager and calls a virtual at vtable
`+0x34` to get its facing. The minimap rotates with the player's facing rather than the camera's
*(uncertain: the virtual is not yet identified, but the surrounding use is a single float angle)*.

### 2c. Tracking state is persisted

`0051d9b0` registers two CVars: a limit named *Max Number of Portals to traverse for minimap*, and
one described as *Stores the minimap tracking that was active last session*. So tracking survives a
restart and the minimap has a portal-traversal budget, which implies the terrain it draws is
gathered by walking the world like the main scene rather than blitted from a prebaked tile.

---

## 3. What porting it needs that frozen does not have

Ordered by how much is missing, not by draw order:

1. **Minimap terrain imagery.** The 3.3.5a client keeps per-tile minimap BLPs under `textures\Minimap`
   indexed by `md5translate.trs`, which maps `<map>\map<x>_<y>.blp` to a hashed filename. Frozen has
   no reader for that index. Until it does there is nothing to draw underneath the blips.
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
3. Build the `md5translate.trs` reader, then the tile draw. This is the large one.
4. Blips and tracking (`00581e80`, `0057f7f0`, `0057f1b0`), which unblock the tracking bindings.
5. Ping, which unblocks `PingLocation` and `GetPingPosition`.

Do **not** port `SetZoom`'s missing side effects until step 3 exists: the dirty bit and terrain
refill have nothing to invalidate while no tiles are drawn, which is why that divergence is recorded
rather than treated as a defect.
