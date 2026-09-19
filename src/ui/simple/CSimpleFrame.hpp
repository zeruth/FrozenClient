#ifndef UI_SIMPLE_C_SIMPLE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_FRAME_HPP

#include "event/CEvent.hpp"
#include "ui/CRenderBatch.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/Types.hpp"
#include "ui/simple/CSimpleRegion.hpp"
#include <storm/Hash.hpp>
#include <storm/List.hpp>
#include <cstdint>

class CBackdropGenerator;
class CCharEvent;
class CKeyEvent;
class CMouseEvent;
class CSimpleTitleRegion;
class CSimpleTop;
struct lua_State;

struct FRAMEATTR : TSHashObject<FRAMEATTR, HASHKEY_STRI> {
    int32_t luaRef;
};

// CSimpleFrame::m_flags bits.
//
// Recovered from the reference rather than invented: its script method table at 0x00ac16f0 names
// each binding beside its function, and the functions set the bit through the same flag setter --
// SetToplevel 0x1 (FUN_0049fbb0), SetMovable 0x100 (FUN_004a0800), SetResizable 0x200
// (FUN_004a0980), SetUserPlaced 0x1000 (FUN_004a0c70). IsMovable reads them back at frame + 0xb4,
// which is the same field. 0x100 and 0x200 also match what this codebase's own XML loader already
// used for the movable/resizable attributes, which is a second independent agreement.
enum {
    FRAME_FLAG_TOPLEVEL    = 0x0001,
    FRAME_FLAG_MOVABLE     = 0x0100,
    FRAME_FLAG_RESIZABLE   = 0x0200,
    FRAME_FLAG_DISABLED    = 0x0400,
    FRAME_FLAG_USER_PLACED = 0x1000,
    // Suppresses the depth queries: GetDepth (FUN_004a1cc0) and GetEffectiveDepth (FUN_004a1d20)
    // both test frame + 0xb4 against this bit and return nothing at all while it is set. Nothing in
    // Frozen sets it yet -- the reference path that does has not been ported -- so it reads as 0.
    FRAME_FLAG_NO_DEPTH    = 0x40000,
};

class CSimpleFrame : public CScriptRegion {
    public:
        // Static members
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        CSimpleTop* m_top = nullptr;
        CSimpleTitleRegion* m_titleRegion = nullptr;
        int32_t m_intAC = 1;
        int32_t m_id = 0;        uint32_t m_flags = 0;
        // <ResizeBounds> minResize / maxResize (reference +0x64..+0x70), consumed by user resizing
        float m_minResizeWidth = 0.0f;
        float m_minResizeHeight = 0.0f;
        float m_maxResizeWidth = 0.0f;
        float m_maxResizeHeight = 0.0f;
        float m_frameScale = 1.0f;
        uint8_t m_alpha = 255;
        uint8_t alphaBD = 255;
        float m_depth = 0.0;
        // Depth inherited from the parent chain (reference +0xc8, beside m_depth at +0xc4).
        // GetEffectiveDepth returns m_inheritedDepth + m_depth. CSimpleFrame::UpdateDepth is still a
        // stub, so nothing writes this yet and the effective depth equals the frame's own depth.
        float m_inheritedDepth = 0.0f;
        // Reference +0xcc, read back by IsIgnoringDepth (FUN_004a1e00). CSimpleFrame_IgnoreDepth is
        // still a no-op, so nothing sets it.
        int32_t m_ignoreDepth = 0;
        FRAME_STRATA m_strata = FRAME_STRATA_MEDIUM;
        int32_t m_level = 0;
        uint32_t m_eventmask = 0;
        int32_t m_shown = 0;
        int32_t m_visible = 0;
        CRect m_hitRect = {};
        CRect m_hitOffset = {};
        int32_t m_highlightLocked = 0;
        uint32_t m_lookForDrag = 0;
        int32_t m_mouseDown = 0;
        int32_t m_dragging = 0;
        int32_t m_dragButton;
        C2Vector m_clickPoint;
        int32_t m_loading = 0;
        ScriptIx m_onLoad;
        ScriptIx m_onSizeChanged;
        ScriptIx m_onUpdate;
        ScriptIx m_onShow;
        ScriptIx m_onHide;
        ScriptIx m_onEnter;
        ScriptIx m_onLeave;
        ScriptIx m_onMouseDown;
        ScriptIx m_onMouseUp;
        ScriptIx m_onMouseWheel;
        ScriptIx m_onDragStart;
        ScriptIx m_onDragStop;
        ScriptIx m_onReceiveDrag;
        ScriptIx m_onChar;
        ScriptIx m_onKeyDown;
        ScriptIx m_onKeyUp;
        ScriptIx m_onAttributeChange;
        ScriptIx m_onEnable;
        ScriptIx m_onDisable;
        TSHashTable<FRAMEATTR, HASHKEY_STRI> m_attributes;
        int32_t m_drawenabled[NUM_SIMPLEFRAME_DRAWLAYERS];
        CBackdropGenerator* m_backdrop = nullptr;
        STORM_EXPLICIT_LIST(CSimpleRegion, m_regionLink) m_regions;
        STORM_EXPLICIT_LIST(CSimpleRegion, m_layerLink) m_drawlayers[NUM_SIMPLEFRAME_DRAWLAYERS];
        uint32_t m_batchDirty = 0;
        CRenderBatch* m_batch[NUM_SIMPLEFRAME_DRAWLAYERS] = {};
        STORM_EXPLICIT_LIST(CRenderBatch, renderLink) m_renderList;
        TSList<SIMPLEFRAMENODE, TSGetLink<SIMPLEFRAMENODE>> m_children;
        TSLink<CSimpleFrame> m_framesLink;
        TSLink<CSimpleFrame> m_destroyedLink;
        TSLink<CSimpleFrame> m_strataLink;

        // Virtual member functions
        virtual ~CSimpleFrame();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void PreOnAnimUpdate();
        virtual void OnLayerShow();
        virtual void OnLayerHide();
        virtual void OnLayerUpdate(float elapsedSec);
        virtual int32_t OnLayerTrackUpdate(const CMouseEvent& evt);
        virtual void OnFrameRender();
        virtual void OnFrameRender(CRenderBatch* batch, uint32_t layer);
        virtual void OnScreenSizeChanged();
        virtual void OnFrameSizeChanged(float width, float height);
        virtual void OnLayerCursorEnter(int32_t a2);
        virtual void OnLayerCursorExit(int32_t a2, int32_t a3);
        virtual int32_t OnLayerKeyDownRepeat(const CKeyEvent& evt);
        virtual int32_t OnLayerChar(const CCharEvent& evt);
        virtual int32_t OnLayerKeyDown(const CKeyEvent& evt);
        virtual int32_t OnLayerKeyUp(const CKeyEvent& evt);
        virtual int32_t OnLayerMouseDown(const CMouseEvent& evt, const char* btn);
        virtual int32_t OnLayerMouseUp(const CMouseEvent& evt, const char* btn);
        virtual int32_t OnLayerMouseWheel(const CMouseEvent& evt);
        virtual void PostLoadXML(const XMLNode* node, CStatus* status);
        virtual void UnregisterRegion(CSimpleRegion* region);
        virtual int32_t GetBoundsRect(CRect& bounds);
        virtual void PreLoadXML(XMLNode* node, CStatus* status);
        virtual void LockHighlight(int32_t lock);
        virtual int32_t HideThis();
        virtual int32_t ShowThis();
        virtual bool UpdateScale(bool a2);
        virtual void UpdateAlpha();
        virtual void UpdateDepth(bool a2);
        virtual void ParentFrame(CSimpleFrame* frame);
        virtual void OnFrameSizeChanged(const CRect& rect);

        // Member functions
        CSimpleFrame(CSimpleFrame* parent);
        void AddFrameRegion(CSimpleRegion* region, uint32_t drawlayer);
        int32_t AttributeChangesAllowed();
        void DisableDrawLayer(uint32_t drawlayer);
        void DisableEvent(CSimpleEventType eventType);
        void EnableDrawLayer(uint32_t drawlayer);
        void EnableEvent(CSimpleEventType eventType, int32_t priority);
        bool GetAttribute(const char* name, int32_t& luaRef);
        int32_t GetHitRect(CRect& rect);
        void Hide();
        void LoadXML_Attributes(const XMLNode* node, CStatus* status);
        void LoadXML_Backdrop(const XMLNode* node, CStatus* status);
        void LoadXML_Layers(const XMLNode* node, CStatus* status);
        void LoadXML_Scripts(const XMLNode* node, CStatus* status);
        void NotifyDrawLayerChanged(uint32_t drawlayer);
        void NotifyScrollParent();
        void PostLoadXML_Frames(const XMLNode* node, CStatus* status);
        void Raise();
        void RegisterForEvents(int32_t a2);
        void RegisterRegion(CSimpleRegion* region);
        void RemoveFrameRegion(CSimpleRegion* region, uint32_t drawlayer);
        void RunOnAttributeChangedScript(const char* name, int32_t luaRef);
        void RunOnCharScript(const char* chr);
        void RunOnEnableScript();
        void RunOnDisableScript();
        void RunOnEnterScript(int32_t a2);
        void RunOnHideScript();
        void RunOnKeyDownScript(const char* key);
        void RunOnKeyUpScript(const char* key);
        void RunOnLeaveScript(int32_t a2);
        void RunOnLoadScript();
        void RunOnMouseDownScript(const char* btn);
        void RunOnMouseUpScript(const char* btn);
        void RunOnShowScript();
        void RunOnSizeChangedScript(float width, float height);
        void RunOnUpdateScript(float elapsedSec);
        void SetAttribute(const char* name, int32_t luaRef);
        void SetBackdrop(CBackdropGenerator* backdrop);
        void SetBeingScrolled(int32_t a2, int32_t a3);        void SetFrameAlpha(uint8_t alpha);
        void SetClampedToScreen(int32_t clamped);
        void SetDepth(float depth, int32_t force);
        void SetProtected();
        void SetFrameFlag(int32_t flag, int32_t on);
        void SetFrameLevel(int32_t level, int32_t shiftChildren);
        bool SetFrameScale(float scale, bool force);
        void SetFrameStrata(FRAME_STRATA strata);
        void SetHitRect();
        void SetHitRectInsets(float left, float right, float top, float bottom);
        void GetHitRectInsets(float& left, float& right, float& top, float& bottom);
        void SetParent(CSimpleFrame* parent);
        void Show();
        int32_t TestHitRect(const C2Vector& pt);
        void UnregisterForEvents(int32_t a2);
};

#endif
