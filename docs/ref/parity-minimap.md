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
```

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
