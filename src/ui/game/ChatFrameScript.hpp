#ifndef UI_GAME_CHAT_FRAME_SCRIPT_HPP
#define UI_GAME_CHAT_FRAME_SCRIPT_HPP

#include <tempest/Vector.hpp>
#include <cstdint>

// One record of the ring of recent chat lines, 0x17C0 bytes. Nothing ported reads into it yet.
struct ChatLogEntry {
    uint32_t data[0x5F0];
};

const char* ChannelListEntryName(uint32_t index, int32_t* type);

void ChatFrameRegisterScriptFunctions();

ChatLogEntry* ChatLogGetEntry(int32_t index);

void ChatTypeGetColor(CImVector* color, int32_t type);

bool ChatTypeIsPlayerMessage(int32_t type);

#endif
