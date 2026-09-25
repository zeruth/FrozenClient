#include "ui/game/TalentInfo.hpp"
#include "util/Lua.hpp"
#include <storm/Array.hpp>
#include <storm/Memory.hpp>

// The player's active talent group. Written only by code that is not ported yet, so it stays 0,
// the first group.
static uint32_t s_activeTalentGroup;        // ref: DAT_00c20ff4

// The per-tab talent records, one heap block per tab. Filled by code that is not ported yet.
struct TalentTabInfo;
static TSFixedArray<TalentTabInfo*> s_talentTabInfos;  // ref: DAT_00c2112c

// ref: FUN_005c57a0
uint32_t TalentInfoDefaultGroup(int32_t pet) {
    return pet ? 0 : s_activeTalentGroup;
}

// ref: FUN_005c57d0
uint32_t TalentInfoGroupArg(lua_State* L, int32_t index) {
    if (lua_isnumber(L, index)) {
        return static_cast<uint32_t>(lua_tointeger(L, index) - 1);
    }

    return s_activeTalentGroup;
}

// ref: FUN_005c6980
// Each tab is released with a plain operator delete: the reference runs no destructor on it.
void TalentInfoDestroyTabs() {
    for (uint32_t i = 0; i < s_talentTabInfos.Count(); i++) {
        if (s_talentTabInfos[i]) {
            SMemFree(s_talentTabInfos[i], "delete", -1, 0);
        }

        s_talentTabInfos[i] = nullptr;
    }

    s_talentTabInfos.Clear();
}
