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
    this->uint58 = 0;
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

    // The transport block, in the order the writer (FUN_004f4ed0) puts it.
    if (move.moveFlags & 0x200) {
        SmartGUID transport;
        msg >> transport;
        move.transport = transport;

        msg >> move.position18;
        msg.Get(move.facing24);
        msg.Get(move.uint54);
        msg.Get(move.byte16);

        if (move.uint14 & 0x400) {
            msg.Get(move.uint58);
        } else {
            move.uint58 = 0;
        }
    } else {
        move.transport = 0;
        move.position18 = { 0.0f, 0.0f, 0.0f };
        move.facing24 = 0.0f;
        move.uint54 = 0;
        move.uint58 = 0;
        move.byte16 = 0xFF;
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

// ref: FUN_004f4ed0
CDataStore& operator<<(CDataStore& msg, const CMovementStatus& move) {
    msg.Put(move.moveFlags);
    msg.Put(move.uint14);
    msg.Put(move.uint0);
    msg.Put(move.position28.x);
    msg.Put(move.position28.y);
    msg.Put(move.position28.z);
    msg.Put(move.facing34);

    if (move.moveFlags & 0x200) {
        SmartGUID transport;
        transport = move.transport;
        msg << transport;

        msg.Put(move.position18.x);
        msg.Put(move.position18.y);
        msg.Put(move.position18.z);
        msg.Put(move.facing24);
        msg.Put(move.uint54);
        msg.Put(move.byte16);

        if (move.uint14 & 0x400) {
            msg.Put(move.uint58);
        }
    }

    if ((move.moveFlags & 0x2200000) || (move.uint14 & 0x20)) {
        msg.Put(move.float38);
    }

    msg.Put(move.uint3C);

    if (move.moveFlags & 0x1000) {
        msg.Put(move.float40);
        msg.Put(move.float44);
        msg.Put(move.float48);
        msg.Put(move.float4C);
    }

    if (move.moveFlags & 0x4000000) {
        msg.Put(move.float50);
    }

    return msg;
}
