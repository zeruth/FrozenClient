#ifndef GX_D3D_NV_API_HPP
#define GX_D3D_NV_API_HPP

#include <cstdint>

// The parts of NVIDIA's NVAPI the D3D9 device uses for stereo. The reference links nvapi.lib,
// which loads the driver's DLL and resolves each entry point by id through nvapi_QueryInterface;
// this is that loader, with the same ids and the same failure values.

typedef void* NvStereoHandle;

int32_t NvAPI_Initialize();
int32_t NvAPI_GetErrorMessage(int32_t status, char* message);
int32_t NvAPI_Stereo_Enable();
int32_t NvAPI_Stereo_Disable();
int32_t NvAPI_Stereo_IsEnabled(uint8_t* enabled);
int32_t NvAPI_Stereo_CreateConfigurationProfileRegistryKey(uint32_t profile);
int32_t NvAPI_Stereo_CreateHandleFromIUnknown(void* device, NvStereoHandle* handle);
int32_t NvAPI_Stereo_DestroyHandle(NvStereoHandle handle);
int32_t NvAPI_Stereo_SetConvergence(NvStereoHandle handle, float convergence);
int32_t NvAPI_Stereo_SetSeparation(NvStereoHandle handle, float separation);

#endif
