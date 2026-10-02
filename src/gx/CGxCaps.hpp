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
// TWO OF THOSE OFFSETS ARE NOW IDENTIFIED, 2026-09-26, from the shadow map's own caps check
// (FUN_008740d0): it compares caps+0xb4 against 1, 2, 3, 6 and 10 and caps+0xc4 against 3, 4, 0xb
// and 0xc, which are exactly the EGxShVertexShader and EGxShPixelShader values for
// vs_1_1/vs_2_0/vs_3_0 and arbvp1/nvvp3, and ps_2_0/ps_3_0 and nvfp2/arbfp1. So the reference's
// m_shaderTargets begins at 0xb4 and 0xc4 is its GxSh_Pixel entry, four ints along -- which also
// explains why 0xc4 has the most reads after 0x14: it is the pixel shader profile. The same
// function reads caps+0xac and caps+0xa8 as "is either non-zero", three and two ints below
// m_shaderTargets and so where m_texFmt's tail would land; they are read as m_texFmt[GxTex_D24X8]
// and m_texFmt[GxTex_R32F], the two formats the shadow targets are created with.
//
// Two consequences worth stating. First, do not reason about a reference caps field by offset and
// expect to find it here -- 0xb4 is m_shaderTargets rather than a scalar, and CM2Lighting::SetupGxFog
// reads a scalar at 0xb4 that this class does not have (it gates the FogStart/FogEnd writes on it),
// so those two readings cannot both be right and the fog one is the unresolved half. Second, filling this class in is a real porting task
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

        // The rest of the reference's CGxCaps (device +0x214..+0x350), which ISetCaps fills and
        // whoa's layout did not carry. Frozen's order differs from the reference's above this line,
        // so offsets are given in reference terms on each.
        int32_t m_texFmtRtt[GxTexFormats_Last] = { 0 }; // +0x7c: usable as a render target (D24X8: depth)
        int32_t m_texNonPow2Conditional = 1;              // +0x68: non-pow2 only with the usual restrictions
        int32_t m_shaderConsts[GxShTargets_Last] = { 0 }; // +0xcc: constant registers (vertex only, set)
        int32_t m_colorWrite = 0;                         // +0xf4: D3DPMISCCAPS_COLORWRITEENABLE
        uint32_t m_maxClipPlanes = 0;                     // +0xf8
        int32_t m_hardwareCursor = 0;                     // +0xfc: D3DCURSORCAPS_COLOR
        int32_t m_occlusionQuery = 0;                     // +0x100
        int32_t m_pointSprites = 0;                       // +0x104: max point size above 1
        float m_maxPointSize = 0.0f;                      // +0x108
        int32_t m_pointScale = 0;                         // +0x10c
        int32_t m_blendFactor = 0;                        // +0x110: D3DPBLENDCAPS_BLENDFACTOR
        int32_t int114[5] = { 0 };                        // +0x114..+0x124: zeroed, meaning unknown
        int32_t int130b = 1;                              // +0x130
        int32_t m_notPs30a = 0;                           // +0x134: pixel target below ps_3_0
        int32_t m_notPs30b = 0;                           // +0x138: the same, a second consumer
};

#endif
