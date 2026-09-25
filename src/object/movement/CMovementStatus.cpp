#include "object/movement/CMovementStatus.hpp"
#include "util/DataStore.hpp"

// ref: FUN_004f4c50
CMovementStatus::CMovementStatus() {
    this->uint0 = 0;
    this->transport = 0;
    this->moveFlags = 0x0;
    this->uint14 = 0;
    this->byte16 = 0xFF;
    this->position18 = { 0.0f, 0.0f, 0.0f };
    this->facing24 = 0.0f;
    this->position28 = { 0.0f, 0.0f, 0.0f };
    this->facing34 = 0.0f;
    this->float38 = 0.0f;
    this->uint3C = 0;
    this->float40 = 0.0f;
    this->float44 = 0.0f;
    this->float48 = 0.0f;
    this->float4C = 0.0f;
    this->float50 = 0.0f;
    this->uint54 = 0;
}

uint32_t CMovementStatus::Skip(CDataStore* msg) {
    uint32_t moveFlags = 0;
    msg->Get(moveFlags);

    uint16_t uint14;
    msg->Get(uint14);

    void* data;
    msg->GetDataInSitu(data, 20);

    uint32_t skipBytes = 0;

    if (moveFlags & 0x200) {
        SmartGUID guid;
        *msg >> guid;

        skipBytes += 21;

        if (uint14 & 0x400) {
            skipBytes += 4;
        }
    }

    if ((moveFlags & (0x200000 | 0x2000000)) || (uint14 & 0x20)) {
        skipBytes += 4;
    }

    skipBytes += 4;

    if (moveFlags & 0x1000) {
        skipBytes += 16;
    }

    if (moveFlags & 0x4000000) {
        skipBytes += 4;
    }

    msg->GetDataInSitu(data, skipBytes);

    return moveFlags;
}

CDataStore& operator>>(CDataStore& msg, CMovementStatus& move) {
    msg.Get(move.moveFlags);
    msg.Get(move.uint14);
    msg.Get(move.uint0);

    msg >> move.position28;
    msg.Get(move.facing34);

    if (move.moveFlags & 0x200) {
        // TODO
    } else {
        move.transport = 0;
        // TODO
    }

    if ((move.moveFlags & (0x200000 | 0x2000000)) || (move.uint14 & 0x20)) {
        msg.Get(move.float38);
    } else {
        move.float38 = 0.0f;
    }

    msg.Get(move.uint3C);

    if (move.moveFlags & 0x1000) {
        msg.Get(move.float40);
        msg.Get(move.float44);
        msg.Get(move.float48);
        msg.Get(move.float4C);
    }

    if (move.moveFlags & 0x4000000) {
        msg.Get(move.float50);
    }

    return msg;
}
