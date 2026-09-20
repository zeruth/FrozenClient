#include "ui/simple/MovieDecoder.hpp"

#include <mp4dec_api.h>

#include <storm/Memory.hpp>
#include <cstring>

namespace {

// 'DXGM', the fourcc DivX's game profile writes and the only one the cinematics use.
const uint32_t CODEC_DXGM = 0x4D475844;

int32_t Clamp255(int32_t v) {
    if (v < 0) {
        return 0;
    }

    return v > 255 ? 255 : v;
}

} // namespace

struct MovieDecoder {
    VideoDecControls ctrl = {};
    bool started = false;

    int32_t width = 0;
    int32_t height = 0;

    // The decoder writes whole macroblocks, so its picture is padded up to a multiple of 16.
    int32_t paddedWidth = 0;
    int32_t paddedHeight = 0;

    uint8_t* yuv = nullptr;
    uint8_t* bgra = nullptr;

    bool haveFrame = false;
    uint32_t lastIndex = 0xFFFFFFFF;
};

MovieDecoder* MovieDecoderCreate(const MovieAvi& movie) {
    if (movie.videoCodec != CODEC_DXGM || movie.width <= 0 || movie.height <= 0 || !movie.videoChunkCount) {
        return nullptr;
    }

    auto decoder = new (SMemAlloc(sizeof(MovieDecoder), __FILE__, __LINE__, 0)) MovieDecoder();

    decoder->width = movie.width;
    decoder->height = movie.height;
    decoder->paddedWidth = (movie.width + 15) & ~15;
    decoder->paddedHeight = (movie.height + 15) & ~15;

    // The stream carries its own VOL header in the first frame, so the decoder is initialised from
    // that rather than from a separate configuration block -- AVI has nowhere to put one.
    uint8_t* volbuf[1] = { movie.videoData + movie.videoChunks[0].offset };
    int32_t volsize[1] = { static_cast<int32_t>(movie.videoChunks[0].size) };

    if (!PVInitVideoDecoder(&decoder->ctrl, volbuf, volsize, 1, movie.width, movie.height, MPEG4_MODE)) {
        SMemFree(decoder, __FILE__, __LINE__, 0);

        return nullptr;
    }

    decoder->started = true;

    // No post-processing: it costs time and the reference does not deblock these either.
    PVSetPostProcType(&decoder->ctrl, 0);

    int32_t displayWidth = 0;
    int32_t displayHeight = 0;
    PVGetVideoDimensions(&decoder->ctrl, &displayWidth, &displayHeight);

    if (displayWidth > 0 && displayHeight > 0) {
        decoder->width = displayWidth;
        decoder->height = displayHeight;
        decoder->paddedWidth = (displayWidth + 15) & ~15;
        decoder->paddedHeight = (displayHeight + 15) & ~15;
    }

    size_t luma = static_cast<size_t>(decoder->paddedWidth) * decoder->paddedHeight;

    decoder->yuv = static_cast<uint8_t*>(SMemAlloc(luma + luma / 2, __FILE__, __LINE__, 0));
    decoder->bgra = static_cast<uint8_t*>(SMemAlloc(static_cast<size_t>(decoder->width) * decoder->height * 4, __FILE__, __LINE__, 0));

    return decoder;
}

void MovieDecoderDestroy(MovieDecoder* decoder) {
    if (!decoder) {
        return;
    }

    if (decoder->started) {
        PVCleanUpVideoDecoder(&decoder->ctrl);
    }

    if (decoder->yuv) {
        SMemFree(decoder->yuv, __FILE__, __LINE__, 0);
    }

    if (decoder->bgra) {
        SMemFree(decoder->bgra, __FILE__, __LINE__, 0);
    }

    SMemFree(decoder, __FILE__, __LINE__, 0);
}

bool MovieDecoderFrame(MovieDecoder* decoder, const MovieAvi& movie, uint32_t index) {
    if (!decoder || !decoder->started || index >= movie.videoChunkCount) {
        return false;
    }

    if (decoder->haveFrame && index == decoder->lastIndex) {
        return true;
    }

    const MovieAviChunk& chunk = movie.videoChunks[index];

    uint8_t* bitstream[1] = { movie.videoData + chunk.offset };
    int32_t bufferSize[1] = { static_cast<int32_t>(chunk.size) };
    uint32_t timestamp = 0;
    uint32_t useExternal[1] = { 0 };

    if (!PVDecodeVideoFrame(&decoder->ctrl, bitstream, &timestamp, bufferSize, useExternal, decoder->yuv)) {
        return false;
    }

    decoder->haveFrame = true;
    decoder->lastIndex = index;

    // YUV 4:2:0 planar to BGRA. The chroma planes are half size in both directions, and the
    // decoder's stride is the padded width, not the display width.
    int32_t w = decoder->width;
    int32_t h = decoder->height;
    int32_t stride = decoder->paddedWidth;
    int32_t chromaStride = stride / 2;

    const uint8_t* planeY = decoder->yuv;
    const uint8_t* planeU = planeY + static_cast<size_t>(stride) * decoder->paddedHeight;
    const uint8_t* planeV = planeU + (static_cast<size_t>(stride) * decoder->paddedHeight) / 4;

    uint8_t* out = decoder->bgra;

    for (int32_t y = 0; y < h; y++) {
        const uint8_t* rowY = planeY + static_cast<size_t>(y) * stride;
        const uint8_t* rowU = planeU + static_cast<size_t>(y / 2) * chromaStride;
        const uint8_t* rowV = planeV + static_cast<size_t>(y / 2) * chromaStride;

        for (int32_t x = 0; x < w; x++) {
            // BT.601, the range these were encoded in.
            int32_t c = static_cast<int32_t>(rowY[x]) - 16;
            int32_t d = static_cast<int32_t>(rowU[x / 2]) - 128;
            int32_t e = static_cast<int32_t>(rowV[x / 2]) - 128;

            int32_t r = (298 * c + 409 * e + 128) >> 8;
            int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
            int32_t b = (298 * c + 516 * d + 128) >> 8;

            out[0] = static_cast<uint8_t>(Clamp255(b));
            out[1] = static_cast<uint8_t>(Clamp255(g));
            out[2] = static_cast<uint8_t>(Clamp255(r));
            out[3] = 0xFF;
            out += 4;
        }
    }

    return true;
}

const uint8_t* MovieDecoderPixels(MovieDecoder* decoder) {
    return (decoder && decoder->haveFrame) ? decoder->bgra : nullptr;
}

int32_t MovieDecoderWidth(MovieDecoder* decoder) {
    return decoder ? decoder->width : 0;
}

int32_t MovieDecoderHeight(MovieDecoder* decoder) {
    return decoder ? decoder->height : 0;
}
