#include "ui/LuaExtraFuncs.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScriptInternal.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>
#include <cstring>
#include <ctime>
#include <storm/String.hpp>

luaL_Reg FrameScriptInternal::extra_funcs[31] = {
    { "setglobal", &sub_8168D0 },
    { "getglobal", &sub_816910 },
    { "strtrim", &strtrim },
    { "strsplit", &strsplit },
    { "strjoin", &strjoin },
    { "strreplace", &sub_816C40 },
    { "strconcat", &sub_816D80 },
    { "strlenutf8", &strlenutf8 },
    { "issecure", &issecure },
    { "issecurevariable", &issecurevariable },
    { "forceinsecure", &forceinsecure },
    { "securecall", &securecall },
    { "hooksecurefunc", &hooksecurefunc },
    { "debugload", &debugload },
    { "debuginfo", &debuginfo },
    { "debugprint", &debugprint },
    { "debugdump", &debugdump },
    { "debugbreak", &debugbreak },
    { "debughook", &debughook },
    { "debugtimestamp", &debugtimestamp },
    { "debugprofilestart", &debugprofilestart },
    { "debugprofilestop", &debugprofilestop },
    { "seterrorhandler", &seterrorhandler },
    { "geterrorhandler", &geterrorhandler },
    { "date", &os_date },
    { "time", &os_time },
    { "difftime", &os_difftime },
    { "debugstack", &debugstack },
    { "debuglocals", &debuglocals },
    { "scrub", &scrub },
    { nullptr, nullptr }
};

int32_t sub_8168D0(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t sub_816910(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00816960
// strtrim(s [, chars]) -- chars defaults to " \t\r\n", read out of the reference at 00a16860
// rather than assumed. Trims from both ends; a string made entirely of trim characters comes back
// empty rather than untouched.
int32_t strtrim(lua_State* L) {
    size_t length = 0;
    auto text = luaL_checklstring(L, 1, &length);
    auto chars = luaL_optlstring(L, 2, " \t\r\n", nullptr);

    auto trimmed = [chars](char c) {
        for (auto p = chars; *p; p++) {
            if (*p == c) {
                return true;
            }
        }

        return false;
    };

    size_t first = 0;
    while (first < length && trimmed(text[first])) {
        first++;
    }

    size_t last = length;
    while (last > first && trimmed(text[last - 1])) {
        last--;
    }

    lua_pushlstring(L, text + first, last - first);

    return 1;
}

// ref: FUN_00816a60
// strsplit(delimiters, s [, limit]) -- delimiters FIRST, which is the argument order the reference
// reads and the opposite of what the name suggests. Any character in the first argument splits.
//
// limit caps the number of pieces: once one short of it, the rest of the string comes back whole,
// separators and all. Zero or absent means no cap.
int32_t strsplit(lua_State* L) {
    size_t delimLength = 0;
    size_t length = 0;
    auto delims = luaL_checklstring(L, 1, &delimLength);
    auto text = luaL_checklstring(L, 2, &length);
    auto limit = static_cast<int32_t>(luaL_optinteger(L, 3, 0));

    auto isDelim = [delims, delimLength](char c) {
        for (size_t i = 0; i < delimLength; i++) {
            if (delims[i] == c) {
                return true;
            }
        }

        return false;
    };

    lua_settop(L, 0);

    int32_t pieces = 0;
    size_t start = 0;

    for (size_t i = 0; i <= length; i++) {
        auto atEnd = i == length;

        if (limit > 0 && pieces == limit - 1 && !atEnd) {
            continue;
        }

        if (atEnd || isDelim(text[i])) {
            lua_pushlstring(L, text + start, i - start);
            pieces++;
            start = i + 1;
        }
    }

    return pieces;
}

// ref: FUN_00816b60
// strjoin(delimiter, ...) -- the delimiter goes between the remaining arguments, not after each,
// so a single argument comes back unchanged.
int32_t strjoin(lua_State* L) {
    size_t delimLength = 0;
    auto delim = luaL_checklstring(L, 1, &delimLength);
    auto count = lua_gettop(L);

    if (count < 2) {
        lua_pushlstring(L, "", 0);

        return 1;
    }

    // Pushed in interleaved order -- value, separator, value -- and concatenated in one go. The
    // separators cannot simply be pushed after the values: lua_concat joins the top N as they lie,
    // so that would yield every value followed by every delimiter.
    int32_t pushed = 0;

    for (int32_t i = 2; i <= count; i++) {
        if (i > 2) {
            lua_pushlstring(L, delim, delimLength);
            pushed++;
        }

        lua_pushvalue(L, i);
        pushed++;
    }

    lua_concat(L, pushed);

    return 1;
}

// ref: FUN_00816c40
// strreplace(s, needle, replacement [, limit]) -> newString, count
//
// Case-sensitive, non-overlapping, left to right. limit caps the number of replacements; zero or
// absent means every occurrence. When nothing matched, the subject comes back as the same string
// rather than a copy, and the count is 0.
//
// The 4096-byte working buffer is the reference's, and so is the silent truncation when the result
// outgrows it -- a caller that replaces its way past 4KB gets a short string back with no error.
// Kept because the cap is observable behaviour that FrameXML could depend on.
int32_t sub_816C40(lua_State* L) {
    size_t length = 0;
    size_t needleLength = 0;
    size_t replacementLength = 0;
    auto text = luaL_checklstring(L, 1, &length);
    auto needle = luaL_checklstring(L, 2, &needleLength);
    auto replacement = luaL_checklstring(L, 3, &replacementLength);
    auto limit = static_cast<int32_t>(luaL_optinteger(L, 4, 0));

    char buffer[4096];
    auto out = buffer;
    auto remaining = sizeof(buffer);
    auto cursor = text;
    int32_t count = 0;

    while (limit == 0 || count < limit) {
        auto found = SStrStr(cursor, needle);

        if (!found) {
            break;
        }

        auto head = static_cast<size_t>(found - cursor);
        head = head < remaining ? head : remaining;
        memcpy(out, cursor, head);
        out += head;
        remaining -= head;

        // The reference clamps this length and then copies the unclamped one, overrunning the
        // buffer on a result that lands within a replacement's width of 4096. Clamped properly
        // here: a truncated result is the reference's visible behaviour, the overrun is not.
        auto tail = replacementLength < remaining ? replacementLength : remaining;
        memcpy(out, replacement, tail);
        out += tail;
        remaining -= tail;

        cursor = found + needleLength;
        count++;
    }

    if (count < 1) {
        lua_pushlstring(L, text, length);
    } else {
        auto rest = static_cast<size_t>(length - (cursor - text));
        rest = rest < remaining ? rest : remaining;
        memcpy(out, cursor, rest);
        out += rest;

        lua_pushlstring(L, buffer, out - buffer);
    }

    lua_pushinteger(L, count);

    return 2;
}

// ref: FUN_00816d80
// Everything on the stack, joined with nothing between. Two lines in the reference and two here.
int32_t sub_816D80(lua_State* L) {
    lua_concat(L, lua_gettop(L));

    return 1;
}

// ref: FUN_00817c70
// Characters rather than bytes, so an accented name measures the way a player would count it.
// Errors on a non-string instead of coercing, which is why this takes lua_isstring rather than
// luaL_checklstring.
int32_t strlenutf8(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        luaL_error(L, "Usage: strlenutf8(string)");

        return 0;
    }

    auto text = lua_tolstring(L, 1, nullptr);

    lua_pushnumber(L, static_cast<double>(SStrLenUTF8(text)));

    return 1;
}

int32_t issecure(lua_State* L) {
    // TODO taint check

    lua_pushnumber(L, 1.0);

    return 1;
}

int32_t issecurevariable(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t forceinsecure(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t securecall(lua_State* L) {
    // TODO taint checks and management

    // If string is provided, resolve to actual function
    if (lua_isstring(L, 1)) {
        auto fnName = lua_tostring(L, 1);
        lua_pushstring(L, fnName);
        lua_rawget(L, LUA_GLOBALSINDEX);
        lua_remove(L, 1);
        lua_insert(L, 1);
    }

    if (lua_gettop(L) == 0) {
        lua_pushnil(L);
    }

    // Set up error handler
    lua_rawgeti(L, LUA_REGISTRYINDEX, FrameScript::s_errorHandlerRef);
    lua_insert(L, 1);

    // Make function call
    auto nargs = lua_gettop(L) - 2;
    if (lua_pcall(L, nargs, -1, 1)) {
        lua_settop(L, -3);
    } else {
        lua_remove(L, 1);
    }

    auto top = lua_gettop(L);

    return top;
}

int32_t hooksecurefunc(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugload(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debuginfo(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugprint(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugdump(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugbreak(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debughook(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugtimestamp(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugprofilestart(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t debugprofilestop(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t seterrorhandler(lua_State* L) {
    if (lua_type(L, 1) != LUA_TFUNCTION) {
        luaL_error(L, "Usage: seterrorhandler(errfunc)");
        return 0;
    }

    if (FrameScript::s_errorHandlerFun != -1) {
        luaL_unref(L, LUA_REGISTRYINDEX, FrameScript::s_errorHandlerFun);
    }

    FrameScript::s_errorHandlerFun = luaL_ref(L, LUA_REGISTRYINDEX);

    return 0;
}

int32_t geterrorhandler(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// Lua 5.1 exposes these from the os library; the interface uses them as plain globals. They were
// stubbed, and date() returning nothing is what left the interface's own error frame unable to
// format an error -- turning one error into an unbounded stream of them.

namespace {

void SetDateField(lua_State* L, const char* key, int32_t value) {
    lua_pushstring(L, key);
    lua_pushnumber(L, value);
    lua_settable(L, -3);
}

void SetDateFlag(lua_State* L, const char* key, bool value) {
    lua_pushstring(L, key);
    lua_pushboolean(L, value);
    lua_settable(L, -3);
}

int32_t GetDateField(lua_State* L, const char* key, int32_t fallback) {
    lua_pushstring(L, key);
    lua_gettable(L, -2);

    int32_t value = fallback;

    if (lua_type(L, -1) == LUA_TNUMBER) {
        value = static_cast<int32_t>(lua_tonumber(L, -1));
    }

    lua_settop(L, -2);

    return value;
}

} // namespace

int32_t os_date(lua_State* L) {
    const char* format = lua_type(L, 1) == LUA_TSTRING ? lua_tolstring(L, 1, nullptr) : "%c";

    time_t when = lua_type(L, 2) == LUA_TNUMBER
        ? static_cast<time_t>(lua_tonumber(L, 2))
        : time(nullptr);

    // A leading "!" asks for UTC rather than local time.
    bool utc = *format == '!';

    if (utc) {
        format++;
    }

    struct tm parts;

    if (utc ? gmtime_s(&parts, &when) : localtime_s(&parts, &when)) {
        lua_pushnil(L);

        return 1;
    }

    // "*t" asks for the broken-down time as a table instead of a formatted string.
    if (!SStrCmp(format, "*t", STORM_MAX_STR)) {
        lua_newtable(L);

        SetDateField(L, "year", parts.tm_year + 1900);
        SetDateField(L, "month", parts.tm_mon + 1);
        SetDateField(L, "day", parts.tm_mday);
        SetDateField(L, "hour", parts.tm_hour);
        SetDateField(L, "min", parts.tm_min);
        SetDateField(L, "sec", parts.tm_sec);
        SetDateField(L, "wday", parts.tm_wday + 1);
        SetDateField(L, "yday", parts.tm_yday + 1);
        SetDateFlag(L, "isdst", parts.tm_isdst > 0);

        return 1;
    }

    char text[256];

    if (!strftime(text, sizeof(text), format, &parts)) {
        text[0] = 0;
    }

    lua_pushstring(L, text);

    return 1;
}

int32_t os_time(lua_State* L) {
    // With no table argument this is just the current time.
    if (lua_type(L, 1) != LUA_TTABLE) {
        lua_pushnumber(L, static_cast<double>(time(nullptr)));

        return 1;
    }

    struct tm parts;
    memset(&parts, 0, sizeof(parts));

    lua_settop(L, 1);

    parts.tm_year = GetDateField(L, "year", 1970) - 1900;
    parts.tm_mon = GetDateField(L, "month", 1) - 1;
    parts.tm_mday = GetDateField(L, "day", 1);
    parts.tm_hour = GetDateField(L, "hour", 12);
    parts.tm_min = GetDateField(L, "min", 0);
    parts.tm_sec = GetDateField(L, "sec", 0);
    parts.tm_isdst = -1;

    time_t when = mktime(&parts);

    if (when == static_cast<time_t>(-1)) {
        lua_pushnil(L);
    } else {
        lua_pushnumber(L, static_cast<double>(when));
    }

    return 1;
}

int32_t os_difftime(lua_State* L) {
    auto later = static_cast<time_t>(lua_tonumber(L, 1));
    auto earlier = lua_type(L, 2) == LUA_TNUMBER
        ? static_cast<time_t>(lua_tonumber(L, 2))
        : static_cast<time_t>(0);

    lua_pushnumber(L, difftime(later, earlier));

    return 1;
}

int32_t debugstack(lua_State* L) {
    // Returns a printable call stack. Blizzard_DebugTools is the interface's own error handler and
    // concatenates this into its message, so a stub that returns nothing turns every single error
    // into a second error inside the handler -- 4173 of them in one run before this was written.
    //
    // debugstack([start[, count1[, count2]]]); the two counts select how many frames to show from
    // the top and the bottom. Only the start and a combined limit are honoured here.
    int32_t start = 1;
    int32_t limit = 12;

    if (lua_type(L, 1) == LUA_TNUMBER) {
        start = static_cast<int32_t>(lua_tonumber(L, 1));
    }

    if (lua_type(L, 2) == LUA_TNUMBER) {
        limit = static_cast<int32_t>(lua_tonumber(L, 2));
    }

    if (start < 1) {
        start = 1;
    }

    char stack[4096];
    stack[0] = 0;

    size_t used = 0;
    lua_Debug info;

    for (int32_t level = start; level < start + limit; level++) {
        if (!lua_getstack(L, level, &info)) {
            break;
        }

        lua_getinfo(L, "Snl", &info);

        const char* name = info.name;

        if (!name || !*name) {
            name = *info.what == 'm' ? "main chunk" : (*info.what == 'C' ? "?" : "function <anonymous>");
        }

        char entry[512];
        SStrPrintf(entry, sizeof(entry), "%s:%d: in %s\n", info.short_src, info.currentline, name);

        size_t length = SStrLen(entry);

        if (used + length >= sizeof(stack)) {
            break;
        }

        SStrCopy(&stack[used], entry, sizeof(stack) - used);
        used += length;
    }

    lua_pushstring(L, stack);

    return 1;
}

int32_t debuglocals(lua_State* L) {
    // Names and values of the locals visible at a stack level, as one printable block. Callers
    // concatenate the result, so this must return a string even when there is nothing to show.
    int32_t level = lua_type(L, 1) == LUA_TNUMBER ? static_cast<int32_t>(lua_tonumber(L, 1)) : 1;

    char locals[2048];
    locals[0] = 0;

    size_t used = 0;
    lua_Debug info;

    if (lua_getstack(L, level, &info)) {
        for (int32_t index = 1; ; index++) {
            const char* name = lua_getlocal(L, &info, index);

            if (!name) {
                break;
            }

            // Skip the compiler's own temporaries, which are named "(*temporary)".
            if (*name != '(') {
                char entry[256];
                SStrPrintf(entry, sizeof(entry), "%s = %s\n", name, luaL_typename(L, -1));

                size_t length = SStrLen(entry);

                if (used + length < sizeof(locals)) {
                    SStrCopy(&locals[used], entry, sizeof(locals) - used);
                    used += length;
                }
            }

            lua_settop(L, -2);
        }
    }

    lua_pushstring(L, locals);

    return 1;
}

int32_t scrub(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}
