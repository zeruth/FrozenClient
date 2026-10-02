#ifndef WORLD_MAP_C_MAP_OBJ_HPP
#define WORLD_MAP_C_MAP_OBJ_HPP

#include "gx/Texture.hpp"
#include "world/map/CMapObjGroup.hpp"
#include <storm/Array.hpp>
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Segment.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CAsyncObject;
class CMapObjDef;
class CMapObjDefGroup;
class CM2Lighting;
class CShaderEffect;

// MOHD: the WMO root header, 64 bytes, the first chunk after MVER.
struct SMOHeader {
    uint32_t nTextures;         // +0x00
    uint32_t nGroups;           // +0x04
    uint32_t nPortals;          // +0x08
    uint32_t nLights;           // +0x0c
    uint32_t nDoodadNames;      // +0x10
    uint32_t nDoodadDefs;       // +0x14
    uint32_t nDoodadSets;       // +0x18
    CImVector ambColor;         // +0x1c
    uint32_t wmoID;             // +0x20
    CAaBox bounds;              // +0x24
    uint16_t flags;             // +0x3c: bit 0 attenuates vertex colour, bit 2 keeps the unshifted
                                //   liquid type, bit 3 skips the MOCV fixup
    uint16_t numLod;            // +0x3e
};

static_assert(sizeof(SMOHeader) == 0x40, "SMOHeader is 64 bytes");

// MOGI: one group's entry in the root, 32 bytes. The root keeps these so it can cull and place a
// group before its own file has arrived.
struct SMOGroupInfo {
    uint32_t flags;             // +0x00: the group's MOGP flags; bit 18 (0x40000) is the skybox flag
    CAaBox bounds;              // +0x04
    int32_t nameOffset;         // +0x1c: into MOGN, or -1
};

static_assert(sizeof(SMOGroupInfo) == 0x20, "SMOGroupInfo is 32 bytes");

// Does the segment touch the box? Woo's candidate-plane test: pick, per axis, whichever face the
// start point is outside of, take the furthest of the three entry distances, and check that the
// point at that distance is inside the box on the other two axes.
//
// The reference keeps it in a module frozen has not identified, but all seven of its callers are
// map object code, so it lives here. ref: FUN_007f9480
int32_t SegmentIntersectsBox(const CAaBox& box, const C3Vector& start, const C3Vector& end);

// Which buildings, and which room of each, a segment passes into. This is what tells the client it
// is indoors: the camera drops a short segment straight down, and whatever group it lands in is the
// room it is standing in.
//
// Reports up to TWO results, and the slot is chosen by the def's own flag 0x400 rather than by
// order or distance, so a flagged building never displaces an ordinary one. `outDefs` takes two
// defs and `outGroups` four group indices -- per slot, the group hit and a second group when the
// hit came through a portal -- with 0xffff meaning none. Returns whether anything was found, and
// when only the flagged slot was filled it is moved down to slot 0 first, so a caller that wants
// one answer can just read slot 0.
//
// A hit on an EXTERIOR group (MOGP flag 0x8) discards that slot: standing on a building's outside
// surface is not being inside it. ref: FUN_007d59b0
int32_t QuerySegmentMapObjs(const C3Vector& start, const C3Vector& end, float maxT,
                            CMapObjDef** outDefs, uint32_t* outGroups);

// MOPT: one portal, 20 bytes. The vertices are MOPV entries.
struct SMOPortal {
    uint16_t startVertex;       // +0x00
    uint16_t count;             // +0x02
    C4Plane plane;              // +0x04
};

static_assert(sizeof(SMOPortal) == 0x14, "SMOPortal is 20 bytes");

// MOPR: one portal reference from a group, 8 bytes.
struct SMOPortalRef {
    uint16_t portalIndex;       // +0x00
    uint16_t groupIndex;        // +0x02
    int16_t side;               // +0x04: which side of the portal's plane the group is on
    uint16_t pad;               // +0x06
};

static_assert(sizeof(SMOPortalRef) == 0x8, "SMOPortalRef is 8 bytes");

// MOMT: one material of a WMO (64 bytes). The group queries only read texture1, to tell a
// textured face from an untextured one; the loader fills the two texture handles at the end.
struct SMOMaterial {
    uint32_t flags;             // +0x00
    uint32_t shader;            // +0x04
    uint32_t blendMode;         // +0x08
    uint32_t texture1;          // +0x0c: offset into MOTX
    uint32_t sidnColor;         // +0x10
    uint32_t frameSidnColor;    // +0x14
    uint32_t texture2;          // +0x18: offset into MOTX
    uint32_t diffColor;         // +0x1c
    uint32_t groundType;        // +0x20
    uint32_t texture3;          // +0x24
    uint32_t color3;            // +0x28
    uint32_t flags3;            // +0x2c
    uint32_t runtime[4];        // +0x30: the file leaves these for the client
};

static_assert(sizeof(SMOMaterial) == 0x40, "SMOMaterial is 64 bytes");

// The two texture handles a material draws with. The reference keeps them inside the material's
// own runtime bytes (+0x38 and +0x3c) and so writes into the file buffer; a handle is eight
// bytes on 64-bit and two would not fit there, so frozen keeps them in an array beside the
// materials with the same lifetime (diverged).
struct SMOMaterialTextures {
    HTEXTURE texture1 = nullptr;
    HTEXTURE texture2 = nullptr;
};

// A loaded WMO root (reference 0x9f8 bytes). One instance per distinct .wmo path: the defs that
// place it share it through the name cache and a reference count.
//
// The reference's field offsets are noted per field. They hold from m_name (+0x1c) on; the hash
// fields above it sit at +0x04..+0x18 there and the memory handle at +0x00, which is the one
// place frozen's layout differs (the base class has to come first here).
class CMapObj : public TSHashObject<CMapObj, HASHKEY_NONE> {
    public:
        // Static variables
        // Every loaded root by SStrHash of its path (DAT_00d1c428): Create hands back the one
        // that is already there and counts a reference instead of reading the file twice.
        static TSHashTable<CMapObj, HASHKEY_NONE> s_cache;

        // Static functions
        // ref: FUN_007ae140
        static uint32_t QuerySkipFlags(uint32_t queryFlags);
        static CMapObj* Create(const char* path);
        static void ReadCallback(void* arg);
        // How many shader ids a WMO material can name. Six draw lit; the seventh is only
        // in the unlit set, where it composites.
        static const uint32_t SHADER_COUNT = 7;

        // The effect each material shader id draws through, resolved by name out of the
        // registry the .wfx files fill. s_effects is the lit set the shader draw paths
        // use; s_effectsUnlit is the one the lower shader levels take.
        static CShaderEffect* s_effects[SHADER_COUNT];
        static CShaderEffect* s_effectsUnlit[SHADER_COUNT];

        // ObjectAlloc heap for the map object occlusion records (0x24 bytes each, 128 to a
        // block). The record itself is not identified yet, so nothing allocates from it.
        static uint32_t* s_occlusionHeap;
        static const size_t OCCLUSION_RECORD_SIZE = 0x24;

        // ref: FUN_007afee0
        static void Initialize();
        // ref: FUN_007ad020
        static void UpdateAll();

        // Which of the reference's six WMO draw paths the device takes. It reads a
        // constant 5 there, the shader path, and nothing ever writes it.
        static const int32_t SHADER_LEVEL = 5;

        void UpdateMaterialColors();

        // ref: FUN_007abf50
        void Render(uint32_t groupIndex, const C44Matrix& inversePlacement, CMapObjDefGroup* defGroup);

        // --- the portal walk's reporting channel ------------------------------------------
        // Who to tell about a group the walk reaches, and what to hand them with it. The walk
        // is several calls deep by the time it decides, so the destination is parked here
        // rather than threaded through (DAT_00d1bed8 and DAT_00d1bedc).
        static void (*s_visibleCallback)(uint32_t groupIndex, CMapObjDef* def);
        static CMapObjDef* s_visibleCallbackArg;

        // --- the space the portal walk works in -------------------------------------------
        // The walk tests portal planes against the camera, and both the planes and the
        // building's geometry are in the building's own space. Rather than bring every plane
        // out to the world, the camera is brought in once per instance and parked here
        // (DAT_00d1c42c onwards).
        static C3Vector s_localCameraPos;
        static C3Vector s_localCameraTarget;
        static C4Plane s_localViewPlane;

        // What the walk worked out about one portal this frame: the rectangle of the screen
        // it covers, and how to treat it. Kept per portal rather than per visit, because a
        // portal reached twice in one frame covers the same rectangle both times
        // (DAT_00adff54, 28 bytes each).
        // Componentwise arithmetic over a PortalRect's four floats, which the reference keeps as two
        // tiny helpers of its own rather than inlining. Both take the rect's FLOAT BLOCK, so callers
        // pass &rect.minY -- the flags word is not part of the arithmetic.
        static void RectAdd(float* result, const float* a, const float* b);
        static void RectDivide(float* result, const float* a, const float* b);

        struct PortalRect {
            // Bit 0: the portal faces away or projects to nothing, so it opens onto nowhere.
            // Bit 1: the camera is standing in the doorway, so it opens onto everything.
            // Bit 4: the walk is crossing from lit to unlit, or the other way.
            uint16_t flags;
            // The reference orders these vertical-first, and the window the walk narrows is
            // ordered to match, so the overlap test lines up index for index. Keeping its
            // order rather than the obvious one is what makes that work.
            float minY;
            float minX;
            float maxY;
            float maxX;
            // The frame this was worked out on; anything older is recomputed.
            int32_t stamp;
        };

        // How much of the screen one doorway covers, as seen from here. Every corner is moved
        // by `offset` first, the outline goes out to world space, is clipped to the camera's
        // frustum, and what survives is projected. Returns how many corners survived, and
        // writes their screen positions to `screen` (room for CLIP_POLYGON_MAX).
        //
        // The offset is zero for the doorway itself. The anti-portal pass nudges the outline
        // off the plane by a hundredth along its own normal, so the two rectangles it compares
        // are not coplanar. ref: FUN_007a85e0
        static uint32_t ProjectPortal(const SMOPortal* portal, const C3Vector* vertices,
                                      const C3Vector& offset, const C44Matrix& placement,
                                      C3Vector* screen);

        // Work out one doorway's rectangle for this frame: whether the camera is standing in
        // it, whether it faces away or projects to nothing, and otherwise how much of the
        // screen it covers. ref: FUN_007a9090
        static void MeasurePortal(CMapObj* mapObj, const SMOPortal* portal, PortalRect* rect,
                                  const C44Matrix& placement);

        // The rectangle a run of projected corners covers. ref: FUN_007a6b90
        static void ScreenBounds(PortalRect* rect, const C3Vector* points, uint32_t count);

        // ref: FUN_007a7210
        // Is the camera standing in this doorway? Then it opens onto everything.
        static void TestCameraInPortal(CMapObj* mapObj, const SMOPortal* portal, PortalRect* rect);

        // How many doorways deep the walk will go. The reference reads a global set once at
        // startup; frozen caps it so the frustum stack it pushes onto cannot overflow.
        static const uint32_t PORTAL_DEPTH_MAX = 4;

        // The instance the walk is currently inside, kept here the way the reference keeps it
        // (DAT_00adff10 and DAT_00d1c424) because the walk is several calls deep by the time
        // it needs them.
        static C44Matrix s_portalPlacement;
        static int32_t s_portalStamp;
        // Which building the walk is in, and which it was in last. The stamp advances when
        // these differ, so every group of one building shares one measurement of its doorways
        // (DAT_00d1c420 and DAT_00d1c41c).
        static CMapObjDef* s_walkDef;
        static CMapObjDef* s_lastWalkDef;
        // Set while the walk is running from inside a building rather than looking in from
        // outside (DAT_00cfbec0), and set when a walk from inside reached daylight
        // (DAT_00cd8620).
        static int32_t s_insideBuilding;
        static int32_t s_sawExterior;

        // Step through every doorway of one group that is still in view, marking each room it
        // reaches and narrowing the view by the doorway it came through. `fromGroup` is where
        // it came from, so it does not go straight back. ref: FUN_007ac060
        void WalkPortals(uint32_t groupIndex, uint32_t fromGroup, const float* window,
                         uint32_t depth, int32_t interior);

        // Look into a building from outside, through one of its groups. ref: FUN_007ad350
        void WalkFromOutside(const C44Matrix& placement, const C44Matrix& inversePlacement,
                             const C3Vector& cameraPos, const C3Vector& cameraTarget,
                             const float* window, uint32_t groupIndex);
        // Walk out from the groups the camera is standing in. ref: FUN_007ad1f0
        void WalkFromInside(const C44Matrix& placement, const C44Matrix& inversePlacement,
                            const C3Vector& cameraPos, const C3Vector& cameraTarget,
                            const uint32_t* groups, uint32_t groupCount);

        // ref: FUN_007b3b20
        static void EnterPortalWalk(CMapObjDef* def, const uint32_t* groups, uint32_t groupCount);

        // ref: FUN_007a6e00
        static void SetupPortalContext(const C44Matrix& placement, const C44Matrix& inversePlacement,
                                       const C3Vector& cameraPos, const C3Vector& cameraTarget);

        // ref: FUN_007a6b40
        static void SetVisibleCallback(void (*callback)(uint32_t, CMapObjDef*), CMapObjDef* def);
        // Report one group, if its file is in. ref: FUN_007a6b60
        void ReportVisible(uint32_t groupIndex);

        // --- draw state -------------------------------------------------------------------
        // What the draw last set, so a run of batches sharing a setting pushes it once. The
        // reference keeps each as a MapObj.cpp file static.
        static uint32_t s_fogState;             // DAT_00cfbeb0
        static int32_t s_lightingMode;          // DAT_00cfbeac
        static uint32_t s_materialColor;        // DAT_00d1bef8
        static int32_t s_shadowState;           // DAT_00cfbea8
        static uint32_t s_vertexPermuteBase;    // DAT_00cfbeb4
        static uint32_t s_shadowMode;           // DAT_00d43010
        // Added to every self-illuminated material's colour. The per-instance setup clears
        // it and nothing on the shader path writes it again.
        static CImVector s_instanceColor;       // DAT_00d1befc
        // Which of the light's two fog sets this instance draws with, from the def's own
        // flag. The per-instance setup in the map object pass writes it.
        static int32_t s_interiorFog;           // DAT_00cfbeb8

        // ref: FUN_007a8440
        static void SetupFog(uint32_t state);
        // ref: FUN_007a8b10
        static void SetupLighting(CMapObjGroup* group, int32_t mode);
        // ref: FUN_007a8940
        static void SetMaterialColor(const CImVector& color);
        // ref: FUN_007a84d0
        static void SelectShaders();
        // ref: FUN_00873ee0
        static void SetAlphaRefForBlendMode();
        // ref: FUN_007a8320
        static void SetInstanceTransform(const C44Matrix& world);
        // ref: FUN_007a9160
        static void SetupLocalLights(CM2Lighting* lighting, const C3Vector& cameraPos);

        // Member variables
        uint32_t m_memHandle = 0;                 // +0x00: CMap::s_mapObjHeap slot
        char m_name[260] = {};                    // +0x1c: the path the root was read from

        // The file's chunks, pointed straight into m_fileBuffer (nothing here is owned)
        SMOHeader* m_mohd = nullptr;              // +0x120
        const char* m_motx = nullptr;             // +0x124: texture name block
        const char* m_mogn = nullptr;             // +0x128: group name block
        const char* m_mosb = nullptr;             // +0x12c: skybox model name, null when empty
        SMOGroupInfo* m_mogi = nullptr;           // +0x130
        const C3Vector* m_mopv = nullptr;         // +0x134: portal vertices
        SMOPortal* m_mopt = nullptr;              // +0x138
        // One rectangle per doorway, worked out at most once a frame. The reference keeps a
        // single global array because only one building is ever being walked; per root costs
        // the same and cannot be indexed past its end.
        TSGrowableArray<PortalRect> m_portalRects;
        const SMOPortalRef* m_mopr = nullptr;     // +0x13c
        const C3Vector* m_movv = nullptr;         // +0x140: visible block vertices
        const uint8_t* m_movb = nullptr;          // +0x144: visible blocks, 4 bytes each
        const uint8_t* m_molt = nullptr;          // +0x148: lights, 0x30 bytes each
        const uint8_t* m_mods = nullptr;          // +0x14c: doodad sets, 0x20 bytes each
        const char* m_modn = nullptr;             // +0x150: doodad name block
        const uint8_t* m_modd = nullptr;          // +0x154: doodad defs, 0x28 bytes each
        const uint8_t* m_mfog = nullptr;          // +0x158: fog, 0x30 bytes each
        const C4Plane* m_mcvp = nullptr;          // +0x15c: convex volume planes, optional
        SMOMaterial* m_materials = nullptr;       // +0x160: MOMT
        TSGrowableArray<SMOMaterialTextures> m_materialTextures;  // diverged, see the struct

        uint32_t m_motxSize = 0;                  // +0x164: in bytes, the name blocks have no count
        uint32_t m_mognSize = 0;                  // +0x168
        uint32_t m_groupCount = 0;                // +0x16c
        uint32_t m_portalVertexCount = 0;         // +0x170
        uint32_t m_portalCount = 0;               // +0x174
        uint32_t m_portalRefCount = 0;            // +0x178
        uint32_t m_visibleBlockVertexCount = 0;   // +0x17c
        uint32_t m_visibleBlockCount = 0;         // +0x180
        uint32_t m_lightCount = 0;                // +0x184
        uint32_t m_doodadSetCount = 0;            // +0x188
        uint32_t m_modnSize = 0;                  // +0x18c
        uint32_t m_doodadDefCount = 0;            // +0x190
        uint32_t m_fogCount = 0;                  // +0x194
        uint32_t m_convexVolumePlaneCount = 0;    // +0x198
        uint32_t m_materialCount = 0;             // +0x19c

        CImVector m_ambientColor;                 // +0x1a0: MOHD ambColor
        CAaBox m_bounds;                          // +0x1a8: MOHD bounds

        TSLink<CMapObj> m_link;                   // +0x1c4: CMap::s_mapObjLoadList while reading
        void* m_fileBuffer = nullptr;             // +0x1cc: the whole .wmo root, owned
        uint32_t m_fileSize = 0;                  // +0x1d0
        int32_t m_refCount = 0;                   // +0x1d4: defs sharing this root
        float m_idleTime = 0.0f;                  // +0x1d8: seconds with none
        CAsyncObject* m_asyncObject = nullptr;    // +0x1dc: the read in flight
        float m_nearestDistanceSq = 0.0f;         // +0x1c0: to the streaming target
        int32_t m_rootLoaded = 0;                 // +0x1e0: the root's chunks are parsed
        uint32_t m_groupsToLoad = 0;              // +0x1f4: groups whose files have not arrived
        // The reference sizes its heap record for 512 groups and writes the pointers into the
        // object itself (0x9f8 - 0x1f8 = 0x800 bytes of them).
        CMapObjGroup* m_groups[512] = {};         // +0x1f8
        // The groups whose files have arrived and parsed (+0x1e8); a group joins it at the
        // end of its own read
        STORM_EXPLICIT_LIST(CMapObjGroup, m_link) m_loadedGroups;

        // Member functions
        int32_t Read(const char* path);
        void ReadComplete();
        void ParseChunks();
        void ClearMaterialTextures();

        bool GroupFloorColor(uint32_t groupIndex, const C3Segment& segment, CImVector* outColor, uint8_t* outFlag);

        // The vertex colour at one KNOWN face of one group, rather than whatever a probe finds.
        // The floor-light path takes this when it already knows which face it landed on.
        // ref: FUN_007aeb40
        bool GroupFaceColor(uint32_t groupIndex, const C3Vector& point, uint16_t face,
                            CImVector* outColor, uint8_t* outFlag);

        // One group's MOGI record, or null when the root's chunks have not been parsed yet.
        // ref: FUN_007aeb10
        SMOGroupInfo* GroupInfo(uint32_t groupIndex);
        // Is a point (in this building's space) inside its MOHD bounds? False until the root
        // is parsed. ref: FUN_007ae810
        bool PointInBounds(const C3Vector& point);
        // A segment (in this building's space) against one loaded group's faces.
        // ref: FUN_007af200
        bool QuerySegmentGroup(const C3Vector& start, const C3Vector& end, float* t, uint32_t queryFlags,
                               uint32_t skipFlags, uint32_t groupIndex, uint32_t* face);
        // Does a box (in this building's space) meet a group's MOGI box? With requireLoaded the
        // group itself has to be in. ref: FUN_007ae8d0
        bool GroupBoxIntersects(const CAaBox& box, uint32_t groupIndex, int32_t requireLoaded);
        // The liquid under a point (in this building's space): the first loaded group whose MOGI
        // box holds it, skipping groups with any of `excludeFlags`, that has liquid there.
        // ref: FUN_007aeb90
        bool GetLiquidAt(uint32_t excludeFlags, const C3Vector& point, uint32_t* liquidType, float* height);

        // Does the segment touch the whole object's MOHD bounds? False until the root is parsed,
        // because the bounds are not known before that. ref: FUN_007ae840
        bool SegmentVsBounds(const C3Vector& start, const C3Vector& end);
        // Does a box (in this building's space) reach its MOHD bounds? ref: FUN_007ae7e0
        bool BoxVsBounds(const CAaBox& box);
        // Whether group `index` has its own file loaded. ref: FUN_007ae4c0
        bool IsGroupLoaded(uint32_t index);
        // A box query over every loaded, solid group the box reaches. ref: FUN_007aef00
        bool QueryBoxGroups(const CAaBox& box, uint32_t queryFlags, const C44Matrix* placement, void* object);

        // Does the segment touch one group's bounds? Also false when that group's own file has not
        // arrived -- a group whose state bit 0 is clear has no geometry to hit. ref: FUN_007ae880
        bool SegmentVsGroupBounds(const C3Vector& start, const C3Vector& end, uint32_t groupIndex);

        // Is the point, grown by `slack` on every side, inside one group's bounds? Same loaded and
        // group-state gates as SegmentVsGroupBounds. ref: FUN_007ae970
        bool PointInGroupBounds(const C3Vector& point, uint32_t groupIndex, float slack);

        // The same test with no slack at all -- a separate function in the reference, sitting
        // 0x50 bytes before the one above, so a separate one here. ref: FUN_007ae920
        bool PointInGroupBox(const C3Vector& point, uint32_t groupIndex);

        // Which of a group's portals a segment passes through, nearest first, and the groups
        // on either side of it. `t` is a fraction of the segment, in and out. outGroups[0] is
        // the group the segment is heading INTO. ref: FUN_007af520
        bool SegmentVsPortals(uint32_t groupIndex, const C3Segment& segment, float* t,
                              uint32_t* outGroups);

        // One group's own name, out of MOGN. Null until the group's file has arrived, same gates as
        // the bounds queries. ref: FUN_007aeae0
        const char* GroupName(uint32_t groupIndex);

        // Which groups the segment passes between, by walking the PORTALS rather than the geometry.
        //
        // Every group whose bounds the segment touches has its portal references tried; a portal
        // the segment actually crosses -- the ray meets its plane within the segment, and the
        // meeting point is inside the portal's polygon -- puts the two groups it joins into
        // `outGroups`, nearer side first. `t` is in and out: in, how far along the segment to look
        // as a fraction; out, where the nearest crossing was. It is only written when something was
        // found, so a caller keeps its own value otherwise.
        //
        // `fromPoint` switches the per-group filter from "does the segment touch this group's
        // bounds" to "is the segment's start inside them", which is what a caller asking where a
        // point IS wants rather than what it passes through.
        //
        // Returns whether anything was crossed. Ghidra types this void, which is wrong: the flag is
        // loaded into AL at 0x007af4f6 and survives the epilogue untouched, and the one caller does
        // test it. ref: FUN_007af280
        bool QuerySegmentPortals(const C3Segment& segment, float* t, uint32_t* outGroups,
                                 int32_t fromPoint);

        // What the streaming and visibility passes ask a root about itself and its groups. Each
        // answers nothing at all until the root's own file has parsed.
        void BoundingSphere(C3Vector* center, float* radius);
        void Bounds(CAaBox* bounds);
        void GroupBoundingSphere(uint32_t index, C3Vector* center, float* radius);
        void GroupBounds(uint32_t index, CAaBox* bounds);
        uint32_t GroupFlags(uint32_t index);
        CMapObjGroup* GetGroup(uint32_t index, int32_t allowUnloaded);

        // ref: FUN_007a6d70
        // The material one group's liquid draws with, or null when the group is not loaded.
        SMOMaterial* GetGroupLiquidMaterial(uint32_t groupIndex);

        // ref: FUN_007d8010
        // How far a point is from the nearest portal polygon reachable from `group`, or FLT_MAX when
        // none is within range.
        float NearestPortalDistance(CMapObjGroup* group, const C3Vector& point,
                                    float includeFlag40);

        // ref: FUN_007d77c0
        // The recursive half of the above. Walks outward through portals, narrowing `best`.
        void AccumulateNearestPortalDistance(uint32_t depth, uint32_t stopMask,
                                            CMapObjGroup* group, CMapObjGroup* cameFrom,
                                            const C3Vector& point, float* best);
        void WaitForRoot();
        void WaitForGroup(uint32_t index);
        void ReadGroup(uint32_t index);
};

// Put the device into the fixed single-light state the map object passes draw under: light 0 a
// directional one down (1,1,1), lights 1..3 off, lighting enabled. ref: FUN_007a8800
void SetDefaultDirectionalLight();

#endif
