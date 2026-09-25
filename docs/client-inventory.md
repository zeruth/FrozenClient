# Client inventory against the reference (3.3.5a 12340)

Taken 2026-09-24. This is a subsystem-level view of what frozen has, what is partial, and what is
missing, measured against the reference client. The function-level truth is
`docs/recomp/REPORT.md` (generated; the percentages quoted here are its 2026-09-24 08:13 run) and
the render pipeline stage list is `docs/world-render-inventory.md`. This file sits above both: it
groups the numbers by subsystem and adds the facts the report cannot see (a feature that exists but
is never called, a CVar that is registered but never read).

Vocabulary, used consistently below:

- **have** - present, called, and either linked to the reference or a stand-in that has been seen
  working on screen.
- **partial** - present but incomplete: a stand-in that does not make the reference's calls, a
  port with stubbed branches, or a feature that exists but is not wired to anything.
- **missing** - no frozen code for it, or a stub body.
- **(unverified)** - built, never seen running. Treat as suspect per CLAUDE.md.

## Headline numbers

| measure | reference | frozen | note |
|---|---:|---:|---|
| functions (non-thunk) | 27,156 | 3,232 linked (11.9%) | 2,489 ported, 689 stubs, 14 verified by a run |
| faithful (>= 80% of the call sequence) | | 1,294 (4.8%) | |
| render surface (modules that draw the world) | 4,589 | 292 linked (6.4%) | the thinnest area, and the standing priority |
| Lua bindings | 2,964 names | 2,924 registered | 1,493 of those are stubs, so ~1,430 real (48%) |
| packet handlers | 579 opcodes | 41 | everything else is silently dropped |
| CVars | 426 | 426 registered | at least ten graphics CVars are read nowhere |
| DBC files read | 241 | 60 | 55 record types in `src/db/rec` |
| `WHOA_UNIMPLEMENTED` bodies in `src/` | | 580 | 425 of them in `src/ui/game` |
| frozen functions with no reference link | | ~9,100 | includes vendor code; the world ones are the stand-ins |

Progress is logged one row per recomp run in the report's Iterations table.

## Subsystem inventory

### 1. Platform and boot

**Have.** Windows (D3D9) is the daily driver. Android builds through `android/gradlew`
(arm64-v8a, NDK 28, native activity, touch and key input, FMOD Core) and as of 2026-09-24 reaches
the same in-game state as desktop. Auto-login (`FROZEN_AUTO_LOGIN`) walks the glue into the world.
Linux and Mac app shells exist (`src/app/linux`, `src/app/mac`, GLL device for Mac).

**Partial.** Android is rendering the whole scene at full detail (see section 3 on LOD), so it is
slow by construction rather than by any one bug. The quit path still relies on skipping static
destructors (CLAUDE.md, static destruction order).

**Missing.** Warden (`WardenClient_Initialize` is a TODO in `ClientConnection.cpp`), executable
signature checks (`CheckExecutableSignature.cpp` 3.2%), patch download, launcher hand-off, billing
and token/PIN/matrix authentication (all `Script_*` stubs in `GlueScript.cpp`).

### 2. Gx: device, textures, shaders, fonts

**Have.** `CGxDevice` state machine with D3D9, GLES and GLL back ends; vertex and pixel shader
loading from the archives with the reference's profile fallback (`IShaderLoad`); BLP decode
(`blp.cpp` 22%); render-to-texture (2026-09-15); polygon offset on D3D; depth format, near/far,
viewport, cull and depth tables all confirmed bit-identical (CLAUDE.md "Settled"); FreeType font
rasterising and string batching; loading screen; window and monitor mode handling.

**Partial.** The D3D device port is thin against the reference (`CGxDeviceD3d9Ex.cpp` 15 of 238
functions, `CGxD3d9ExTexture.cpp` 4%); the 2026-09-23 audit found which empty state-sync bodies
matter and closed the ones that did. `Texture.cpp` 17%, `TextureCache.cpp` 7% (async BLP path,
`CreateBlpAsync` and `CreateTgaTexture` are stubs; `docs/ref/parity-texture-async.md`).
`TextureBlob.cpp` 15%. Font utility modules are mostly stand-ins (`GxuFontMiscClasses.cpp` 4%,
`GxuFontUtil.cpp` 3%); `SetFont`/`GetFont` deliberately skip the reference's NDC/DDC conversion.

**Missing.** Fifteen pixel-format blits in `src/gx/Blit.cpp` (DXT to RGB565/ARGB1555/4444, R32F,
D24X8, ...). Full-screen effects: `FFXEffects.cpp` 7%, `EffectGlow.cpp` 3%, and nothing in
`CGWorldFrame` calls them, so glow, death and netherworld effects never draw. Anisotropic
filtering is never set on D3D (`textureFilteringMode` registered, unread). Hardware occlusion
query (`occlusion` CVar) does nothing.

### 3. Draw distance, LOD and quality gating

This is the part that makes Android slow and it is almost entirely missing. Everything in the tile
window that passes a frustum test draws at full detail.

| mechanism | reference | frozen |
|---|---|---|
| far plane | `farclip` (fog end, doodad cutoff) x `horizonFarclipScale` (world extent), blended per area in `CWorld::Update` FUN_007831a0 | same two values since 2026-09-16 (`GetHorizonFarClip`); the per-area blend is a stand-in |
| tile window | min/max chunk coords from the frustum box, FUN_00795400 | fixed ring, `MAX_TILE_RADIUS = 3` (7x7 = 49 tiles) sized from the horizon distance |
| terrain chunk LOD | `CMap::lowDetailIndexPool` FUN_007ba600, low-detail visibility FUN_007cd850, WDL horizon mesh | none: every chunk at full 9x9/8x8 resolution; no WDL horizon |
| model distance bands | five bands at `DAT_00adf350` (near/far/fade per band, scaled by `environmentDetail`) decide whether a doodad draws and how it fades | the table exists as `s_detailBands` in `CWorld.cpp` and **nothing reads it**; doodads are frustum-sphere culled only (`Terrain.cpp:4728`), no distance cutoff, no fade |
| M2 geometry LOD | picks a skin profile (`00.skin` .. `03.skin`) by distance | `CM2Shared` always loads profile `numSkinProfiles - 1` (`CM2Shared.cpp:761`, marked "implement logic to select skin profile"); one fixed LOD for every model at every distance |
| WMO distance | per-def distance FUN_007b48c0 vs def `+0x1c0` | portal-gated plus frustum only |
| detail doodads | global instance pool sized by `groundEffectDensity` (1024..4096), `groundEffectDist` clamped to [0,140] | per-chunk scatter pre-built, a fraction drawn; both CVars polled per frame (works at defaults, wrong semantics above them) |
| occluders | `CWorldOccluder` screen-space polygons FUN_00796c10 | none |
| particles | thinned by `particleDensity` | CVar unread; a per-frame quad ceiling instead (`ParticleFx.cpp:25`) |
| shadows | `shadowLevel`, `shadowLOD` handler FUN_007e3a20, `mapShadows`, `extShadowQuality` | `shadowLevel` read only by the video options panel; the others registered, unread |
| textures | `baseMip`, `textureCacheSize`, `textureFilteringMode` | registered, unread |
| water | `waterLOD` | fixed to 0 |
| clouds | `skyCloudLOD` | registered, unread |
| weather | `weatherDensity` | read (budget scaling) |
| resolution, multisample, vsync, window | | handled |

`docs/world-render-inventory.md` (2026-09-23 entry "the graphics CVars") lists the ten CVars that
are read nowhere: `extShadowQuality`, `specular`, `baseMip`, `textureCacheSize`, `footstepBias`,
`violenceLevel`, `skyCloudLOD`, `terrainAlphaBitDepth`, `hwPCF`, `bspcache`.

### 4. Model (M2)

Reference modules: `M2Scene.cpp` 34% (95/283, the best-covered render module), `M2Shared.cpp` 15%,
`M2Cache` not implemented.

**Have.** Loading and `M2Init` offset patching, `.skin` profiles, `.anim` external sequences
(64-bit reach fixed 2026-09-24), bone animation and skinning, sequence lookup with fallback chain
(`GetSequenceInfo`), attachments, texture transforms, M2 lights (`CM2Light`, `CM2Lighting`),
the scene (`CM2Scene::AdvanceTime`/`Animate`/`Draw`, passes 0/1/2 with the under-liquid order
flip), batch sorting, shader-effect selection, character component textures (`src/component`).

**Partial.** Particle emitters: runtime ported end to end (`CM2ParticleEmitter`) but never wired
into the scene or run (memory `m2-particle-runtime-port`); the world-side `ParticleFx.cpp` is an
unlinked stand-in. `SubstituteSpecializedShaders`, `SetIndices`, `UnoptimizeVisibleGeometry` are
stubs. `SetBoneSequence` can leave `0xFFFF` because the variation resolver is a stub.

**Missing.** Ribbons (`CM2Ribbon` has three methods; nothing renders). `DrawBatchDoodad` and
`DrawBatchProj` (projected/decal batches, so M2 shadow receivers) are stubs. Model cache: every
request re-parses the file (memory `model-cache-not-implemented`, `docs/ref/parity-model-cache.md`).
Geometry LOD (section 3). M2 cameras are read but not driven (`CSimpleModel` uses its own).

### 5. Map and world rendering

Reference modules: `Map.cpp` 1.3% (3/237), `MapChunk.cpp` 18%, `MapChunkLiquid.cpp` 0%,
`MapMem.cpp` 1%, `MapLoad.cpp` 3%, `MapObjRead.cpp` 0%, `DetailDoodad.cpp` 0%, `WorldParam.cpp`
4%. Frozen's `src/world/Terrain.cpp` is one monolithic stand-in (`LoadTile`, `ParseChunk`,
`LoadWmoInstance`, `ParseLegacyLiquid`, `TerrainRender` are the largest unlinked functions in
the tree). The stage-by-stage status in the render inventory (43 counted stages): 6 ported, 27
stand-in, 10 missing as of the 2026-09-14 recount, with several of the missing ones built since.

**Have (stand-in, seen on screen).** ADT loading with MCCV vertex colour, MCSH baked shadow,
MCAL alpha layers, holes, MH2O and legacy MCLQ liquids, MDDF/MODF placements; WMO loading with
portals (MOPT/MOPR/MOPV), BSP (MOBN/MOBR, used for floor queries), doodad sets, MOCV, MLIQ; portal
walk from the camera group; WMO placement verified 106/106 (2026-09-15); local-space vertices with
per-chunk / per-instance matrices; terrain and WMO through the terrain vertex program; fog colour
and end; outdoor light from Light.dbc bands; sky gradient (verified 2026-09-15); sun and moon
discs; clouds; weather (rain/snow); underwater overlay; world text (player names); minimap
(`Minimap.cpp`, `docs/ref/parity-minimap.md`); overhead icons; liquid render with distance sort.

**Partial / (unverified).** Blob shadows through the receiver's own vertex program with a
depth-EQUAL test (built 2026-09-23, footprint shape still a circle rather than the oriented
rectangle, M2 receivers missing); map shadow map (`MapShadow.cpp`, stages S2/S3 partial, content
established but never captured); the seven-ring dome, six-band sky colours and sky highlight
(built 2026-09-23, needs a dawn/dusk run in a `highlightSky` zone); fog start (`fogStart = fogEnd
* band1`); detail doodads (works at default density, different pool architecture); liquids
(buckets 0/1 and WMO liquid carry known defects); outdoor colour: the reference blends a second
light frozen does not (thread stopped 2026-09-15, still open); terrain shaders are frozen's own
D3D9 bytecode rather than the archive's `Terrain.bls` set (`docs/ref/parity-map-memory.md`);
WMO material loader misses what the reference's does (2026-09-23 entry).

**Missing.** Footprints (`Map.cpp` FUN_0079fcc0; also blocked on movement); occluder polygons;
render buckets 0/1; barriers; FFX begin/end; ground marker; WDL low-resolution horizon; MFOG
(WMO fog volumes) and MOLT (WMO lights); MFBO flight bounds; MCSE sound emitters; `CMap::Update`
visibility lists (every stage re-culls with its own AABB tests); terrain chunk LOD.

### 6. Objects and gameplay

Reference modules: `Unit_C.cpp` 1.6% (11/705), `Player_C.cpp` 1.6%, `GameObject_C.cpp` 1.4%,
`Item_C.cpp` 0.9%, `ObjectMgrClient.cpp` 10%, `MovementShared.cpp` 1.2%, `SpellCast.cpp` 0%,
`Spell_C.cpp` 4%, `UnitCombatLog_C.cpp` 1%, `UnitMissileTrajectory_C.cpp` 0%, `Vehicle*` 0%,
`HealthBar.cpp` 2%, `NamePlateFrame.cpp` 0%, `ChatBubbleFrame.cpp` 0%, `DBCache.cpp` 15%.

**Have.** Object manager and update-field parsing (`SMSG_UPDATE_OBJECT`, compressed updates,
destroy); `CGObject`/`CGUnit`/`CGPlayer`/`CGItem`/`CGContainer`/`CGGameObject`/`CGCorpse`/
`CGDynamicObject` with display-info resolution and model attachment; unit visuals (equipment,
component textures, hair/skin geosets); mirror (`docs/ref/parity-mirror.md`); name, item, creature
and quest-status caches; spell book (`Rebuild` is a large stand-in); aura cache and `UnitAura`;
casting bar packets; action buttons; game time.

**Partial.** Item cache (`docs/ref/parity-itemcache.md`); party info (`docs/ref/parity-party.md`);
pet info; raid target icons; quest giver status; emotes.

**Missing.** **Unit movement**: `src/object/movement` totals 284 lines, no `MSG_MOVE_*` handler is
registered, no keyboard or click-to-move input exists (`MoveForwardStart` is a Lua stub), so the
player and every other unit stand still; this also blocks footprints and ribbons. Spell casting
(`SpellCast.cpp` 0%), spell visuals and missiles, combat log, melee/ranged attack, vehicles and
passengers, transports, taxi, loot, trade, mail, auction, guild, friends, who, channels, chat
receive (`SMSG_CHAT` unhandled), pet name cache, health bars and nameplates, chat bubbles, duels,
currency, equipment manager, talents, trade skills, achievements (bindings all stubbed), calendar
(all stubbed), LFG, battlefields (24 of 57 stubbed), dance studio, object effects.

### 7. Network

**Have.** SRP6 auth handshake (`src/net/srp`), realm list, `Grunt` client link, world session
(`WowConnection`, `ClientConnection`, `RealmConnection`), auth challenge/response, addon info,
char enum/create/delete/rename/customize/faction change, login verify, new world / transfer
pending, logout, time and speed, kick, notifications, realm split, tutorial flags. Non-blocking
sockets on Linux/Android.

**Partial.** 41 of 579 opcodes have handlers. `WowConnection.cpp` 2%, `Grunt.cpp` 3%,
`BattlenetLogin.cpp` 1%.

**Missing.** Every social, party, guild, chat, query (quest/gameobject/page text/pet name/guild
info), movement, loot, trade, mail, auction, battleground and vehicle opcode. Warden. Bot check.

### 8. UI framework (CSimple*)

Reference modules: `CSimpleFrameScript.cpp` 85% (the best-covered module in the client),
`ScriptEvents.cpp` 74%, `CSimpleHTML.cpp` 57%, `CScriptRegion.cpp` 54%, `XMLTree.cpp` 43%,
`CSimpleAnim.cpp` 36%, `CSimpleHyperlinkedFrame.cpp` 36%, `CSimpleMovieFrame.cpp` 36%,
`CSimpleRender.cpp` 22%, `CSimpleFrame.cpp` 18%, `CSimpleEditBox.cpp` 18%,
`CSimpleMessageScrollFrame.cpp` 11%, `CSimpleAnimScript.cpp` 5%.

**Have.** XML/Lua frame system end to end: frames, textures, font strings, buttons, edit boxes,
sliders, check boxes, status bars, scroll frames, scrolling message frames, HTML, hyperlinks,
animations, model frames (`CSimpleModel`, `CSimpleModelFFX`), movie frames (MPEG-4 via
`m4vh263dec`, soundtrack through FMOD), strata and levels, secure-frame taint tracking (partial),
FrameXML load with 8 nil-global errors left in the last measured round.

**Partial.** Widget methods are nearly complete (a handful of stubs per class: `HookScript`,
`SetGradient`, orientation/texture setters on slider/status bar/checkbox, `ClearFocus`,
`SetFontString`, five `ScrollingMessageFrame` names missing outright); `CFrameStrata::FrameOccluded`
and `CSimpleTop::CompressStrata` are stubs; cursor (`Cursor.cpp` 3%, `docs/ref/parity-cursor.md`);
addon loading (`AddOns.cpp` 1%: enable/disable/dependency bindings stubbed); macros (`UIMacros.cpp`
3%); key bindings (`UIBindings.cpp` 36%, spell/item/macro/click bindings stubbed).

### 9. Game UI bindings (the global function blocks)

By reference table, with stubs / entries from the report: FrameXML core 168/310, Calendar 93/95,
chat 73/89, LFG 56/67, quest log 52/67, misc client 50/113, gossip 48/59, Battle.net 47/57,
movement 47/52, battlefields 41/51, tooltip 39/69, commentator 35/35, trade skill 34/36, mail
33/38, `Unit*` 33/169, achievements 30/37, world map 27/40, guild roster 27/43, trainer 25/28,
guild bank 25/29, auction 24/30, pet actions 24/31, friends 23/31, camera 22/22, macros 19/22,
spells 18/45, merchant 18/21, inventory 18/33, equipment sets 15/17, bindings 14/26, loot 14/17,
GM tickets 14/15, sound 14/23, raid 13/20, taxi 13/14, factions 13/15, trade 12/14, containers
12/22, stables 11/14, sockets 11/12, combat log 10/11, voice 10/15. Two tables are absent
entirely: `KBSetup_*` (22 names, knowledge base) and `AccountMsg_*` (11).

Most of these stubs are honest: the data they would return has no source until the matching
packet handlers and caches in section 6 exist. `tools/stubtriage.py` and `tools/livestubs.py`
separate the stubs with live callers from the dead ones.

### 10. Glue (login, realm, character select and create)

`CGlueMgr.cpp` 50%, `CharacterCreation.cpp` 43%, `RealmList.cpp` 57%, `ScanDLLGlue.cpp` 6%.

**Have.** Login screen with auto-login, realm list and selection, character list, creation with
name generator and random names, deletion, character display models, login snow
(`docs/ref/parity-login-snow.md`), video options panel (resolution, multisample, some sliders).

**Missing.** Addon management panel, patch download, billing, ScanDLL, token/PIN/matrix,
`RequestRealmSplitInfo`, cursor show/hide, video defaults restore, changed-option warnings.

### 11. Sound

`SoundEngine.cpp` 4%, `SoundInterface2Internal.cpp` 11%, `SoundInterface2ZoneSounds.cpp` 0%,
`ComSatSoundIOSoundEngine.cpp` (voice) 6% by count but nothing real, `OggDecompress.cpp` 1%
(the reference decodes Vorbis itself; frozen lets FMOD do it, a legitimate divergence).

**Have.** FMOD Core on Windows and Android; `SESound` and `SI2` shells; sound kit properties;
`CreatureSoundData`, `SoundEntries`, `SoundEntriesAdvanced` records; cinematic soundtrack.

**Missing.** Zone music and ambience (`ZoneMusic`, `SoundAmbience`, `ZoneIntroMusicTable`,
`WorldStateZoneSounds` DBCs unread), footsteps and impacts (`FootstepTerrainLookup`,
`WeaponImpactSounds`, `TerrainTypeSounds`), UI sounds (`UISoundLookups`), `PlayMusic`,
`PlaySoundFile`, `StopMusic`, sound-system restart, all voice chat, sound CVar handlers (12 stubs).

### 12. Data: archives, async IO, DBC

**Have.** MPQ through StormLib, `SFile`, async file reads (`src/async`), all 426 CVars, the
console with command dispatch and device detection, 55 DBC record types covering characters,
creatures, items (display), lights, liquids, maps, spells (base, icon, visual kit), sounds,
ground effects, names.

**Missing.** 181 of the 241 DBCs the reference opens. The ones that block visible features:
`Faction`/`FactionTemplate` reputation, `Talent`/`TalentTab`, `TaxiNodes`/`TaxiPath`,
`WorldMapArea`/`Continent`/`Overlay`/`Transforms`, `AreaTrigger`, `AreaPOI`, `ChatChannels`,
`CinematicSequences`/`CinematicCamera`, `Movie*`, `ItemSet`, `ItemRandomProperties`/`Suffix`,
`SpellCastTimes`/`Duration`/`Radius`/`Missile`/`MissileMotion`/`ItemEnchantment`, all `gt*`
combat-rating tables, `Vehicle*`, `Transport*`, `Lock`/`LockType`, `Languages`/`LanguageWords`,
`EmotesText*`, `WMOAreaTable`, `FootprintTextures`, `TerrainType`, `LiquidMaterial`, `Material`,
`DungeonMap*`, `HelmetGeosetVisData`, `GameObjectArtKit`, `DestructibleModelData`,
`ScreenEffect`, `CameraShakes`. `DBClient.cpp` shows 0.3% because the reference's accessors are
templated per record and the matcher cannot anchor them.

### 13. Tooling and verification

**Have.** `tools/recomp` (map, report, queue, `--diff`, `--fix`, call tracer and trace compare),
`tools/scene-compare` (pixel parity, run by hand only), `tools/crashstack.py` (debugger attach),
`tools/watchwrite.py` (hardware write watchpoint), `tools/arity.py`/`arityfill.py`,
`stubfill.py`/`stubtriage.py`/`livestubs.py`, `audit-ported.py`, `luadump.py`, `memcompare.py`,
`drive.py`/`scene-run.py`/`relog-reference.py`, terrain carve/stitch, shader compile, MPQ probe.
Unit tests: 9 files (CVar, BLP, camera, font, status, SFile, game time).

**The verification gap is the biggest single number in this document.** 14 functions are
`verified` by a run. The render pipeline entries from 2026-09-23 (about 25 commits) have never
been launched (memory `unverified-work-owed-a-run`). The Android run on 2026-09-24 is the first
observation of most of them on any device, and it was not a comparison.

## What the numbers say to do

1. **Verification before more porting.** Every "built, unverified" item in sections 4 and 5 is
   cheaper to confirm than to re-derive later. One scene-compare run on the desktop and one
   dawn/dusk run in a `highlightSky` zone would settle most of the sky and shadow list.
2. **The world render is 6.4% linked and is where the player looks.** `Map.cpp`, `MapChunk.cpp`,
   `MapChunkLiquid.cpp`, `DetailDoodad.cpp`, `MapObjRead.cpp` are between 0% and 18%; the
   monolithic `Terrain.cpp` stand-in is what has to be replaced by their ports.
3. **LOD and distance gating is a prerequisite for Android, not a performance tweak.** The
   detail bands, skin-profile selection, chunk low-detail pool and the unread quality CVars are
   the reference's mechanisms, and all four are already identified (section 3). They should land
   as ports of those functions rather than as a new culling system.
4. **Movement is the gameplay blocker.** Nothing in section 6 past "stand still and look" can be
   evaluated until `MovementShared.cpp` and the `MSG_MOVE_*` handlers exist; footprints and ribbons
   wait on it too.
5. **Opcodes and DBCs gate the UI stubs.** The 1,493 Lua stubs are mostly downstream of the 538
   missing handlers and 181 missing DBCs; porting the caches in section 6 turns whole binding
   tables real at once.
