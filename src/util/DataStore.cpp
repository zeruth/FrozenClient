#include "util/DataStore.hpp"

// ref: FUN_00401190
CDataStore& operator>>(CDataStore& msg, C3Vector& vector) {
    msg.Get(vector.x);
    msg.Get(vector.y);
    msg.Get(vector.z);

    return msg;
}
