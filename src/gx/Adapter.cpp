#include "gx/Adapter.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/CGxMonitorMode.hpp"

// ref: FUN_00681220
int32_t GxAdapterDesktopMode(CGxMonitorMode& mode) {
    return CGxDevice::AdapterDesktopMode(mode);
}

int32_t GxAdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& adapterFormats) {
    return CGxDevice::AdapterFormats(api, adapterFormats);
}

// ref: FUN_00681210
int32_t GxAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
    return CGxDevice::AdapterMonitorModes(monitorModes);
}
