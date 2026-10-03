#include "util/Lua.hpp"
#include <common/Time.hpp>
#include "catch.hpp"
#include <cstring>
#include <string>

// The secure-execution rules the reference's VM implements (docs/ref/parity-lua-vm.md). These
// drive the VM directly through the taint globals, the way FrameScript does.

namespace {

const char* const ADDON = "AddOn A";

struct TaintFixture {
    lua_State* L;

    TaintFixture() {
        lua_tainted = nullptr;
        lua_taintexpected = 0;
        lua_taintedclosure = 0;
        lua_closuretaint = nullptr;
        lua_taintcreate = nullptr;

        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~TaintFixture() {
        lua_close(L);

        lua_tainted = nullptr;
        lua_taintexpected = 0;
        lua_taintedclosure = 0;
    }

    // Compiles and runs a chunk under the given taint, as the client runs an AddOn's file.
    void Run(const char* code, const char* taint) {
        lua_tainted = taint;
        REQUIRE(luaL_loadstring(L, code) == 0);
        REQUIRE(lua_pcall(L, 0, 0, 0) == 0);
    }
};

bool IsTaint(const char* taint, const char* expected) {
    if (!taint || !expected) {
        return taint == expected;
    }

    return !strcmp(taint, expected);
}

}

TEST_CASE("Lua taint", "[lua][taint]") {
    TaintFixture fx;

    SECTION("nothing is tainted while taint is not being tracked") {
        fx.Run("x = { 1, 2, 3 }; y = x[2] + 1", nullptr);
        CHECK(lua_tainted == nullptr);
    }

    SECTION("a value written by tainted code taints secure code that reads it") {
        lua_taintexpected = 1;

        fx.Run("taintedGlobal = { answer = 42 }", ADDON);

        // Secure code that never touches the value stays secure...
        fx.Run("local untouched = 1 + 1", nullptr);
        CHECK(lua_tainted == nullptr);

        // ...and becomes tainted by reading it.
        fx.Run("local v = taintedGlobal", nullptr);
        CHECK(IsTaint(lua_tainted, ADDON));
    }

    SECTION("a value written by secure code keeps secure code secure") {
        lua_taintexpected = 1;

        fx.Run("secureGlobal = { answer = 42 }", nullptr);
        fx.Run("local v = secureGlobal.answer", nullptr);
        CHECK(lua_tainted == nullptr);
    }

    SECTION("calling a function defined by tainted code taints the caller") {
        lua_taintexpected = 1;

        fx.Run("function TaintedFunction() return 1 end", ADDON);
        fx.Run("secureResult = 1", nullptr);
        CHECK(lua_tainted == nullptr);

        // The function value is read from a global the tainted chunk wrote, and its closure
        // carries the taint it was created under, so the call taints the run.
        lua_tainted = nullptr;
        REQUIRE(luaL_loadstring(fx.L, "local f = TaintedFunction") == 0);
        REQUIRE(lua_pcall(fx.L, 0, 0, 0) == 0);
        CHECK(IsTaint(lua_tainted, ADDON));
    }

    SECTION("taint tracking is suspended while a table rehashes") {
        lua_taintexpected = 1;

        // Grow a secure table from secure code: the moved values never taint the run.
        fx.Run("local t = {} for i = 1, 200 do t[i] = i end secureTable = t", nullptr);
        CHECK(lua_tainted == nullptr);
    }

    SECTION("the taint log hears about tainted globals") {
        static int writes;
        static int reads;
        writes = 0;
        reads = 0;

        lua_taintexpected = 1;
        lua_taintloghook = [](lua_State*, int access, const char* name, const char* taint) {
            if (strcmp(name, "logged") || strcmp(taint, ADDON)) {
                return;
            }

            if (access == LUA_TAINTLOG_WRITE) {
                writes++;
            } else if (access == LUA_TAINTLOG_READ) {
                reads++;
            }
        };

        fx.Run("logged = 1", ADDON);
        fx.Run("local v = logged", nullptr);

        lua_taintloghook = nullptr;

        CHECK(writes == 1);
        CHECK(reads == 1);
    }
}

TEST_CASE("Lua script profiling state", "[lua][taint]") {
    // A state opened with profiling on gives every closure a FuncProfile and runs Lua through the
    // profiled interpreter; it must behave exactly like the plain one. Profiling reads the OS
    // clock, which the client starts long before FrameScript.
    OsTimeStartup(OS_TIMING_BEST_AVAILABLE);

    lua_State* L = lua_newstate([](void*, void* ptr, size_t, size_t nsize) -> void* {
        if (nsize == 0) {
            free(ptr);
            return nullptr;
        }

        return realloc(ptr, nsize);
    }, nullptr, 1);

    luaL_openlibs(L);

    REQUIRE(luaL_loadstring(L, R"(
        local function fib(n) if n < 2 then return n end return fib(n - 1) + fib(n - 2) end
        local ok, err = pcall(function() error("x") end)
        local co = coroutine.wrap(function(a) local b = coroutine.yield(a * 2) return b + 1 end)
        local first = co(5)
        profiled = fib(15) + first + co(10) + (ok and 1000 or 0)
    )") == 0);
    REQUIRE(lua_pcall(L, 0, 0, 0) == 0);

    lua_getfield(L, LUA_GLOBALSINDEX, "profiled");
    CHECK(lua_tonumber(L, -1) == 610 + 10 + 11);
    lua_pop(L, 1);

    lua_close(L);
}
