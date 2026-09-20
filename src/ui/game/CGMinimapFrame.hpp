#ifndef UI_GAME_C_G_MINIMAP_FRAME_HPP
#define UI_GAME_C_G_MINIMAP_FRAME_HPP

#include "gx/Texture.hpp"
#include "ui/simple/CSimpleFrame.hpp"

class CSimpleTexture;

// One row of the minimap's non-spell tracking list. The reference keeps 15 of these in a static
// table (DAT_00a11c50, 0x14 bytes each); the contents here were read out of its .rdata rather than
// transcribed from FrameXML, so the order, the flags and the class masks are the binary's own.
struct MINIMAP_TRACKING_TYPE {
    // What the minimap should look for. 1 matches an NPC flag, 2 a game object type, 3 is the
    // trivial-quest special case that has no flag behind it at all.
    uint32_t kind;

    // The NPC flag bit for kind 1, the game object type for kind 2 (0x13, mailbox), unused for 3.
    uint32_t value;

    // A global string name, looked up through FrameScript_GetText -- not the display text itself.
    // It doubles as the value stored in the minimapTrackedInfo CVar.
    const char* name;

    // Basename under Interface\\Minimap\\Tracking.
    const char* texture;

    // Which classes may select this. Zero means everyone; otherwise it is a bitmask of 1 << class
    // id, so Poisons is 0x10 (rogue), Ammunition 0x1a (warrior, hunter, rogue) and StableMaster
    // 0x08 (hunter).
    uint32_t classMask;
};

#define NUM_MINIMAP_TRACKING_TYPES 15

class CGMinimapFrame : public CSimpleFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // The minimap's overlay textures. The reference keeps these as module-level globals in the
        // minimap translation unit (DAT_00beba24 .. DAT_00beba3c, one contiguous block) rather than
        // per-frame state, because only one minimap ever exists; they live here so that the minimap
        // renderer can reach them once it is ported.
        static HTEXTURE s_staticPOIArrowTexture; // ref: DAT_00beba24
        static HTEXTURE s_corpsePOIArrowTexture; // ref: DAT_00beba28
        static HTEXTURE s_poiArrowTexture;       // ref: DAT_00beba2c
        static HTEXTURE s_maskTexture;           // ref: DAT_00beba30
        static HTEXTURE s_classBlipTexture;      // ref: DAT_00beba34
        static HTEXTURE s_blipTexture;           // ref: DAT_00beba38
        static HTEXTURE s_iconTexture;           // ref: DAT_00beba3c

        // The teardown at FUN_0057bd90 releases two more groups this list used to miss, so the set
        // is wider than the seven above. One unnamed neighbour sits below them, and a run of seven
        // more below that which the teardown frees in a single 0x1c-byte loop. Named by position
        // until the renderer shows what each is for.
        static HTEXTURE s_unknownTexture;        // ref: DAT_00beba20
        static HTEXTURE s_overlayTextures[7];    // ref: DAT_00beba04

        // Zoom radii, precomputed once in the reference's constructor (FUN_0057dca0) into
        // DAT_00beba44..5c. Four radii, each stored beside its own half, which is what the
        // interior flag below picks between: the full value outdoors and the half indoors. The
        // reference derives them as NDCToDDCHeight(1.0f) * (5/3), scaled by 0.055, 0.045, 0.0375
        // and 0.0125. TODO four radii against six zoom levels does not divide, so how a level
        // indexes this is still unknown -- do not wire SetZoom to it on the strength of the
        // arithmetic alone.
        static float s_zoomRadius[4][2];         // ref: DAT_00beba44

        // The zoom level, one per map kind: index 0 outdoors, index 1 indoors. The reference keeps
        // this pair in the minimap module too (DAT_00af4e4c and DAT_00af4e50, adjacent), selected
        // by the interior flag below; both start at 3.
        static uint32_t s_zoom[2];               // ref: DAT_00af4e4c

        // Selects which of the two zoom levels -- and which zoom radius table -- the minimap uses.
        // The reference's minimap update sets it when the player moves under an interior map; Frozen
        // has no minimap update yet, so it stays 0.
        static uint8_t s_indoors;                // ref: DAT_00d39434

        // The non-spell half of the tracking list, and which of its rows is currently selected.
        static const MINIMAP_TRACKING_TYPE s_trackingTypes[NUM_MINIMAP_TRACKING_TYPES]; // ref: DAT_00a11c50
        static const MINIMAP_TRACKING_TYPE* s_otherTracking;                            // ref: DAT_00beba64

        // The spell half. The reference builds a list of the tracking spells the player knows into
        // DAT_00be8dec/DAT_00be8de8 and puts it BEFORE the rows above, so a tracking id indexes the
        // spells first and the table second. That list needs the spellbook side, which is not
        // ported, so the count stays zero -- the indexing below is the reference's arithmetic with
        // an empty first half, not a different scheme.
        // Every tracking spell the player knows, rebuilt from the spellbook on demand. The
        // reference keeps this incrementally as spells are learned (ref: FUN_00542030 appends,
        // FUN_0053fad0 removes) in DAT_00be8dec with its count in DAT_00be8de8.
        static uint32_t GetNumTrackingSpells();
        static uint32_t GetTrackingSpell(uint32_t index);

        // ref: FUN_007fdf60
        static bool IsTrackingSpell(uint32_t spellID);

        // The tracking spell currently active, 0 for none (ref: DAT_00beba68). Nothing sets it yet
        // for the same reason.
        static uint32_t s_trackingSpell;

        // How many zoom levels the minimap offers. The reference returns a literal 6, and clamps
        // SetZoom to 5.
        static const uint32_t s_zoomLevels;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);
        static uint32_t GetZoomLevels();
        static uint32_t GetZoom();
        static void SetZoom(uint32_t level);
        static uint32_t GetNumOtherTrackingTypes();
        static const MINIMAP_TRACKING_TYPE* GetOtherTrackingType(uint32_t index);
        static void SetOtherTracking(const MINIMAP_TRACKING_TYPE* type);

        // Member variables
        // The region the player arrow is drawn into, at +0x2a0 in the reference. SetPlayerTexture
        // hands it a file name through CSimpleTexture::SetTexture, and so does the minimapPlayer-
        // Texture XML attribute, so the region is a texture; SetPlayerTextureWidth /
        // SetPlayerTextureHeight reach the same object through its CLayoutFrame interface.
        // Nothing creates it yet -- the XML attribute that does is not ported -- so it stays null.
        CSimpleTexture* m_playerTexture = nullptr;

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGMinimapFrame(CSimpleFrame* parent);
};

#endif
