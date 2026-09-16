#include "ui/simple/CSimpleScrollingMessageFrameScript.hpp"

#include "ui/simple/CSimpleScrollingMessageFrame.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

namespace {

CSimpleScrollingMessageFrame* This(lua_State* L) {
    auto type = CSimpleScrollingMessageFrame::GetObjectType();

    return static_cast<CSimpleScrollingMessageFrame*>(FrameScript_GetObjectThis(L, type));
}

} // namespace

int32_t CSimpleScrollingMessageFrame_ScrollUp(lua_State* L) {
    This(L)->ScrollBy(1);

    return 0;
}

int32_t CSimpleScrollingMessageFrame_ScrollDown(lua_State* L) {
    This(L)->ScrollBy(-1);

    return 0;
}

int32_t CSimpleScrollingMessageFrame_PageUp(lua_State* L) {
    auto frame = This(L);
    frame->ScrollBy(frame->LinesThatFit());

    return 0;
}

int32_t CSimpleScrollingMessageFrame_PageDown(lua_State* L) {
    auto frame = This(L);
    frame->ScrollBy(-frame->LinesThatFit());

    return 0;
}

int32_t CSimpleScrollingMessageFrame_ScrollToTop(lua_State* L) {
    This(L)->ScrollToTop();

    return 0;
}

int32_t CSimpleScrollingMessageFrame_ScrollToBottom(lua_State* L) {
    This(L)->ScrollToBottom();

    return 0;
}

int32_t CSimpleScrollingMessageFrame_AtTop(lua_State* L) {
    lua_pushboolean(L, This(L)->AtTop());

    return 1;
}

int32_t CSimpleScrollingMessageFrame_AtBottom(lua_State* L) {
    lua_pushboolean(L, This(L)->AtBottom());

    return 1;
}

int32_t CSimpleScrollingMessageFrame_SetMaxLines(lua_State* L) {
    if (lua_type(L, 2) == LUA_TNUMBER) {
        This(L)->SetMaxLines(static_cast<int32_t>(lua_tonumber(L, 2)));
    }

    return 0;
}

int32_t CSimpleScrollingMessageFrame_GetMaxLines(lua_State* L) {
    lua_pushnumber(L, This(L)->m_maxLines);

    return 1;
}

int32_t CSimpleScrollingMessageFrame_GetNumMessages(lua_State* L) {
    lua_pushnumber(L, This(L)->MessageCount());

    return 1;
}

int32_t CSimpleScrollingMessageFrame_GetNumLinesDisplayed(lua_State* L) {
    lua_pushnumber(L, This(L)->LinesThatFit());

    return 1;
}

int32_t CSimpleScrollingMessageFrame_GetCurrentScroll(lua_State* L) {
    lua_pushnumber(L, This(L)->m_scrollOffset);

    return 1;
}

int32_t CSimpleScrollingMessageFrame_SetHyperlinksEnabled(lua_State* L) {
    This(L)->m_hyperlinksEnabled = lua_toboolean(L, 2) != 0;

    return 0;
}

FrameScript_Method SimpleScrollingMessageFrameMethods[NUM_SIMPLE_SCROLLING_MESSAGE_FRAME_SCRIPT_METHODS] = {
    { "ScrollUp",                   &CSimpleScrollingMessageFrame_ScrollUp },
    { "ScrollDown",                 &CSimpleScrollingMessageFrame_ScrollDown },
    { "PageUp",                     &CSimpleScrollingMessageFrame_PageUp },
    { "PageDown",                   &CSimpleScrollingMessageFrame_PageDown },
    { "ScrollToTop",                &CSimpleScrollingMessageFrame_ScrollToTop },
    { "ScrollToBottom",             &CSimpleScrollingMessageFrame_ScrollToBottom },
    { "AtTop",                      &CSimpleScrollingMessageFrame_AtTop },
    { "AtBottom",                   &CSimpleScrollingMessageFrame_AtBottom },
    { "SetMaxLines",                &CSimpleScrollingMessageFrame_SetMaxLines },
    { "GetMaxLines",                &CSimpleScrollingMessageFrame_GetMaxLines },
    { "GetNumMessages",             &CSimpleScrollingMessageFrame_GetNumMessages },
    { "GetNumLinesDisplayed",       &CSimpleScrollingMessageFrame_GetNumLinesDisplayed },
    { "GetCurrentScroll",           &CSimpleScrollingMessageFrame_GetCurrentScroll },
    { "SetHyperlinksEnabled",       &CSimpleScrollingMessageFrame_SetHyperlinksEnabled },
};
