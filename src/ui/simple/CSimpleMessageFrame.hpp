#ifndef UI_SIMPLE_C_SIMPLE_MESSAGE_FRAME_HPP
#define UI_SIMPLE_C_SIMPLE_MESSAGE_FRAME_HPP

#include "ui/simple/CSimpleFontedFrame.hpp"
#include "ui/simple/CSimpleFrame.hpp"
#include <tempest/Vector.hpp>
#include <storm/List.hpp>

class CSimpleFontString;
class CSimpleFontedFrameFont;
class CStatus;
class XMLNode;

// Where a new message goes relative to the ones already shown.
enum MESSAGE_INSERT_MODE {
    INSERT_MODE_TOP = 0x0,
    INSERT_MODE_BOTTOM = 0x1,
};

// One displayed message. It owns the font string that draws it; `age` runs from zero and drives
// first the hold and then the fade.
struct MESSAGENODE : TSLinkedNode<MESSAGENODE> {
    CSimpleFontString* string = nullptr;
    CImVector color = { 255, 255, 255, 255 };
    float age = 0.0f;
    int32_t id = 0;

    ~MESSAGENODE();
};

// A stack of transient text messages that fade away on their own -- UIErrorsFrame, RaidWarningFrame
// and the zone-change banners are all this type. Without it the interface's own error reporting
// frame does not exist, so the first script error becomes a nil-index error on top of it.
class CSimpleMessageFrame : public CSimpleFrame, public CSimpleFontedFrame {
    public:
        // Static variables
        static int32_t s_metatable;
        static int32_t s_objectType;

        // Static functions
        static void CreateScriptMetaTable();
        static int32_t GetObjectType();
        static void RegisterScriptMethods(lua_State* L);

        // Member variables
        STORM_LIST(MESSAGENODE) m_messages;
        CSimpleFontedFrameFont* m_font = nullptr;
        MESSAGE_INSERT_MODE m_insertMode = INSERT_MODE_BOTTOM;
        float m_displayDuration = 10.0f;
        float m_fadeDuration = 3.0f;
        bool m_fade = true;

        // Virtual member functions
        virtual bool IsA(int32_t type);
        virtual int32_t GetScriptMetaTable();
        virtual void LoadXML(const XMLNode* node, CStatus* status);
        virtual void OnLayerUpdate(float elapsedSec);
        virtual void FontUpdated(CSimpleFontedFrameFont* font, int32_t a3);
        virtual void AddMessage(const char* text, float r, float g, float b, int32_t id);
        virtual void LayoutMessages();

        // Member functions
        CSimpleMessageFrame(CSimpleFrame* parent);
        void Clear();
        int32_t MessageCount();
};

#endif
