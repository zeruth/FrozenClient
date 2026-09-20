#include "ui/simple/CSimpleMovieFrameScript.hpp"

#include "ui/simple/CSimpleMovieFrame.hpp"
#include "util/Lua.hpp"
#include <cstdint>
#include <storm/String.hpp>

namespace {

CSimpleMovieFrame* This(lua_State* L) {
    auto type = CSimpleMovieFrame::GetObjectType();

    return static_cast<CSimpleMovieFrame*>(FrameScript_GetObjectThis(L, type));
}

} // namespace

// StartMovie("path", volume) -- the path carries no extension; the container is always .avi.
//
// Answers 1 when the movie is playing and nil when it is not, and on failure fires OnMovieFinished
// so FrameXML takes the same path it takes for a missing file rather than waiting on a movie that
// never ends.
int32_t CSimpleMovieFrame_StartMovie(lua_State* L) {
    auto frame = This(L);

    if (!lua_isstring(L, 2)) {
        lua_pushnil(L);
        frame->RunOnMovieFinishedScript();

        return 1;
    }

    // MovieFrame.lua passes "Interface\Cinematics\Logo_1024"; the reference appends .avi when
    // it opens the file, so the extension belongs here and not in the caller.
    char path[512];
    SStrPrintf(path, sizeof(path), "%s.avi", lua_tostring(L, 2));

    if (!frame->StartMovie(path)) {
        lua_pushnil(L);
        frame->RunOnMovieFinishedScript();

        return 1;
    }

    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t CSimpleMovieFrame_StopMovie(lua_State* L) {
    auto frame = This(L);

    frame->StopMovie();
    frame->RunOnMovieFinishedScript();

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
