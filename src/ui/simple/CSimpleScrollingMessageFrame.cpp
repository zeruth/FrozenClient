#include "ui/simple/CSimpleScrollingMessageFrame.hpp"

#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleFontedFrameFont.hpp"
#include "ui/simple/CSimpleScrollingMessageFrameScript.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/CStatus.hpp"
#include <common/XML.hpp>
#include <storm/String.hpp>

int32_t CSimpleScrollingMessageFrame::s_metatable;
int32_t CSimpleScrollingMessageFrame::s_objectType;

void CSimpleScrollingMessageFrame::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleScrollingMessageFrame::RegisterScriptMethods);
    CSimpleScrollingMessageFrame::s_metatable = ref;
}

int32_t CSimpleScrollingMessageFrame::GetObjectType() {
    if (!CSimpleScrollingMessageFrame::s_objectType) {
        CSimpleScrollingMessageFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleScrollingMessageFrame::s_objectType;
}

void CSimpleScrollingMessageFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleMessageFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(
        L,
        SimpleScrollingMessageFrameMethods,
        NUM_SIMPLE_SCROLLING_MESSAGE_FRAME_SCRIPT_METHODS
    );
}

CSimpleScrollingMessageFrame::CSimpleScrollingMessageFrame(CSimpleFrame* parent)
    : CSimpleMessageFrame(parent) {
    // Chat scrollback holds its history rather than expiring it; fading is opt-in per window.
    this->m_fade = false;
}

void CSimpleScrollingMessageFrame::AddMessage(const char* text, float r, float g, float b, int32_t id) {
    CSimpleMessageFrame::AddMessage(text, r, g, b, id);

    // Drop the oldest lines once the history is full. The newest message is at the head, so the
    // oldest is whatever the tail holds.
    while (this->MessageCount() > this->m_maxLines) {
        auto oldest = this->m_messages.Tail();

        if (!oldest) {
            break;
        }

        this->m_messages.UnlinkNode(oldest);
        delete oldest;
    }

    // A window sitting at the bottom follows new output; one scrolled back stays put, so the line
    // the reader was looking at does not move under them.
    if (this->m_scrollOffset > 0) {
        this->m_scrollOffset++;
    }

    this->LayoutMessages();
}

int32_t CSimpleScrollingMessageFrame::GetScriptMetaTable() {
    return CSimpleScrollingMessageFrame::s_metatable;
}

bool CSimpleScrollingMessageFrame::IsA(int32_t type) {
    return type == CSimpleScrollingMessageFrame::s_objectType
        || type == CSimpleMessageFrame::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

bool CSimpleScrollingMessageFrame::AtBottom() {
    return this->m_scrollOffset <= 0;
}

bool CSimpleScrollingMessageFrame::AtTop() {
    return this->m_scrollOffset >= this->MessageCount() - this->LinesThatFit();
}

int32_t CSimpleScrollingMessageFrame::LinesThatFit() {
    float lineHeight = this->m_font ? this->m_font->m_attributes.m_fontHeight : 0.0f;

    if (lineHeight <= 0.0f) {
        return 1;
    }

    auto fit = static_cast<int32_t>(this->GetHeight() / lineHeight);

    return fit < 1 ? 1 : fit;
}

void CSimpleScrollingMessageFrame::LayoutMessages() {
    // Walk from the newest message, skip `m_scrollOffset` of them, then anchor as many as fit
    // upward from the bottom edge. Everything outside that window is hidden rather than moved.
    int32_t skip = this->m_scrollOffset;
    int32_t shown = 0;
    int32_t capacity = this->LinesThatFit();
    float offset = 0.0f;

    for (auto node = this->m_messages.Head(); node; node = this->m_messages.Link(node)->Next()) {
        if (!node->string) {
            continue;
        }

        if (skip > 0) {
            skip--;
            node->string->Hide();
            continue;
        }

        if (shown >= capacity) {
            node->string->Hide();
            continue;
        }

        node->string->ClearAllPoints();
        node->string->SetPoint(FRAMEPOINT_BOTTOMLEFT, this, FRAMEPOINT_BOTTOMLEFT, 0.0f, offset, 1);
        node->string->Show();

        offset += CSimpleTop::RoundToPixelHeight(node->string->GetHeight());
        shown++;
    }
}

void CSimpleScrollingMessageFrame::LoadXML(const XMLNode* node, CStatus* status) {
    CSimpleMessageFrame::LoadXML(node, status);

    const char* maxLinesAttr = node->GetAttributeByName("maxLines");
    if (maxLinesAttr && *maxLinesAttr) {
        this->SetMaxLines(SStrToInt(maxLinesAttr));
    }
}

void CSimpleScrollingMessageFrame::ScrollBy(int32_t lines) {
    this->SetScrollOffset(this->m_scrollOffset + lines);
}

void CSimpleScrollingMessageFrame::ScrollToBottom() {
    this->SetScrollOffset(0);
}

void CSimpleScrollingMessageFrame::ScrollToTop() {
    this->SetScrollOffset(this->MessageCount() - this->LinesThatFit());
}

void CSimpleScrollingMessageFrame::SetMaxLines(int32_t maxLines) {
    if (maxLines < 1) {
        return;
    }

    this->m_maxLines = maxLines;

    while (this->MessageCount() > this->m_maxLines) {
        auto oldest = this->m_messages.Tail();

        if (!oldest) {
            break;
        }

        this->m_messages.UnlinkNode(oldest);
        delete oldest;
    }

    this->LayoutMessages();
}

void CSimpleScrollingMessageFrame::SetScrollOffset(int32_t offset) {
    int32_t highest = this->MessageCount() - this->LinesThatFit();

    if (highest < 0) {
        highest = 0;
    }

    this->m_scrollOffset = offset < 0 ? 0 : (offset > highest ? highest : offset);

    this->LayoutMessages();
}

FrameScript_Object::ScriptIx* CSimpleScrollingMessageFrame::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnHyperlinkClick")) {
        data.wrapper = "return function(self, link, text, button) %s end";
        return &this->m_onHyperlinkClick;
    }

    if (!SStrCmpI(name, "OnHyperlinkEnter")) {
        data.wrapper = "return function(self, link, text) %s end";
        return &this->m_onHyperlinkEnter;
    }

    if (!SStrCmpI(name, "OnHyperlinkLeave")) {
        data.wrapper = "return function(self, link, text) %s end";
        return &this->m_onHyperlinkLeave;
    }

    return CSimpleMessageFrame::GetScriptByName(name, data);
}
