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

        // Member variables. Reference layout (3.3.5a): 0x58 bytes, recovered from every code
        // reference into the requested format at 0x00cabcd8 (tools/recomp/ghidra/ExportStructRefs)
        // and the cvar callbacks that write it. Offsets are the reference's.
        uint32_t unk0;              // +0x00 never written by the cvar layer; copied with the rest
        bool hwTnL;                 // +0x04
        int8_t cursor;              // +0x05 gxCursor
        int8_t fixLag;              // +0x06 gxFixLag
        int8_t window;              // +0x07 gxWindow
        int8_t aspect;              // +0x08 gxAspect
        int32_t maximize;           // +0x0c gxMaximize
        Format depthFormat;         // +0x10 gxDepthBits
        C2iVector size;             // +0x14 gxResolution
        uint32_t backBufferCount;   // +0x1c gxTripleBuffer: 1 or 2
        uint32_t multisampleCount;  // +0x20 gxMultisample
        float multisampleQuality;   // +0x24 gxMultisampleQuality
        Format colorFormat;         // +0x28 gxColorBits
        uint32_t refreshRate;       // +0x2c gxRefresh
        uint32_t vsync;             // +0x30 gxVSync
        int8_t stereoEnabled;       // +0x34 gxStereoEnabled
        int32_t unk38;              // +0x38 zeroed when fixedFunction is set
        int32_t unk3c;
        int32_t unk40;
        int32_t unk44;
        int32_t unk48;              // +0x48 zeroed when fixedFunction is set
        int32_t unk4c;
        int32_t unk50;
        int32_t unk54;

        // whoa-only: the window position the D3D backend creates the window at. Not part of the
        // reference struct.
        C2iVector pos;
};

int32_t AdapterFormatSort(const void* a, const void* b);

#endif
