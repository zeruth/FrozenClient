#include "gx/Adapter.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/CGxMonitorMode.hpp"

// ref: FUN_00681220
int32_t GxAdapterDesktopMode(CGxMonitorMode& mode) {
    return CGxDevice::AdapterDesktopMode(mode);
}

// ref: FUN_006811f0
int32_t GxAdapterID(uint16_t* vendorID, uint16_t* deviceID, uint32_t* driverVersionHi, uint32_t* driverVersionLo) {
    return CGxDevice::AdapterID(vendorID, deviceID, driverVersionHi, driverVersionLo);
}

// ref: FUN_00681200
int32_t GxAdapterInfer(uint16_t* deviceID) {
    return CGxDevice::AdapterInfer(deviceID);
}

int32_t GxAdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& adapterFormats) {
    return CGxDevice::AdapterFormats(api, adapterFormats);
}

// ref: FUN_00681210
int32_t GxAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes) {
    return CGxDevice::AdapterMonitorModes(monitorModes);
}
