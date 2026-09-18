#include "client/ClientHandlers.hpp"
#include "Client.hpp"
#include "console/Console.hpp"
#include "db/Db.hpp"
#include "gx/LoadingScreen.hpp"
#include "object/Client.hpp"
#include "ui/game/CGGameUI.hpp"
#include "util/Time.hpp"
#include "util/Unimplemented.hpp"
#include "world/World.hpp"
#include <common/DataStore.hpp>
#include <cstdint>
#include <tempest/Vector.hpp>

static float s_newFacing;
static C3Vector s_newPosition;
static uint32_t s_newZoneID;
static const char* s_newMapname;

void LoadNewWorld(const void* eventData, void* param) {
    // TODO

    ClntObjMgrInitializeStd(s_newZoneID);

    // TODO

    CWorld::SetLoadProgressCallback(&LoadingScreenSetProgress3);
    CWorld::LoadMap(s_newMapname, s_newPosition, s_newZoneID);
    CWorld::SetLoadProgressCallback(nullptr);

    // TODO
};

int32_t LoginVerifyWorldHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t zoneID;
    msg->Get(zoneID);

    C3Vector position = { 0.0f, 0.0f, 0.0f };
    msg->Get(position.x);
    msg->Get(position.y);
    msg->Get(position.z);

    float facing;
    msg->Get(facing);

    if (zoneID == ClntObjMgrGetMapID()) {
        return 1;
    }

    s_newFacing = facing;
    s_newPosition = position;
    s_newZoneID = zoneID;

    auto map = g_mapDB.GetRecord(zoneID);

    if (!map) {
        ConsoleWrite("Bad SMSG_NEW_WORLD zoneID\n", DEFAULT_COLOR);

        return 0;
    }

    s_newMapname = map->m_directory;

    LoadNewWorld(nullptr, nullptr);

    return 1;
}

int32_t NewWorldHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    // TODO

    return 0;
}

// ref: FUN_00753bb0
// INVENTORY_RESULT (the byte the server sends) -> index into CGGameUI::DisplayError's table;
// 730 is past the end and displays nothing, 11 (ERR_BAG_FULL) is the default.
uint32_t InventoryResultToError(uint8_t result) {
    switch (result) {
    case 0:
    case 81:
    case 83:
        return 730;
    case 1:
        return 2;
    case 2:
        return 3;
    case 3:
        return 9;
    case 5:
        return 14;
    case 6:
        return 16;
    case 7:
        return 17;
    case 8:
        return 8;
    case 9:
    case 12:
    case 18:
        return 18;
    case 10:
    case 11:
        return 4;
    case 13:
        return 34;
    case 14:
        return 173;
    case 15:
    case 16:
        return 19;
    case 17:
        return 20;
    case 19:
    case 55:
        return 22;
    case 20:
        return 21;
    case 21:
        return 23;
    case 22:
        return 24;
    case 23:
    case 54:
        return 25;
    case 24:
        return 42;
    case 25:
        return 134;
    case 26:
        return 26;
    case 27:
        return 27;
    case 28:
        return 226;
    case 29:
        return 40;
    case 30:
        return 28;
    case 31:
        return 13;
    case 32:
        return 29;
    case 33:
        return 30;
    case 34:
        return 31;
    case 35:
        return 32;
    case 36:
        return 33;
    case 37:
        return 434;
    case 38:
        return 135;
    case 39:
        return 136;
    case 40:
    case 73:
        return 12;
    case 41:
        return 293;
    case 42:
        return 294;
    case 43:
        return 297;
    case 44:
        return 298;
    case 45:
        return 299;
    case 46:
        return 300;
    case 47:
        return 301;
    case 48:
        return 302;
    case 49:
        return 311;
    case 50:
        return 0;
    case 51:
        return 1;
    case 52:
    case 57:
        return 37;
    case 58:
        return 367;
    case 59:
        return 386;
    case 60:
        return 450;
    case 61:
        return 451;
    case 63:
        return 5;
    case 64:
        return 7;
    case 65:
        return 15;
    case 66:
        return 507;
    case 67:
        return 513;
    case 68:
        return 549;
    case 69:
        return 552;
    case 70:
        return 553;
    case 71:
        return 556;
    case 72:
        return 376;
    case 75:
        return 559;
    case 76:
        return 560;
    case 77:
        return 562;
    case 78:
        return 575;
    case 79:
        return 43;
    case 80:
        return 6;
    case 82:
        return 622;
    case 84:
        return 626;
    case 85:
        return 628;
    case 86:
        return 632;
    case 87:
        return 633;
    case 88:
        return 10;
    case 89:
        return 629;
    case 90:
        return 630;
    case 91:
        return 631;
    default:
        return 11;
    }
}

// The SMSG_INVENTORY_CHANGE_FAILURE case of the reference's player message dispatcher
// (FUN_006e2e90, case 0x112): result byte, the two item GUIDs, the bag slot, then per result the
// extra arguments its error string formats.
int32_t InventoryChangeFailureHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint8_t result = 0;
    msg->Get(result);

    if (!result) {
        return 1;
    }

    uint32_t error = InventoryResultToError(result);
    uint64_t item1 = 0;
    uint64_t item2 = 0;
    uint8_t bag = 0;
    msg->Get(item1);
    msg->Get(item2);
    msg->Get(bag);

    switch (result) {
    case 1:     // EQUIP_ERR_CANT_EQUIP_LEVEL_I
    case 0x57: { // EQUIP_ERR_PURCHASE_LEVEL_TOO_LOW
        uint32_t level = 0;
        msg->Get(level);
        CGGameUI::DisplayError(error, level);
        break;
    }
    case 0x10:  // EQUIP_ERR_ITEM_CANT_BE_EQUIPPED: only when the item is not known to us
        // TODO the reference checks the item (FUN_006d6720) before falling through to the plain message
        CGGameUI::DisplayError(error);
        break;
    case 0x51: { // EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_* purchase refund data: 3 extra fields
        uint64_t vendor = 0;
        uint32_t count = 0;
        uint64_t extra = 0;
        msg->Get(vendor);
        msg->Get(count);
        msg->Get(extra);
        // TODO FUN_006e1a70(item1, vendor, count, extra, bag)
        break;
    }
    case 0x54:  // EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_COUNT_EXCEEDED
    case 0x55:  // EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_SOCKETED_EXCEEDED
    case 0x59: { // EQUIP_ERR_ITEM_MAX_LIMIT_CATEGORY_EQUIPPED_EXCEEDED
        uint32_t limitCategory = 0;
        msg->Get(limitCategory);
        // TODO ItemLimitCategory.dbc (FUN_0065c290 lookup): DisplayError(error, rec->name, rec->quantity)
        CGGameUI::DisplayError(error, "", 0);
        break;
    }
    default:
        CGGameUI::DisplayError(error);
        break;
    }

    // TODO the reference then refreshes the two items' slots (FUN_00513770), cancels a pending
    // equip on the active player (FUN_00523640 / FUN_0073ac30) and signals the guild bank event
    // 0xf9 when a guild bank move was pending

    return 1;
}

int32_t NotifyHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    // TODO

    return 0;
}

int32_t PlayedTimeHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    // TODO

    return 0;
}

int32_t ReceiveGameTimeUpdate(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t ReceiveNewGameSpeed(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t ReceiveNewGameTime(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t ReceiveNewTimeSpeed(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t encodedTime;
    msg->Get(encodedTime);

    float newSpeed;
    msg->Get(newSpeed);

    uint32_t holidayOffset;
    msg->Get(holidayOffset);

    if (!msg->IsRead()) {
        STORM_ASSERT(msg->IsFinal());
        // TODO ConsoleWriteA("Malformed message received: Id = %d, Len = %d, Read = %d\n", DEFAULT_COLOR, msgId, msg->Size(), msg->Tell());

        return 0;
    }

    WowTime newTime;
    WowTime::WowDecodeTime(encodedTime, &newTime);
    newTime.m_holidayOffset = holidayOffset;

    g_clientGameTime.GameTimeSetTime(newTime, true);

    // TODO UpdateTime();

    auto oldSpeed = g_clientGameTime.GameTimeSetMinutesPerSecond(newSpeed);

    char logStr[256];
    SStrPrintf(logStr, sizeof(logStr), "Gamespeed set from %.03f to %.03f", oldSpeed, newSpeed);
    ConsoleWrite(logStr, DEFAULT_COLOR);

    return 1;
}

int32_t ReceiveServerTime(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t TransferAbortedHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    // TODO

    return 0;
}

int32_t TransferPendingHandler(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    // TODO

    return 0;
}
