#include "client/AccountData.hpp"

// Both set together by SetClearConfigData; what reads them is not ported yet.
static int32_t s_clearConfigData = 0;      // ref: DAT_00c7bff8
static int32_t s_clearConfigDataCopy = 0;  // ref: DAT_00c7bffc

// ref: FUN_006b90e0
void AccountDataSetClearConfigData(int32_t clear) {
    s_clearConfigData = clear;
    s_clearConfigDataCopy = clear;
}
