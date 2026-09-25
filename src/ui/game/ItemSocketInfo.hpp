#ifndef UI_GAME_ITEM_SOCKET_INFO_HPP
#define UI_GAME_ITEM_SOCKET_INFO_HPP

// The reference's ItemSocketInfo.cpp.

// Send CMSG_SOCKET_GEMS for the item being socketed and the three gems placed in it, unless no
// gem has been placed.
// ref: FUN_005c4ff0
void ItemSocketInfoSendGems();

#endif
