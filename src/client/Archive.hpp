#ifndef CLIENT_ARCHIVE_HPP
#define CLIENT_ARCHIVE_HPP

#include <cstdint>

// Opens the game's MPQ archives from the Data directory, replicating the client's archive
// table and patch discovery. Must run after the "locale" CVar has been registered.
void ClientOpenArchives();

// Closes every archive opened by ClientOpenArchives
void ClientCloseArchives();

// 0 = classic only, 1 = expansion.MPQ present, 2 = lichking.MPQ present
uint8_t ClientGetInstalledExpansionLevel();

#endif
