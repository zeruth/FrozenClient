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

The keys live in a saved profile. The executable references `bindings-cache` (twice) and
`Bindings.xml` (twice); the reference install used here has no `WTF` bindings cache to read, so
**the default key set has not been located yet.** That is the open question, and the next thing to
chase: whichever function writes `bindings-cache` will name the format, and whatever seeds it on a
fresh profile is the default table.

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

## 4. What is left

1. **The default key set** (section 1). Everything else is blocked behind it -- without keys, the
   table is complete and inert.
2. `SetBinding` and friends, and saving to a bindings cache.
3. The dispatch half: a key event resolving to a command and running its Lua body. The bodies are
   already parsed and kept for it.
4. The four override setters (`SetOverrideBinding*`) and the five-set `mode` argument.
