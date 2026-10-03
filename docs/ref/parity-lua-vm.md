# Lua VM parity: the reference's modified Lua 5.1

The reference does not run stock Lua. Its VM (0x0084a000..0x00864000 in WoW.exe 12340, rendered by
`python tools/recomp/corpus.py --range 84a000 864000 --out lua`) is Lua 5.1 with the client's
**secure execution** (taint) woven through the object model, the API, the interpreter loop and the
call machinery, plus a profiled call path and its own allocator. frozen vendored stock 5.1.3 in
`vendor/lua-5.1.3`. The goal is 1:1 in structure, naming and flow: port the reference's VM over
the vendored one, keeping Lua's own source names (the reference has no Lua symbols; Blizzard built
from the Lua sources, so the stock names are the original names).

Status column: **todo**, **ported** (built), **tested** (FrozenTest covers it). Nothing here has
been seen in a running client unless a row says so.

## Taint globals

| addr | name | meaning |
|---|---|---|
| 0x00d4139c | `lua_tainted` | taint of the running execution (`const char*`, null = secure) |
| 0x00d413a0 | `lua_taintexpected` | depth counter; taint only moves while it is non-zero |
| 0x00d413a4 | `lua_taintedclosure` | set while a tainted closure is running, so reads stop re-tainting |
| 0x00d413a8 | (todo name) | taint stamped on code loaded by `FrameScript_ExecuteFile` (set around its `lua_pcall`) |
| 0x00d413ac | (todo name) | taint override for newly created objects (`CSimpleFrame::LoadXML` sets it) |
| 0x00d413b0 | (todo name) | hook called from `luaV_gettable`/`luaV_settable` (set to 0x0052a650 by a script function) |
| 0x00d413b8 | `lua_europeannumbers` | already ported (`lua_seteuropeannumbers`) |

## Object model (32-bit reference offsets)

- `TValue` = `{ Value value; int tt; const char* taint; }` (16 bytes: taint sits in what stock 5.1
  leaves as padding). Taint at +0xc; `FUN_0084f0d0` writes it for a stack index.
- `CommonHeader` = `{ GCObject* next; const char* taint; lu_byte tt; lu_byte marked; }`: every GC
  object records the taint it was created under (`newlstr`, `luaH_new`, `luaE_newthread`,
  `lua_newstate`). Creation taint is `lua_tainted`, replaced by 0x00d413ac when both are set.
- `ClosureHeader`: `next 0, taint 4, tt 8, marked 9, isC 0xa, nupvalues 0xb, gclist 0xc, env 0x10,
  +0x14 profile record (call count at [0], bumped by the profiled precall)`; `LClosure.p 0x18,
  upvals 0x1c`; `CClosure.f 0x18, upvalue[] 0x20`.
- `TString`: `reserved 0xa, hash 0xc, len 0x10, data 0x14`.
- `Table` is 0x24 bytes (`luaH_new`).
- `lua_State` is 0x80: stock 5.1.3 fields shifted by the header's taint word, `l_gt 0x48`, `env 0x58`
  (the environ slot index2adr hands out, stamped with `lua_tainted`), `openupval 0x68`,
  `errorJmp 0x70`, `errfunc 0x74`, and one extra field **+0x78: external abort** -- the interpreter
  raises "external abort" when it is set.
- `global_State` (0x188, allocated after the main thread as one block): **+0x14 = script profiling
  on** (the `scriptProfile` CVar), registry at +0x68.

## The copy rule

Every value copy in the API and the interpreter follows one of these shapes:

- **read-copy** (`lua_pushvalue`, `lua_rawget`, OP_MOVE/LOADK/GETUPVAL/TFORCALL...): copy all
  four words; if the source is clean, stamp the destination with `lua_tainted`; otherwise, if
  `lua_taintexpected && !lua_taintedclosure`, the execution becomes tainted by the source.
- **store-copy** (OP_SETUPVAL, OP_SETLIST, `lua_rawset`): copy verbatim (a clean value stays clean
  in its new home) and taint the execution from a tainted source under the same condition.
- **fresh value** (`lua_pushnumber`, arithmetic results, OP_NEWTABLE, OP_CLOSURE...): taint =
  `lua_tainted`.

## Interpreter (`luaV_execute` 0x00857ca0)

Stock 5.1 opcode for opcode, plus:
- on (re)entry: `lua_taintedclosure = 0`; if the closure has a taint, it becomes `lua_tainted`
  (when expected) and `lua_taintedclosure = 1`.
- OP_CALL / OP_TAILCALL save `lua_taintedclosure ? lua_tainted : 0` and clear the flag across the
  call; on a C return they restore it (re-tainting and re-setting the flag) or clear it.
- OP_RETURN clears `lua_taintedclosure`.
- every instruction checks `L+0x78` (external abort).

## Profiled path

When `global_State+0x14` is set, `luaD_call` and `luaD_callhook`-side resume use a timed precall
(`FUN_00856550`, OsGetAsyncClocks) and a second interpreter loop (`FUN_00859160`, 5573 bytes) that
attributes time to closures. This is what GetFunctionCPUUsage / GetAddOnCPUUsage read.

## Allocator

`lmemPool.cpp` (0x00855570..0x00855a20): pooled small-object allocator behind `luaM_realloc_`.

## Order of work

1. Object model + taint globals + copy macros (lobject.h, lstate.h, a `ltaint` header).
2. Every lapi function to the reference body (taint copy shapes).
3. ldo (precall/call/pcall/poscall/seterrorobj/resume), lvm (execute, gettable/settable, concat,
   arith), lfunc/lstring/ltable/lgc creation taint.
4. FrameScript brackets (`FrameScript_Execute`, `RegisterScriptObject`, script runners) and the
   secure builtins (`issecure`, `issecurevariable`, `forceinsecure`, `securecall`,
   `hooksecurefunc`), `HookScript` on frames/anims, `CScriptRegion::ProtectedFunctionsAllowed`.
5. Profiled path + the CPU-usage script functions.
6. lmemPool allocator.
7. Sweep the remaining ~300 unlinked functions in the range for 1:1 flow.

Each layer gets FrozenTest coverage (`test/lua/`), since a VM bug breaks the whole interface.
