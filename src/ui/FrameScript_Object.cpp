#include "ui/FrameScript_Object.hpp"
#include "util/Lua.hpp"
#include "ui/LuaExtraFuncs.hpp"
#include <cstdint>
#include <storm/String.hpp>

int32_t FrameScript_Object::s_objectTypes = 0;

FrameScript_Object::ScriptIx::~ScriptIx() {
    if (this->luaRef) {
        luaL_unref(FrameScript_GetContext(), LUA_REGISTRYINDEX, this->luaRef);
    }
}

int32_t FrameScript_Object::CreateScriptMetaTable(lua_State* L, void (*a2)(lua_State* L)) {
    lua_createtable(L, 0, 0);
    lua_pushstring(L, "__index");
    lua_createtable(L, 0, 0);

    a2(L);

    lua_settable(L, -3);
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

void FrameScript_Object::FillScriptMethodTable(lua_State *L, FrameScript_Method methods[], int32_t count) {
    for (int32_t i = 0; i < count; i++) {
        lua_pushstring(L, methods[i].name);
        lua_pushcclosure(L, methods[i].method, 0);
        lua_settable(L, -3);
    }
}

// ref: FUN_00819830
// lua_registered 0, lua_objectRef -2 and an empty m_onEvent, all from the member initializers
FrameScript_Object::FrameScript_Object() {
}

const char* FrameScript_Object::GetDisplayName() {
    const char* name = this->GetName();
    return name ? name : "<unnamed>";
}

// The registry reference of this object's Lua table, registering it first if it never was.
// ref: FUN_00488380
int32_t FrameScript_Object::GetLuaObjectRef() {
    if (!this->lua_registered) {
        this->RegisterScriptObject(nullptr);
    }

    return this->lua_objectRef;
}

int32_t FrameScript_Object::GetScript(lua_State* L) {
    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:GetScript(\"type\")", this->GetDisplayName());
        return 0;
    }

    auto name = lua_tostring(L, 2);
    ScriptData data;

    auto script = this->GetScriptByName(name, data);

    if (!script) {
        luaL_error(L, "%s doesn't have a \"%s\" script", this->GetDisplayName(), lua_tostring(L, 2));
        return 0;
    }

    // reading a handler reads the taint it was set under
    if (script->taint && lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = script->taint;
    }

    if (script->luaRef > 0) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, script->luaRef);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

FrameScript_Object::ScriptIx* FrameScript_Object::GetScriptByName(const char* name, FrameScript_Object::ScriptData& data) {
    if (!SStrCmpI(name, "OnEvent", STORM_MAX_STR)) {
        data.wrapper = "return function(self,event,...) %s end";
        return &this->m_onEvent;
    }

    return nullptr;
}

int32_t FrameScript_Object::RegisterScriptEvent(const char* name) {
    auto event = FrameScript::s_scriptEventsHash.Ptr(name);

    if (!event) {
        return 0;
    }

    if (event->pendingSignalCount) {
        auto node = event->unregisterListeners.Head();

        while (node) {
            if (node->listener == this) {
                break;
            }

            node = node->Next();
        }

        if (node) {
            event->unregisterListeners.DeleteNode(node);
        }
    }

    auto node = event->listeners.Head();

    while (node) {
        if (node->listener == this) {
            break;
        }

        node = node->Next();
    }

    if (!node) {
        FrameScript_RegisterScriptEvent(this, event);
    }

    return 1;
}

// ref: FUN_00819880
void FrameScript_Object::RegisterScriptObject(const char* name) {
    auto L = FrameScript_GetContext();

    if (!this->lua_registered) {
        auto savedTaint = lua_tainted;
        lua_taintexpected++;
        if (lua_taintexpected && !lua_taintedclosure) {
            lua_tainted = nullptr;
        }

        lua_createtable(L, 0, 0);
        lua_pushnumber(L, 0.0);
        lua_pushlightuserdata(L, this);
        lua_rawset(L, -3);

        int32_t metatable = this->GetScriptMetaTable();
        lua_rawgeti(L, LUA_REGISTRYINDEX, metatable);
        lua_setmetatable(L, -2);

        this->lua_objectRef = luaL_ref(L, LUA_REGISTRYINDEX);

        if (lua_taintexpected && !lua_taintedclosure) {
            lua_tainted = savedTaint;
        }
        if (--lua_taintexpected <= 0) {
            lua_taintexpected = 0;
        }
    }

    this->lua_registered++;

    if (name) {
        int32_t v8 = 0;

        lua_pushstring(L, name);
        lua_rawget(L, LUA_GLOBALSINDEX);

        if (lua_type(L, -1)) {
            v8 = 1;
        }

        lua_settop(L, -2);

        if (!v8) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, this->lua_objectRef);
            lua_pushstring(L, name);
            lua_insert(L, -2);
            lua_rawset(L, LUA_GLOBALSINDEX);
        }
    }
}

// ref: FUN_0081a2c0
void FrameScript_Object::RunScript(ScriptIx const& script, int32_t argCount, const char* a4) {
    FrameScript_Execute(script.luaRef, this, argCount, a4 ? a4 : script.taint, nullptr);
}

int32_t FrameScript_Object::SetScript(lua_State* L) {
    if (!lua_isstring(L, 2) || lua_type(L, 3) != LUA_TFUNCTION && lua_type(L, 3) != LUA_TNIL) {
        return luaL_error(L, "Usage: %s:SetScript(\"type\", function)", this->GetDisplayName());
    }

    ScriptData data;
    auto script = this->GetScriptByName(lua_tostring(L, 2), data);

    if (!script) {
        return luaL_error(L, "%s doesn't have a \"%s\" script", this->GetDisplayName(), lua_tostring(L, 2));
    }

    if (script->luaRef) {
        luaL_unref(L, LUA_REGISTRYINDEX, script->luaRef);
    }

    auto luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
    script->luaRef = luaRef <= 0 ? 0 : luaRef;
    script->taint = lua_tainted;

    return 0;
}

// ref: FUN_0049edb0 (CSimpleFrame_HookScript; FUN_004a5df0 and FUN_004a7780 are the same body for
// animations and animation groups)
// HookScript(type, function): with no handler set, the function becomes the handler; otherwise
// the handler becomes a closure calling the old handler and then the hook, both under their own
// taints (see FrameScript_HookFunction).
int32_t FrameScript_Object::HookScript(lua_State* L) {
    if (!lua_isstring(L, 2) || lua_type(L, 3) != LUA_TFUNCTION) {
        luaL_error(L, "Usage: %s:HookScript(\"type\", function)", this->GetDisplayName());
        return 0;
    }

    ScriptData data;
    auto script = this->GetScriptByName(lua_tostring(L, 2), data);

    if (!script) {
        luaL_error(L, "%s doesn't have a \"%s\" script", this->GetDisplayName(), lua_tostring(L, 2));
        return 0;
    }

    if (!script->luaRef) {
        auto luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
        script->luaRef = luaRef <= 0 ? 0 : luaRef;
        script->taint = lua_tainted;
        return 0;
    }

    lua_remove(L, 2);

    // the hook is pushed under the handler's taint, and the closure built from both is secure
    auto hookTaint = lua_tainted;
    lua_taintexpected++;
    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = nullptr;
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, script->luaRef);
    luaL_unref(L, LUA_REGISTRYINDEX, script->luaRef);

    auto handlerTaint = script->taint;
    lua_taintexpected++;
    auto closureTaint = lua_tainted;
    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = handlerTaint;
    }

    lua_insert(L, 2);
    lua_settaint(L, 2, handlerTaint);
    lua_settaint(L, 3, hookTaint);
    lua_pushcclosure(L, FrameScript_HookFunction, 2);
    lua_settaint(L, 2, handlerTaint);

    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = closureTaint;
    }
    if (--lua_taintexpected <= 0) {
        lua_taintexpected = 0;
    }

    script->luaRef = luaL_ref(L, LUA_REGISTRYINDEX);

    if (lua_taintexpected && !lua_taintedclosure) {
        lua_tainted = hookTaint;
    }
    if (--lua_taintexpected <= 0) {
        lua_taintexpected = 0;
    }

    return 0;
}

void FrameScript_Object::UnregisterScriptEvent(const char* name) {
    auto event = FrameScript::s_scriptEventsHash.Ptr(name);

    if (!event) {
        return;
    }

    if (event->pendingSignalCount) {
        for (auto node = event->registerListeners.Head(); node; node = event->registerListeners.Next(node)) {
            if (node->listener == this) {
                event->registerListeners.DeleteNode(node);
                break;
            }
        }
    }

    FrameScript_UnregisterScriptEvent(this, event);
}

void FrameScript_Object::UnregisterScriptObject(const char* name) {
    auto L = FrameScript_GetContext();

    if (this->lua_registered > 0) {
        this->lua_registered--;

        if (this->lua_registered == 0) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, this->lua_objectRef);
            lua_pushnumber(L, 0.0);
            lua_pushnil(L);
            lua_rawset(L, -3);
            lua_settop(L, -2);
            luaL_unref(L, LUA_REGISTRYINDEX, this->lua_objectRef);

            this->lua_objectRef = -2;
        }
    }

    if (name) {
        lua_pushnil(L);
        lua_pushstring(L, name);
        lua_insert(L, -2);
        lua_rawset(L, LUA_GLOBALSINDEX);
    }
}

FrameScript_Object* FrameScript_GetObjectThis(lua_State* L, int32_t type) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        luaL_error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
    }

    lua_rawgeti(L, 1, 0);
    auto object = static_cast<FrameScript_Object*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!object) {
        luaL_error(L, "Attempt to find 'this' in non-framescript object");
    }

    if (!object->IsA(type)) {
        luaL_error(L, "Wrong object type for member function");
    }

    return object;
}
