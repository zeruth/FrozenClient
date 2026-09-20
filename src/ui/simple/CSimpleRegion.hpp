#ifndef UI_SIMPLE_C_SIMPLE_REGION_HPP
#define UI_SIMPLE_C_SIMPLE_REGION_HPP

#include "ui/CScriptRegion.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>

class CRenderBatch;

class CSimpleRegion : public CScriptRegion {
    public:
        // Member variables
        uint32_t m_alphaCount = 4;
        uint8_t m_alpha[4];
        uint32_t m_colorCount = 4;
        CImVector m_color[4];
        TSLink<CSimpleRegion> m_regionLink;
        TSLink<CSimpleRegion> m_layerLink;
        int32_t m_drawlayer = 0;
        int32_t m_shown = 0;
        int32_t m_visible = 0;

        // Virtual member functions
        virtual ~CSimpleRegion();
        virtual void OnColorChanged(bool a2);

        // ref: FUN_00487ce0
        // An alpha animation's per-frame contribution. Lives here rather than on CScriptRegion
        // because this is the class that owns the colour -- a texture's vtable reaches it by
        // inheritance, which is how the reference arranges it too.
        virtual void AddAnimAlpha(CScriptRegion* source, int16_t delta);
        virtual void OnScreenSizeChanged() {};
        virtual void Draw(CRenderBatch* batch) = 0;

        // Member functions
        CSimpleRegion(CSimpleFrame* frame, uint32_t drawlayer, int32_t show);
        void GetVertexColor(CImVector& color) const;
        void Hide();
        void HideThis();
        bool IsShown();
        bool IsVisible();
        void OnRegionChanged();
        void SetVertexColor(const CImVector& color);
        void SetVertexGradient(ORIENTATION orientation, const CImVector& minColor, const CImVector& maxColor);
        void SetFrame(CSimpleFrame* frame, uint32_t drawlayer, int32_t show);
        void Show();
        void ShowThis();
};

#endif
