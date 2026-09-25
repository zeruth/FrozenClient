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
        struct PortalRect {
            // Bit 0: the portal faces away or projects to nothing, so it opens onto nowhere.
            // Bit 1: the camera is standing in the doorway, so it opens onto everything.
            // Bit 4: the walk is crossing from lit to unlit, or the other way.
            uint16_t flags;
            float minX;
            float minY;
            float maxX;
            float maxY;
            // The frame this was worked out on; anything older is recomputed.
            int32_t stamp;
        };

        // ref: FUN_007a7210
        // Is the camera standing in this doorway? Then it opens onto everything.
        static void TestCameraInPortal(CMapObj* mapObj, const SMOPortal* portal, PortalRect* rect);

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

        bool GroupFloorColor(CMapObjGroup* group, const C3Segment& segment, CImVector* outColor, uint8_t* outFlag);

        // What the streaming and visibility passes ask a root about itself and its groups. Each
        // answers nothing at all until the root's own file has parsed.
        void BoundingSphere(C3Vector* center, float* radius);
        void Bounds(CAaBox* bounds);
        void GroupBoundingSphere(uint32_t index, C3Vector* center, float* radius);
        void GroupBounds(uint32_t index, CAaBox* bounds);
        uint32_t GroupFlags(uint32_t index);
        CMapObjGroup* GetGroup(uint32_t index, int32_t allowUnloaded);
        void WaitForRoot();
        void WaitForGroup(uint32_t index);
        void ReadGroup(uint32_t index);
};

#endif
