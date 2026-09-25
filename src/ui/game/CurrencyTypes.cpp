#include "ui/game/CurrencyTypes.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGGameUI.hpp"

// The token frame's list and its length. The capacity is inferred, not read: 95 rows of 0x18 bytes
// end exactly at 0x00c1f248, the next address the module touches. Nothing fills the list yet.
static CURRENCYLISTENTRY s_currencyList[95];    // ref: DAT_00c1e960
static int32_t s_currencyListCount = 0;         // ref: DAT_00c1f44c

// ref: FUN_005afc80
CURRENCYLISTENTRY* CurrencyGetListEntry(int32_t index) {
    if (index >= 0 && index < s_currencyListCount) {
        return &s_currencyList[index];
    }

    return nullptr;
}

// ref: FUN_005b0340
// The index-th currency the player both knows and has marked for the backpack, in CurrencyTypes
// order. Bit indices 1..32 live in the low halves of the known-currency mask and of
// currencyTokensBackpack1, 33 and up in the high half and currencyTokensBackpack2.
CurrencyTypesRec* CurrencyGetBackpackCurrency(int32_t index) {
    if (index < 0) {
        return nullptr;
    }

    auto player = static_cast<CGPlayer_C*>(ClntObjMgrObjectPtr(ClntObjMgrGetActivePlayer(), TYPE_PLAYER, __FILE__, __LINE__));

    if (!player) {
        return nullptr;
    }

    int32_t found = 0;

    for (int32_t i = 0; i < g_currencyTypesDB.GetNumRecords(); i++) {
        auto rec = g_currencyTypesDB.GetRecordByIndex(i);

        if (!rec) {
            continue;
        }

        auto bitIndex = rec->m_bitIndex;
        uint32_t known = 0;
        uint32_t shown = 0;
        uint32_t mask = 0;

        if (bitIndex < 0x21) {
            if (bitIndex > 0) {
                known = static_cast<uint32_t>(player->Player()->knownCurrencies);
                shown = CGGameUI::s_currencyTokensBackpack1Cvar->GetInt();
                mask = 1u << ((bitIndex - 1) & 0x1F);
            }
        } else {
            known = static_cast<uint32_t>(player->Player()->knownCurrencies >> 32);
            shown = CGGameUI::s_currencyTokensBackpack2Cvar->GetInt();
            mask = 1u << ((bitIndex - 0x21) & 0x1F);
        }

        if ((known & mask) && (shown & mask)) {
            if (found == index) {
                return rec;
            }

            found++;
        }
    }

    return nullptr;
}
