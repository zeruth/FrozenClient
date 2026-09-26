# Liquid parity: the draw stack

Queue item 4. The reader, creation, row insert, visit, camera liquid, `CreateSurface`,
`UpdateForFrame`, the geometry `Build`, the six materials with their shaders, and the two-bucket
queue and dispatch are all ported. What is left is the **material draw** — and this file is what it
needs, so the next cycle is transcription rather than rediscovery.

Everything here was read off the disassembly, not guessed. Where Ghidra's decompilation disagrees
with the disassembly the disassembly wins, and the four places it does are called out.

## Addresses that were wrong in the tree

These four were named in comments and in the task queue and are none of them what they were called.
Fixed in place, recorded here so they are not re-adopted from an old note:

| address | was called | actually is |
|---|---|---|
| `FUN_007d62a0` | the liquid queue | animates three randomly placed sprites around the camera (rand timers, `RotateAroundZ`, a fade) |
| `FUN_00795f80` | the two-bucket pass | draws map-object groups carrying group flag `0x8` through `FUN_007abac0`, in its own viewport |
| `FUN_007d4f10` | the MH2O parse | slot 2 of `CClientEnvironment`'s vtable, `*(this+4) = arg`; folded with `CMapLiquidData::SetBody`, which is why frozen's tag on it is not wrong |
| `FUN_008a20c0` | wakes the surface's sound | the queue itself |

The real queue is `FUN_008a20c0` and the real dispatch `FUN_008a2240`, both now ported.

## The chain, top to bottom

```
CWorldScene::TraverseRowLiquids            frustum + occlusion, then:
  CChunkLiquid::UpdateForFrame  7cf9a0     makes the surface the first time it is looked at
  Liquid::Add                   8a20c0     puts it in a bucket           [ported]
CMap::Render
  Liquid::Draw(cameraPos, 0)    8a2240     plain water and magma         [ported]
    CInstance::m_material->Draw(...)  vtable slot 2                      <-- THE GAP
  Liquid::Draw(cameraPos, 1)    8a2240     procedural water              [ported, diverged site]
```

### The bucket is not the caller's choice

Ghidra renders `FUN_008a20c0` as taking only the instance and drops the four instructions that
matter. From the disassembly at `0x008a20cd`:

```
movl 0x4(%esi), %edx      ; instance->m_settings
cmpb $0, 0x360(%edx)      ; settings->m_procedural
setne %al
shll $0x4, %eax           ; times the 0x10 bucket stride
addl %eax, %ecx           ; ecx = manager + bucket*0x10
```

So bucket 0 is plain water and magma, bucket 1 procedural water. That is the entire reason there are
two buckets, and why bucket 1 is drawn from a later pass than bucket 0.

### Draw's signature

`__thiscall Draw(const C3Vector* cameraPos, int bucket)`, the manager a global at `0x00cd8610`.
Both sites push `0x00cd8f5c` — the camera position, the same global `FUN_00795f80` negates for its
world `Translate`. Bucket 0 at `0x0079acf1` inside `CMap::Render`, right after the blob shadows;
bucket 1 at `0x00790aa2`. Both gated on bit 24 of `0x00cd774c`, which is the **liquid enable**: all
four of the reference's tests of that bit are on the liquid path (this pair, the row visit
`FUN_007935a0`, and the row insertion at `0x00799378`). Frozen names it `CWorld::Enable_Liquid`.

The comparator comes from a **two-entry** function-pointer table at `0x00b23f6c`
(`0x008a1980`, `0x008a19e0`) indexed by the bucket — which is the independent confirmation that
`BUCKET_COUNT` is 2. Both sort by material, then settings, then geometry, and differ only in the
direction of a fourth key: the float at instance `+0x50`, which is `m_placement.d3`, the matrix's
last element — and `CreateSurface` sets it to 1.0 on every surface. So the fourth key is inert and
the two buckets sort identically. Reproduced as-is; a distance sort would be an invention.

### The lights are saved, not disabled

`FUN_008a2240`'s first loop is four `CGxLight` default constructions (`FUN_00683fb0`, a reset that
writes exactly the values frozen's `CGxLight` carries as default member initialisers), **not** four
`LightEnable(i, 0)` calls. The save is `FUN_00682fd0` = `CGxDevice::LightGet`, now ported: bit 0 of
the returned flags is the slot's enabled bit and gates the rest of the copy, bit 1 is derived from
the stored `w` rather than stored. Only slots that were enabled come back at the end. Nothing is
disabled — the restore exists because the *materials* set lights while they draw.

## The material draw — `FUN_008a48f0`, 2173 bytes

The shader path. There is a second body, `FUN_008a5170`, for the fixed-function flavours: it drives
`CM2Lighting::SetupGxLights` / `SetupGxFog` and `XformSet` instead of shader constants, and asks
`Build` for vertex format **6** (`GxVBF_PNCT2`) where the shader path asks for **11**
(`GxVBF_PT2`) — the format `Build` was verified with.

### Arguments

Read off the dispatch call, seven values from the instance in this order:

```
Draw(m_environment, m_geometry, m_unk0c, cameraPos, &m_placement, &m_sphere, m_settings)
```

`m_material` is the implicit `this`. Frozen's `IMaterial::Draw` already carries this list.

### Shape

```
tex[0] = GetFrame(0, 1250)              six texture slots; 1250 ms for 0, 1, 4
tex[1] = GetFrame(1, 1250)              and m_int[slot-1] for 2, 3, 5
tex[2] = GetFrame(2, GetInt(1))         any slot returning null aborts the draw
tex[3] = GetFrame(3, GetInt(2))
tex[4] = GetFrame(4, 1250)
tex[5] = GetFrame(5, GetInt(3))
                                        eight stage floats scaled by FUN_008a1750's global,
                                        three more by the constant at 0x009ed910
GxRsPush()
needsMatrix = geometry->vtable[4](&placement)
geometry->Build(11, &vbuf, &ibuf, &batch)      vtable slot 2, already ported
GxPrimVertexPtr(vbuf, 11); PrimIndexPtr(ibuf)
  render states: 0x11<-0, 0xf<-0, 6<-2, 7<-table[0x00ad8b7c + state6*4], 0xc<-1
FUN_008a32f0(cameraPos, placement)             the transforms, below
FUN_00790440(&sphere)
environment->vtable[3](&lighting)              fills a stack CM2Lighting, below
FUN_008a38b0(&lighting)                        974 bytes: lighting -> shader constants
FUN_008a3c90(&lighting)                        266 bytes
  ... four-iteration loop, then the per-material pieces
FUN_008a3620 / FUN_008a3710 / FUN_008a3810
GxRsPop()
```

### `FUN_008a32f0(cameraPos, placement)` — the transforms

Ghidra drops every destination into ECX. From the disassembly:

```
view      = device->xformStack[device[0x1af8]]        at device + 0x1b00 + idx*0x40
local     = *placement
if (FUN_008a1740())                                  the global bool at 0x00d43af4
    local.d0 -= cam.x; local.d1 -= cam.y; local.d2 -= cam.z
worldView = local * view          -> stored at 0x00d44cf8
proj      = device[0xfc8]
if (!GxXformProjNativeTranspose()) negate proj's third row
mvp       = worldView * proj      -> stored at 0x00b24120
0x00b24170 = { cam.x, cam.y, cam.z, 1.0 }
```

That camera-relative rebase of the placement's translation row is the same one-cancellation-on-the-CPU
pattern CLAUDE.md prescribes, which is a good sign the terrain and WMO work matches the reference's
intent. `FUN_00407f80` is `C44Matrix::operator=`, a 16-dword copy — 247 callers, so it is worth
recognising on sight.

### The lighting block is a stack `CM2Lighting`

`local_1c4[164]` in the decompilation is an artifact of Ghidra splitting the local: the object is
`0xd4` = 212 bytes, `sizeof(CM2Lighting)`, and `FUN_008a38b0` reads `+0x54` (`m_sunAmbient`),
`+0x60`, and `+0x78..0x80` (`m_sunDir`) — the exact offsets frozen's own `CM2Lighting.hpp` already
records. `FUN_008a38b0` builds a `C33Matrix` from the view matrix and transforms `m_sunDir` into
view space, then packs the colours into the constant block at `0x00d44eb8` onwards.

This is the **same shape as `FUN_007d04a0`**, the reference's per-terrain-chunk lighting setup that
`CM2Lighting.cpp` already documents: a `CM2Lighting` on the stack, `CM2Scene::SelectLights` on it,
sun and fog from the DayNight block. So the liquid draw is not a new lighting path.

### `CClientEnvironment`

Created by `FUN_007d5120` through `CDataAllocator::GetData` by RTTI name; only three dwords are
initialised (vtable, refcount, arg). Vtable at `0x00a404e8`:

| slot | address | what |
|---|---|---|
| 0 | `0x008a32c0` | |
| 1 | `0x007d4f20` | release: virtual call then `CDataAllocator::PutData` |
| 2 | `0x007d4f10` | `*(this+4) = arg` — the indoor flag's setter |
| 3 | `0x007d4f40` | fill a `CM2Lighting` for this liquid |

Slot 3, 439 bytes, decoded in full:

```
block = FUN_007ecef0()                       the DayNight block, 0x00d38b00
if (this->m_indoor == 0)                     +0x04
    fogColor = block[0x8c..0x8e] * (1/255)   the 1/255 is at 0x00a45564
    fogArgs  = block[0x90], [0x94], [0x98]
else
    fogColor = block[0xa0..0xa2] * (1/255)
    fogArgs  = block[0xa4], [0xa8], [0xac]
lighting->SetFog(fogColor, fogArgs...)
if (this->m_arg == 0)                        +0x08
    light = mapLightBlock(0x00ce04a8) + 0x58
else
    light = a static CM2Light, configured once: type 0, directional,
            direction (0, 0, -1), white diffuse, visible
lighting->AddLight(light)
CM2Scene::SelectLights(lighting)
```

So `+0x04` is the indoor flag and `+0x08` chooses between the world's own light and a fixed
straight-down white one. Frozen's fields are still `m_unk04` / `m_unk08`.

## What frozen is missing before the draw can land

This list started at three and is down to one. Two of the three were not gaps at all, which is
worth recording because both looked real from the decompilation alone.

1. ~~**The map light block's `CM2Light`.**~~ Not a gap. `CMap::SetupChunkLighting` already stands
   that light in as `CWorld`'s outdoor ambient plus a directional term, so taking the same pair is
   the divergence terrain already took. `CClientEnvironment::SetupLighting` is ported on that basis.
2. **An indoor fog pair.** A real gap, and the only one left. Frozen keeps `CWorld::s_fogStart` /
   `s_fogEnd` computed rather than as a DayNight struct and has no indoor counterpart, so slot 3's
   indoor branch has no source and the third `SetFog` float — the fog density — has none either.
   The offsets are above. Recorded as a divergence at the call site.
3. ~~**The shader constant upload.**~~ Not a gap. `CGxDevice::ShaderConstantsSet(EGxShTarget,
   index, const float*, count)` already exists with the reference's exact signature, and frozen's
   `GxSh_Pixel = 4` matches the value the reference passes.

## The constant blocks

The upload is one function at `0x008a3da0`, two calls through the device vtable at `+0x118`:

```
ShaderConstantsSet(GxSh_Vertex, 0, 0x00d44ca8, 46)      the vertex block, 46 float4s
ShaderConstantsSet(GxSh_Pixel,  0, 0x00b24120,  6)      the pixel block, 6 float4s
```

So a vertex register is `(addr - 0x00d44ca8) / 16` and a pixel register `(addr - 0x00b24120) / 16`.
Everything the setup helpers write lands in one of these:

| address | reg | written by | what |
|---|---|---|---|
| `0x00b24120` | ps 0–3 | `FUN_008a32f0` | the MVP |
| `0x00b24170` | ps 5 | `FUN_008a32f0` | camera position, w = 1 |
| `0x00d44cf8` | vs 5 | `FUN_008a32f0` | world-view |
| `0x00d44d38` | vs 9 | draw body | texture matrix 0 |
| `0x00d44d78` | vs 13 | draw body | texture matrix 1 |
| `0x00d44db8` | vs 17 | draw body | texture matrix 2 |
| `0x00d44df8` | vs 21 | draw body | texture matrix 3 |
| `0x00d44e38` | vs 25 | draw body | scale, x 1 and y from stage float 8 |
| `0x00d44e78` | vs 29 | draw body | `RotationAroundZ(f10) * Scale(f9)` |
| `0x00d44eb8` | vs 33 | `FUN_008a38b0` | sun direction, in VIEW space |
| `0x00d44ec8` | vs 34 | `FUN_008a38b0` | sun ambient |
| `0x00d44ed8` | vs 35 | `FUN_008a38b0` | next colour |

`FUN_008a3c90` writes a separate block at `0x00d44c48`, which is *below* the uploaded vertex range
and so goes somewhere else: four float4s holding the NEGATED sun direction in world space, then the
ambient, diffuse and specular straight off the `CM2Lighting` (`+0x54`, `+0x60`, `+0x6c`), with the
constant at `0x009f22ec` in the last slot. `FUN_008a38b0` writes the same terms transformed into
view space; `FUN_008a3c90` writes them untransformed.

The four texture matrices come from the stage floats: each is
`RotationAroundZ(stage[i + 5]) * Scale(stage[i])`, where the first eight stage floats were scaled by
`FUN_008a1750`'s global and the rest by the constant at `0x009ed910`.

## The texture stages are not in slot order

`RsSet(0x15 + n, tex)` binds six textures, and `n` does not follow the slot:

| state | slot | period |
|---|---|---|
| `0x15` | 0 | 1250 ms |
| `0x16` | 1 | 1250 ms |
| `0x17` | 4 | 1250 ms |
| `0x18` | 5 | `GetInt(3)` |
| `0x19` | 2 | `GetInt(1)` |
| `0x1a` | 3 | `GetInt(2)` |

Any slot whose `GetFrame` returns null aborts the whole draw before `GxRsPush`.

## `m_unk0c` is a record provider

The draw's third argument, `CInstance`'s `+0x0c`, is asked for a pointer through its vtable slot 3
and a count through slot 4, and the draw walks that range **six dwords at a time**, substituting
zeroes when it runs off the end. So it supplies per-something animation records — most likely the
wave parameters — and the field frozen records as "refcounted, released through vtable slot 2" is
that provider. Nothing sets it yet.

`FUN_008a1750` returns the module float at `0x00b23f64`, which eight of the stage floats are
multiplied by. Its only writer is `0x008a1770` — which also holds the procedural shader name suffix
— and nothing frozen has found calls that, so the value's source is still open. Recorded `unlinked`
rather than guessed: porting the value without its writer would bake in whatever it happens to hold.

## Also open in item 4

`FUN_008a2f00` uploads two 64x8 liquid ramp textures once each.

## Map object liquid: water inside buildings

Only terrain liquid is queued, so water inside a WMO does not draw at all. Scoped 2026-09-26; the
groundwork landed in d467a678 and the rest is ~3,500 bytes across six functions.

**Done:** `Liquid::IGeomFactory`, the polymorphic base both factories sit on. The reference gives
each a four-slot vtable and stores either in `CInstance`'s `+0x08`:

| class | vtable | slot 0 | slot 1 release | slot 2 build | slot 3 |
|---|---|---|---|---|---|
| `CChunkGeomFactory` | `0x00a404c0` | `FUN_007d48c0` | `FUN_007d4760` | `FUN_007d4ab0` | `FUN_007d4390` |
| `CMeshGeomFactory` | `0x00a404d4` | `FUN_007d4980` | `FUN_007d43b0` | `FUN_007d43f0` | — |

**`Liquid::CMeshGeomFactory`**, allocated by `FUN_007d4920(0)` under the RTTI name
`.?AVCMeshGeomFactory@Liquid@@`, built by `FUN_007d49b0(mapObj, group)`. Field map read off the
allocator's zero-fill and the three setters:

| offset | set by | meaning |
|---|---|---|
| `+0x00` | allocator | vtable `0x00a404d4` |
| `+0x04` | allocator = 1 | refcount |
| `+0x08` | `FUN_007d49b0` | the root `CMapObj` |
| `+0x0c` | `FUN_007d49b0` | the `CMapObjGroup` |
| `+0x10` | — | the cached buffer holder, passed to `FUN_007cbdc0` |
| `+0x14` | `FUN_007d43e0` | texture id, default `0xffffffff`, from `m_materials[group->m_liquidMaterial]` at `+0x1c` via `FUN_007a6d70` |
| `+0x1c` | `FUN_007d4360` | `materialRec->m_LVF == 1` |
| `+0x20` | `FUN_007d4370` | fixed light, `1.0f` outdoors and `0.0f` indoors |
| `+0x28` | — | a block the two writers share |
| `+0x2c`, `+0x3c` | — | extra grid extents added to the group's vert counts |

**The build, `FUN_007d43f0`** (865 bytes). Fatals with
`Water in chunk "%s" of object "%s" has no materialId.` when
`group->m_liquidMaterial >= mapObj->m_materialCount`. Then the vertex count is
`(m_liquidYVerts + this->+0x3c) * (m_liquidXVerts + this->+0x2c) + FUN_007c8bf0() * 6`, indices
three times that, through `FUN_007cbdc0(format, verts, indices, &this->+0x10)`. The buffers come
off the GROUP, at `group->+0x10` and `group->+0x14`, each holding a `CGxBuf` at `+0x18`. It
early-returns the cached batch when both buffers report ready at `+0x1c`/`+0x1d`. Attribute offsets
come from `GxVertexAttribOffset` for attrs 0, 3, 4, 6 and 7, each gated on `FUN_00681260(format, n)`.

**The three writers:** `FUN_007a7cc0` (388), `FUN_007a7920` (462) and `FUN_007a7f60` (944). The
first was decoded 2026-09-26; the other two are still unread.

`FUN_007a7cc0(group, &matrix, this+0x14, this->+0x1c, this->+0x20, this+0x28, stride, &attr0,
&attr3, &attr4, &attr6, &attr8)` is the **vertex writer over the MLIQ grid**, and it returns the
vertex count the third writer is then handed:

```
step = _DAT_00a3fdc0                         // the tile step
y = m_liquidPos.y (+0x128)
for row in 0 .. m_liquidYVerts (+0x118):
    x = m_liquidPos.x (+0x124)
    for col in 0 .. m_liquidXVerts (+0x114):
        v = m_liquidVerts (+0x134) + (m_liquidXVerts * row + col) * 8
        height = *(float*)(v + 4)            // the 8-byte entry's height, as frozen already notes
        emit through FUN_007a7b00(group, &matrix, &pos, v, ...)   // NOT yet read
        x += step
    y += step
return (m_liquidXVerts + dupCols) * (dupRows + m_liquidYVerts)
```

**The `+0x28` block is a pair of duplicate-index lists**, and this is the part that would have been
guessed wrong. Relative to the `this+0x28` pointer the writer receives:

| offset | meaning |
|---|---|
| `+0x04` (`this+0x2c`) | how many COLUMNS are duplicated |
| `+0x08` (`this+0x30`) | the byte array of those column indices |
| `+0x14` (`this+0x3c`) | how many ROWS are duplicated |
| `+0x18` (`this+0x40`) | the byte array of those row indices |

When the current column or row index matches the next entry in its list, the vertex is emitted
**twice** -- a seam split. That is what the `+ this->+0x2c` / `+ this->+0x3c` terms in the build's
vertex count are for, which cross-checks the field map: the two independently agree.

**Still unknown for this piece:** `FUN_007a7b00` (the per-vertex emit), the two remaining writers,
and **who fills the duplicate lists** -- nothing in the build or `FUN_007d49b0` writes `+0x2c`/`+0x3c`,
so there is a prepare step elsewhere that has not been found. Do not port the writer until that is
located, or the mesh will be built with no seams where the reference splits them.

**The queue, `FUN_00793d20`** (679 bytes), called from `CMap::Render` at `0x0079acce`. Walks the
def-group list at `DAT_00cdb08c`/`DAT_00cdb094`, unlinking each through a link at `+0xb8`/`+0xbc`,
and for a def group with no surface yet at `+0x68` builds one exactly as `CreateSurface` does for
terrain, then calls `Liquid::Add`. Gated on `CWorld::s_enables & 0x100` and `DAT_00cd8610`.

Its indoor decision, which is what picks the environment and the fixed light:

```
indoor = !( (!(group->m_flags & 0x48) || (defGroup->m_flags & 2)) && !(typeRec->flags & 0x200) )
```

and when NOT indoor, a liquid type under 0x15 with `((type - 1) & 3) == 0` is remapped to `0x11`.
The surface takes `def->m_placement` (`FUN_00407f80`, from `def + 0x70`) and copies the def group's
own sphere from `+0x3c..+0x48` straight into `CInstance::m_sphere`.

**Still to identify:** `FUN_007c8bf0` (110) and `FUN_007cbdc0` (109), and who appends to the
`DAT_00cdb08c` list. **Frozen needs new fields** on `CMapObjDefGroup`: a liquid surface at `+0x68`
and a list link at `+0xb8`. MLIQ itself is already fully parsed -- `m_liquidXVerts`,
`m_liquidYVerts`, `m_liquidVerts`, `m_liquidTiles`, `m_liquidMaterial` (`+0x130`) and
`m_liquidType` (`+0x144`) -- and `mapObj + 0x160` is `m_materials`, so nothing in the group loader
needs changing.

## Divergences recorded in code

- Bucket 1 is drained at the end of `CMap::Render` rather than from the unported pass at
  `0x00790a80` (`SetupFogRenderStates`, this draw, `FUN_0079d5e0`, called twice from CGWorldFrame at
  `0x004f9170` / `0x004f91b0`). Leaving it undrawn would grow the bucket without bound with
  `m_queued` stuck on every procedural surface. With the material draw a stub the only difference
  today is ordering.
- `CChunkGeomFactory::Build` streams its buffer pair where the reference takes it from a pool keyed
  on exact byte sizes (`FUN_007cf140` / `FUN_007cefd0`). An allocation strategy, not behaviour.
