# Parity: the model cache

`CM2Cache` caches nothing. `CreateShared` opens and parses the `.m2` every time it is asked for a
model, and `CM2Shared::Release` destroys the model the moment its refcount reaches zero. A scene
with fifty of the same tree parses that file fifty times, and walking away from a model and back
re-reads it.

This is not a visible bug, which is why it has survived: the client draws correctly, just
wastefully. It is a real divergence from the reference and the single largest one left in the
model path.

**It should be ported as one focused change with a run to verify it**, not folded into an
unrelated cycle. It rewrites model lifetime globally, and a mistake in it is a crash or a wrong
model on screen rather than a cosmetic difference.

## What the reference does

Three functions, all decompiled and understood.

### The hash table

`CM2Cache` holds **1021 buckets** (`0x3fd`) of chain heads at `+0x10`. The key is the model's name
hashed as `hash = hash * 0x13 + c` over each character. By default the name used starts after the
last `\` or `/`, so two identical models in different directories share; flag `0x10` on the create
call switches to hashing the whole path instead.

Each `CM2Shared` carries the chain fields:

| offset | meaning |
|---|---|
| `+0x3c` | the stored path, as a buffer inside the object |
| `+0x140` | pointer to the basename within that buffer |
| `+0x144` | chain prev, as a pointer to the previous link's next field |
| `+0x148` | chain next |
| `+0x14c` | the hash |

The object is `0x1a8` bytes, allocated with the tag `.\M2Cache.cpp`.

### `CM2Cache::CreateShared` (`FUN_0081c390`)

Normalises the name (lowercased, extension forced to `.m2` unless flag `0x1000`), queries the
model blob, hashes, then walks the bucket. The chain is kept ordered by hash, so the walk stops
early; on an equal hash it compares the names and, **on a match, calls AddRef and returns the
existing object**. That is the cache hit, and it is the whole point.

On a miss it opens the file, allocates, loads, copies the name in, records the hash and basename,
and links itself at the head of the bucket — unless flag `0x8` is set, which creates an
uncached model. Flag `0x40` sets a bit on the result.

### `CM2Shared::Release` (`FUN_0083dc90`)

```
if (--refCount == 0) {
    if (!cache || !chainPrev) { destroy immediately; return 0; }
    timestamp = now;
    link onto the cache's pending list;
}
return refCount;
```

So an uncached model (no chain prev, which is exactly the flag `0x8` case) dies at once, and a
cached one goes on a **pending list** instead: head at `cache+0x8`, a tail pointer-to-pointer at
`cache+0xc`, and the entry's own links at `+0x30` / `+0x34` with the timestamp at `+0x38`.

This is the second half of the caching: a model released and re-requested within the window is
still there to be found.

### `CM2Cache::GarbageCollect` (`FUN_0081c290`)

Pops the pending list from the head while the entry is older than **9999 ms**, or unconditionally
when passed a non-zero argument. Each popped entry is destructed and freed. Called every frame
from `CM2Scene::AdvanceTime` with 0, and presumably with 1 on teardown.

Note the entry is still in its hash bucket while it sits on the pending list — that is what lets
`CreateShared` find and revive it.

## What frozen needs

1. `CM2Cache`: the 1021 bucket array, and the pending list head and tail.
2. `CM2Shared`: the chain prev/next/hash/basename fields, and the pending links and timestamp.
3. `CreateShared`: hash, walk, AddRef on hit; link on miss. The normalisation is already there.
4. `Release`: queue instead of `delete this` when the model is in a bucket. The `// TODO free list
   management etc` comment sits exactly where this goes.
5. `GarbageCollect`: the pop loop above. `UpdateShared` (`FUN_0081c790`) is a separate list at
   `+0x10a0` / `+0x10a8` keyed on `+0x19c`, which has not been worked out yet.

Get the hash and the name comparison right: a collision that passes the name check would hand back
the wrong model, which is a visible, confusing failure rather than a crash.
