#ifndef UI_GAME_C_G_WORLD_FRAME_HPP
#define UI_GAME_C_G_WORLD_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include <cstdint>

class CGCamera;

namespace FFX {
    class Effect;
}

class CGWorldFrame : public CSimpleFrame {
    public:
        // Static variables
        // Declared under "Static variables" and assigned in the constructor as the one current
        // frame, but it was missing the keyword, so every instance carried its own copy and the
        // qualified writes in member functions silently meant "this->".
        static CGWorldFrame* s_currentWorldFrame;

        // The world's full-screen effects (DAT_00b74364, DAT_00b74368, DAT_00b74360 and
        // DAT_00b7435c): the glow that normally runs, and the three a screen effect can swap in.
        static FFX::Effect* s_glowEffect;
        static FFX::Effect* s_deathEffect;
        static FFX::Effect* s_netherEffect;
        static FFX::Effect* s_specialEffect;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void RenderWorld(void* param);

        // The one current frame's camera, or null before the world frame exists. The reference
        // reads it straight off the current frame -- `s_currentWorldFrame ? its camera : null` --
        // and 103 call sites go through it. ref: FUN_004f5960
        static CGCamera* GetActiveCamera();

        // Run the full-screen effect a ScreenEffect.dbc record names, or the glow when there is
        // none. ref: FUN_004f7020
        static void SetScreenEffect(int32_t id);
        // The screen effect the player's auras ask for, if any. ref: FUN_004f88b0
        static void UpdateScreenEffect();
        // Feed the glow the day's glow, and under water or drunk the grey of its combine.
        // ref: FUN_004f8770
        static void UpdateGlowParams();

        // Virtual member functions
        virtual void OnFrameRender(CRenderBatch* batch, uint32_t layer);
        // TODO
        virtual void OnFrameSizeChanged(const CRect& rect);
        virtual int32_t OnLayerMouseDown(const CMouseEvent& evt, const char* btn);
        virtual int32_t OnLayerMouseUp(const CMouseEvent& evt, const char* btn);
        virtual int32_t OnLayerTrackUpdate(const CMouseEvent& evt);
        virtual int32_t OnLayerMouseWheel(const CMouseEvent& evt);

        // Member functions
        CGWorldFrame(CSimpleFrame* parent);
        void OnWorldRender();
        void OnWorldUpdate();
        // Hands the day/night block the camera, the clock and the weather for the frame.
        void UpdateDayNight(float elapsedSec);

    private:
        // Private member variables
        // TODO
        CRect m_screenRect;
        int32_t m_cameraDragging = 0;
        float m_dragLastX = 0.0f;
        float m_dragLastY = 0.0f;
        CRect m_viewport;

    public:
        // The world's viewport rect (NDC). The world text pass runs after OnWorldRender has
        // restored the UI viewport, so it needs this to place screen-space text correctly when the
        // WorldFrame does not fill the window.
        static const CRect* GetWorldViewport();

    protected:
        // TODO
        CGCamera* m_camera;
        // TODO
};

#endif
