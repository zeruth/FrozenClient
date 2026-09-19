#include "ui/simple/CSimpleScrollFrameScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/simple/CSimpleScrollFrame.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

// ref: FUN_00972210
// GetScrollChild was implemented while this was a stub, so every scroll frame in the interface
// could be asked what it was scrolling and none could be told.
int32_t CSimpleScrollFrame_SetScrollChild(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    // Nil detaches whatever is there. The reference takes this path before any other check.
    if (lua_type(L, 2) == LUA_TNIL) {
        scrollFrame->SetScrollChild(nullptr);

        return 0;
    }

    if (lua_isstring(L, 2)) {
        // The reference looks the name up in its frame-name registry and, when that misses, says
        // so. Frozen keeps no such registry -- frames are reached through their Lua objects -- so
        // every name takes the miss path. FrameXML passes the frame itself everywhere it calls
        // this, so nothing shipped depends on the name form.
        return luaL_error(L, "%s:SetScrollChild(): Couldn't find frame named '%s'",
                          scrollFrame->GetDisplayName(), lua_tostring(L, 2));
    }

    if (lua_type(L, 2) != LUA_TTABLE) {
        return luaL_error(L, "%s:SetScrollChild(): Couldn't find frame named '%s'",
                          scrollFrame->GetDisplayName(), lua_tostring(L, 2));
    }

    lua_rawgeti(L, 2, 0);
    auto child = static_cast<CSimpleFrame*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!child) {
        return luaL_error(L, "%s:SetScrollChild(): Couldn't find 'this' in child object",
                          scrollFrame->GetDisplayName());
    }

    if (!child->IsA(CSimpleFrame::GetObjectType())) {
        return luaL_error(L, "%s:SetScrollChild(): Wrong child object type, expected frame",
                          scrollFrame->GetDisplayName());
    }

    // Walking up from the scroll frame, the prospective child must not already be an ancestor.
    // Without this a frame can be made to scroll one of its own parents, and the layout pass then
    // recurses until the stack runs out.
    for (auto ancestor = static_cast<CSimpleFrame*>(scrollFrame); ancestor;
         ancestor = ancestor->m_parent) {
        if (ancestor == child) {
            return luaL_error(L, "%s:SetScrollChild(): Would create a loop adding child %s",
                              scrollFrame->GetDisplayName(), child->GetDisplayName());
        }
    }

    scrollFrame->SetScrollChild(child);

    return 0;
}

int32_t CSimpleScrollFrame_GetScrollChild(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    auto child = scrollFrame->m_scrollChild;

    if (!child) {
        lua_pushnil(L);

        return 1;
    }

    if (!child->lua_registered) {
        child->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, child->lua_objectRef);

    return 1;
}

int32_t CSimpleScrollFrame_SetHorizontalScroll(lua_State* L) {
    // TODO horizontal scrolling; nothing in the shipped interface scrolls sideways
    return 0;
}

int32_t CSimpleScrollFrame_SetVerticalScroll(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    if (!scrollFrame->ProtectedFunctionsAllowed()) {
        // TODO handle check

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetVerticalScroll(offset)", scrollFrame->GetDisplayName());
    }

    float offset = lua_tonumber(L, 2);
    float ndcOffset = offset / (CoordinateGetAspectCompensation() * 1024.0f);
    float ddcOffset = NDCToDDCWidth(ndcOffset);

    scrollFrame->SetVerticalScroll(ddcOffset);

    return 0;
}

int32_t CSimpleScrollFrame_GetHorizontalScroll(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    float ddcOffset = CoordinateGetAspectCompensation() * 1024.0f * scrollFrame->m_scrollOffset.x;
    float ndcOffset = DDCToNDCWidth(ddcOffset);

    lua_pushnumber(L, ndcOffset);

    return 1;
}

int32_t CSimpleScrollFrame_GetVerticalScroll(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    float ddcOffset = CoordinateGetAspectCompensation() * 1024.0f * scrollFrame->m_scrollOffset.y;
    float ndcOffset = DDCToNDCWidth(ddcOffset);

    lua_pushnumber(L, ndcOffset);

    return 1;
}

int32_t CSimpleScrollFrame_GetHorizontalScrollRange(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    float ddcRange = CoordinateGetAspectCompensation() * 1024.0f * scrollFrame->m_scrollRange.x;
    float ndcRange = DDCToNDCWidth(ddcRange);

    lua_pushnumber(L, ndcRange);

    return 1;
}

int32_t CSimpleScrollFrame_GetVerticalScrollRange(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    float ddcRange = CoordinateGetAspectCompensation() * 1024.0f * scrollFrame->m_scrollRange.y;
    float ndcRange = DDCToNDCWidth(ddcRange);

    lua_pushnumber(L, ndcRange);

    return 1;
}

// ref: FUN_009727b0
// Raises the dirty flag and nothing else. The recalculation itself happens on the next layer
// update, so this is the whole of the reference function -- the name promises more than it does.
int32_t CSimpleScrollFrame_UpdateScrollChildRect(lua_State* L) {
    auto type = CSimpleScrollFrame::GetObjectType();
    auto scrollFrame = static_cast<CSimpleScrollFrame*>(FrameScript_GetObjectThis(L, type));

    scrollFrame->m_updateScrollChild = 1;

    return 0;
}

FrameScript_Method SimpleScrollFrameMethods[NUM_SIMPLE_SCROLL_FRAME_SCRIPT_METHODS] = {
    { "SetScrollChild",             &CSimpleScrollFrame_SetScrollChild },
    { "GetScrollChild",             &CSimpleScrollFrame_GetScrollChild },
    { "SetHorizontalScroll",        &CSimpleScrollFrame_SetHorizontalScroll },
    { "SetVerticalScroll",          &CSimpleScrollFrame_SetVerticalScroll },
    { "GetHorizontalScroll",        &CSimpleScrollFrame_GetHorizontalScroll },
    { "GetVerticalScroll",          &CSimpleScrollFrame_GetVerticalScroll },
    { "GetHorizontalScrollRange",   &CSimpleScrollFrame_GetHorizontalScrollRange },
    { "GetVerticalScrollRange",     &CSimpleScrollFrame_GetVerticalScrollRange },
    { "UpdateScrollChildRect",      &CSimpleScrollFrame_UpdateScrollChildRect }
};
