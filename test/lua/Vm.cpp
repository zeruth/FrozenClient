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
        CHECK(lua.Run(R"(
            band = bit.band(12, 10, 8); bor = bit.bor(12, 10); bxor = bit.bxor(12, 10)
            bnot = bit.bnot(0); lshift = bit.lshift(1, 31); rshift = bit.rshift(-1, 28)
            arshift = bit.arshift(-16, 2); mod = bit.mod(-7, 3); modzero = bit.mod(7, 0.5)
            trunc = bit.band(2.9, 7)
        )") == "");
        CHECK(lua.Number("band") == 8);
        CHECK(lua.Number("bor") == 14);
        CHECK(lua.Number("bxor") == 6);
        CHECK(lua.Number("bnot") == 4294967295.0);  // unsigned
        CHECK(lua.Number("lshift") == 2147483648.0);  // unsigned
        CHECK(lua.Number("rshift") == 15);  // logical
        CHECK(lua.Number("arshift") == -4);  // signed
        CHECK(lua.Number("mod") == -1);  // C's truncating %
        CHECK(lua.Number("modzero") == 2);  // 1 / 0.5: the divisor truncated to zero
        CHECK(lua.Number("trunc") == 2);
    }

    SECTION("the table additions: wipe and removemulti") {
        CHECK(lua.Run(R"(
            local t = { a = 1, b = 2, 1, 2, 3 }
            assert(table.wipe(t) == t and next(t) == nil)
            local r = { 10, 20, 30, 40, 50 }
            local x, y = table.removemulti(r, 2, 2)
            removed = x + y
            remaining = table.concat(r, ",")
        )") == "");
        CHECK(lua.String("remaining") == "10,40,50");
        CHECK(lua.Number("removed") == 50);
    }

    SECTION("only the libraries the client has") {
        CHECK(lua.Run("assert(io == nil and os == nil and package == nil and debug == nil)") == "");
        CHECK(lua.Run("assert(print == nil and dofile == nil and loadfile == nil and load == nil)") == "");
        CHECK(lua.Run("assert(math.mod == nil and math.randomseed == nil and math.fmod ~= nil)") == "");
        CHECK(lua.Run("string.gfind('a', 'a')") != "");
        CHECK(lua.Run("local function f(...) return arg end assert(f(1) == nil)") == "");
    }

    SECTION("source details: a UTF-8 BOM is skipped, strings stop at 16 MB") {
        CHECK(lua.Run("\xEF\xBB\xBF" "bom = 1") == "");
        CHECK(lua.Number("bom") == 1);
        CHECK(lua.Run("local s = string.rep('x', 0x800000) local t = s .. s") != "");
    }
}
