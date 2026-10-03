#ifndef UTIL_GUID_SMART_GUID_HPP
#define UTIL_GUID_SMART_GUID_HPP

#include "util/guid/Types.hpp"
#include <cstdint>

class CDataStore;

struct SmartGUID {
    WOWGUID guid;

    operator WOWGUID() const;
    SmartGUID& operator=(WOWGUID value);
};

CDataStore& operator>>(CDataStore& msg, SmartGUID& guid);

// ref: FUN_0076dd00
// The packed form: a mask byte, then each non-zero byte of the guid, low first. The mask is
// written as a placeholder and patched once the bytes are known (FUN_0076dc80).
CDataStore& operator<<(CDataStore& msg, const SmartGUID& guid);

#endif
