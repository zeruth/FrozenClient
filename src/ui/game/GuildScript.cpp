#include "ui/game/GuildScript.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include <common/DataStore.hpp>

namespace {

// The number of ranks in the player's guild. Nothing fills it yet: the guild query response that
// does is not ported.
// ref: DAT_00c22ab8
uint32_t s_numGuildRanks;

// Only sent while the guild has more than five ranks.
// ref: FUN_005cb3f0
int32_t Script_GuildControlDelRank(lua_State* L) {
    if (s_numGuildRanks > 5) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(CMSG_GUILD_DELETE_RANK));
        msg.Finalize();
        ClientServices::Send(&msg);
    }

    return 0;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GuildControlDelRank", &Script_GuildControlDelRank },
};

void GuildRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
