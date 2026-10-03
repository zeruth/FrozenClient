#ifndef UI_GAME_C_G_WORLD_FRAME_HPP
#define UI_GAME_C_G_WORLD_FRAME_HPP

#include "ui/simple/CSimpleFrame.hpp"
#include "util/GUID.hpp"
#include <storm/List.hpp>
#include <cstdint>

class CGCamera;
class CGObject_C;
class CM2Model;

// One model the frame drew this frame, kept for the cursor's ray query (RTTI CModelRecord, 0x20
// bytes): the model (held), the distance the query found it at, and whose it is.
class CModelRecord : public TSLinkedNode<CModelRecord> {
    public:
        CM2Model* m_model = nullptr;        // +0x08
        float m_distance = 0.0f;            // +0x0c, starts at infinity (0x00ac79a8)
        WOWGUID m_guid = 0;                 // +0x10
        CGObject_C* m_object = nullptr;     // +0x18, set by the query
        uint32_t m_unk1c = 0;               // +0x1c
};

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

        // ref: FUN_004f9f70
        // The handler every client object's world entry carries (CGObject_C::AddWorldObject).
        // The map's walk calls it for each entity it reaches: an object not disabled whose model
        // can draw is added to the frame; one that cannot has its draw bits cleared.
        static int32_t ObjectWorldHandler(void* param, int32_t flags, uint32_t guidLow, uint32_t guidHigh,
                                          uint32_t param32);

        // Virtual member functions
        virtual void OnFrameRender(CRenderBatch* batch, uint32_t layer);
        // TODO
        virtual void OnFrameSizeChanged(const CRect& rect);
        virtual int32_t OnLayerKeyDown(const CKeyEvent& evt);
        virtual int32_t OnLayerKeyUp(const CKeyEvent& evt);
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

        // ref: FUN_004f8d10
        // One object the map reached: it says whether it hides this frame (slot 0x90), updates for
        // the frame (0x88) and places its model (0x8c); a drawn unit, game object or corpse joins
        // the pick list; and the model's draw bits follow.
        void AddVisibleObject(CGObject_C* object, uint32_t flags);
        // ref: FUN_004f89e0
        CModelRecord* NewModelRecord();
        // ref: FUN_004f9310
        // Every record's model let go, and the records back on the free list.
        void ReleaseModelRecords(STORM_EXPLICIT_LIST(CModelRecord, m_link)& list);
        // ref: FUN_004fa5d0
        void ReleaseAllModelRecords();

        // +0x380 and +0x388: the locked target and the pet, when the frame drew them this frame
        // (CGObject_C::UpdateForFrame); the selection circles are drawn under them.
        WOWGUID m_selectionTarget = 0;
        WOWGUID m_selectionPet = 0;
        // +0xb14: the elapsed seconds the frame's update ran with, which the objects place their
        // models with.
        float m_elapsed = 0.0f;
        // +0x29c: the models drawn this frame, for the cursor; +0x2a8 the second record list the
        // update releases; +0x2b4 the records ready for reuse.
        STORM_EXPLICIT_LIST(CModelRecord, m_link) m_modelRecords;
        STORM_EXPLICIT_LIST(CModelRecord, m_link) m_modelRecords2;
        STORM_EXPLICIT_LIST(CModelRecord, m_link) m_freeModelRecords;

    private:
        // Private member variables
        // TODO
        CRect m_screenRect;
        int32_t m_cameraDragging = 0;
        float m_dragLastX = 0.0f;
        float m_dragLastY = 0.0f;
        CRect m_viewport;

    public:
        // +0xb18: per key, the binding string its press ran, so the release runs the same one
        // whatever the modifiers have done meanwhile (0x24 bytes each: the string, then the
        // modifiers it was made with).
        struct KeyBinding {
            char name[32];
            uint32_t modifiers;
        };

        KeyBinding m_keyBindings[0x313] = {};

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
