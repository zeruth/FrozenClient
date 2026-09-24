#ifndef GX_C_GX_CAPS_HPP
#define GX_C_GX_CAPS_HPP

#include "gx/Types.hpp"
#include <cstdint>

// NOT LAYOUT-FAITHFUL, and the field names say so if you check them. int130, int134 and int138
// are named for their offsets in the REFERENCE's CGxCaps, but laid out as declared below they land
// at 0xa0, 0xa4 and 0xa8 -- this class is about 0x90 bytes shorter than the one it mirrors, with
// the missing fields somewhere in the middle rather than at the end.
//
// Measured 2026-09-23 rather than assumed. The reference reaches its caps through a 7-byte
// accessor (FUN_00532af0, `return device + 0x214`) called on the global device, and tallying what
// its 146 call sites read gives 0x14 (28 reads), 0xc4 (26), 0xb4 (10), 0x134 (7), 0xf8 (5),
// 0x138 (5), 0x108 (4), 0x130 (3) and others. The presence of 0x130, 0x134 and 0x138 in that list
// is what identifies the object as this class.
//
// Two consequences worth stating. First, do not reason about a reference caps field by offset and
// expect to find it here -- there is no field at 0xb4, and CM2Lighting::SetupGxFog needs one (it
// gates the FogStart/FogEnd writes on it). Second, filling this class in is a real porting task
// with a measurable target: eight or so distinct offsets are read across the reference and only
// three of them have names here.
class CGxCaps {
    public:
        int32_t m_numTmus = 0;
        int32_t m_pixelCenterOnEdge = 0;
        int32_t m_texelCenterOnEdge = 0;
        int32_t m_numStreams = 0;
        int32_t int10 = 0;
        EGxColorFormat m_colorFormat = GxCF_argb;
        uint32_t m_maxIndex = 0;
        int32_t m_generateMipMaps = 0;
        int32_t m_texFmt[GxTexFormats_Last] = { 0 };
        int32_t m_texTarget[GxTexTargets_Last];
        uint32_t m_texMaxSize[GxTexTargets_Last];
        int32_t m_shaderTargets[GxShTargets_Last] = { 0 };
        int32_t m_texFilterTrilinear = 0;
        int32_t m_texFilterAnisotropic = 0;
        uint32_t m_maxTexAnisotropy = 0;
        int32_t m_depthBias = 0;
        int32_t m_stereoAvailable = 0;
        int32_t int130 = 1;
        int32_t int134 = 0;
        int32_t int138 = 0;
};

#endif
