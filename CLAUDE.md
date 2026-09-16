# RunicWorld client (whoa fork)

A from-scratch reimplementation of the World of Warcraft 3.3.5a (build 12340) client in C++, forked
from [whoa](https://github.com/whoahq/whoa). `origin` is the user's fork (FrozenClient); work lands
on `develop` as scoped `type(subsystem)` commits per CONTRIBUTING.md. The long-term goal is an
Android port; the near-term goal is **rendering parity with the reference client**.

Read CONTRIBUTING.md before writing code: this is a decompilation project, so match the original's
function names, signatures, layouts and behaviour. When the original name is unknown, name by
behaviour. Do not import behaviour from other client versions.

## Build, install, run

```bash
cmake --build build --config Release --target Whoa      # build (VS 2026, x64)
cp build/bin/Release/Whoa.exe build/dist/bin/Whoa.exe   # install
cp build/bin/Release/Whoa.pdb build/dist/bin/Whoa.pdb   # ALWAYS install the PDB with it
```

Run `build/dist/bin/Whoa.exe` with the working directory set to the vanilla reference install:
`.reference/WOTLK 3.3.5a - Windows/WoW_WOTLK_3.3.5a`. It auto-logs in and enters the world.

Gotchas, each of which has cost a session before:
- `cmake --install` silently keeps a stale exe if Whoa is running, and exits 0. Kill it first and
  check the timestamp.
- **Install the PDB alongside the exe.** A stale PDB symbolizes crashes to the wrong function, which
  has sent debugging down the wrong path more than once.
- Only the Release build is trustworthy for visuals; RelWithDebInfo renders hair untextured.
- Lua errors go to stdout: redirect it to capture them.

### Servers

Local AzerothCore: `C:\Users\tyler\Documents\code\azerothcore-wotlk\build\bin\RelWithDebInfo`.
Start `authserver.exe` then `worldserver.exe` detached with that directory as the working directory
(its configs live in `configs/` beside the exe, and `Console.Enable = 0`, so do not attach a
console). Ready when ports 3724 (auth) and 8085 (world) are listening; worldserver takes ~15-20s. MySQL 9.6
runs as a service. Test account TEST/TEST; the scene-compare harness uses SCENE/SCENE.

## Working agreements

- **Never launch a game client unless the user asks in that moment.** The user plays on this
  machine, and the reference client takes the whole screen. This applies to the scene-compare
  harness too, which launches two clients in turn. It has a guard that refuses to run while
  `RunicWorldGame.exe` or `RunicWorldLauncher.exe` is up; keep it.
- **A clean build is not evidence.** Rendering work is not done until it has been seen running.
  Say plainly what has and has not been observed; do not describe unverified work as working.
- Prefer one change, one run, one observation over batching many unverified changes. A long run of
  unverified iterations on 2026-09-14 produced a client that crashed on the first launch.

## Debugging tools

### Crash triage: `tools/crashstack.py`

There is no cdb/WinDbg on this machine. This attaches as a real debugger, catches the first-chance
access violation, and prints the faulting instruction, the address it touched, the registers, and a
symbolized stack.

```bash
python tools/crashstack.py                 # launch under the debugger and wait
python tools/crashstack.py --attach <pid>  # attach to a running client
```

Read the registers, not just the top frame. Learned signatures:
- `0xBAADF00D...` in a register = **uninitialized** heap memory.
- `0xFEEEFEEE...` = **freed** heap memory (use-after-free, or static destruction order).
- "read of `0xFFFFFFFFFFFFFFFF`" usually means the address was **non-canonical** (e.g. a garbage
  pointer like `0xBAADF00D...`), not literally address -1.

The Windows event log gives only a fault offset, and symbolizing it alone points at a single line
that is often 20 lines from the real fault. To decode the faulting instruction exactly, dump the
bytes at that RVA from the exe and read them; that is what finally settled the appearance-table bug.

```bash
echo 0x<rva> | "/c/Program Files/LLVM/bin/llvm-symbolizer" --obj=build/dist/bin/Whoa.exe \
    --relative-address --demangle --functions=linkage
```

### Visual parity: `tools/scene-compare/`

Captures the reference client and Whoa at the same viewpoints and diffs them, producing a match
percentage plus side-by-side and heat-map images under `build/scene-compare/`. This is the only
objective check on rendering work. See its README for the safety guards and the known issue with
capturing the fullscreen reference client. **Run it by hand when the desktop is free**, never in a
loop.

### Reference binary (Ghidra)

Project `RunicWorld` at `C:\Users\tyler\tools\ghidra-projects`, program **RunicWorldGame.exe**
(Windows 3.3.5a 12340). **The Mac i386 build in that project has NO symbols** — do not plan around
it. Helpers in `C:\Users\tyler\tools`: `decomp.sh <out> <hexaddr>...`, `callers.sh`, `strrefs.sh`;
scripts in `ghidra-scripts`: CallTree, CallerTree, DataRefs, FindStringRefs2, ListSymbols,
VtableFromRtti. Each headless run takes 1-3 minutes, so batch addresses.

Raw dumps already captured live in `docs/ref/` with an `INDEX.txt`. **Read those before running
Ghidra again** — most of the pipeline has already been walked.

## Bug classes that have bitten this codebase

- **`M2Array` resolves its data as (its own address + offset).** Element 0 of an *empty* array is a
  wild pointer, not null. Always gate on `Count()` before indexing.
- **Model data is not ready when the pointer is set.** `CM2Shared::m_data` is assigned when the
  async read lands; `M2Init` patches the array offsets afterwards, and only then is
  `m_m2DataLoaded` set (and it stays 0 if init fails). Check `m_m2DataLoaded` — not just
  `m_data != nullptr` — before touching any model data. Same for `m_skinProfileLoaded`.
- **Tables built from sparse DBC data keep allocator garbage in the gaps.** The character
  appearance table is allocated with placement new over raw Storm memory and then filled only for
  the combinations the DBC carries; a slot struct without a default initializer returns non-null
  garbage that passes the caller's null check. Give such structs default initializers.
- **Static destruction order.** The client keeps ~20 caches and lists as namespace-scope Storm
  containers whose nodes live in Storm/ObjectAlloc heaps. Destroying them at process exit walks
  freed memory. The quit path should leave without running static destructors.
- **Unimplemented helpers leave sentinels in place.** `CM2Model::SetBoneSequence` resolves a
  sequence to `0xFFFF` when the model lacks it, and the variation-resolving helper that would clear
  that is still a stub — so callers must not assume the index is valid.

## Rendering parity: plan of action

The goal is 100% parity with the reference world render. The method is:

1. **`docs/world-render-inventory.md` is the checklist.** It maps the reference pipeline stage by
   stage (entry `CGWorldFrame::OnFrameRender` FUN_004fb080 -> `RenderWorld` FUN_004faf90 ->
   `OnWorldRender` FUN_004f8ea0 -> `CMap::Render` FUN_0079a870, then the M2 passes) with a status
   per stage and the reference function each stage should port. Keep its status column honest: a
   stage is not "ported" until it has been seen working.
2. **Per-area parity docs** hold the detailed task lists, each task naming the whoa function to
   change and the reference function it ports:
   - `docs/ref/parity-shadows.md` — entity blob shadows and the map shadow map.
   - `docs/ref/parity-depth.md` — depth buffer configuration and z-fighting.
   - `docs/ref/parity-sky.md` — DayNight, sky dome, clouds, glare, fog.
3. **Work one area at a time, and verify each with a run** before moving on. Use scene-compare for
   the visual check and crashstack when it faults.

### Shader assets

The terrain and decal shaders are compiled D3D9 bytecode embedded in
`src/world/TerrainShadersD3d9.hpp`. **The HLSL sources live in `src/world/shaders/`** — keep them
there and regenerate when changing a shader (the original terrain HLSL was lost, and the header had
to be disassembled with `fxc -dumpbin` to recover its interface):

```bash
FXC="/c/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/fxc.exe"
MSYS_NO_PATHCONV=1 "$FXC" -nologo -T ps_2_0 -E main -Fo out.cso src/world/shaders/blob_decal_ps.hlsl
```

`terrain_vs` emits `oPos` from constants c0-c3, `oT0` = layer UV, `oT1` = `position.xy * 0.2`, and
`oD0` = the baked vertex colour. Any pass that needs to be coplanar with terrain must use this
exact vertex shader with the same constants and vertex streams (see the blob shadow pass).

### Current priorities

Ordered by visible impact. Items marked **(built, unverified)** have been implemented but never
seen running — treat them as suspect until a run confirms them.

1. **Entity shadows / z-fighting — root cause found.** The reference does not use depth bias for
   coplanar decals: it re-draws the receiver's own triangles through the *same* vertex program and
   constants, so depth is bit-identical, then selects the visible surface with a depth-EQUAL test
   (`FUN_007e4480` sets `GxRs_DepthFunc = 1`). Depth bias would be the wrong tool, and is a no-op
   anyway: `CGxDeviceD3d` never handles `GxRs_PolygonOffset` (it only reports the `m_depthBias`
   cap; `D3DRS_DEPTHBIAS`/`D3DRS_SLOPESCALEDEPTHBIAS` are never set). The GL backend does drive
   `glPolygonOffset`, so this is a D3D-side gap worth closing on its own merits.
   **(built, unverified)** — the blob pass now binds `s_terrainVS` + `g_blobDecalPsD3d9` with
   identical vertex streams and an EQUAL test. Still open: `GxBlend_Mod` with the `ShadowAdd`/
   `ShadowMod` ramps from `ShadowInit`, animation-bbox footprints, doodad casters, and WMO/M2
   receivers. See `docs/ref/parity-shadows.md`.
2. **Sky and atmosphere — the sun direction is solved.** It is *not* a real arc and *not* from
   Light.dbc: `FUN_007eea90` holds the azimuth at a constant 225 degrees and wobbles the zenith
   angle between 127 and 110 degrees twice a day from a four-key band, storing a vector that points
   away from the light. Earlier hunts failed because DayNight `+0x30` is the **camera forward**
   vector, not the sun. **(built, unverified)** — direction, the seven-ring dome with the fog
   colour on its bottom two rings, and the corrected fog formulas
   (`fogEnd = min(farClip, band0)`, `fogStart = fogEnd * band1`) have all landed. Still open:
   clouds, the sun and moon discs, the sky highlight. See `docs/ref/parity-sky.md`.
3. **Vertex positions are absolute world coordinates.** The deepest parity break behind the depth
   problems: streams bake absolute world coordinates (up to ±17066) and cancel the camera
   translation inside the per-vertex `dp4`, leaving millimetres of view-dependent depth error. The
   reference keeps local vertex data and folds the origin into a per-chunk / per-instance matrix
   (`FUN_007d0050` -> `FUN_00790440`), so the cancellation happens once on the CPU.
   **Terrain is done (built, unverified):** `TerrainChunk` keeps `localPos` + `origin` beside the
   world `position` array, and `ChunkMatrixT` builds the per-chunk matrix. Every pass that draws
   chunk geometry MUST use that pair — the shaded terrain pass, blob shadows and detail doodads all
   do. `RenderFallback` deliberately still draws world-space (non-D3D9 path only).
   **WMO groups are done too (built, unverified):** `WmoInstance::origin` holds the placement
   position, group vertices are rebased onto it right after their world bounds are taken (bounds,
   culling, sorting and the liquid queries all stay world-space), and both WMO passes set a
   per-instance matrix. Shadows on WMO floors work as of 2026-09-15: WMO geometry now draws
   through the terrain vertex program too (its streams already matched), so one program covers
   terrain, detail doodads and buildings, and `BlobShadowDrawWmo` re-draws building floors with the
   same per-instance matrix and a depth-EQUAL test.
4. **Verify the 2026-09-14 batch.** Portals, liquids, weather, world text, detail doodads and
   particles all landed without ever being seen. The inventory flags five stages as wired-up but
   suspect, with concrete defects listed per row.

### Settled, do not re-investigate

- Depth format is D24S8 in both; near/far match the reference; the sky's `[0.9990234375, 1.0]`
  viewport is bit-identical to the reference constants and restores correctly; the depth-compare,
  cull and master-enable tables match bit-for-bit; M2 material depth flags match. None of these
  causes z-fighting.
- **The reference `farclip` CVar default is `350`, not 727.**
- Polygon offset is **decal-only** in the reference (footprints, post-liquid decals, projected
  decals); `CM2SceneRender` sets it to zero for models, so it was never a model z-fighting cause.
  It is now implemented in `CGxDeviceD3d` anyway: one state, negated, caps-gated, no slope-scale.
- The DayNight block's `+0x30` is the **camera forward** vector, not the sun. Two hunts died there.

### Known blockers

- ~~**Sun and moon discs.**~~ Solved and implemented 2026-09-15 (`SkyBodiesRender` in
  src/world/Terrain.cpp). Each body is a DIRECTION at a fixed radius of 12 from the camera, built
  from day-driven theta/phi bands; all tables are in `docs/ref/parity-sky-bodies.md`.
- ~~**Clouds.**~~ Implemented 2026-09-15 in `src/world/Clouds.cpp`: 4-octave 3D value noise with
  cosine interpolation, generated 8 rows per frame into one of two textures and flipped when a
  sheet completes, density ramp from LightFloatBand 3, drawn on the reference's 12-ring dome. The
  noise tables are seeded deterministically rather than lifted byte-for-byte (the reference seeds
  from `rand()`); the reference's static permutation table is in `docs/ref/parity-sky-bodies.md` if
  bit-exact parity is ever wanted. Cloud shading is simplified: base plus light weighted by density,
  without the reference's sun-direction highlight term.
- The sky layering now matches the reference: the scene clears to **black** under an open sky
  (colour only when under liquid), the sun/moon discs draw first, and the gradient dome is **added**
  over them, so the dome's colour IS the sky. Changed together on 2026-09-15 because the clear and
  the dome's blend only make sense as a pair. **Unverified on screen** — if the sky comes out black
  or blown out, this trio is the first place to look.


- ~~**Render-to-texture.**~~ Done 2026-09-15: `GxRenderTargetSet` / `CGxDevice::RenderTargetSet`
  with `CGxDeviceD3d::IRenderTargetSet` binding a texture's surface (colour to slot 0, depth as the
  depth-stencil) and restoring the captured default surfaces when passed null. Render-target
  *texture creation* already worked (`D3DUSAGE_RENDERTARGET` + `D3DPOOL_DEFAULT`); only the binding
  was missing. This unblocks the FFX glow and the map shadow map — both still need their own ports.
  Only the D3D backend implements the hook; the GL backends inherit a no-op.
- **Unit movement.** Not ported, so ribbons and footprints would draw nothing and cannot be
  evaluated yet.
