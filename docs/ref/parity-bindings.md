# Key bindings parity: 3.3.5a reference vs frozen

Scope: the key binding subsystem -- where the commands come from, where the keys come from, and
what each Lua getter is shaped like. Program: `RunicWorldGame.exe` (Win 3.3.5a 12340).

**Why this matters.** Until it exists no key does anything in game, and no action button shows a
hotkey. `UIBindingsScript.cpp` had the whole table stubbed.

---

## 1. Commands come from XML, keys do not

`Interface\FrameXML\Bindings.xml` declares 273 `<Binding>` rows: a command name, the Lua it runs,
an optional `runOnUp`, and an optional `header` that groups it in the key binding pane. The header
appears once per group and the rows after it inherit it.

It does **not** carry default keys. The only `default=` attributes in the file are on the sixteen
`<ModifiedClick>` rows, which are a different feature (self-cast modifier, chat link modifier).

The command names are not in the executable either -- searching it for `ACTIONBUTTON1`,
`MOVEFORWARD` or `TOGGLEGAMEMENU` finds nothing. So the command list has exactly one source, and
this half is fully portable from data.

The keys live in a saved profile, or in a `default` attribute the shipped file never uses --
**see section 3c, which answers this; sections 3a and 3b are kept as the record of how, including
one hypothesis that turned out wrong.**

---

## 2. The getters

All of them take an optional trailing `mode`, an index into five binding sets, clamped as
`mode - 1 < 4`. Frozen has one set, so the argument is accepted and ignored.

| reference | shape |
|---|---|
| `FUN_0055dc00` `GetNumBindings` | pushes `*DAT_00beadd8`, the command count |
| `FUN_0055e8d0` `GetBinding(index[, mode])` | 1-based index; pushes the command name, then every key bound to it; returns `1 + keys` |
| `FUN_0055e9b0` `GetBindingKey("CMD"[, mode])` | pushes every key; returns `keys`, so nothing at all when unbound |
| `FUN_00562550` `GetBindingAction("KEY"[, checkOverride][, mode])` | the command, or the empty string |
| `FUN_005625f0` `GetBindingByKey` | resolves through `FUN_005622e0` then `FUN_0055e470` |

Two helpers do the work under all of them: `FUN_0055e700(index, &name)` reads a command name by
position, and `FUN_0055e750(mode, name, n)` reads the nth key bound to a command, returning null
when there is no nth. That "walk n upward until null" shape is why the getters above return a
variable number of values.

**`GetBindingText` and `GetBindingFromClick` are not client bindings at all.** They are Lua,
defined in `Interface\FrameXML\UIParent.lua`. Frozen not registering them is correct; a count of
"missing" bindings that includes them is wrong.

---

## 3. What frozen has

`src/ui/game/UIBindings.cpp` holds the command table, parsed from Bindings.xml, and
`GetNumBindings`, `GetBinding`, `GetBindingKey` and `GetBindingAction` are wired to it.

Every command is declared and none is bound. That is not a placeholder -- it is the state the
reference is in on a profile that has never saved a binding, and the key binding pane renders it
as a full command list with "Not Bound" against each row.

The load is lazy, on the first query, where the reference parses during UI initialisation. Marked
as a divergence at the call site: for a read-only table with no dependencies the two are
indistinguishable, and it avoids taking a position on startup ordering that cannot be verified yet.

## 3a. The reference's store, and where the defaults are NOT (2026-09-20)

`DAT_00beadd8` is a pointer to the binding manager. Two fields are known:

```
+0x000   the command count      (GetNumBindings pushes it)
+0x124   the current binding set (GetCurrentBindingSet pushes it)
```

It holds two intrusive Storm lists:

- **commands** -- head at `+0x18`, link at `+0x10`. `FUN_0055e700(index, &out)` walks it for the
  node whose `+0x18` equals the index and returns its `+0x14`, the name.
- **key bindings** -- head at `+0xb8`, link at `+0xb0`. `FUN_0055e750(mode, command, n)` walks it,
  taking each node's command for `mode` (`FUN_0055e470`) and its ordinal (`FUN_0055e4e0`), and
  returns the node's `+0x14`, the key string, when both match.

So a key binding node carries a key and, per mode, a command and an ordinal -- which is what lets
two keys map to one command and be returned in a stable order.

**Four places the default keys are not.** Each was checked, not assumed:

1. Not in `Bindings.xml`. Its `<Binding>` rows carry exactly `name`, `runOnUp`, `header`, `hidden`,
   `debug` and `platform` -- no `default`. The sixteen `default=` attributes in the file are all on
   `<ModifiedClick>`, a different feature.
2. Not in FrameXML. Nothing under `Interface\FrameXML` calls `SetBinding`; the only match is an
   unrelated method on a restricted-frame handle.
3. Not in a saved cache. The reference install here has been run many times -- it has per-character
   `config-cache.wtf` files -- and there is **no `bindings-cache.wtf` anywhere in its WTF tree**.
   The client had defaults without ever having written them down.
4. Not as command-name strings in the executable. `ACTIONBUTTON1`, `MOVEFORWARD` and
   `TOGGLEGAMEMENU` appear zero times in `WoW.exe`.

Taken together those say the defaults are a table in the binary that does **not** name commands by
string -- most likely by position in the parsed `Bindings.xml`, since that file and the executable
ship together.

## 3b. The four sets, and where the search stands (2026-09-20, second pass)

The manager holds **four** binding sets, not one list. Each is a Storm list head at
`manager + 0x40 + set * 0x28`, with its link offset at `manager + 0x38 + set * 0x28`. The `+0xb8`
head found earlier is simply set 3.

```
set 0   default
set 1   account
set 2   character
set 3   effective -- the one every getter reads
```

`FUN_00562b80(from, to)` copies one set to another, and both Lua entry points are thin wrappers
over it: `LoadBindings(0|1|2)` is `copy(n, 3)` and `SaveBindings(1|2)` is `copy(3, n)` (plus
`copy(1, 2)` when saving to the account set). The copy **walks downward from the requested set
until it finds a non-empty one**, which is the fallback chain: character, else account, else
default.

So set 0 is where the defaults must land. What fills it is still not found. Ruled out this pass,
on top of the four in section 1:

5. Not seeded at construction. The manager is allocated in `FUN_005620f0` -- 0x13c bytes, and it
   names its own source file, `.\UIBindings.cpp` line 187 -- and the constructor `FUN_00561b80`
   zeroes all four sets and sets the command count to 0. Nothing is populated there.
6. Not loaded by FrameXML. Nothing under `Interface\FrameXML` calls `LoadBindings` either, so the
   load is entirely client-side at startup.
7. Not a separate data file. `BindingsDefault.xml`, `KeyBindings.xml`, `DefaultBindings.xml` and a
   shipped `bindings-cache.wtf` were each probed by exact path against the archives (the listfile
   is incomplete, so a name search is not enough) and none exists.

## 3c. Answered (2026-09-20, third pass)

The `<Binding>` handler is `FUN_00564470`, reached from the element dispatch at 0x00564828. It
ends with exactly this:

```c
key = GetAttribute("default");
if (key && *key && !AlreadyBound(key)) {
    FUN_00562ed0(0, 0, key, commandName);   // bind, into SET 0
    return;
}
```

`AlreadyBound` is `FUN_0055f4d0`, a generic hash lookup by name whose table arrives in ECX, so
which table it searches cannot be read from the decompilation. The bytes settle it -- the three
call sites in this function load ECX differently:

```
005644f4   lea ebx, [edi+0x0c] ; mov ecx, ebx      -> manager+0x0c, the command table
0056455d   mov ecx, ebx                            -> manager+0x0c, again (the HEADER_ check)
0056470f   lea ecx, [ebx+0x34]                     -> manager+0x40, which is SET 0
```

So the guard on `default` is "this key is not already bound **in the default set**", not a check
against command names.

**So the mechanism was the `default` attribute all along, and it writes straight into set 0.**
There is no index-keyed table and no hidden data file; the hypothesis in 3b was wrong.

What makes this confusing is that the shipped 3.3.5a `Interface\FrameXML\Bindings.xml` uses
`default` on **no `<Binding>` row at all** -- its sixteen uses are all on `<ModifiedClick>` -- and
no Blizzard add-on ships a `Bindings.xml` either (probed by exact path). Against this data the
mechanism is real and produces nothing, which is consistent with every other observation: a
constructor that seeds nothing, a FrameXML that never calls `LoadBindings`, and an install played
across three characters with no `bindings-cache.wtf` ever written.

Frozen implements the attribute. It is correct against the reference and inert against this data,
which is the right state to be in: if a `Bindings.xml` with defaults ever appears, it works.

### What the same handler gave up, which matters more

Reading it corrected a real fidelity bug in the frozen port:

- **A header is registered as a command in its own right**, named `HEADER_<group>`, and consumes
  an index. That is how the key binding pane draws its section titles -- by walking indices and
  finding a `HEADER_` row where a heading belongs. Frozen was skipping them, which renumbered
  every command after the first group.
- Rows with `debug` set are dropped, and rows naming a `platform` are dropped unless it is
  literally `windows`.
- `hidden` rows (and joystick rows with no joystick) are still registered and still bindable by
  name, but take a NEGATIVE index so the numbered walk never reaches them.
- The Lua body is wrapped as
  `return function(keystate, pressure, angle, precision) %s end`.
- `pressure` and `angle` are attributes too, alongside `runOnUp`.
- Every registration is guarded against a duplicate name, headers included.

With the header rows and the two drops, the shipped file yields **270 entries: 253 commands and
17 headers** -- not the 273 a naive pass produces.

## 4. What is left

1. ~~**The default key set**~~ -- answered in section 3c. The mechanism is the `default` attribute
   on `<Binding>`, which the shipped file never uses. Implemented and inert against this data.
2. ~~`SetBinding`~~ -- done. `FUN_00563520` is `SetBinding("KEY"[, "COMMAND"][, mode])`: it writes
   through `FUN_00562ed0(3, mode, key, command)` -- set 3, the effective one -- signals
   UPDATE_BINDINGS (0x177 = 375) on success, and returns exactly one value, nil included. Omitting
   the command unbinds. Saving to a bindings cache is still open.
3. The dispatch half, **partly done**. Running a command is ported (section 5); what remains is the
   front of it -- turning a key event into a key string with its modifier prefixes, and calling the
   dispatcher when no frame consumed the key.
4. The four override setters (`SetOverrideBinding*`) and the five-set `mode` argument.

---

## 5. Running a command (2026-09-20)

`FUN_0055f860(command, keyDown, pressure, ?, ?, ?, angle, precision, ?)` is the runner, and its
node offsets confirm the parser read in section 3c exactly:

```
+0x1c   the compiled Lua function   (the parser's puVar5[7])
+0x20   runOnUp                     (puVar5[8])
+0x24   pressure                    (puVar5[9])
+0x28   angle                       (puVar5[10])
```

It looks the command up by name, returns early when the event is a release and `runOnUp` is clear,
and otherwise pushes four arguments and calls the function: the key state as a string, then
pressure, angle and precision. The two state strings are literals at `00a0eef0` and `00a0eef4`,
which are `"up"` and `"down"`.

The body is compiled once, at parse time, with
`FrameScript_CompileFunction(name, "return function(keystate, pressure, angle, precision) %s end", body, status)`
-- frozen has that helper already, and it is the same one the reference uses (`FUN_008190c0`).

`FUN_00563150` is the key dispatcher above it. It resolves a key to a binding node, then splits on
the command's prefix: `"SPELL "`, `"ITEM "` and `"MACRO "` are the override forms that
`SetBindingSpell`/`Item`/`Macro` write, and anything else is a plain command that reaches the
runner. Only the plain form is ported.
