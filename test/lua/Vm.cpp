#include "util/Lua.hpp"
#include "catch.hpp"
#include <string>

namespace {

// A bare state with the standard libraries, as the client opens them.
struct LuaFixture {
    lua_State* L;

    LuaFixture() {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    ~LuaFixture() {
        lua_close(L);
    }

    // Runs a chunk; returns the error text, or an empty string on success.
    std::string Run(const char* code) {
        if (luaL_loadstring(L, code) || lua_pcall(L, 0, LUA_MULTRET, 0)) {
            std::string error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "(non-string error)";
            lua_pop(L, 1);
            return error;
        }

        return "";
    }

    double Number(const char* global) {
        lua_getfield(L, LUA_GLOBALSINDEX, global);
        double value = lua_tonumber(L, -1);
        lua_pop(L, 1);
        return value;
    }

    std::string String(const char* global) {
        lua_getfield(L, LUA_GLOBALSINDEX, global);
        std::string value = lua_tostring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 1);
        return value;
    }
};

}

TEST_CASE("Lua VM basics", "[lua]") {
    LuaFixture lua;

    SECTION("arithmetic, closures, varargs and tail calls") {
        CHECK(lua.Run(R"(
            local function counter()
                local n = 0
                return function(step) n = n + (step or 1); return n end
            end
            local c = counter()
            c(); c(5)
            result = c()

            local function sum(...)
                local total = 0
                for i = 1, select("#", ...) do total = total + select(i, ...) end
                return total
            end
            varargs = sum(1, 2, 3, 4)

            local function loop(n, acc) if n == 0 then return acc end return loop(n - 1, acc + n) end
            tail = loop(10000, 0)
        )") == "");

        CHECK(lua.Number("result") == 7);
        CHECK(lua.Number("varargs") == 10);
        CHECK(lua.Number("tail") == 50005000);
    }

    SECTION("tables, metatables and the string library") {
        CHECK(lua.Run(R"(
            local t = setmetatable({}, { __index = function(_, k) return k * 2 end })
            indexed = t[21]
            local parts = {}
            for word in string.gmatch("alpha beta gamma", "%a+") do parts[#parts + 1] = word:upper() end
            joined = table.concat(parts, ",")
            formatted = string.format("%d-%s", 7, "x")
        )") == "");

        CHECK(lua.Number("indexed") == 42);
        CHECK(lua.String("joined") == "ALPHA,BETA,GAMMA");
        CHECK(lua.String("formatted") == "7-x");
    }

    SECTION("errors unwind through pcall and coroutines resume") {
        CHECK(lua.Run(R"(
            local ok, err = pcall(function() error("boom", 0) end)
            caught = (not ok) and err
            local co = coroutine.create(function(a) local b = coroutine.yield(a + 1); return b * 2 end)
            local _, first = coroutine.resume(co, 1)
            local _, second = coroutine.resume(co, 10)
            resumed = first + second
        )") == "");

        CHECK(lua.String("caught") == "boom");
        CHECK(lua.Number("resumed") == 22);
    }

    SECTION("the bit library the client adds") {
        CHECK(lua.Run("band = bit.band(12, 10); bor = bit.bor(12, 10)") == "");
        CHECK(lua.Number("band") == 8);
        CHECK(lua.Number("bor") == 14);
    }
}
