#include "ui/game/ItemSocketInfo.hpp"
#include "client/ClientServices.hpp"
#include "net/Types.hpp"
#include "util/guid/Types.hpp"
#include <common/DataStore.hpp>

// Written by the socketing code that is not ported yet; the reference zero-initialises both.
static WOWGUID s_socketGems[3];             // ref: DAT_00c20fc8
static WOWGUID s_socketItem;                // ref: DAT_00c20fe0

// ref: FUN_005c4ff0
void ItemSocketInfoSendGems() {
    if (!s_socketItem) {
        return;
    }

    int32_t gems = 0;

    CDataStore msg;
    msg.Put(static_cast<uint32_t>(CMSG_SOCKET_GEMS));
    msg.Put(s_socketItem);

    for (uint32_t i = 0; i < 3; i++) {
        if (s_socketGems[i]) {
            gems++;
        }

        msg.Put(s_socketGems[i]);
    }

    if (gems) {
        msg.Finalize();
        ClientServices::Send(&msg);
    }
}
