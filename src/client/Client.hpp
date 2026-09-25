#ifndef CLIENT_CLIENT_HPP
#define CLIENT_CLIENT_HPP

#include "event/Event.hpp"
#include "util/Time.hpp"
#include <tempest/Vector.hpp>

class CVar;

namespace Client {
    extern CVar* g_accountNameVar;
    extern CVar* g_accountListVar;
    extern CVar* g_readTOSVar;
    extern CVar* g_readEULAVar;
    extern CVar* g_readTerminationWithoutNoticeVar;
    extern CVar* g_readScanningVar;
    extern CVar* g_readContestVar;
    extern HEVENTCONTEXT g_clientEventContext;
}

extern CGameTime g_clientGameTime;

// ref: DAT_00b2f988 ("g_accountUsesToken")
extern CVar* s_accountUsesTokenCvar;

// ref: DAT_00b2f978 ("checkAddonVersion")
extern CVar* s_checkAddonVersionCvar;

void ClientInitializeGame(uint32_t mapId, C3Vector position);

void ClientPostClose(int32_t a1);

void CommonMain();

void StormInitialize();

void WowClientDestroy();

void WowClientInit();

#endif
