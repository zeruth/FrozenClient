#include "model/CM2Lighting.hpp"
#include "world/map/CMap.hpp"
#include "model/CM2Model.hpp"
#include "world/ParticleFx.hpp"
#include "model/M2Data.hpp"
#include "model/CM2Shared.hpp"
#include "world/map/CMapArea.hpp"
#include "world/map/CMapChunk.hpp"
#include "world/map/CMapObj.hpp"
#include "world/map/CMapObjDefGroup.hpp"
#include "world/map/CMapObjGroup.hpp"
#include "world/map/CMapObjDef.hpp"
#include "model/CM2Scene.hpp"
#include "world/CWorld.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>

// Turning a tile's placement records into map objects (reference MapLoad.cpp).

TSHashTable<CMapObjDef, HASHKEY_NONE> CMapObjDef::s_uniqueIds;

// Distance from the map's NW corner to its centre: the placement records are corner-relative
static const float MAP_HALF_EXTENT = 17066.666015625f;

static const float DEG2RAD = 0.0174532924f;
static const float PI = 3.14159274f;

// ref: FUN_007bf460
// One WMO placement. With dedup set, a building another tile already placed is handed back
// instead of placed twice. The record's axes are the ADT's: its x is the world's -y, its y the
// world's z, and its z the world's -x, all off the map's corner.
CMapObjDef* CMap::CreateMapObjDef(const char* name, const SMODF* modf, const C3Vector& origin, int32_t dedup) {
    if (dedup) {
        auto existing = CMapObjDef::s_uniqueIds.Ptr(modf->uniqueId, HASHKEY_NONE());

        if (existing) {
            return existing;
        }
    }

    auto def = CMap::AllocMapObjDef();

    if (dedup) {
        CMapObjDef::s_uniqueIds.Insert(def, modf->uniqueId, HASHKEY_NONE());
    }

    def->m_position.x = origin.x - modf->position.z;
    def->m_position.y = origin.y - modf->position.x;
    def->m_position.z = origin.z + modf->position.y;

    float rotZ = modf->rotation.z * DEG2RAD;
    float rotX = modf->rotation.x * DEG2RAD;
    float rotY = modf->rotation.y * DEG2RAD + PI;

    def->m_flags = 0;
    def->m_nameId = modf->nameId;
    def->m_doodadSet = modf->doodadSet;
    def->m_nameSet = modf->nameSet;
    def->m_unk138 = 0;

    C44Matrix placement;
    placement.d0 = def->m_position.x;
    placement.d1 = def->m_position.y;
    placement.d2 = def->m_position.z;

    placement.RotateAroundZ(rotY);
    placement.RotateAroundY(rotX);
    placement.RotateAroundX(rotZ);

    def->m_placement = placement;
    def->m_inversePlacement = placement.AffineInverse();

    def->m_bounds.b.x = origin.x - modf->extentsMax.z;
    def->m_bounds.b.y = origin.y - modf->extentsMax.x;
    def->m_bounds.b.z = origin.z + modf->extentsMin.y;
    def->m_bounds.t.x = origin.x - modf->extentsMin.z;
    def->m_bounds.t.y = origin.y - modf->extentsMin.x;
    def->m_bounds.t.z = origin.z + modf->extentsMax.y;

    def->m_center.x = (def->m_bounds.b.x + def->m_bounds.t.x) * 0.5f;
    def->m_center.y = (def->m_bounds.t.y + def->m_bounds.b.y) * 0.5f;
    def->m_center.z = (def->m_bounds.t.z + def->m_bounds.b.z) * 0.5f;

    float dx = def->m_bounds.t.x - def->m_center.x;
    float dy = def->m_bounds.t.y - def->m_center.y;
    float dz = def->m_bounds.t.z - def->m_center.z;
    def->m_radius = sqrtf(dz * dz + dy * dy + dx * dx);

    def->m_mapObj = CMapObj::Create(name);

    return def;
}

// ref: FUN_007c6150
// The chunk's MCRF references turned into map objects: the WMO placements first, then the
// doodads. Each def the chunk reaches gets a link the chunk holds and the def counts.
//
// Two parts of the reference are not here. A MODF flagged destructible (bit 0) goes to the game
// object system instead of the map, through a path that is not ported; and the doodad half needs
// CMapDoodadDef, which is the next subsystem. Both are marked where they belong.
void CMapChunk::CreateRefs(CMapArea* area, const uint32_t* refs, uint32_t doodadCount, uint32_t mapObjCount) {
    C3Vector origin = { MAP_HALF_EXTENT, MAP_HALF_EXTENT, 0.0f };
    auto mapObjRefs = refs + doodadCount;

    for (uint32_t i = 0; i < mapObjCount; i++) {
        auto modf = &reinterpret_cast<const SMODF*>(area->m_mapObjDefs)[mapObjRefs[i]];

        if (modf->flags & 0x1) {
            // TODO the destructible building: the reference checks the def table for the sentinel
            // uniqueId 0x5476ed and the list at DAT_00aeede8 for this one, then builds a game
            // object from the MODF extents (FUN_0077f290 -> FUN_0079eff0) and warns
            // "Destructible building WMO(%s) has invalid geobox" when they are degenerate
            continue;
        }

        auto name = area->m_mapObjNames + area->m_mapObjNameOffsets[modf->nameId];
        auto def = CMap::CreateMapObjDef(name, modf, origin, 1);

        auto link = CMap::AllocBaseObjLink(def);
        link->ref = this;
        this->m_mapObjDefLinkList.LinkToTail(link);
    }

    for (uint32_t i = 0; i < doodadCount; i++) {
        auto mddf = &reinterpret_cast<const SMDDF*>(area->m_doodadDefs)[refs[i]];

        auto name = area->m_doodadNames + area->m_doodadNameOffsets[mddf->nameId];
        auto def = CMap::CreateDoodadDef(name, mddf, origin);

        if (!def) {
            continue;
        }

        auto link = CMap::AllocBaseObjLink(def);
        link->ref = this;
        this->m_entityLinkList.LinkToTail(link);

        def->m_opacity = 1.0f;
        def->m_flags |= 0x4;

        // TODO FUN_007b4fa0: a doodad flagged as a building's own joins the map object def group
        // it stands in, so the interior light reaches it.
        //
        // Note for when the doodads are wired. A def leaves here with flags 5, or 0x805 when
        // it belongs to a building, and with no detail level. Both traversal walks test bit 7
        // before they will draw an entity, and the detail level decides how far away it
        // survives, so as things stand every doodad would take the other branch and none would
        // draw.
        //
        // Bit 7 is settled, and an earlier note here had it wrong: it IS OR'd in, by
        // FUN_007b5740, the static entity's placement update. That function sets bit 0, and
        // then, only once CM2Model::IsLoaded says the model has arrived, calls FUN_007bdb10 to
        // work out the entity's real bounds from the placed model and ORs bit 7 in beside it.
        // Until the model lands it takes the other branch, which puts approximate bounds on the
        // entity from the model's global box and leaves bit 7 clear.
        //
        // So bit 7 means "the model arrived and this entity has been placed", and it is not a
        // load-time flag at all -- nothing here can or should set it. Wiring the doodads means
        // porting that update: FUN_007b5740 (303 bytes) with FUN_007bdb10 (607), FUN_007c2f80
        // (100) and FUN_007b55e0 (66), driven from FUN_007b5630, which CMap::Update already
        // names as a TODO. The detail level comes from the same place.
    }
}

// ----------------------------------------------------------------------------------------------
// Streaming the buildings the camera can reach (reference Map.cpp)

// ref: FUN_007b48c0
// The squared distance from a point to a box: zero inside it, otherwise to the nearest face.
static float BoxDistanceSq(const CAaBox& box, const C3Vector& point) {
    float x = box.b.x <= point.x ? (point.x <= box.t.x ? point.x : box.t.x) : box.b.x;
    float y = box.b.y <= point.y ? (point.y <= box.t.y ? point.y : box.t.y) : box.b.y;
    float z = box.b.z <= point.z ? (point.z <= box.t.z ? point.z : box.t.z) : box.b.z;

    return (z - point.z) * (z - point.z) + (x - point.x) * (x - point.x) + (y - point.y) * (y - point.y);
}

// ref: FUN_007bde50
// One CMapObjDefGroup per group of the root, each holding the group's own box and sphere brought
// into world space by the def's placement, and flagged interior or exterior by its MOGI flags.
void CMap::CreateDefGroups(CMapObj* mapObj, CMapObjDef* def) {
    def->m_defGroups.SetCount(mapObj->m_groupCount);

    for (uint32_t i = 0; i < mapObj->m_groupCount; i++) {
        auto defGroup = CMap::AllocMapObjDefGroup();

        auto link = CMap::AllocBaseObjLink(defGroup);
        link->ref = def;
        def->m_defGroupLinkList.LinkToTail(link);

        def->m_defGroups[i] = defGroup;

        C3Vector center;
        float radius;
        mapObj->GroupBoundingSphere(i, &center, &radius);
        TransformPointInPlace(defGroup->m_center, center, def->m_placement);
        defGroup->m_radius = radius;

        CAaBox bounds;
        mapObj->GroupBounds(i, &bounds);
        defGroup->m_bounds = TransformBox(bounds, def->m_placement);

        defGroup->m_groupIndex = i;
        defGroup->m_ambientColor = def->m_ambientColor;
        defGroup->m_flags = 0;

        // MOGI bit 3 (indoor) or bit 6 (unreachable) makes the group interior; anything else is
        // outside weather and outdoor light
        if (mapObj->GroupFlags(i) & 0x48) {
            defGroup->m_flags |= 0x4;
        } else {
            defGroup->m_flags |= 0x2;
        }
    }
}

// ref: FUN_007b5d00
// The def's own setup, once its root has parsed: its world bounds from the root's, room for one
// light per MOLT entry, the root's ambient colour, and a group object per group.
// Every WMO doodad lives on the building's light, not the world's: the MODD colour is baked, so a
// prop in a blue-lit hall reads blue while the sun outside cycles past it.
static void MapObjDoodadLighting(CM2Model* model, CM2Lighting* lighting, void* arg) {
    auto ambient = static_cast<const C3Vector*>(arg);

    lighting->AddAmbient(ambient ? *ambient : CWorld::GetOutdoorAmbient());
}

// The building's props: one CM2Model per MODD placement in this def's doodad set.
//
// The set choice follows the reference's data: MODS entry 0 is always placed, and the def's own
// m_doodadSet adds a second range when it names a different one. A root with no MODS has no sets, so
// every MODD entry belongs to it.
//
// DIVERGED, and worth knowing before trusting the lighting: the reference refines a building's
// fallback ambient with the average of its interior MOCV, which needs every group file read up front.
// This uses the MOHD ambient alone, so a prop whose MODD colour is unset (all zero) in a building
// with dark MOHD ambient will read darker here than in the reference. Props WITH a MODD colour --
// which is most of them -- are unaffected.
// ref: FUN_007bf740
// The doodads of one placed map object: walk the chosen doodad set and build each one.
//
// The DATA LAYOUT here diverges from the reference and that is worth knowing before comparing
// them. The reference builds a DoodadDef per doodad (CMap::AllocDoodadDef inside FUN_007bef40)
// carrying a full placement matrix and a split floor light, and threads each onto the instance
// through a base-object link; frozen keeps three parallel arrays on the def instead -- the model,
// its scale and its ambient colour. Same doodads on screen, different bookkeeping, so the
// reference's per-doodad half (FUN_007bef40) has no counterpart here at all.
void CMap::CreateMapObjDoodads(CMapObjDef* def, CMapObj* mapObj) {
    auto scene = CWorld::GetM2Scene();

    if (!scene || !mapObj->m_modn || !mapObj->m_modd || !mapObj->m_doodadDefCount) {
        return;
    }

    struct Range { uint32_t start; uint32_t end; };
    Range ranges[2];
    uint32_t rangeCount = 0;

    if (mapObj->m_mods && mapObj->m_doodadSetCount) {
        // A MODS entry is 0x20 bytes: a 20-byte name, then firstDoodad and count.
        auto setAt = [mapObj](uint32_t index) {
            const uint8_t* e = mapObj->m_mods + index * 0x20;

            return Range{
                *reinterpret_cast<const uint32_t*>(e + 0x14),
                *reinterpret_cast<const uint32_t*>(e + 0x14)
                    + *reinterpret_cast<const uint32_t*>(e + 0x18)
            };
        };

        ranges[rangeCount++] = setAt(0);

        if (def->m_doodadSet && def->m_doodadSet < mapObj->m_doodadSetCount) {
            Range chosen = setAt(def->m_doodadSet);

            if (chosen.end > chosen.start && chosen.start != ranges[0].start) {
                ranges[rangeCount++] = chosen;
            }
        }
    } else {
        ranges[rangeCount++] = Range{ 0, mapObj->m_doodadDefCount };
    }

    uint32_t capacity = mapObj->m_doodadDefCount;

    def->m_doodads = static_cast<CM2Model**>(
        SMemAlloc(capacity * sizeof(CM2Model*), __FILE__, __LINE__, SMEM_FLAG_ZEROMEMORY));
    def->m_doodadScale = static_cast<float*>(
        SMemAlloc(capacity * sizeof(float), __FILE__, __LINE__, 0));
    def->m_doodadAmbient = static_cast<C3Vector*>(
        SMemAlloc(capacity * sizeof(C3Vector), __FILE__, __LINE__, 0));
    def->m_doodadCount = 0;

    if (!def->m_doodads || !def->m_doodadScale || !def->m_doodadAmbient) {
        return;
    }

    C3Vector mohdAmbient = {
        mapObj->m_ambientColor.r / 255.0f,
        mapObj->m_ambientColor.g / 255.0f,
        mapObj->m_ambientColor.b / 255.0f
    };

    for (uint32_t r = 0; r < rangeCount; r++) {
        uint32_t end = ranges[r].end > mapObj->m_doodadDefCount
            ? mapObj->m_doodadDefCount
            : ranges[r].end;

        for (uint32_t i = ranges[r].start; i < end; i++) {
            // A MODD entry is 40 bytes: a 24-bit name offset, position, an orientation quaternion,
            // a scale and a BGRA colour.
            const uint8_t* e = mapObj->m_modd + i * 40;

            uint32_t nameOffset = *reinterpret_cast<const uint32_t*>(e + 0) & 0xFFFFFF;

            if (nameOffset >= mapObj->m_modnSize) {
                continue;
            }

            C3Vector local = {
                *reinterpret_cast<const float*>(e + 4),
                *reinterpret_cast<const float*>(e + 8),
                *reinterpret_cast<const float*>(e + 12)
            };

            C4Quaternion rotation(
                *reinterpret_cast<const float*>(e + 16),
                *reinterpret_cast<const float*>(e + 20),
                *reinterpret_cast<const float*>(e + 24),
                *reinterpret_cast<const float*>(e + 28)
            );

            float scale = *reinterpret_cast<const float*>(e + 32);

            if (scale <= 0.0f) {
                scale = 1.0f;
            }

            auto model = scene->CreateModel(mapObj->m_modn + nameOffset, 0);

            if (!model) {
                continue;
            }

            // An all-zero MODD colour means "unset": fall back to the building's ambient.
            bool hasColor = (e[36] | e[37] | e[38]) != 0;

            def->m_doodadAmbient[def->m_doodadCount] = hasColor
                ? C3Vector{ e[38] / 255.0f, e[37] / 255.0f, e[36] / 255.0f }
                : mohdAmbient;

            model->SetLightingCallback(&MapObjDoodadLighting,
                                       &def->m_doodadAmbient[def->m_doodadCount]);

            // The doodad's own rotation and scale in the building's space, then the whole thing
            // through the def's placement. That is strictly better than the stand-in this replaces,
            // which applied only the placement's yaw and so mis-oriented any building with pitch
            // or roll.
            C44Matrix placement;
            placement.Identity();
            placement.Rotate(rotation);
            placement.Scale(scale);
            placement.d0 = local.x;
            placement.d1 = local.y;
            placement.d2 = local.z;

            model->matrixB4 = placement * def->m_placement;
            model->m_flag8000 = 1;
            model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
            model->SetAnimating(1);
            model->SetVisible(1);
            model->m_flag10000 = 1;

            def->m_doodadScale[def->m_doodadCount] = scale;
            def->m_doodads[def->m_doodadCount] = model;
            def->m_doodadCount++;
        }
    }
}

// Hand every placed building's props to `fn`, for the frame's particle-emitter gather.
void CMap::ForEachMapObjDoodad(void (*fn)(CM2Model* model, void* arg), void* arg) {
    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        for (uint32_t i = 0; i < def->m_doodadCount; i++) {
            if (def->m_doodads[i]) {
                fn(def->m_doodads[i], arg);
            }
        }
    }
}

// A doodad's cull radius: the model's own bounding radius when that is larger than the default,
// so big props (trees) are not culled while their canopy is still on screen.
static float DoodadCullRadius(CM2Model* m, float scale) {
    // The reference culls a doodad by its own bounding sphere (model radius x placement scale), like
    // units; a small floor only guards degenerate/zero bounds. A blanket 40 yd floor here just kept
    // off-screen props in the draw list.
    float r = 2.0f;

    if (m && m->m_shared && m->m_shared->m_m2DataLoaded && m->m_shared->m_data) {
        float sr = m->m_shared->m_data->bounds.radius * scale;

        if (sr > r) {
            r = sr;
        }
    }

    // A prop with emitters reaches past its mesh: a brazier's sphere is its bowl, not its flames.
    return r + ParticleFxCullExtent(m, scale);
}

// The world-space centre of a doodad's bounding sphere. The sphere is centred on the mesh (often
// well above the feet the doodad is placed by), so offset the feet position by the model-space box
// centre rotated by the placement yaw and scaled -- otherwise a tall prop is culled the moment its
// base leaves the screen while its body is still in view.
static C3Vector DoodadCullCenter(CM2Model* m) {
    if (!m) {
        return { 0.0f, 0.0f, 0.0f };
    }

    // Transform the model-space bounding-box centre by the doodad's full placement matrix (rotation,
    // scale and translation baked in). This is exact for tilted props, not just yaw-rotated ones, and
    // needs no separately stored feet/yaw. matrixB4 is row-major with the translation in d0..d2.
    if (m->m_shared && m->m_shared->m_m2DataLoaded && m->m_shared->m_data) {
        const CAaBox& e = m->m_shared->m_data->bounds.extent;
        float lx = (e.b.x + e.t.x) * 0.5f;
        float ly = (e.b.y + e.t.y) * 0.5f;
        float lz = (e.b.z + e.t.z) * 0.5f;
        const C44Matrix& M = m->matrixB4;
        return {
            lx * M.a0 + ly * M.b0 + lz * M.c0 + M.d0,
            lx * M.a1 + ly * M.b1 + lz * M.c1 + M.d1,
            lx * M.a2 + ly * M.b2 + lz * M.c2 + M.d2
        };
    }

    return { m->matrixB4.d0, m->matrixB4.d1, m->matrixB4.d2 };
}

// Frustum-cull the buildings' props: only those in view animate and draw, which spares the scene
// thousands of out-of-view models. Runs on the scene's own frustum -- item 11's "TerrainSphereVisible
// to the scene frustum", now that the stand-in's private copy of one is gone.
void CMap::CullMapObjDoodads() {
    CMap::ForEachMapObjDoodad([](CM2Model* model, void*) {
        // DoodadCullRadius scales a MODEL-SPACE radius, so it needs the placement scale, and that
        // lives in the model's matrix. Recover it as the length of the matrix's first row, which is
        // exact because the matrix is rotation times a uniform scale.
        const C44Matrix& m = model->matrixB4;
        float scale = sqrtf(m.a0 * m.a0 + m.a1 * m.a1 + m.a2 * m.a2);

        if (scale <= 0.0f) {
            scale = 1.0f;
        }

        C3Vector center = DoodadCullCenter(model);
        float radius = DoodadCullRadius(model, scale);
        int32_t visible = CWorldScene::SphereOutsideFrustum(center, radius) ? 0 : 1;

        model->SetVisible(visible);
        model->SetAnimating(visible);
    }, nullptr);
}

void CMap::SetupMapObjDef(CMapObjDef* def, CMapObj* mapObj) {
    def->m_flags |= 0x80;

    C3Vector center;
    float radius;
    mapObj->BoundingSphere(&center, &radius);
    TransformPointInPlace(def->m_center, center, def->m_placement);
    def->m_radius = radius;

    CAaBox bounds;
    mapObj->Bounds(&bounds);
    def->m_bounds = TransformBox(bounds, def->m_placement);

    def->m_ambientColor = mapObj->m_ambientColor;

    def->m_lights.SetCount(mapObj->m_lightCount);

    for (uint32_t i = 0; i < mapObj->m_lightCount; i++) {
        def->m_lights[i] = nullptr;
    }

    CMap::CreateDefGroups(mapObj, def);
    CMap::CreateMapObjDoodads(def, mapObj);
}

// ref: FUN_007b6110
// Every placed building, once per map update. A def whose box reaches the world's far box gets
// its root waited for and set up; each of its groups that reaches the far box starts its own
// file read, and one that reaches the near box is waited for rather than left to arrive.
//
// Not ported, and marked where they belong: the doodads and lights a loaded group places
// (FUN_007bdd70, FUN_007bf740, FUN_007c1f20), and the collision and portal work behind the
// def's own 0x80000000 flag.
void CMap::UpdateMapObjDefs(int32_t update) {
    int32_t canWait = CMap::s_streamingMode == 0 && CMap::s_loading == 0;

    for (auto def = CMapObjDef::s_uniqueIds.Head(); def; def = CMapObjDef::s_uniqueIds.Next(def)) {
        auto mapObj = def->m_mapObj;

        if (CWorld::s_farBox.Intersects(def->m_bounds)) {
            if (canWait && !mapObj->m_rootLoaded) {
                mapObj->WaitForRoot();
            }

            if (!(def->m_flags & 0x80) && mapObj->m_rootLoaded) {
                CMap::SetupMapObjDef(def, mapObj);
            }
        }

        float distance = BoxDistanceSq(def->m_bounds, CWorld::s_targetPos);

        if (distance < mapObj->m_nearestDistanceSq) {
            mapObj->m_nearestDistanceSq = distance;
        }

        for (auto link = def->m_defGroupLinkList.Head(); link; link = def->m_defGroupLinkList.Next(link)) {
            auto defGroup = static_cast<CMapObjDefGroup*>(link->owner);
            auto group = mapObj->GetGroup(defGroup->m_groupIndex, 1);

            if (group && CWorld::s_farBox.Intersects(defGroup->m_bounds)) {
                if (!(group->m_state & 0x1)) {
                    if (!group->m_asyncObject) {
                        mapObj->ReadGroup(defGroup->m_groupIndex);
                    }

                    if (canWait && CWorld::s_nearBox.Intersects(defGroup->m_bounds)) {
                        mapObj->WaitForGroup(defGroup->m_groupIndex);
                    }
                }

                group->m_unk190 = 0;

                if (group->m_state & 0x1) {
                    if (!(defGroup->m_flags & 0x10)) {
                        // TODO FUN_007bdd70(mapObj, group, def, defGroup): the group's doodads
                    }

                    if (!(defGroup->m_flags & 0x8)) {
                        // TODO FUN_007bf740(mapObj, group, def, defGroup): the group's lights,
                        // then FUN_007c1f20(defGroup->m_bounds)
                    }
                }
            }

            if (group) {
                float groupDistance = BoxDistanceSq(defGroup->m_bounds, CWorld::s_targetPos);

                if (groupDistance < group->m_nearestDistanceSq) {
                    group->m_nearestDistanceSq = groupDistance;
                }
            }

            // A set-up def hands each of its groups to the frame, once the group's file is in
            // and the group reaches what the frustum covers
            if (update && (def->m_flags & 0x80)) {
                if (!group || !(group->m_state & 0x1)) {
                    // TODO FUN_00794ad0(defGroup, FLT_MAX): the group leaves the collision grid
                    // while its file is still coming
                } else if (!(def->m_flags & 0x20)) {

                    if (CWorldScene::s_frustumBounds.Intersects(defGroup->m_bounds)) {
                        CWorldScene::BucketMapObjDefGroup(def, defGroup);
                    }

                    // TODO the group's portals, walked from here into the neighbours they open

                    // TODO two more occlusion feeds the reference runs per visible group, both
                    // needing a list frozen's CMapObjGroup does not carry:
                    //
                    //   FUN_00794ad0(defGroup, 50.0f) submits the GROUP's own box as an occluder,
                    //   through SubmitOccluderBox again plus FUN_00791eb0.
                    //
                    //   Then it walks the group's occluder-EDGE list at +0x1b0 -- each record holds
                    //   two points at +0x04 and +0x10 and links on at +0x20 -- brings both points
                    //   out by the def's placement matrix, and hands each edge to the horizon
                    //   clipper FUN_007927e0. That clipper cuts an edge at the 33.33-yard distance
                    //   row boundaries and links one CWorldOccluder per row; see
                    //   MapHorizonTable.hpp, which describes it for the fixed occluders.
                    //
                    // frozen has nothing at +0x1b0 (its m_link is at +0x1b4), so the edges are not
                    // read at load either.
                }
            }
        }

        // Every building near enough becomes an occluder, whether or not it is inside the far box
        // above -- the reference makes this call outside that test. FUN_007946d0 is
        // CWorldScene::SubmitOccluderBox, which this TODO named correctly and then described
        // wrongly as a collision grid; and the distance is the 50.0 at 0x009f22ec, not FLT_MAX, so
        // only buildings within fifty yards are offered.
        //
        // It costs almost nothing today because SubmitOccluderBox builds its five faces and then
        // stops at its own TODO, the volume submission FUN_00792360. Wiring it now is what makes
        // that TODO the only thing between here and buildings occluding.
        if (!(def->m_flags & 0x80)) {
            CWorldScene::SubmitOccluderBox(def->m_bounds, 50.0f);
        }
    }
}

TSHashTable<CMapDoodadDef, HASHKEY_NONE> CMap::s_doodadUniqueIds;

// ref: FUN_007becd0
// One doodad placed. The tile records it in its own axes; the world turns that round, scales it
// from the thousand-and-twenty-fourths the file counts in, and turns the three Euler angles into
// the matrix its model draws through.
CMapDoodadDef* CMap::CreateDoodadDef(const char* name, const SMDDF* mddf, const C3Vector& origin) {
    // Already placed by the neighbouring tile.
    auto existing = CMap::s_doodadUniqueIds.Ptr(mddf->uniqueId, HASHKEY_NONE());

    if (existing) {
        return existing;
    }

    auto def = CMap::AllocDoodadDef();

    if (!def) {
        return nullptr;
    }

    CMap::s_doodadUniqueIds.Insert(def, mddf->uniqueId, HASHKEY_NONE());

    def->m_position.x = origin.x - mddf->position.z;
    def->m_position.y = origin.y - mddf->position.x;
    def->m_position.z = origin.z + mddf->position.y;

    // Nothing knows how far it reaches until its model arrives, so both start on the point.
    def->m_sphere.c = def->m_position;
    def->m_sphere.r = 0.0f;
    def->m_bounds.b = def->m_position;
    def->m_bounds.t = def->m_position;

    def->m_scale = mddf->scale * (1.0f / 1024.0f);

    def->m_flags = 0x1;

    if (mddf->flags & 0x1) {
        def->m_flags = 0x801;
    }

    def->m_model = nullptr;

    const float DEG_TO_RAD = 0.017453292f;
    const float PI = 3.14159274f;

    def->m_placement.Identity();
    def->m_placement.Translate(def->m_position);
    def->m_placement.RotateAroundZ(mddf->rotation.y * DEG_TO_RAD + PI);
    def->m_placement.RotateAroundY(mddf->rotation.x * DEG_TO_RAD);
    def->m_placement.RotateAroundX(mddf->rotation.z * DEG_TO_RAD);
    def->m_placement.Scale(def->m_scale);

    def->m_inversePlacement.Identity();

    // The model. It starts neither visible nor animating: the traversal decides both, every
    // frame, from where the doodad is and how far off. Until CMap::UpdatePendingEntities has
    // placed it, nothing will draw it at all.
    auto scene = CWorld::GetM2Scene();

    def->m_model = scene ? scene->CreateModel(name, 0) : nullptr;

    if (def->m_model) {
        def->m_model->SetLightingCallback(&CWorld::LightingCallback, nullptr);

        // The placement goes to the model whole. SetWorldTransform would rebuild it from a
        // position, one angle and a scale, which is the stand-in's shape and throws away two of
        // the three rotations the file carries.
        def->m_model->matrixB4 = def->m_placement;
        def->m_model->m_flag8000 = 1;

        def->m_model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 0, 1);
    }

    // TODO the reference also registers the doodad's sound-event callback (FUN_007bd5a0) here.

    // It waits for its model, however long that takes. Placement is what gives it its real
    // bounds and its detail band, and what sets the bit the traversal tests.
    CMap::s_pendingEntityList.LinkToTail(def);

    return def;
}
