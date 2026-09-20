# Parity: the UI animation system

Frozen has none of it. `CScriptRegion::LoadXML_Animations` is an empty TODO, `CreateAnimationGroup`
and `GetAnimationGroups` are `WHOA_UNIMPLEMENTED`, and `CSimpleAnimGroup` exists only as a forward
declaration in `CScriptRegion.hpp`. That makes it **the largest genuinely missing block in the Lua
API**: 62 of the 101 names the report still counts missing are animation widget methods, and unlike
a stub, a missing name raises "attempt to call a nil value" and aborts the enclosing FrameXML
script rather than quietly doing nothing.

Everything below was read out of `WoW.exe` directly -- the method tables byte by byte, the class
sizes and constructors from `CreateAnimation`'s dispatch -- rather than guessed. Nothing here has
been seen running, because none of it is implemented yet.

## Reference source file

`.\CSimpleAnimScript.cpp`, named by the allocation asserts inside `CreateAnimation`
(`FUN_004a7e00`). The C++ classes are `CSimpleAnim*`, which is what the existing
`CSimpleAnimGroup` forward declaration already assumes.

## Class hierarchy

Sizes and constructors come from `CreateAnimation`'s type dispatch, which allocates by type name
and calls a different constructor per branch. Each is `FUN_0076e540(size, ".\CSimpleAnimScript.cpp",
line, 0)` followed by the constructor.

| Lua type name | size | constructor | alloc line |
|---|---:|---|---:|
| `Animation` (base) | 0x98 | `FUN_00499bb0` | 0x4d3 |
| `Translation` | 0xa0 | `FUN_00499c50` | 0x4d5 |
| `Rotation` | 0xa8 | `FUN_00499c80` | 0x4d7 |
| `Scale` | 0xac | `FUN_00499cc0` | 0x4d9 |
| `Path` (string at `DAT_009ebd00`) | 0xb8 | `FUN_00499d50` | 0x4db |
| `Alpha` | 0x9c | `FUN_00499e80` | 0x4dd |

An unrecognised type name falls through to the base `Animation` branch at line 0x4e2. The type
name is compared with `FUN_0076e780` (SStrCmpI, limit `0x7fffffff`), so it is case-insensitive.

`Path` is the odd one: it owns an ordered list of control points, which is a seventh Lua type
(`ControlPoint`) with its own method table but no `CreateAnimation` branch -- control points are
made by `Path:CreateControlPoint` instead.

`00ac1a48` is ControlPoint rather than Translation, which the two tables' contents settle: it
carries `SetOrder`/`GetOrder`/`SetParent` alongside the offset pair, and an ordered, reparentable
point is a control point. Translation has its own two-entry table at `00ac19dc`.

## Widget type ids

The bindings resolve `this` through `FrameScript_GetObjectThis` (`FUN_004a81b0`) against a lazily
allocated type id, the same pattern every other widget class uses.

| global | class |
|---|---|
| `DAT_00b499dc` | AnimationGroup |
| `DAT_00b499f8` | Animation |
| `DAT_00b4997c` | the shared script-object base (`GetObjectType`, `GetName`, `GetParent`) |

## Method tables

Eight tables, 81 entries, of which the report counts 62 as missing from frozen: the rest are
either names frozen already registers on other widget classes, or the four in the two short tables
the report cannot see (below). Addresses are the reference's Lua thunk, which
is a thin wrapper over the C++ method -- e.g. `AnimationGroup:Play` (`004a6ea0`) resolves `this`
and tail-calls `FUN_0049a8f0`; `Animation:Play` (`004a4f40`) calls `FUN_0049ad80`.

### `00ac18e0` -- Animation (31)

| method | addr | method | addr |
|---|---|---|---|
| Play | 004a4f40 | GetSmoothProgress | 004a53e0 |
| Pause | 004a4f80 | SetSmoothProgress | 004a5430 |
| Stop | 004a4fc0 | GetProgress | 004a54b0 |
| IsDone | 004a5000 | GetProgressWithDelay | 004a5500 |
| IsPlaying | 004a5060 | SetMaxFramerate | 004a5550 |
| IsPaused | 004a50b0 | GetMaxFramerate | 004a55d0 |
| IsStopped | 004a5100 | GetElapsed | 004a5610 |
| IsDelaying | 004a5150 | SetOrder | 004a5660 |
| SetStartDelay | 004a51a0 | GetOrder | 004a56f0 |
| GetStartDelay | 004a5220 | SetSmoothing | 004a5740 |
| SetEndDelay | 004a5260 | GetSmoothing | 004a5810 |
| GetEndDelay | 004a52e0 | SetParent | 004a5880 |
| SetDuration | 004a5320 | GetRegionParent | 004a5a70 |
| GetDuration | 004a53a0 | HasScript | 004a5af0 |
| GetScript | 004a5bb0 | SetScript | 004a5cc0 |
| HookScript | 004a5df0 | | |

### `00ac1ab8` -- AnimationGroup (26)

| method | addr | method | addr |
|---|---|---|---|
| Play | 004a6ea0 | GetMaxOrder | 004a7290 |
| Pause | 004a6ee0 | SetInitialOffset | 004a72f0 |
| Stop | 004a6f20 | GetInitialOffset | 004a73e0 |
| Finish | 004a6f60 | GetAnimations | 004a7b00 |
| GetProgress | 004a6fa0 | CreateAnimation | 004a7e00 |
| IsDone | 004a6ff0 | HasScript | 004a7480 |
| IsPlaying | 004a7aa0 | GetScript | 004a7540 |
| IsPaused | 004a7040 | SetScript | 004a7650 |
| IsPendingFinish | 004a7090 | HookScript | 004a7780 |
| GetDuration | 004a70e0 | GetObjectType | 004a8240 |
| SetLooping | 004a7130 | IsObjectType | 004a8290 |
| GetLooping | 004a71f0 | GetName | 004a8340 |
| GetLoopState | 004a7240 | GetParent | 004a83a0 |

### `00ac19f0` -- Rotation (6)

SetOrigin 004a61f0, GetOrigin 004a62c0, SetDegrees 004a6340, GetDegrees 004a63c0,
SetRadians 004a6410, GetRadians 004a6490.

### `00ac1a24` -- Scale (4)

SetOrigin 004a6500, GetOrigin 004a65d0, SetScale 004a6650, GetScale 004a6700.

### `00ac1a48` -- ControlPoint (5)

SetParent 004a6790, SetOffset 004a6980, GetOffset 004a6a70, SetOrder 004a6b10,
GetOrder 004a6ba0.

### `00ac1a74` -- Path (5)

SetCurve 004a6c10, GetCurve 004a6cd0, GetMaxOrder 004a6d20, GetControlPoints 004a79d0,
CreateControlPoint 004a7bf0.

### `00ac19dc` -- Translation (2)

SetOffset 004a6040, GetOffset 004a6130.

### `00ac1aa0` -- Alpha (2)

SetChange 004a6da0, GetChange 004a6e20.

### These two do not appear in the report

`lua_coverage` skips any reference table with fewer than four entries, on the reasoning that a
short run of `{string, pointer}` pairs is more likely to be part of some other structure than a
binding table. Translation and Alpha have exactly two methods each, so the report has never
counted their four names -- they are missing from frozen on top of the 62 it does count.

Both are real binding tables, and a scan of the whole `00ac1700`-`00ac1c00` neighbourhood finds
these eight animation tables and nothing else, laid out in class order with no gaps:

    00ac18e0  Animation        31
    00ac19dc  Translation       2
    00ac19f0  Rotation          6
    00ac1a24  Scale             4
    00ac1a48  ControlPoint      5
    00ac1a74  Path              5
    00ac1aa0  Alpha             2
    00ac1ab8  AnimationGroup   26

Lowering the threshold is not obviously right -- it would admit genuine noise elsewhere -- so the
honest fix is probably to let `overrides.json` name known short tables. Left open.

## `AnimationGroup:CreateAnimation(type, name, inheritsFrom)` -- `FUN_004a7e00`

Ported order matters here, so it is written out:

1. Resolve `this` as an AnimationGroup.
2. Argument 2, a string, is the TYPE, defaulting to `"Animation"` when absent.
3. Argument 3, a string, is the NAME, defaulting to null.
4. Argument 4, if it is a table (`lua_type == 4`), is the `inheritsFrom` template, resolved with
   `FUN_00812ce0`. Two failure modes, both `luaL_error` and both naming the group via its
   `GetName` virtual with `"<unnamed>"` as the fallback:
   - not found: `%s:CreateAnimation(): Couldn't find inherited node "%s"`
   - recursive: `%s:CreateAnimation(): Recursively inherited node "%s"`
5. Allocate and construct by type as per the hierarchy table above.
6. If a name was given, register it as a global via `FUN_0048b6c0`.
7. If inheriting, run the template through virtuals `+0x20` and `+0x24` on the new object.
8. Push the new object's Lua table (`piVar1[2]`, the object's cached table reference).

## Staging

Each stage should end in a committable increment; none of it should go in blind as one batch.

1. **Skeleton and state. DONE, unverified** -- landed 2026-09-20. `CSimpleAnim` and
   `CSimpleAnimGroup` with both method tables in the reference's order, the handler slots, and the
   region's `CreateAnimationGroup` / `GetAnimationGroups` / `StopAnimating`. Both tables now report
   0 missing; only `HookScript` is stubbed in each, for want of a chaining helper frozen has
   nowhere. Two corrections came out of measuring it: the group's `GetScriptByName` is
   `FUN_00497800`, which carries an **OnLoad** handler the string scan alone had missed and sets no
   wrapper for OnLoad/OnPlay/OnPause; and `FUN_00497fe0` is `CSimpleAnim::GetRegionParent`, which
   the matcher had first paired with a local helper on callgraph evidence alone.
   Original scope: `CSimpleAnimation` and `CSimpleAnimGroup` with the fields the accessors
   read, the two widget type ids, `CreateAnimationGroup` / `CreateAnimation`, and the pure
   accessors (durations, delays, order, smoothing, looping, the `Is*` predicates). `Play`, `Pause`,
   `Stop` as state transitions only. This alone converts the nil-calls into working methods: with
   no per-frame driver the animation simply never advances, which leaves the UI at its start state
   instead of aborting the script that touched it.
2. **The subclasses. DONE, unverified** -- landed 2026-09-20, all six rather than four:
   Translation, Rotation, Scale, Alpha, Path and ControlPoint, in one `CSimpleAnimTypes` pair
   because each is two to six accessors over two or three fields. `CreateAnimation` now dispatches
   by type name the way `FUN_004a7e00` does, case-insensitively, with an unrecognised name falling
   through to the base Animation as the reference does. Curve types (`NONE`, `SMOOTH`) came from
   the table at `00a440d4`.

   Three stage-1 divergences were found and fixed while doing it, all from reading error strings
   rather than from the build: `SetSmoothing` and `SetLooping` **raise** on an unrecognised name
   (009ed788, 009eda60) where stage 1 silently ignored it, and `SetParent` tests for **nil first**,
   then a string, then a table -- stage 1 treated "not a table" as the nil case, which reported a
   number as a nil parent instead of letting it reach the type error. `SetParent` also has a
   "Couldn't find 'this' in parent object" branch stage 1 folded into the wrong-type one.
3. **XML loading.** `CScriptRegion::LoadXML_Animations`, currently an empty TODO, plus the
   `<Animations>` / `<AnimationGroup>` node shapes. FrameXML declares most animations in XML, so
   until this lands stage 1 only serves the scripted path.
4. **The driver.** Per-frame advance, ordering, delays, smoothing curves, looping, and the
   `OnPlay`/`OnFinished`/`OnUpdate`/`OnLoop` script handlers via `NotifyAnimBegin` (also a TODO in
   `CScriptRegion`). This is the stage that makes anything move, and the one to verify on screen.
5. **Path and ControlPoint**, which nothing in the default FrameXML appears to use; last.
