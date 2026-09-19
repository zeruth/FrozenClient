#ifndef UI_GAME_C_G_MINIMAP_FRAME_HPP
#define UI_GAME_C_G_MINIMAP_FRAME_HPP

#include "gx/Texture.hpp"
#include "ui/simple/CSimpleFrame.hpp"

class CSimpleTexture;

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

        // The zoom level, one per map kind: index 0 outdoors, index 1 indoors. The reference keeps
        // this pair in the minimap module too (DAT_00af4e4c and DAT_00af4e50, adjacent), selected
        // by the interior flag below; both start at 3.
        static uint32_t s_zoom[2];               // ref: DAT_00af4e4c

        // Selects which of the two zoom levels -- and which zoom radius table -- the minimap uses.
        // The reference's minimap update sets it when the player moves under an interior map; Frozen
        // has no minimap update yet, so it stays 0.
        static uint8_t s_indoors;                // ref: DAT_00d39434

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
