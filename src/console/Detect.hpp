#ifndef CONSOLE_DETECT_HPP
#define CONSOLE_DETECT_HPP

#include <storm/Array.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class CGxFormat;
class VideoHardwareRec;

// What ConsoleDetectDetectHardware found (reference DAT_00cabb38).
struct HardwareInfo {
    uint16_t vendorID;                      // +0x00 the adapter's PCI vendor, 0xFFFF when inferred
    uint16_t deviceID;                      // +0x02 its PCI device, or the inferred class 0-3
    uint32_t driverVersionHi;               // +0x04
    uint32_t driverVersionLo;               // +0x08
    uint32_t unk0C[3];                      // +0x0C
    int32_t cpuIdx;                         // +0x18 0 up to 1.5 GHz, 1 above
    int32_t videoID;                        // +0x1C the VideoHardware row
    int32_t soundIdx;                       // +0x20
    int32_t memIdx;                         // +0x24
    const VideoHardwareRec* videoHardware;  // +0x28
    const int32_t* cpuClass;                // +0x2C the CPU class's column in each table
    const int32_t* memClass;                // +0x30
};

// The settings the hardware calls for (reference DAT_00cabaf0), read by the callbacks that
// restore the video options.
struct DefaultSettings {
    float farClip;                          // +0x00
    int32_t shadowLevel;                    // +0x04
    int32_t groundEffectDensity;            // +0x08
    int32_t groundEffectAlpha;              // +0x0C
    uint8_t animatingDoodads;               // +0x10
    uint8_t trilinear;                      // +0x11
    int32_t maxLights;                      // +0x14
    uint8_t specularity;                    // +0x18
    uint8_t pixelShaders;                   // +0x19
    uint8_t specular;                       // +0x1A
    int32_t waterLOD;                       // +0x1C
    float particleDensity;                  // +0x20
    float unitDrawDist;                     // +0x24
    float smallCull;                        // +0x28
    float distCull;                         // +0x2C
    const CGxFormat* format;                // +0x30
    int32_t baseMip;                        // +0x34
    int32_t unk38;                          // +0x38
    uint8_t unk3C;                          // +0x3C
};

void ConsoleDetectDetectHardware(HardwareInfo* hardware, bool* changed);

void ConsoleDetectGetResolutions(TSGrowableArray<C2iVector>& resolutions, int32_t widescreen);

void ConsoleDetectSetDefaults(DefaultSettings* defaults, const HardwareInfo* hardware);

void ConsoleDetectSetDefaultsFormat(DefaultSettings* defaults, const HardwareInfo* hardware);

#endif
