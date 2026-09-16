# scene-compare

Pixel comparison of the reference 3.3.5a client and Whoa at fixed viewpoints. It turns "the sky
looks funky" into a per-viewpoint match percentage and a side-by-side image, and catches
regressions when a later change breaks something that already matched.

## Run it yourself, when the desktop is free

The harness puts two game windows up in turn and the reference client can take the whole
display. It is a tool you run by hand between gaming sessions, not something to leave running.
Guards built in:

- It refuses to start while `RunicWorldGame.exe` or `RunicWorldLauncher.exe` is running
  (`--force` overrides, and you will lose the screen).
- It never takes focus. Keystrokes are posted to the game window; captures use `PrintWindow`.
- A client that comes up without a window frame (fullscreen exclusive) is killed at once.

## Requirements

- Local AzerothCore authserver + worldserver running (ports 3724 / 8085).
- MySQL 9.6 with the `acore` user. The harness logs in as account `SCENE` / `SCENE`, which owns
  exactly one character (`Scenecmp`, guid 1000) so both clients enter with the same one. The
  character is teleported by updating `acore_characters.characters` after the server has
  finished its logout save (`online = 0`).
- The reference install at `.reference/WOTLK 3.3.5a - Windows/WoW_WOTLK_3.3.5a` with
  `WTF/Config.wtf` pointing at `127.0.0.1`. Both clients run from that directory so they share
  resolution, cvars and game data. For the run the harness sets `accountName`, `gxWindow "1"`
  and `gxMaximize "0"` in that file and restores the original bytes afterwards.
- Python 3 with Pillow. Nothing else; Win32 is driven through ctypes.

## Run

```
python tools/scene-compare/compare.py                    # all viewpoints, both clients
python tools/scene-compare/compare.py --view goldshire   # one viewpoint
python tools/scene-compare/compare.py --only whoa        # re-capture whoa only, diff against the saved ref
```

Output: `build/scene-compare/<viewpoint>/{ref,whoa,diff,side-by-side}.png` and
`build/scene-compare/report.md`.

## Known issue: reference capture

The stock 3.3.5a client has so far come up fullscreen even with `gxWindow "1"` written to
Config.wtf, and it drops that line when it rewrites the file on world entry. A fullscreen
exclusive window cannot be read by `PrintWindow`, so the harness kills it and reports
"came up without a window frame" instead of leaving it on screen. If that happens, the fix is
on the reference side (find the cvar or command line switch that makes 3.3.5a start windowed,
e.g. `-windowed`, and put it in `--ref-exe` / a wrapper), not in the harness.

## Tuning

The waits are fixed and generous (`--wait-login 12 --wait-charselect 10 --wait-world 30`).
If a capture shows the loading screen, raise `--wait-world`. If the password lands in the
account box, the login screen was not ready: raise `--wait-login`.

The camera is the client's default after login (behind the character, default distance and
pitch), so a camera difference between the clients shows up in the diff, which is intended.
Time of day comes from the server, so the two captures of a viewpoint run back to back.

## Adding viewpoints

Append to `viewpoints.json`: `name`, `map`, `x`, `y`, `z`, `o` (facing, radians). Pick spots
that exercise one system each (open terrain, a WMO interior, water, a city) so a regression
points at a subsystem.
