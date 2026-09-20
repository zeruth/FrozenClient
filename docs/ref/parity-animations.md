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
3. **XML loading. DONE, unverified** -- landed 2026-09-20. `CScriptRegion::LoadXML_Animations`
   (`FUN_004883f0`) plus a `LoadXML` on every class, and `LoadXML_AnimOrigin` for the `<Origin>`
   element. The `<Scripts>` block goes through its own `AnimLoadXML_Scripts` (`FUN_00497c30`)
   rather than `CSimpleFrame`'s: the reference keeps two of these, and only the frame's says
   "Frame %s:".

   Constants and quirks taken from the binary rather than assumed: `startDelay`/`endDelay` clamp at
   zero silently; `order` is 1-based in XML, stored 0-based, and an out-of-range value is reported
   **and then clamped** rather than dropped; `maxFramerate` is tested against 1e-4, not zero, and a
   1/rate interval is stored beside it; `scaleX`/`scaleY` are bounded below at 0.001, reported and
   clamped; `degrees` and `radians` write the same field so the last one in the element wins;
   offsets go through the `<AbsDimension>` conversion rather than being raw pixels.

   One correction: `FUN_004980d0`, blocked as unlinked in stage 2 on the reading that it was driver
   arithmetic, is `CSimpleScaleAnim::GetScale`. The loader settles it -- it stores `1 - scale`, so
   that function's `{1 - x, 1 - y}` is simply how the scale reads back out. Now linked, with the
   storage difference recorded as a divergence.
4. **The driver.** Split in two after surveying it on 2026-09-20; the halves have very different
   risk and only the second touches rendering.

   **4a -- timing and callbacks. DONE, unverified** -- landed 2026-09-20. `CSimpleAnim::Advance`
   and `OnUpdate`, `CSimpleAnimGroup::OnUpdate`, `AdvanceOrder`, `OnAnimationFinished` and
   `ShouldStopStepping`, driven from `CScriptRegion::OnLayerUpdate` -- a call site that already ran
   every frame and was simply empty. Groups advance, orders sequence, REPEAT and BOUNCE loop, and
   `OnPlay`/`OnUpdate`/`OnFinished`/`OnStop`/`OnLoop` all fire.

   **`ShouldStopStepping` is not optional.** The tick loops so that leftover time carries into the
   next order, and without that guard a looping group whose animations have zero duration finishes
   every pass, consumes nothing, and comes straight back round -- a hang, not a glitch. The
   reference's first test in `FUN_00497920` is exactly that case.

   **Smoothing curve: done** -- `AnimSmoothingApply`, ported from `FUN_00497ba0`. The reference
   keeps a 12-byte object (`.\CSimpleAnim.cpp` line 0x30d, vtable `009ebd44`: slot 0 sets the
   weights, slot 1 evaluates) holding an ease-in and an ease-out weight clamped to [0, 1], and
   branches on whether each is within 0.001 of zero. `SetSmoothing` maps the enum to exactly those
   pairs, so frozen branching on the enum reaches the same four cases without the object.

   The curves are the sine-ease family, with every constant read from the binary:

   | smoothing | weights | curve |
   |---|---|---|
   | NONE | (0, 0) | `t` -- the reference allocates no curve at all |
   | IN | (1, 0) | `1 - cos(t * pi/2)` |
   | OUT | (0, 1) | `-cos((t + 1) * pi/2)`, which is `sin(t * pi/2)` |
   | IN_OUT | (1, 1) | `0.5 - cos(t * pi) * 0.5` |
   | OUT_IN | (1, 1) | identical to IN_OUT |

   OUT_IN sharing IN_OUT's pair is the same quirk that stops `GetSmoothing` ever answering
   "OUT_IN": one cause, visible two ways.

   **4b -- application to the region.** Split again in practice.

   **The animation half is DONE, unverified** -- landed 2026-09-20. Each subclass forms its
   contribution in `OnApply` and hands it to the region; `OnUnapply` takes it back off, and Scale
   overrides that because it composes multiplicatively.

   **The region half is one quarter done.** `CSimpleRegion::AddAnimAlpha` (`FUN_00487ce0`) is
   ported -- it was the one accumulator whose body could be read end to end and whose frozen
   counterpart already existed, since `GetVertexColor` already returns opaque white for an unset
   colour exactly as the reference's fallback does. Translation, rotation and scale are not:
   `00481740` accumulates into a texture field whose frozen equivalent has not been identified, and
   `00481770` and `004817a0` have not been read at all.

   **The rest is NOT started.** It is render-surface work and the part that must be seen on
   screen. What was missing until now was the composition rule -- whether the region accumulates
   additively, multiplicatively, or by replacement. That is answered below.

### The region side, and how it composes

The `CScriptRegion` family vtable was found by scanning `.rdata` for tables whose slots cluster in
the `00487000`-`00489000` band where the known `CScriptRegion` code lives. The alignment is
confirmed rather than assumed: slot `+0x30` lands on `FUN_004889c0`, which allocates from
`".\CScriptRegion.cpp"` line 0xdf and is unmistakably `NotifyAnimBegin` -- it builds the region's
list node and looks up the Alpha animation type id to see whether the group carries a fade.

A texture's table (`009ea1d8`) then reads:

| slot | method | address |
|---|---|---|
| `+0x30` | `NotifyAnimBegin` | `004889c0` |
| `+0x34` | `NotifyAnimEnd` | `00488980` |
| `+0x38` | `StopAnimating` | `004888f0` |
| `+0x3c` | `AnimActivated` | `00488000` |
| `+0x40` | `AnimDeactivated` | `00488060` |
| `+0x44` | `AddAnimTranslation` | `00481740` (CSimpleTexture's own) |
| `+0x48` | `AddAnimRotation` | `00481770` |
| `+0x4c` | `AddAnimScale` | `004817a0` |
| `+0x50` | `AddAnimAlpha` | `00487ce0` (CScriptRegion's, inherited) |

**The composition is additive and cumulative, onto live state.** Two bodies settle it:

`CSimpleTexture::AddAnimTranslation` (`00481740`) is two calls: accumulate the incoming vector into
a field at `texture+0xe0`, then `CSimpleRegion::OnRegionChanged` (`00487ca0`) to mark the region
dirty. It does not replace, and it does not recompute from progress.

`CScriptRegion::AddAnimAlpha` (`00487ce0`) reads the region's CURRENT colour -- a packed ARGB at
`+0xa4`..`+0xae`, or opaque white when the region has no colour set -- adds the incoming integer to
the alpha byte, clamps above at 255 and below at 0, and writes the colour back through
`FUN_00487a10`.

That cumulative design is exactly why `OnUnapply` exists and why `PreOnAnimUpdate` runs before the
tick: nothing recomputes an absolute value, so last frame's contribution has to be subtracted
before this frame's is added. It is also why porting this half blind is a poor idea -- an
un-apply that does not exactly cancel its apply makes a region drift a little every frame, which
looks like a slow colour or position leak rather than an obvious break.

**Remaining unknowns for the region half:** what `FUN_00487a10` does with the colour, what
`texture+0xe0` is in frozen's layout, and the rotation and scale accumulators (`00481770`,
`004817a0`), none of which have been read yet.

   **NOT STARTED, but no longer blocked.** The advance function was found on 2026-09-20 by
   scanning for call sites of the script runner `FUN_0081a2c0` inside the animation address range
   and walking outwards. 4a can now be written from the reference rather than from the documented
   API. The map is below.

### A warning about the order matcher in this address range

On 2026-09-20 the recomp's `order` evidence produced twelve links across
`00497e60`-`004985a0` in one go, all justified as "definition order between AnimSmoothingApply and
CSimpleAnim::Advance". The heuristic takes the gap between two anchored functions and lays
frozen's remaining definitions onto the reference's remaining addresses in source order.

Two were provably wrong from functions read the same afternoon: `00497f30` is
`CSimpleAnim::SetSmoothing`, the C++ one that builds the curve object, and `004982e0` is
`CSimpleAlphaAnim::SetChange`. Correcting those two collapsed the other ten, because the run was a
chain and breaking a link invalidated the ordering either side of it.

Everything in the animation range is now `annotated` -- a `// ref:` tag placed after reading the
reference function -- or one deliberate override. Treat any future `order` link in this range as a
question rather than an answer.

### The driver's functions

Found by scanning `.text` for calls to `FUN_0081a2c0` -- the script runner -- between `00497000`
and `0049d000`, which lands inside every function that fires a handler, then following callers.

| address | what it is |
|---|---|
| `FUN_0049c350` | `CSimpleAnimGroup::OnUpdate(elapsed)` -- the per-frame tick |
| `FUN_0049b470` | the group's order advance, run when the current order completes |
| `FUN_0049aab0` | `CSimpleAnimGroup::Pause` |
| `FUN_0049b0f0` | `CSimpleAnimGroup::Stop(requested)` |
| `FUN_0049ab60` | run when one animation finishes: stops the group once every other is stopped |
| `FUN_0049adc0` | the animation's own stop |
| `FUN_0049a8f0` | `CSimpleAnimGroup::Play` (already ported) |
| `FUN_0049ad80` | `CSimpleAnim::Play` (already ported) |

**Group state**, all confirmed by two or more of those functions agreeing:

| offset | field |
|---|---|
| `+0x3c` | the full animation list |
| `+0x44` | number of orders |
| `+0x48` | array of per-order lists |
| `+0x88` | flags: `0x04` paused, `0x08` un-apply pending, `0x10` finished, `0x20` stopping |
| `+0x8c` | loop type |
| `+0x8d` | loop state; zero also means "not playing" |
| `+0x90` | current order index, `-1` when idle |
| `+0x94` | elapsed |
| `+0x98` | the current order's duration |
| `+0x9c` | progress, `elapsed / duration` capped at 1 |
| `+0xa0` | the initial offset pair, handed to the region's `AddAnimTranslation` each tick |

**Animation state:** `+0x34` play state (0 stopped, 1 playing, 2 paused), `+0x35` loop state,
`+0x84` elapsed, `+0x88` the fraction `IsDone` tests against 1, `+0x8c` progress.

**Handler slots.** Both classes start with OnLoad and step by 8:

    animation  +0x3c OnLoad  +0x44 OnPlay  +0x4c OnPause  +0x54 OnStop  +0x5c OnFinished  +0x64 OnUpdate
    group      +0x50 OnLoad  +0x58 OnPlay  +0x60 OnPause  +0x68 OnStop  +0x70 OnFinished  +0x78 OnUpdate  +0x80 OnLoop

Only the group has OnLoop. Only the group's OnFinished takes `requested`; the animation's takes
nothing. Both OnStops take `requested` and both OnUpdates take `elapsed`.

**How a tick runs**, from `FUN_0049c350`: orders before the current one are re-applied at a full
amount of 1.0 so completed steps hold; the group's initial offset is re-added; a paused group
re-applies each current animation at its stored amount and returns; otherwise the incoming elapsed
is clamped to `[0, _DAT_009ec218]` before being added, so one enormous frame cannot skip an
animation.

### What is established (2026-09-20)

**The application model.** An animation reaches its region through its group: `anim[0x28]` is the
group, `group[0x30]` is the region. Two vtable slots on the animation drive it:

| anim vtable | meaning |
|---|---|
| `+0x2c` | apply this animation's contribution, scaled by an amount |
| `+0x30` | un-apply -- the shared body at `FUN_00497700` simply calls `+0x2c` with the negated amount |

That pairing is why `CScriptRegion::PreOnAnimUpdate` exists and why `CSimpleFrame::OnLayerUpdate`
calls it on the frame and every region *before* updating: each animation removes last frame's
contribution, then adds this frame's.

Confirmed bodies: `FUN_00498040` is Translation's apply -- it forms
`{offsetX * amount, offsetY * amount}` and hands it to the region -- and `FUN_00498330` is Alpha's.

**Region vtable slots**, from those two applies (`+0x44` and `+0x50`) and from
`CSimpleAnimGroup::Play` calling `+0x30` on the region:

| region vtable | method |
|---|---|
| `+0x28` | `PreOnAnimUpdate` |
| `+0x2c` | `OnLayerUpdate` |
| `+0x30` | `NotifyAnimBegin` |
| `+0x34` | `NotifyAnimEnd` |
| `+0x38` | `StopAnimating` |
| `+0x3c` | `AnimActivated` |
| `+0x40` | `AnimDeactivated` |
| `+0x44` | `AddAnimTranslation` |
| `+0x48` | `AddAnimRotation` |
| `+0x4c` | `AddAnimScale` |
| `+0x50` | `AddAnimAlpha` |

Only `+0x30`, `+0x44` and `+0x50` are read directly from the binary. The rest follow from those
three and from the fact that frozen's `CScriptRegion` already declares this exact sequence -- the
two orderings agree at every anchor, which is good evidence but is not the same as having read
each slot. Treat the unanchored rows as strong inference until a region vtable is dumped.

**Animation vtables** (`CSimpleAnim` `009ebe64`, Translation `009ebe98`, Alpha `009ebf38`,
`CSimpleAnimGroup` `009ebfa0`): `+0x0c` is `GetScriptByName`, `+0x20` is `LoadXML` and `+0x24` is
`PostLoadXML` -- `CreateAnimation` calls that pair in order when copying an inherited template.
The group's table is only eight entries and ends at `+0x1c`, so its `LoadXML` and its update are
both non-virtual, which is why neither can be found through a vtable.

**Found since:** the advance is `FUN_0049c350`, listed above. It is not reachable through the
group vtable, which is why looking there failed -- the group's update and its `LoadXML` are both
non-virtual. `FUN_0049a580` is a SIMPLEANIMNODE list helper and `FUN_0049a700` the destructor.

**Still open:** which region function calls `FUN_0049c350`. The region's `OnLayerUpdate` sits at
region vtable `+0x2c` and needs a CScriptRegion-derived constructor to read the table from;
`CSimpleTexture::Init` (`00483060`) is linked but is not the constructor and assigns no vtable.
4a does not need it -- frozen already has its own call site -- but 4b probably will.

**Frozen's side is already wired for 4a.** `CSimpleFrame::OnLayerUpdate` runs every frame and
already calls `PreOnAnimUpdate` on the frame and its regions and then
`CScriptRegion::OnLayerUpdate`, which is an empty TODO. The driver drops into that empty body; no
new call site is needed. Nothing in frozen implements any of the `AddAnim*` virtuals yet, so 4b
starts from nothing.

5. **Path and ControlPoint**, which nothing in the default FrameXML appears to use; last.
