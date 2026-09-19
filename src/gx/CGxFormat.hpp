#ifndef GX_C_GX_FORMAT_HPP
#define GX_C_GX_FORMAT_HPP

#include <cstdint>
#include <tempest/Vector.hpp>

class CGxFormat {
    public:
        // Types
        enum Format {
            Fmt_Rgb565 = 0,
            Fmt_ArgbX888 = 1,
            Fmt_Argb8888 = 2,
            Fmt_Argb2101010 = 3,
            Fmt_Ds160 = 4,
            Fmt_Ds24X = 5,
            Fmt_Ds248 = 6,
            Fmt_Ds320 = 7,
            Formats_Last = 8
        };

        // Static variables
        static int32_t formatToBitsUint[Formats_Last];
        static const char* formatToBitsString[Formats_Last];

        // Member variables
        bool hwTnL;
        int8_t cursor;   // hardware cursor (gxCursor)
        int8_t fixLag;   // gxFixLag
        int8_t window;
        int32_t maximize;
        Format depthFormat;
        C2iVector size;
        uint32_t multisampleCount;
        Format colorFormat;
        uint32_t refreshRate;
        uint32_t vsync;
        C2iVector pos;
        int32_t aspect;           // gxAspect: constrain the window aspect
        int32_t backBufferCount;  // gxTripleBuffer: 1 or 2 (the D3D device's "int1C")
        float multisampleQuality; // gxMultisampleQuality, [0, 1)
};

// Reference layout (3.3.5a, the requested format at 0x00cabcd8), recovered from the gx CVar
// callbacks and still to be confirmed field by field before this struct is reordered to match:
//   +0x04 hwTnL, +0x05 cursor, +0x06 fixLag, +0x07 window (bytes), +0x08 aspect, +0x0c maximize,
//   +0x10 depthFormat, +0x14 size, +0x1c backBufferCount, +0x20 multisampleCount,
//   +0x24 multisampleQuality, +0x28 colorFormat, +0x30 vsync. +0x00 and +0x2c are not yet known.

int32_t AdapterFormatSort(const void* a, const void* b);

#endif
