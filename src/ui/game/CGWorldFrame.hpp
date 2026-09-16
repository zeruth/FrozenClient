#ifndef UI_GAME_C_G_WORLD_FRAME_HPP
#define UI_GAME_C_G_WORLD_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include <cstdint>

class CGCamera;

class CGWorldFrame : public CSimpleFrame {
    public:
        // Static variables
        // Declared under "Static variables" and assigned in the constructor as the one current
        // frame, but it was missing the keyword, so every instance carried its own copy and the
        // qualified writes in member functions silently meant "this->".
        static CGWorldFrame* s_currentWorldFrame;

        // Static functions
        static CSimpleFrame* Create(CSimpleFrame* parent);
        static void RenderWorld(void* param);

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
