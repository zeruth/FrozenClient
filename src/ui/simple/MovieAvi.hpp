#ifndef UI_SIMPLE_MOVIE_AVI_HPP
#define UI_SIMPLE_MOVIE_AVI_HPP

#include <cstdint>

// The AVI container the cinematics come in, read the way the reference reads it.
//
// The reference does not use a system demuxer for these: CSimpleMovieFrame.cpp walks the RIFF tree
// itself (FUN_0095dc80 compares RIFF, "AVI ", LIST, hdrl, movi, idx1, strh, strf, vids and auds),
// reads the index, and pulls the whole video and audio streams into two buffers before playing.
// That is portable, so it is ported here rather than replaced -- the only piece that cannot be is
// the codec, which the reference hands to DivxDecoder.dll.
//
// What the shipped files actually are, read out of their headers rather than assumed:
//
//   video  fourcc DXGM, DivX's game profile -- MPEG-4 Part 2
//   audio  wFormatTag 0x0055 -- MP3
//
// so decoding needs an MPEG-4 Part 2 decoder and an MP3 decoder. Neither is in this client, and the
// reference's DivxDecoder.dll is 32-bit Windows, which rules it out for a 64-bit build and for
// Linux and Android. See MovieDecoder below for the seam a decoder plugs into.

// One chunk of a stream, as the index describes it: an offset into the buffer and a length.
struct MovieAviChunk {
    uint32_t offset;
    uint32_t size;
};

struct MovieAvi {
    // Video
    int32_t width = 0;
    int32_t height = 0;
    uint32_t videoCodec = 0;      // biCompression, e.g. 'DXGM'
    float frameRate = 0.0f;       // strh rate / scale
    uint32_t frameCount = 0;

    // Audio
    uint16_t audioFormat = 0;     // WAVEFORMATEX wFormatTag, 0x0055 for MP3
    uint16_t audioChannels = 0;
    uint32_t audioSampleRate = 0;

    // The streams, read whole. The reference does the same -- it sums the index entries for the
    // 'd' (video) and 'w' (audio) chunks, allocates one buffer each, and reads every chunk in.
    uint8_t* videoData = nullptr;
    uint32_t videoSize = 0;
    uint8_t* audioData = nullptr;
    uint32_t audioSize = 0;

    // Where each video frame sits inside videoData.
    MovieAviChunk* videoChunks = nullptr;
    uint32_t videoChunkCount = 0;
};

// Open and demux. Returns false and leaves nothing allocated when the file is missing or is not an
// AVI this reader understands. Goes through SFile, so it finds the cinematics whether they are
// loose on disk (which is where the shipped ones are) or inside an archive.
bool MovieAviOpen(const char* path, MovieAvi& movie);

// Release everything MovieAviOpen allocated.
void MovieAviClose(MovieAvi& movie);

// A four character code as text, for messages. Returns a pointer to a static buffer.
const char* MovieAviFourCC(uint32_t fourcc);

#endif
