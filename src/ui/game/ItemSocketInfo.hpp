#ifndef UI_GAME_ITEM_SOCKET_INFO_HPP
#define UI_GAME_ITEM_SOCKET_INFO_HPP

// The reference's ItemSocketInfo.cpp.

#include "util/guid/Types.hpp"

// The item being socketed and the gems placed in its three sockets.
extern WOWGUID s_socketGems[3];
extern WOWGUID s_socketItem;

// Send CMSG_SOCKET_GEMS for the item being socketed and the three gems placed in it, unless no
// gem has been placed.
// ref: FUN_005c4ff0
void ItemSocketInfoSendGems();

#endif
