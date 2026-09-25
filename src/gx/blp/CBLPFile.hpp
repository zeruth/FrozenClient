#ifndef GX_BLP_C_BLP_FILE_HPP
#define GX_BLP_C_BLP_FILE_HPP

#include "gx/blp/Types.hpp"
#include "gx/Types.hpp"
#include <storm/Array.hpp>
#include <cstdint>

class CBLPFile {
    public:
        // Static variables
        static TSGrowableArray<unsigned char> s_blpFileLoadBuffer;

        // Member variables
        MipBits* m_images = nullptr;
        BLPHeader m_header;
        void* m_inMemoryImage = nullptr;
        int32_t m_inMemoryNeedsFree;
        uint32_t m_numLevels;
        uint32_t m_quality = 100;
        void* m_colorMapping;
        MipMapAlgorithm m_mipMapAlgorithm = MMA_BOX;
        char* m_lockDecompMem;

        // Member functions
        CBLPFile();
        void Close();
        void DecompPalARGB8888Alpha8(uint32_t* out, const unsigned char* in, uint32_t count);
        void DecompPalARGB1555DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height);
        void DecompPalRGB565DitherFS(uint16_t* out, const unsigned char* in, uint32_t width, uint32_t height);
        uint32_t GetMipPixelCount(uint32_t mipLevel);
        int32_t GetMipSize(PIXEL_FORMAT format, uint32_t mipLevel, uint32_t* size, uint32_t* stride);
        int32_t Lock2(const char*, PIXEL_FORMAT, uint32_t, unsigned char*, uint32_t&);
        int32_t LockChain2(const char*, PIXEL_FORMAT, MipBits*&, uint32_t, int32_t);
        int32_t Open(const char*, int32_t);
        int32_t Source(void*);
        int32_t Unlock(uint32_t mipLevel);
};

#endif
