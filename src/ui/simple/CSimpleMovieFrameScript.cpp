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

    // The reference raises the usage error rather than answering, and takes the volume as
    // required rather than optional (FUN_00970660).
    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:StartMovie(\"filename\", volume_0_to_255)",
                          frame->GetDisplayName());
    }

    // MovieFrame.lua passes "Interface\Cinematics\Logo_1024"; the reference appends .avi when
    // it opens the file, so the extension belongs here and not in the caller.
    char path[512];
    SStrPrintf(path, sizeof(path), "%s.avi", lua_tostring(L, 2));

    // StartMovie("file", volume_0_to_255); the reference's own usage string names that range.
    int32_t volume = static_cast<int32_t>(lua_tonumber(L, 3));

    if (!frame->StartMovie(path, volume)) {
        // Answer nil and STOP. Do not run OnMovieFinished here.
        //
        // MovieFrame_PlayMovie already handles a refused movie itself: it retries at the other
        // resolution and then falls through to MovieFrame_PlayNextMovie. Firing the finished
        // script from inside StartMovie meant failure re-entered PlayMovie -> StartMovie ->
        // failure, which is a C stack overflow and a hung client, not an error message. The
        // reference pushes 1 or nil from this binding and runs no script at all.
        lua_pushnil(L);

        return 1;
    }

    lua_pushnumber(L, 1.0);

    return 1;
}

// ref: FUN_00970730
//
// Stops and says nothing. It must NOT run OnMovieFinished, and that is not a style choice:
// FrameXML calls StopMovie from inside the finished handler's own chain --
//
//   MovieFrame_OnMovieFinished -> MovieFrame_PlayNextMovie -> self:StopMovie()
//
// so firing the script from here re-enters OnMovieFinished forever. That is the C stack overflow
// the cinematics hit once a movie actually reached its end. The reference's binding is three
// calls and none of them is a script.
int32_t CSimpleMovieFrame_StopMovie(lua_State* L) {
    This(L)->StopMovie();

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
