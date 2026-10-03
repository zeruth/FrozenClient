#include "util/guid/SmartGUID.hpp"
#include <common/DataStore.hpp>

SmartGUID::operator WOWGUID() const {
    return this->guid;
}

SmartGUID& SmartGUID::operator=(WOWGUID guid) {
    this->guid = guid;
    return *this;
}

CDataStore& operator>>(CDataStore& msg, SmartGUID& guid) {
    guid = 0;

    uint8_t mask;
    msg.Get(mask);

    for (int32_t i = 0; i < 8; i++) {
        if (mask & (1 << i)) {
            uint8_t byte;
            msg.Get(byte);

            guid.guid |= static_cast<uint64_t>(byte) << (i * 8);
        }
    }

    return msg;
}

// ref: FUN_0076dd00
CDataStore& operator<<(CDataStore& msg, const SmartGUID& guid) {
    uint32_t maskPos = msg.Size();
    uint8_t mask = 0;

    msg.Put(static_cast<uint8_t>(0));

    for (int32_t i = 0; i < 8; i++) {
        auto byte = static_cast<uint8_t>(guid.guid >> (i * 8));

        if (byte) {
            mask |= static_cast<uint8_t>(1 << i);
            msg.Put(byte);
        }
    }

    msg.Set(maskPos, mask);

    return msg;
}
