#ifndef DB_REC_VIDEO_HARDWARE_REC_HPP
#define DB_REC_VIDEO_HARDWARE_REC_HPP

#include <cstdint>

class SFile;

// VideoHardware.dbc: one row per known display adapter, keyed by PCI vendor and device, plus the
// generic rows (vendor 0xFFFF) for adapters it does not know. Each row picks the default video
// settings for that hardware: indexes into the console's tables, a few direct values, and the
// gxOverride strings for each API.
class VideoHardwareRec {
    public:
        int32_t m_ID;                       // +0x00
        int32_t m_vendorID;                 // +0x04
        int32_t m_deviceID;                 // +0x08
        int32_t m_farclipIdx;               // +0x0C
        int32_t m_terrainLODDistIdx;        // +0x10
        int32_t m_terrainShadowLOD;         // +0x14
        int32_t m_detailDoodadDensityIdx;   // +0x18
        int32_t m_detailDoodadAlpha;        // +0x1C
        int32_t m_animatingDoodadIdx;       // +0x20
        int32_t m_trilinear;                // +0x24
        int32_t m_numLights;                // +0x28
        int32_t m_specularity;              // +0x2C
        int32_t m_waterLODIdx;              // +0x30
        int32_t m_particleDensityIdx;       // +0x34
        int32_t m_unitDrawDistIdx;          // +0x38
        int32_t m_smallCullDistIdx;         // +0x3C
        int32_t m_resolutionIdx;            // +0x40
        int32_t m_baseMipLevel;             // +0x44
        const char* m_oglOverrides;         // +0x48
        const char* m_d3dOverrides;         // +0x4C
        int32_t m_fixLag;                   // +0x50
        int32_t m_multisample;              // +0x54
        int32_t m_atlasdisable;             // +0x58

        static const char* GetFilename();
        static uint32_t GetNumColumns();
        static uint32_t GetRowSize();
        static bool NeedIDAssigned();
        int32_t GetID();
        void SetID(int32_t id);
        bool Read(SFile* f, const char* stringBuffer);
};

#endif
