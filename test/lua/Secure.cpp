#include "ui/FrameScript.hpp"
#include "ui/FrameScriptInternal.hpp"
#include "util/Lua.hpp"
#include "catch.hpp"
#include <cstring>

// The client's secure builtins (issecure, issecurevariable, forceinsecure, securecall,
// hooksecurefunc) on a bare state, with taint tracked the way FrameScript tracks it.

namespace {

const char* const ADDON = "AddOn A";

int32_t PassError(lua_State* L) {
    return 1;
}

struct SecureFixture {
    lua_State* L;

    SecureFixture() {
        lua_tainted = nullptr;
        lua_taintexpected = 1;
        lua_taintedclosure = 0;

        L = luaL_newstate();
        luaL_openlibs(L);
        luaL_register(L, "_G", FrameScriptInternal::extra_funcs);
        lua_settop(L, 0);

        lua_pushcclosure(L, PassError, 0);
        FrameScript::s_errorHandlerRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    ~SecureFixture() {
        lua_close(L);

        lua_tainted = nullptr;
        lua_taintexpected = 0;
        lua_taintedclosure = 0;
    }

    void Run(const char* code, const char* taint) {
        lua_tainted = taint;
        REQUIRE(luaL_loadstring(L, code) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }

    bool Global(const char* name) {
        lua_pushstring(L, name);
        lua_rawget(L, LUA_GLOBALSINDEX);
        bool value = lua_toboolean(L, -1);
        lua_pop(L, 1);
        return value;
    }
};

}

TEST_CASE("Lua secure builtins", "[lua][secure]") {
    SecureFixture fx;

    SECTION("issecure and forceinsecure") {
        fx.Run("before = issecure() == 1; forceinsecure(); after = issecure()", nullptr);
        CHECK(fx.Global("before"));
        CHECK_FALSE(fx.Global("after"));
        CHECK(!strcmp(lua_tainted, "*** TaintForced ***"));
    }

    SECTION("issecurevariable names the taint that wrote a global") {
        fx.Run("secureValue = 1", nullptr);
        fx.Run("taintedValue = 1", ADDON);
        fx.Run(R"(
            local ok, taint = issecurevariable("secureValue")
            secureOk = ok == 1 and taint == nil
            local ok2, taint2 = issecurevariable("taintedValue")
            taintedOk = ok2 == nil and taint2 == "AddOn A"
        )", nullptr);

        // asking does not taint the asker
        CHECK(lua_tainted == nullptr);
        CHECK(fx.Global("secureOk"));
        CHECK(fx.Global("taintedOk"));
    }

    SECTION("hooksecurefunc keeps a secure function secure and runs the hook") {
        fx.Run("function SecureFunction(x) return x * 2 end", nullptr);
        fx.Run("hooksecurefunc('SecureFunction', function(x) hookedWith = x end)", ADDON);

        fx.Run("doubled = SecureFunction(21)", nullptr);
        CHECK(lua_tainted == nullptr);

        fx.Run(R"(
            hookRan = hookedWith == 21
            local ok = issecurevariable("SecureFunction")
            stillSecure = ok == 1
        )", nullptr);
        CHECK(fx.Global("hookRan"));
        CHECK(fx.Global("stillSecure"));
    }

    SECTION("securecall keeps a tainted callee's taint away from the caller") {
        fx.Run("function TaintedHelper() return 7 end", ADDON);
        // by name: the lookup happens inside securecall, where the taint it reads is contained
        fx.Run("result = securecall('TaintedHelper')", nullptr);
        CHECK(lua_tainted == nullptr);

        fx.Run("ok = result == 7", nullptr);
        CHECK(fx.Global("ok"));
    }
}
