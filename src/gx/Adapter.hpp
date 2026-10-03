#ifndef GX_ADAPTER_HPP
#define GX_ADAPTER_HPP

#include "gx/CGxFormat.hpp"
#include "gx/CGxMonitorMode.hpp"
#include "gx/Types.hpp"
#include <storm/Array.hpp>
#include <cstdint>

int32_t GxAdapterDesktopMode(CGxMonitorMode& mode);

int32_t GxAdapterID(uint16_t* vendorID, uint16_t* deviceID, uint32_t* driverVersionHi, uint32_t* driverVersionLo);

int32_t GxAdapterInfer(uint16_t* deviceID);

int32_t GxAdapterFormats(EGxApi api, TSGrowableArray<CGxFormat>& adapterFormats);

int32_t GxAdapterMonitorModes(TSGrowableArray<CGxMonitorMode>& monitorModes);

#endif
