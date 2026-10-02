#include "gx/d3d/NvApi.hpp"
#include <windows.h>

namespace {

typedef void* (__cdecl* QUERYINTERFACE)(uint32_t id);

// Reference 0x00b2376c (initialized) and 0x00b23770 (the status Initialize reports).
int32_t s_initialized;
int32_t s_status = -2;
// Reference 0x00b23774: what an entry point the driver does not provide returns.
int32_t s_unavailable = -3;

// Reference 0x00b23778 onwards: each entry point, resolved by id when Initialize succeeds.
void* s_getErrorMessage;      // 0x6C2D048C
void* s_stereoEnable;         // 0x239C4545
void* s_stereoDisable;        // 0x2EC50C2B
void* s_stereoIsEnabled;      // 0x348FF8E1
void* s_stereoCreateProfile;  // 0xBE7692EC
void* s_stereoCreateHandle;   // 0xAC7E37F4
void* s_stereoDestroyHandle;  // 0x3A153134
void* s_stereoSetConvergence; // 0x3DD6B54B
void* s_stereoSetSeparation;  // 0x5C069FA3

}

// ref: FUN_008a09ba
// The reference is 32-bit and loads nvapi.dll; this build is 64-bit, so the 64-bit driver DLL.
int32_t NvAPI_Initialize() {
    if (s_initialized) {
        return 0;
    }

    auto library = LoadLibraryA("nvapi64.dll");
    s_status = -2;

    if (library) {
        auto queryInterface = reinterpret_cast<QUERYINTERFACE>(GetProcAddress(library, "nvapi_QueryInterface"));
        auto initialize = queryInterface ? reinterpret_cast<int32_t (__cdecl*)()>(queryInterface(0x0150E828)) : nullptr;

        if (initialize && initialize() >= 0) {
            s_status = 0;
            s_unavailable = -3;

            s_getErrorMessage = queryInterface(0x6C2D048C);
            s_stereoEnable = queryInterface(0x239C4545);
            s_stereoDisable = queryInterface(0x2EC50C2B);
            s_stereoIsEnabled = queryInterface(0x348FF8E1);
            s_stereoCreateProfile = queryInterface(0xBE7692EC);
            s_stereoCreateHandle = queryInterface(0xAC7E37F4);
            s_stereoDestroyHandle = queryInterface(0x3A153134);
            s_stereoSetConvergence = queryInterface(0x3DD6B54B);
            s_stereoSetSeparation = queryInterface(0x5C069FA3);
        } else {
            s_status = -1;
        }
    }

    if (s_status == 0) {
        s_initialized = 1;
    }

    return s_status;
}

// ref: FUN_008a0a54
int32_t NvAPI_GetErrorMessage(int32_t status, char* message) {
    return s_getErrorMessage ? reinterpret_cast<int32_t (__cdecl*)(int32_t, char*)>(s_getErrorMessage)(status, message) : s_unavailable;
}

// ref: FUN_008a0fa0
int32_t NvAPI_Stereo_Enable() {
    return s_stereoEnable ? reinterpret_cast<int32_t (__cdecl*)()>(s_stereoEnable)() : s_unavailable;
}

// ref: FUN_008a0fa6
int32_t NvAPI_Stereo_Disable() {
    return s_stereoDisable ? reinterpret_cast<int32_t (__cdecl*)()>(s_stereoDisable)() : s_unavailable;
}

// ref: FUN_008a0fac
int32_t NvAPI_Stereo_IsEnabled(uint8_t* enabled) {
    return s_stereoIsEnabled ? reinterpret_cast<int32_t (__cdecl*)(uint8_t*)>(s_stereoIsEnabled)(enabled) : s_unavailable;
}

// ref: FUN_008a0f88
int32_t NvAPI_Stereo_CreateConfigurationProfileRegistryKey(uint32_t profile) {
    return s_stereoCreateProfile ? reinterpret_cast<int32_t (__cdecl*)(uint32_t)>(s_stereoCreateProfile)(profile) : s_unavailable;
}

// ref: FUN_008a0fb2
int32_t NvAPI_Stereo_CreateHandleFromIUnknown(void* device, NvStereoHandle* handle) {
    return s_stereoCreateHandle ? reinterpret_cast<int32_t (__cdecl*)(void*, NvStereoHandle*)>(s_stereoCreateHandle)(device, handle) : s_unavailable;
}

// ref: FUN_008a0fb8
int32_t NvAPI_Stereo_DestroyHandle(NvStereoHandle handle) {
    return s_stereoDestroyHandle ? reinterpret_cast<int32_t (__cdecl*)(NvStereoHandle)>(s_stereoDestroyHandle)(handle) : s_unavailable;
}

// ref: FUN_008a0fee
int32_t NvAPI_Stereo_SetConvergence(NvStereoHandle handle, float convergence) {
    return s_stereoSetConvergence ? reinterpret_cast<int32_t (__cdecl*)(NvStereoHandle, float)>(s_stereoSetConvergence)(handle, convergence) : s_unavailable;
}

// ref: FUN_008a0fd6
int32_t NvAPI_Stereo_SetSeparation(NvStereoHandle handle, float separation) {
    return s_stereoSetSeparation ? reinterpret_cast<int32_t (__cdecl*)(NvStereoHandle, float)>(s_stereoSetSeparation)(handle, separation) : s_unavailable;
}
