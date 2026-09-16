#ifndef UI_SIMPLE_C_SIMPLE_SCROLLING_MESSAGE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_SCROLLING_MESSAGE_FRAME_HPP

#include "ui/simple/CSimpleMessageFrame.hpp"

// The chat windows. Same message list as a plain message frame, plus a bounded history and a scroll
// position into it, so only the lines that fit are anchored and shown.
//
// FrameXML creates ten of these (ChatFrame1..10) and names the first DEFAULT_CHAT_FRAME, which most
// of the interface prints through -- so without this type the client has no chat at all, and every
// `DEFAULT_CHAT_FRAME:AddMessage` in the interface throws.
class CSimpleScrollingMessageFrame : public CSimpleMessageFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        int32_t m_maxLines = 120;
        // FrameXML declares these on all ten chat windows. The reference gets them from
        // CSimpleHyperlinkedFrame; this type does not derive from it, so they are declared here.
        ScriptIx m_onHyperlinkClick;
        ScriptIx m_onHyperlinkEnter;
        ScriptIx m_onHyperlinkLeave;
        int32_t m_scrollOffset = 0;
        bool m_hyperlinksEnabled = false;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void AddMessage(const char* text, float r, float g, float b, int32_t id);
        virtual void LayoutMessages();
        virtual ScriptIx* GetScriptByName(const char* name, ScriptData& data);

        // Member functions
        CSimpleScrollingMessageFrame(CSimpleFrame* parent);
        bool AtBottom();
        bool AtTop();
        int32_t LinesThatFit();
        void ScrollBy(int32_t lines);
        void ScrollToBottom();
        void ScrollToTop();
        void SetMaxLines(int32_t maxLines);
        void SetScrollOffset(int32_t offset);
};

#endif
