#include "ui/game/GuildScript.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "object/client/CGPlayer_C.hpp"
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

// One guild rank, 0x38 bytes. Only the rights mask is read by ported code.
struct GuildRankInfo {
    uint32_t rights;
    uint32_t unk04[13];
};

static_assert(sizeof(GuildRankInfo) == 0x38, "GuildRankInfo is 0x38 bytes in the reference");

// Nothing fills these yet: the guild query response that does is not ported.
// ref: DAT_00c21e60
static GuildRankInfo s_guildRanks[10];

// ref: FUN_005cbca0
// Whether the player is in a guild whose rank for them carries rights bit 0x100000, the right the
// calendar bindings ask about before creating a guild event.
bool GuildRankCanCreateEvent() {
    auto player = CGPlayer_C::GetActivePtr();

    if (!player || !player->Player()->guildID) {
        return false;
    }

    return (s_guildRanks[player->Player()->guildRank].rights >> 20) & 1;
}

static FrameScript_Method s_ScriptFunctions[] = {
    { "GuildControlDelRank", &Script_GuildControlDelRank },
};

void GuildRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
