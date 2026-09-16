#include "ui/simple/CSimpleMovieFrame.hpp"

#include "ui/simple/CSimpleMovieFrameScript.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>

int32_t CSimpleMovieFrame::s_metatable;
int32_t CSimpleMovieFrame::s_objectType;

void CSimpleMovieFrame::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleMovieFrame::RegisterScriptMethods);
    CSimpleMovieFrame::s_metatable = ref;
}

int32_t CSimpleMovieFrame::GetObjectType() {
    if (!CSimpleMovieFrame::s_objectType) {
        CSimpleMovieFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleMovieFrame::s_objectType;
}

void CSimpleMovieFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleMovieFrameMethods,
                                              NUM_SIMPLE_MOVIE_FRAME_SCRIPT_METHODS);
}

CSimpleMovieFrame::CSimpleMovieFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
}

FrameScript_Object::ScriptIx* CSimpleMovieFrame::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnMovieFinished")) {
        return &this->m_onMovieFinished;
    }

    if (!SStrCmpI(name, "OnMovieShowSubtitle")) {
        data.wrapper = "return function(self, text) %s end";
        return &this->m_onMovieShowSubtitle;
    }

    if (!SStrCmpI(name, "OnMovieHideSubtitle")) {
        return &this->m_onMovieHideSubtitle;
    }

    return CSimpleFrame::GetScriptByName(name, data);
}

int32_t CSimpleMovieFrame::GetScriptMetaTable() {
    return CSimpleMovieFrame::s_metatable;
}

bool CSimpleMovieFrame::IsA(int32_t type) {
    return type == CSimpleMovieFrame::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

void CSimpleMovieFrame::RunOnMovieFinishedScript() {
    if (this->m_onMovieFinished.luaRef) {
        this->RunScript(this->m_onMovieFinished, 0, nullptr);
    }
}
