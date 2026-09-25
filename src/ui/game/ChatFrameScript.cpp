#include "ui/game/ChatFrameScript.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include <common/DataStore.hpp>
#include <storm/String.hpp>

namespace {

// A joined chat channel. The list is indexed by channel number - 1, and a record only answers to
// its number when it holds that number and +0x9C is clear. Only the fields read here are named;
// how far the name extends before +0x9C is not known, so the bytes between are kept with it.
struct ChatChannel {
    int32_t number;
    char name[0x98];
    int32_t unk9C;
    uint8_t unkA0[0x08];
};

static_assert(sizeof(ChatChannel) == 0xA8, "ChatChannel is 0xA8 bytes in the reference");

// Nothing fills these yet: the channel notifications that do are not ported.
// ref: DAT_00bcf090
int32_t s_numChatChannels;
// ref: DAT_00bcf094
ChatChannel* s_chatChannels;

// One row of the channel roster list: a header, or a joined channel. Only the fields read by
// ported code are named.
struct ChannelListEntry {
    int32_t channel;        // +0x00, index into s_chatChannels for a channel row
    int32_t unk04;
    int32_t type;           // +0x08, 0 for a channel row
    uint32_t unk0C[5];
};

static_assert(sizeof(ChannelListEntry) == 0x20, "ChannelListEntry is 0x20 bytes in the reference");

// Nothing fills these yet: the code that builds the roster list is not ported.
// ref: DAT_00b74460
uint32_t s_numChannelListEntries;
// ref: DAT_00b74468
ChannelListEntry s_channelListEntries[20];

// A channel argument is a name, or a channel number that resolves to the joined channel's name.
// A number that matches no joined channel resolves to nothing.
// ref: FUN_004fe160
const char* ResolveChannelName(const char* channel) {
    if (!channel || !*channel) {
        return nullptr;
    }

    auto number = SStrToInt(channel);

    if (number != 0) {
        if (number > 0 && number <= s_numChatChannels) {
            auto rec = &s_chatChannels[number - 1];

            if (rec->number == number && rec->unk9C == 0) {
                return rec->name;
            }
        }

        return nullptr;
    }

    return channel;
}

// ref: FUN_004fe460
void SendChannelCommand(lua_State* L, NETMESSAGE opcode, const char* scriptName) {
    if (!lua_isstring(L, 1)) {
        char usage[512];
        SStrPrintf(usage, sizeof(usage), "Usage: %s(\"channel\")", scriptName);
        luaL_error(L, usage);

        return;
    }

    auto channel = ResolveChannelName(lua_tolstring(L, 1, nullptr));

    if (channel) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(opcode));
        msg.PutString(channel);
        msg.Finalize();
        ClientServices::Send(&msg);
    }
}

// ref: FUN_004fe330
void SendChannelCommandWithName(lua_State* L, NETMESSAGE opcode, const char* scriptName) {
    if (!lua_isstring(L, 1) || !lua_isstring(L, 2)) {
        char usage[512];
        SStrPrintf(usage, sizeof(usage), "Usage: %s(\"channel\", \"name\")", scriptName);
        luaL_error(L, usage);

        return;
    }

    auto channel = ResolveChannelName(lua_tolstring(L, 1, nullptr));

    if (!channel) {
        return;
    }

    auto name = lua_tolstring(L, 2, nullptr);

    if (name && SStrLen(name) >= 0x31) {
        luaL_error(L, "Name too long");

        return;
    }

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(opcode));
    msg.PutString(channel);
    msg.PutString(name);
    msg.Finalize();
    ClientServices::Send(&msg);
}

// ref: FUN_004fe630
int32_t Script_ListChannelByName(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_LIST, "ListChannelByName");

    return 0;
}

// ref: FUN_004fe810
int32_t Script_SetChannelOwner(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_SET_OWNER, "SetChannelOwner");

    return 0;
}

// ref: FUN_004fe830
int32_t Script_DisplayChannelOwner(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_OWNER, "DisplayChannelOwner");

    return 0;
}

// ref: FUN_004fe950
int32_t Script_ChannelModerator(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_MODERATOR, "ChannelModerator");

    return 0;
}

// ref: FUN_004fe970
int32_t Script_ChannelUnmoderator(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_UNMODERATOR, "ChannelUnmoderator");

    return 0;
}

// ref: FUN_004fe990
int32_t Script_ChannelMute(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_MUTE, "ChannelMute");

    return 0;
}

// ref: FUN_004fe9b0
int32_t Script_ChannelUnmute(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_UNMUTE, "ChannelUnmute");

    return 0;
}

// ref: FUN_004fe9d0
int32_t Script_ChannelInvite(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_INVITE, "ChannelInvite");

    return 0;
}

// ref: FUN_004fe9f0
int32_t Script_ChannelKick(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_KICK, "ChannelKick");

    return 0;
}

// ref: FUN_004fea10
int32_t Script_ChannelBan(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_BAN, "ChannelBan");

    return 0;
}

// ref: FUN_004fea30
int32_t Script_ChannelUnban(lua_State* L) {
    SendChannelCommandWithName(L, CMSG_CHAT_CHANNEL_UNBAN, "ChannelUnban");

    return 0;
}

// ref: FUN_004fea50
int32_t Script_ChannelToggleAnnouncements(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_ANNOUNCEMENTS, "ChannelToggleAnnouncements");

    return 0;
}

// ref: FUN_004fea70
int32_t Script_ChannelVoiceOn(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_VOICE_ON, "ChannelVoiceOn");

    return 0;
}

// ref: FUN_004fea90
int32_t Script_ChannelVoiceOff(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_VOICE_OFF, "ChannelVoiceOff");

    return 0;
}

// ref: FUN_004ffc30
int32_t Script_SetChannelWatch(lua_State* L) {
    SendChannelCommand(L, CMSG_SET_CHANNEL_WATCH, "SetChannelWatch");

    return 0;
}

// ref: FUN_004ffcc0
int32_t Script_DeclineInvite(lua_State* L) {
    SendChannelCommand(L, CMSG_CHAT_CHANNEL_DECLINE_INVITE, "DeclineInvite");

    return 0;
}

}

// A channel row answers with its channel's name and type 0; a header of type 1 to 3 with an empty
// name and its type; anything else with nothing and type 4.
// ref: FUN_004fe1c0
const char* ChannelListEntryName(uint32_t index, int32_t* type) {
    *type = 4;

    if (index >= s_numChannelListEntries) {
        return nullptr;
    }

    auto entryType = s_channelListEntries[index].type;

    if (entryType == 0) {
        auto channel = s_channelListEntries[index].channel;

        if (channel >= 0 && channel < s_numChatChannels) {
            auto number = s_chatChannels[channel].number;

            if (number > 0 && number <= s_numChatChannels && s_chatChannels[number - 1].number == number) {
                *type = 0;

                return s_chatChannels[number - 1].name;
            }
        }
    } else if (entryType == 2 || entryType == 3 || entryType == 1) {
        *type = entryType;

        return "";
    }

    return nullptr;
}

static FrameScript_Method s_ScriptFunctions[] = {
    { "ListChannelByName",          &Script_ListChannelByName },
    { "SetChannelOwner",            &Script_SetChannelOwner },
    { "DisplayChannelOwner",        &Script_DisplayChannelOwner },
    { "ChannelModerator",           &Script_ChannelModerator },
    { "ChannelUnmoderator",         &Script_ChannelUnmoderator },
    { "ChannelMute",                &Script_ChannelMute },
    { "ChannelUnmute",              &Script_ChannelUnmute },
    { "ChannelInvite",              &Script_ChannelInvite },
    { "ChannelKick",                &Script_ChannelKick },
    { "ChannelBan",                 &Script_ChannelBan },
    { "ChannelUnban",               &Script_ChannelUnban },
    { "ChannelToggleAnnouncements", &Script_ChannelToggleAnnouncements },
    { "ChannelVoiceOn",             &Script_ChannelVoiceOn },
    { "ChannelVoiceOff",            &Script_ChannelVoiceOff },
    { "SetChannelWatch",            &Script_SetChannelWatch },
    { "DeclineInvite",              &Script_DeclineInvite },
};

void ChatFrameRegisterScriptFunctions() {
    for (auto& func : s_ScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}
