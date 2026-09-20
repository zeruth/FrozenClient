#ifndef UI_SIMPLE_MOVIE_DECODER_HPP
#define UI_SIMPLE_MOVIE_DECODER_HPP

#include "ui/simple/MovieAvi.hpp"

#include <cstdint>

// Video decoding for the cinematics.
//
// The shipped movies are MPEG-4 Part 2 in an AVI, fourcc DXGM. Scoping the bitstream of all twelve
// before choosing a decoder: rectangular, progressive, no sprite or GMC, half-pel, and I and P
// frames only -- no B-frames anywhere. That is Simple Profile.
//
// The reference decodes these through DivxDecoder.dll, which is 32-bit Windows and therefore no
// use to a 64-bit build or to Linux and Android. The decoder behind this is the AOSP MPEG-4 / H.263
// one (Apache-2.0, vendored under vendor/m4vh263dec): self-contained C++, no external dependencies,
// and it builds for every target this client does.
//
// Frames come out as YUV 4:2:0 and are converted here to the BGRA the texture upload wants.

struct MovieDecoder;

// Start decoding the video stream of an opened movie. Null when the codec is not one this can
// decode, or when the decoder rejects the stream's headers.
MovieDecoder* MovieDecoderCreate(const MovieAvi& movie);

void MovieDecoderDestroy(MovieDecoder* decoder);

// Decode frame `index` of the movie.
//
// Frames must be asked for in order: every frame but a keyframe is coded against the one before it,
// so the decoder holds the previous picture and skipping forward would show the difference against
// the wrong image. Asking for the frame just decoded returns it again without re-decoding.
bool MovieDecoderFrame(MovieDecoder* decoder, const MovieAvi& movie, uint32_t index);

// The last decoded frame as BGRA, width * height * 4 bytes, top row first. Null before the first
// successful decode. Valid until the next decode or destroy.
const uint8_t* MovieDecoderPixels(MovieDecoder* decoder);

int32_t MovieDecoderWidth(MovieDecoder* decoder);
int32_t MovieDecoderHeight(MovieDecoder* decoder);

#endif
