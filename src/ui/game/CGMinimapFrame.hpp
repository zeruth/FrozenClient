#ifndef UI_GAME_C_G_MINIMAP_FRAME_HPP
#define UI_GAME_C_G_MINIMAP_FRAME_HPP

#include "gx/Texture.hpp"
#include "ui/simple/CSimpleFrame.hpp"

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

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        // The region the player arrow is drawn into, at +0x2a0 in the reference. SetPlayerTexture-
        // Width / SetPlayerTextureHeight only ever reach it through its CLayoutFrame interface, so
        // the concrete region type is not recoverable from the bindings and is left open here.
        CLayoutFrame* m_playerTexture = nullptr;

        // Virtual member functions
        virtual int32_t GetScriptMetaTable();

        // Member functions
        CGMinimapFrame(CSimpleFrame* parent);
};

#endif
