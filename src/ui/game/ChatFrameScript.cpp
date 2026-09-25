#include "ui/game/ChatFrameScript.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "ui/FrameScript.hpp"
#include "util/Lua.hpp"
#include <common/DataStore.hpp>
#include <storm/String.hpp>
#include <cmath>

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

// One chat type's settings, 0x4C bytes. Only the colour is read by ported code.
struct ChatTypeInfo {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t unk03[0x49];
};

static_assert(sizeof(ChatTypeInfo) == 0x4C, "ChatTypeInfo is 0x4C bytes in the reference");

// Nothing fills these yet: the chat settings and message handlers that do are not ported.
// ref: DAT_00b74728
static ChatTypeInfo s_chatTypeInfo[62];
// ref: DAT_00b75a60
static ChatLogEntry s_chatLog[60];
// ref: DAT_00bceff4
static int32_t s_chatLogStart;

// ref: FUN_004fb1a0
// The chat types that carry a message someone sent: say, party, raid, guild, whisper, channel and
// their kin.
bool ChatTypeIsPlayerMessage(int32_t type) {
    switch (type) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 9:
        case 10:
        case 11:
        case 17:
        case 23:
        case 24:
        case 39:
        case 40:
        case 44:
        case 45:
        case 47:
        case 51:
        case 53:
        case 54:
        case 55:
            return true;

        default:
            return false;
    }
}

// ref: FUN_004fb210
// Records are counted from the oldest, which s_chatLogStart marks.
ChatLogEntry* ChatLogGetEntry(int32_t index) {
    return &s_chatLog[(index + s_chatLogStart) % 60];
}

// ref: FUN_004fb9c0
// Opaque white past the last chat type.
void ChatTypeGetColor(CImVector* color, int32_t type) {
    if (type > 61) {
        color->b = 0xFF;
        color->g = 0xFF;
        color->a = 0xFF;
        color->r = 0xFF;

        return;
    }

    color->b = s_chatTypeInfo[type].b;
    color->g = s_chatTypeInfo[type].g;
    color->a = 0xFF;
    color->r = s_chatTypeInfo[type].r;
}

namespace {

// One chat window's settings, 0x100 bytes. Only the fields written by ported code are named.
struct ChatWindowSettings {
    uint8_t unk00[0x5C];
    int32_t docked;         // +0x5C
    uint8_t unk60[0x18];
    float savedWidth;       // +0x78
    float savedHeight;      // +0x7C
    uint8_t unk80[0x80];
};

static_assert(sizeof(ChatWindowSettings) == 0x100, "ChatWindowSettings is 0x100 bytes in the reference");

// Nothing fills these yet: the chat settings load that does is not ported, and nothing reads them
// back (GetChatWindowInfo still answers from constants).
// ref: DAT_00bcf130
ChatWindowSettings s_chatWindows[10];

// ref: FUN_004fc760
int32_t Script_SetChatWindowDocked(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        luaL_error(L, "Usage: SetChatWindowDocked(index, docked)");

        return 0;
    }

    auto window = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1))) - 1);

    if (window < 10) {
        int32_t docked = 0;

        if (lua_isnumber(L, 2)) {
            docked = static_cast<int32_t>(llrint(lua_tonumber(L, 2)));
        }

        s_chatWindows[window].docked = docked;
    }

    return 0;
}

// ref: FUN_004fc9a0
int32_t Script_SetChatWindowSavedDimensions(lua_State* L) {
    if (lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
        auto window = static_cast<uint32_t>(static_cast<int32_t>(llrint(lua_tonumber(L, 1))) - 1);

        if (window > 9) {
            return 0;
        }

        auto width = lua_tonumber(L, 2);
        auto height = lua_tonumber(L, 3);
        s_chatWindows[window].savedWidth = static_cast<float>(width);
        s_chatWindows[window].savedHeight = static_cast<float>(height);

        return 0;
    }

    luaL_error(L, "Usage: SetChatWindowSavedDimensions(index, width, height)");

    return 0;
}

}

static FrameScript_Method s_ScriptFunctions[] = {
    { "SetChatWindowDocked",          &Script_SetChatWindowDocked },
    { "SetChatWindowSavedDimensions", &Script_SetChatWindowSavedDimensions },
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
