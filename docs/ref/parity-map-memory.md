# Parity: `CMap::MapMemInitialize`, and where the real terrain shaders live

`CMap::MapMemInitialize` (`FUN_0079e7c0`) is the map's one-time setup. frozen ports only its tail,
the object heaps, and reproduces 58% of its call sequence. Reading the rest turned up two things
worth more than the fidelity score.

## The original terrain shaders ship in the game data

`CLAUDE.md` says the terrain shaders are hand-written because "the original terrain HLSL was lost,
and the header had to be disassembled with `fxc -dumpbin` to recover its interface". The HLSL
source is indeed lost. **The compiled originals are not.** They sit in the archives, and frozen's
own loader already reads that format.

`MapMemInitialize` loads them by name through `CGxDevice::ShaderCreate`:

| target | directory | name | permutations |
|---|---|---|---:|
| vertex | `Shaders\Vertex` | `Terrain` | 128 |
| pixel | `Shaders\Pixel` | `Terrain0` | 3 |
| pixel | `Shaders\Pixel` | `Terrain0_env` | 1 |
| pixel | `Shaders\Pixel` | `TerrainSM` | 1 |

and then a set chosen by the shader-level CVar (`FUN_00532af0` + `0xc4`):

| level | loads |
|---|---|
| 1 | `Terrain1` x6 |
| 2 | `Terrain1` x8, then `Terrain1w_1`, `Terrain1w_2`, `Terrain1w_3`, `Terrain1w_4`, each x1 twice over |
| 8, 9, 10 | `Terrain1` x4, then `Terrain1w` x4 |
| default | `Terrain1` x32, then `Terrain1w` |

Verified against the reference install rather than assumed:

- `shaders\Vertex\vs_2_0\Terrain.bls` extracts from `patch.MPQ` at 160,524 bytes, and opens with the
  `HSXG` container magic followed by `0002feff`, which is vertex shader 2.0 bytecode. 160 KB is
  consistent with the 128 permutations the call asks for.
- `shaders\Pixel\ps_2_0\Terrain0.bls` extracts at 936 bytes, consistent with 3 permutations.
- Every profile the fallback chain walks is present: `vs_3_0`, `vs_2_0`, `vs_1_1`, `arbvp1`,
  `arbvp1_cg12`, `nvvp3` for vertex, and `ps_2_0`, `ps_1_4`, `ps_1_1`, `arbfp1`, `nvts` for pixel.

frozen's `CGxDevice::IShaderLoad` already builds exactly this path (`"%s\\%s\\%s.bls"` with the
profile directory) and already walks the same fallback chain; it loads the UI shaders this way
today. So the loading machinery is done.

**What is not done, and why this is not a small change.** frozen's terrain pass was written around
its own hand-authored vertex program: it feeds constants `c0`-`c3`, expects a particular vertex
stream layout, and `TerrainShadersD3d9.hpp` embeds the bytecode. The original shader has its own
constant layout and its own 128-permutation index. Using it means porting the terrain render's
constant setup too, which is `FUN_007cfbe0` in `docs/ref/parity-sky.md`'s notes, and the chunk
draw that selects a permutation. That is a real port, not a swap, and it has to be verified on
screen in the same change.

Until then the hand-written shaders stay. Nothing here changes the running client.

## The reference's map object sizes

The same function creates every map object heap, and the call arguments give the reference's exact
struct sizes. These are worth keeping: any class built for these heaps has to match, and frozen
currently sizes them from its own partial definitions.

| heap | reference size | per block | frozen has the class |
|---|---:|---:|---|
| `WLIGHT` | 212 | 128 | `CMapLight` (skeleton) |
| `WCACHELIGHT` | 132 | 256 | `CMapCacheLight` (skeleton) |
| `WMAPOBJGROUP` | 444 | 128 | `CMapObjGroup` (partly ported) |
| `WMAPOBJ` | 2552 | 32 | `CMapObj` (partly ported) |
| `WAREA` | 1212 | 16 | `CMapArea` (skeleton) |
| `WAREAMED` | 33404 | 16 | **missing** |
| `WAREALOW` | 92 | 16 | `CMapAreaLow` (skeleton) |
| `WCHUNK` | 344 | 256 | `CMapChunk` (skeleton) |
| `WENTITY` | 208 | 128 | `CMapEntity` (partly ported) |
| `WMAPOBJDEFGROUP` | 192 | 128 | `CMapObjDefGroup` (skeleton) |
| `WMAPOBJDEF` | 344 | 64 | `CMapObjDef` (skeleton) |
| `WCHUNKLIQUID` | 1092 | 64 | `CChunkLiquid` (skeleton) |
| `WDETAILDOODADINST` | 164 | 16 | **missing** |

`src/world/map/CMap.cpp` carried `WAREAMED` commented out with a `??`. It is real: 33,404 bytes a
piece, sixteen to a block. The block counts frozen already uses all match the reference.

## The rest of the function

Before the heaps it calls five subsystem initialisers (`FUN_007c3d90`, `FUN_007afee0`,
`FUN_007cb990`, `FUN_007b2760`, `FUN_007a03c0`), clears two 44-byte blocks and three large static
tables, and creates the low-detail index pool: `CMap::lowDetailIndexPool` at 0x1800 bytes with a
0xc00-entry index buffer. None of that is ported.
