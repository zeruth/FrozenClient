#ifndef UI_GAME_C_G_COOLDOWN_HPP
#define UI_GAME_C_G_COOLDOWN_HPP

#include "gx/Texture.hpp"
#include "ui/simple/CSimpleFrame.hpp"
#include <tempest/Vector.hpp>

class CGCooldown : public CSimpleFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();

        // Without this the frame answers only to its base's types, and every one of its own
        // script methods fails the This() check with "Wrong object type for member function" --
        // the methods are registered and reachable, the object just denies being what it is.
        virtual bool IsA(int32_t type);
        static void RegisterScriptMethods(lua_State* L);

        // Member variables, in the reference's order (offsets are the 32-bit reference's).
        // +0x29c: when the sweep started, in OsGetAsyncTimeMs milliseconds.
        uint32_t m_start = 0;
        // +0x2a0: how long the sweep runs, in milliseconds. The finish flash reuses it (1000).
        uint32_t m_duration = 0;
        // +0x2a4: milliseconds since m_start, as of the last update.
        uint32_t m_elapsed = 0;
        // +0x2a8: set while the finish flash plays after the sweep has run out.
        int32_t m_flashing = 0;
        // +0x2ac: SetReverse / the XML "reverse" attribute.
        int32_t m_reverse = 0;
        // +0x2b0: interface\cooldown\star4.blp, drawn by both the dark sweep and the flash.
        HTEXTURE m_starTexture = nullptr;
        // +0x2b4: the sweep fan -- eight rim vertices round the frame, the twelve o'clock start,
        // and the centre (index 9) every triangle shares.
        C3Vector m_swirlPos[10] = {};
        C2Vector m_swirlTexCoord[10] = {};
        // +0x37c
        CImVector m_color37c = {};
        // +0x380: the finish flash quad.
        C3Vector m_flashPos[4] = {};
        C2Vector m_flashTexCoord[4] = {};
        // +0x3d0
        CImVector m_color3d0 = {};
        // +0x3d4: interface\cooldown\edge.blp, the bright line along the sweep's leading edge.
        HTEXTURE m_edgeTexture = nullptr;
        // +0x3d8: SetDrawEdge / the XML "drawEdge" attribute.
        int32_t m_drawEdge = 0;
        // +0x3dc: the edge quad, which stays put while its texture coordinates turn.
        C3Vector m_edgePos[4] = {};
        C2Vector m_edgeTexCoord[4] = {};
        // +0x42c
        CImVector m_color42c = {};

        // Virtual member functions
        virtual ~CGCooldown();
        virtual int32_t GetScriptMetaTable();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnLayerUpdate(float elapsedSec);
        virtual void OnFrameRender(CRenderBatch* batch, uint32_t layer);
        virtual void UpdateAlpha();
        virtual void OnFrameSizeChanged(const CRect& rect);
        using CSimpleFrame::OnFrameRender;
        using CSimpleFrame::OnFrameSizeChanged;

        // Member functions
        CGCooldown(CSimpleFrame* parent);
        void SetCooldown(uint32_t start, uint32_t duration);
        void ResetGeometry();
        void UpdateGeometry();
        void UpdateSegment01(int32_t segment, float fraction);
        void UpdateSegment23(int32_t segment, float fraction);
        void UpdateSegment45(int32_t segment, float fraction);
        void UpdateSegment67(int32_t segment, float fraction);

        // ref: FUN_005ec790
        // Recolour from the frame's effective alpha.
        void UpdateColors();
};

#endif
