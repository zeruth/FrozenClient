# Async texture loading: what `TextureGetGxTex` depends on

Written 2026-09-23 while making `TextureGetGxTex` faithful. Everything here was read out of
`WoW.exe` with `llvm-objdump`, not from a decompilation, and none of it has been seen running.

## Why this matters

`TextureGetGxTex` (`FUN_004b6cb0`, 193 callers) is the function every draw call goes through to
turn a `CTexture*` into a `CGxTex*`. Two of the three helpers it calls are **empty stubs in
frozen with live call sites**, so the blocking fetch does not block and the priority hint does
nothing:

| reference | frozen | state |
|---|---|---|
| `FUN_004b6550` | `AsyncTextureWait` | **stub** |
| `FUN_004b6c50` | `TextureIncreasePriority` | **stub** |
| `FUN_004b63b0` | — | no counterpart (the atlas path) |

The visible consequence, if any, would be textures that are missing on the frame they are first
needed rather than missing permanently, because the next frame finds `gxTex` already set. That is
consistent with pop-in, and it has **not** been confirmed on screen.

## `AsyncTextureWait` — `FUN_004b6550`

Seven instructions, `ESI` = the texture, no stack frame:

```
CAsyncObject* a = texture->asyncObject;      // +0x40
if (!a) return;
if (a->field_4 == 0) FUN_004b64e0(1);        // EDI = a
AsyncFileReadWait(a);                        // 004ba060 -- frozen has this one
```

`FUN_004b64e0` (4 callers, `EDI` = the async object, plus one stack argument) unlinks the node at
`a+0x28` from whatever list holds it, patching the neighbours through the usual Storm intrusive
list dance, then `SMemFree`s `a+0x8`. So the `field_4 == 0` branch is "this request has already
finished, retire it" and the `AsyncFileReadWait` afterwards is the real wait.

**Why it is not ported yet.** `FUN_004b64e0` frees a request out from under the list it is linked
into, and the guard that decides whether to do so is a single field whose meaning has not been
confirmed from the writer's side. Getting the order or the guard wrong in a blocking path is a
hang or a use-after-free, not a wrong pixel. Read `FUN_004b64e0`'s other three callers and the
writers of `field_4` before porting.

## `TextureIncreasePriority` — `FUN_004b6c50`

```
if (!SFile::IsStreamingMode()) return;        // 00422130
CAsyncObject* a = texture->asyncObject;       // +0x40
if (a->field_4 == 0) { FUN_007b5020(0xac337c, a); return; }   // already finished
FUN_004b9950();                                               // take the queue lock
if (!a->byte_21 && !a->byte_22 && !a->byte_23) FUN_004bac20(a);  // requeue at priority
FUN_004b9970();                                               // release it (tail jump)
```

The three bytes at `a+0x21`, `+0x22`, `+0x23` are read together as a "do not touch this request"
test; `a+0x24` is read separately by `TextureGetGxTex` itself for the `a2 == 2` case, so the four
bytes are one flags block. `FUN_004bac20` is the requeue.

**Why it is not ported yet.** The requeue and those flag bytes are the async texture queue's
internals, and frozen's queue is not yet known to have the same shape.

## The atlas path — `FUN_004b63b0`

`TextureGetGxTex`'s `flags & 4` branch ends with:

```
if (texture->atlas) {                        // +0x5c
    if (texture->atlas->field_2 & 1) FUN_004b63b0();
    return texture->atlas->field_0x18;       // the atlas's own CGxTex
}
```

`FUN_004b63b0` builds a `.blp` path on a 0x118-byte stack buffer from `this+0x2c` and reloads.

frozen returns null here instead, with a `// TODO`. That is currently harmless: `CTexture::atlas`
is a `void*` that **nothing in frozen ever assigns** (the one assignment in `Texture.cpp` is
commented out), so the branch is dead. It stays dead until `CTextureAtlas` is ported, which is a
system rather than a fix.

## What was fixed instead

The null-pointer guard. The reference validates and recovers -- it names the parameter, sets last
error to `ERROR_INVALID_PARAMETER` (0x57) and returns null. frozen used `STORM_ASSERT`, which
**compiles out entirely in Release**, so a null texture fell straight through into
`texture->flags`. Now `STORM_VALIDATE_BEGIN` / `STORM_VALIDATE` / `STORM_VALIDATE_END`, which
expands to exactly the reference's behaviour.

## Links recorded from this pass

| reference | frozen | how it was identified |
|---|---|---|
| `00422130` | `SFile::IsStreamingMode` (**a stub in frozen: returns a constant 0**) | three instructions returning a byte from `FUN_00428000`; all 30 call sites are `if (result) <streaming-only work>`, and frozen's `AsyncFileReadWait` calls `SFile::IsStreamingMode` in the same guard position that the reference's `004ba060` calls this |
| `004b6550` | `AsyncTextureWait` | call site and body, above |
| `004b6c50` | `TextureIncreasePriority` | call site and body, above |

## 2026-09-23, later: streaming mode is ON in the reference

`SFile::IsStreamingMode` is two instructions deep: it zero-extends the byte at `0x00b38180`, read
through `FUN_00428000`. That byte is set to **1 unconditionally** during Storm initialisation, at
`0x00461a94`. So a normal reference run has streaming mode on.

That matters here because `TextureIncreasePriority` opens with `if (!IsStreamingMode()) return;`.
frozen's `SFile::IsStreamingMode` is a `// TODO` returning a constant 0, so even once that function
is ported it would do nothing. **Port the predicate first**, or the priority work is dead on
arrival.

It also means frozen's constant 0 is a behavioural difference rather than a conservative default.
Its status in `overrides.json` was corrected from "ported" to "stub" the same day, by
`tools/audit-ported.py`.
