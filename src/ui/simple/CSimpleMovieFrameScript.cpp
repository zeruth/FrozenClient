#include "ui/simple/CSimpleMovieFrameScript.hpp"

#include "ui/simple/CSimpleMovieFrame.hpp"
#include "util/Lua.hpp"
#include <cstdint>

namespace {

CSimpleMovieFrame* This(lua_State* L) {
    auto type = CSimpleMovieFrame::GetObjectType();

    return static_cast<CSimpleMovieFrame*>(FrameScript_GetObjectThis(L, type));
}

} // namespace

int32_t CSimpleMovieFrame_StartMovie(lua_State* L) {
    // There is no video decoder in this client. Report failure the way a missing movie file would
    // and let FrameXML take the path it already has for that -- MovieFrame_OnMovieFinished closes
    // the frame and moves on. Returning success here would leave the interface waiting on a movie
    // that never plays and never ends.
    auto frame = This(L);

    lua_pushnil(L);

    frame->RunOnMovieFinishedScript();

    return 1;
}

int32_t CSimpleMovieFrame_StopMovie(lua_State* L) {
    This(L)->RunOnMovieFinishedScript();

    return 0;
}

int32_t CSimpleMovieFrame_EnableSubtitles(lua_State* L) {
    This(L)->m_subtitlesEnabled = lua_toboolean(L, 2) != 0;

    return 0;
}

int32_t CSimpleMovieFrame_SetMovieOffset(lua_State* L) {
    auto frame = This(L);

    if (lua_type(L, 2) == LUA_TNUMBER) {
        frame->m_offsetX = static_cast<float>(lua_tonumber(L, 2));
    }

    if (lua_type(L, 3) == LUA_TNUMBER) {
        frame->m_offsetY = static_cast<float>(lua_tonumber(L, 3));
    }

    return 0;
}

int32_t CSimpleMovieFrame_GetMovieSubtitleMessage(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

FrameScript_Method SimpleMovieFrameMethods[NUM_SIMPLE_MOVIE_FRAME_SCRIPT_METHODS] = {
    { "StartMovie",               &CSimpleMovieFrame_StartMovie },
    { "StopMovie",                &CSimpleMovieFrame_StopMovie },
    { "EnableSubtitles",          &CSimpleMovieFrame_EnableSubtitles },
    { "SetMovieOffset",           &CSimpleMovieFrame_SetMovieOffset },
    { "GetMovieSubtitleMessage",  &CSimpleMovieFrame_GetMovieSubtitleMessage },
};
