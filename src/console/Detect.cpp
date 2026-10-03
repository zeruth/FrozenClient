#include "console/Detect.hpp"
#include "gx/Adapter.hpp"
#include "client/gui/OsGui.hpp"
#include "db/WowClientDB.hpp"
#include "db/rec/VideoHardwareRec.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/CGxFormat.hpp"
#include "util/OsSystem.hpp"
#include "util/Registry.hpp"
#include <cstdlib>

void AddResolution(TSGrowableArray<C2iVector>& resolutions, const C2iVector& resolution) {
    resolutions.Add(1, &resolution);
}

// TODO sometimes 640x480 is included, sometimes its ignored -- more than one function?
void ConsoleDetectGetResolutions(TSGrowableArray<C2iVector>& resolutions, int32_t widescreen) {
    // "Widescreen" resolutions
    //
    // This branch is just dynamically enumerating resolutions above a minimum size and aspect
    // ratio as opposed to using a fixed list of common 1.33:1 resolutions. The name widescreen
    // comes from the CVar that feeds the argument (see: SetupResolutions).

    if (widescreen) {
        TSGrowableArray<CGxMonitorMode> monitorModes;
        GxAdapterMonitorModes(monitorModes);

        C2iVector previousResolution = { 0, 0 };

        for (uint32_t i = 0; i < monitorModes.Count(); i++) {
            auto& monitorMode = monitorModes[i];
            auto& resolution = monitorMode.size;

            // Minimum aspect ratio
            if (static_cast<float>(resolution.x) / static_cast<float>(resolution.y) < 1.248f) {
                continue;
            }

            // Minimum resolution
            if (resolution.x < 640 || resolution.y < 480) {
                continue;
            }

            // Not already present
            if (resolution.x == previousResolution.x && resolution.y == previousResolution.y) {
                continue;
            }

            AddResolution(resolutions, resolution);
            previousResolution = resolution;
        }
    }

#if defined(WHOA_SYSTEM_ANDROID)
    // The surface's own size is always offered, whatever its aspect ratio; it is the last mode
    {
        TSGrowableArray<CGxMonitorMode> monitorModes;
        GxAdapterMonitorModes(monitorModes);

        if (monitorModes.Count()) {
            auto& native = monitorModes[monitorModes.Count() - 1].size;
            bool present = false;

            for (uint32_t i = 0; i < resolutions.Count(); i++) {
                if (resolutions[i].x == native.x && resolutions[i].y == native.y) {
                    present = true;
                }
            }

            if (!present) {
                AddResolution(resolutions, native);
            }
        }
    }
#endif

    // Fallback resolutions

    if (!widescreen || resolutions.Count() == 0) {
        AddResolution(resolutions, {  640,  480 });
        AddResolution(resolutions, {  800,  600 });
        AddResolution(resolutions, { 1024,  768 });
        AddResolution(resolutions, { 1152,  864 });
        AddResolution(resolutions, { 1280,  960 });
        AddResolution(resolutions, { 1280, 1024 });
        AddResolution(resolutions, { 1600, 1200 });
    }
}

// The default-settings tables, two columns per entry: the VideoHardware row picks the entry and
// the CPU class picks the column (reference 0x00adbe24..0x00adbf24).
static const int32_t s_cpuClasses[2][6] = {
    { 0, 0, 0, 0, 0, 0 },
    { 1, 1, 1, 1, 1, 1 },
};
static const int32_t s_memClass[2] = { 1, 0 };
static const int32_t s_groundEffectDensity[4] = { 8, 12, 16, 24 };
static const uint8_t s_animatingDoodads[4] = { 0, 0, 0, 1 };
static const int32_t s_waterLOD[4] = { 0, 0, 0, 1 };
static const float s_particleDensity[8] = { 0.3f, 0.4f, 0.5f, 0.6f, 0.9f, 1.0f, 1.0f, 1.0f };
static const float s_unitDrawDist[8] = { 35.0f, 50.0f, 50.0f, 75.0f, 75.0f, 300.0f, 300.0f, 300.0f };
static const float s_smallCull[8] = { 0.08f, 0.08f, 0.07f, 0.07f, 0.07f, 0.07f, 0.04f, 0.04f };
static const float s_distCull[8] = { 350.0f, 350.0f, 400.0f, 400.0f, 450.0f, 450.0f, 500.0f, 500.0f };
static const float s_farClip[10] = { 200.0f, 277.0f, 300.0f, 350.0f, 350.0f, 400.0f, 450.0f, 550.0f, 450.0f, 777.0f };

// The registry names the detected hardware is remembered under (reference 0x00adbf28).
static const char* s_hwCpuIdx = "HWCpuIdx";
static const char* s_hwMemIdx = "HWMemIdx";
static const char* s_hwVideoID = "HWVideoID";
static const char* s_hwSoundIdx = "HWSoundIdx";

// The default formats a VideoHardware row's resolution index picks from (reference 0x00cabe98).
static const CGxFormat s_resolutionFormats[] = {
    CGxFormat(false, C2iVector(640, 480), CGxFormat::Fmt_Rgb565, CGxFormat::Fmt_Ds160, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(800, 600), CGxFormat::Fmt_Rgb565, CGxFormat::Fmt_Ds160, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(640, 480), CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Ds24X, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(800, 600), CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Ds24X, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(1024, 768), CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Ds24X, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(1280, 1024), CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Ds24X, 60, 1, true, 0, 1, 1, 0),
    CGxFormat(false, C2iVector(1600, 1200), CGxFormat::Fmt_ArgbX888, CGxFormat::Fmt_Ds24X, 60, 1, true, 0, 1, 1, 0),
};

// The reference keeps this one outside the client's database list (DAT_00adbf88).
static WowClientDB<VideoHardwareRec> s_videoHardwareDB;

// ref: FUN_0076b540
// The row for the adapter's vendor and device. The first row is never matched.
static void ConsoleDetectFindVideoID(HardwareInfo* hardware) {
    for (int32_t i = 1; i < s_videoHardwareDB.GetNumRecords(); i++) {
        auto record = s_videoHardwareDB.GetRecordByIndex(i);

        if (hardware->vendorID == static_cast<uint32_t>(record->m_vendorID)
            && hardware->deviceID == static_cast<uint32_t>(record->m_deviceID)) {
            hardware->videoID = record->m_ID;
            return;
        }
    }
}

// ref: FUN_0076b5a0
// The reference takes the text from Startup_Strings; frozen does not load that table yet, so the
// built-in English text it falls back on is all there is.
static void ConsoleDetectFatal(const char* text) {
    OsGuiMessageBox(OsGuiGetWindow(2), 0, text, "World of Warcraft");
    _exit(0);
}

// ref: FUN_0076b620
// Compares the hardware with what the last run remembered. A difference asks whether to load the
// defaults again; either way the new hardware is remembered. Video IDs 1 and 0xA8-0xAA stand for
// hardware the client cannot tell apart, so any of them matches.
static void ConsoleDetectCheckHardwareChanged(const HardwareInfo* hardware, bool* changed) {
    uint32_t cpuIdx;
    uint32_t memIdx;
    uint32_t videoID;
    uint32_t soundIdx;

    if (!RegistryReadInt("World of Warcraft\\Client", s_hwCpuIdx, 0, &cpuIdx)) {
        cpuIdx = hardware->cpuIdx;
    }

    if (!RegistryReadInt("World of Warcraft\\Client", s_hwMemIdx, 0, &memIdx)) {
        memIdx = hardware->memIdx;
    }

    if (!RegistryReadInt("World of Warcraft\\Client", s_hwVideoID, 0, &videoID)) {
        videoID = hardware->videoID;
    }

    if (!RegistryReadInt("World of Warcraft\\Client", s_hwSoundIdx, 0, &soundIdx)) {
        soundIdx = hardware->soundIdx;
    }

    bool anyVideo = videoID == 1 || videoID == 0xA8 || videoID == 0xA9 || videoID == 0xAA;

    if (hardware->cpuIdx == static_cast<int32_t>(cpuIdx)
        && hardware->memIdx == static_cast<int32_t>(memIdx)
        && (anyVideo || hardware->videoID == static_cast<int32_t>(videoID))
        && hardware->soundIdx == static_cast<int32_t>(soundIdx)) {
        *changed = false;
    } else {
        auto answer = OsGuiMessageBox(OsGuiGetWindow(2), 2, "Hardware changed.  Reload default settings?", "World of Warcraft");
        *changed = answer == 0;
    }

    RegistryWriteInt("World of Warcraft\\Client", s_hwCpuIdx, 0, hardware->cpuIdx);
    RegistryWriteInt("World of Warcraft\\Client", s_hwMemIdx, 0, hardware->memIdx);
    RegistryWriteInt("World of Warcraft\\Client", s_hwVideoID, 0, hardware->videoID);
    RegistryWriteInt("World of Warcraft\\Client", s_hwSoundIdx, 0, hardware->soundIdx);
}

// ref: FUN_0076ba30
// Works out the CPU class and the VideoHardware row: by the adapter's own vendor and device when
// the table knows them, otherwise by the class the adapter's capabilities put it in.
void ConsoleDetectDetectHardware(HardwareInfo* hardware, bool* changed) {
    if (OsIsRemoteSession()) {
        ConsoleDetectFatal("Running World of Warcraft through a Remote Desktop connection is not supported.  Exiting program.");
    }

    s_videoHardwareDB.Load(__FILE__, __LINE__);

    auto cpuSpeed = OsGetCpuSpeed();
    hardware->cpuIdx = static_cast<float>(cpuSpeed) * 0.000001f <= 1500.0f ? 0 : 1;

    hardware->memIdx = 0;
    hardware->videoID = 0;

    if (GxAdapterID(&hardware->vendorID, &hardware->deviceID, &hardware->driverVersionHi, &hardware->driverVersionLo)) {
        ConsoleDetectFindVideoID(hardware);
    }

    if (hardware->videoID == 0) {
        hardware->vendorID = 0xFFFF;

        if (GxAdapterInfer(&hardware->deviceID)) {
            ConsoleDetectFindVideoID(hardware);
        }

        if (hardware->videoID == 0) {
            ConsoleDetectFatal("Failed to find a suitable display device.  Exiting program.");
        }
    }

    hardware->soundIdx = 0;
    hardware->cpuClass = s_cpuClasses[hardware->cpuIdx];
    hardware->videoHardware = s_videoHardwareDB.GetRecord(hardware->videoID);
    hardware->memClass = s_memClass;

    ConsoleDetectCheckHardwareChanged(hardware, changed);

    CGxDevice::Log("ConsoleDetectDetectHardware():");
    CGxDevice::Log("\tcpuIdx: %d", hardware->cpuIdx);
    CGxDevice::Log("\tvideoID: %d", hardware->videoID);
    CGxDevice::Log("\tsoundIdx: %d", hardware->soundIdx);
    CGxDevice::Log("\tmemIdx: %d", hardware->memIdx);
}

// ref: FUN_0076b3f0
void ConsoleDetectSetDefaults(DefaultSettings* defaults, const HardwareInfo* hardware) {
    auto video = hardware->videoHardware;
    auto cpu = hardware->cpuClass;

    defaults->farClip = s_farClip[cpu[0] + video->m_farclipIdx * 2];
    defaults->shadowLevel = video->m_terrainShadowLOD;
    defaults->groundEffectDensity = s_groundEffectDensity[video->m_detailDoodadDensityIdx];
    defaults->groundEffectAlpha = video->m_detailDoodadAlpha;
    defaults->animatingDoodads = s_animatingDoodads[video->m_animatingDoodadIdx * 2 + cpu[1]];
    defaults->trilinear = video->m_trilinear != 0;
    defaults->maxLights = video->m_numLights;
    defaults->specular = video->m_specularity != 0;
    defaults->specularity = video->m_specularity != 0;
    defaults->waterLOD = s_waterLOD[cpu[2] + video->m_waterLODIdx * 2];
    defaults->particleDensity = s_particleDensity[cpu[3] + video->m_particleDensityIdx * 2];
    defaults->unitDrawDist = s_unitDrawDist[cpu[5] + video->m_unitDrawDistIdx * 2];
    defaults->smallCull = s_smallCull[cpu[4] + video->m_smallCullDistIdx * 2];
    defaults->distCull = s_distCull[cpu[4] + video->m_smallCullDistIdx * 2];
    defaults->format = &s_resolutionFormats[video->m_resolutionIdx];
    defaults->baseMip = video->m_baseMipLevel;
    defaults->pixelShaders = 1;
    defaults->unk38 = 0;
    defaults->unk3C = 0;
}

// ref: FUN_0076b520
void ConsoleDetectSetDefaultsFormat(DefaultSettings* defaults, const HardwareInfo* hardware) {
    defaults->format = &s_resolutionFormats[hardware->videoHardware->m_resolutionIdx];
}
