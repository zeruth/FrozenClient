#include "ui/simple/MovieAvi.hpp"

#include "util/SFile.hpp"

#include <storm/Memory.hpp>
#include <cstring>

namespace {

// Little-endian four character code, in the order the bytes appear in the file.
constexpr uint32_t FourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a))
        | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8)
        | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16)
        | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

const uint32_t CC_RIFF = FourCC('R', 'I', 'F', 'F');
const uint32_t CC_AVI  = FourCC('A', 'V', 'I', ' ');
const uint32_t CC_LIST = FourCC('L', 'I', 'S', 'T');
const uint32_t CC_hdrl = FourCC('h', 'd', 'r', 'l');
const uint32_t CC_strl = FourCC('s', 't', 'r', 'l');
const uint32_t CC_movi = FourCC('m', 'o', 'v', 'i');
const uint32_t CC_idx1 = FourCC('i', 'd', 'x', '1');
const uint32_t CC_strh = FourCC('s', 't', 'r', 'h');
const uint32_t CC_strf = FourCC('s', 't', 'r', 'f');
const uint32_t CC_vids = FourCC('v', 'i', 'd', 's');
const uint32_t CC_auds = FourCC('a', 'u', 'd', 's');

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

} // namespace

const char* MovieAviFourCC(uint32_t fourcc) {
    static char text[5];

    for (int32_t i = 0; i < 4; i++) {
        char c = static_cast<char>((fourcc >> (i * 8)) & 0xFF);
        text[i] = (c >= 32 && c < 127) ? c : '?';
    }

    text[4] = '\0';

    return text;
}

void MovieAviClose(MovieAvi& movie) {
    if (movie.videoData) {
        SMemFree(movie.videoData, __FILE__, __LINE__, 0);
    }

    if (movie.audioData) {
        SMemFree(movie.audioData, __FILE__, __LINE__, 0);
    }

    if (movie.videoChunks) {
        SMemFree(movie.videoChunks, __FILE__, __LINE__, 0);
    }

    movie = MovieAvi();
}

bool MovieAviOpen(const char* path, MovieAvi& movie) {
    movie = MovieAvi();

    if (!path || !*path) {
        return false;
    }

    // The reference seeks around the open handle; frozen's SFile has no seek, and the reference
    // ends up holding both whole streams in memory anyway, so the file is read in one go and
    // walked in place. Same bytes resident, one less API.
    void* raw = nullptr;
    size_t size = 0;

    if (!SFile::Load(nullptr, path, &raw, &size, 0, 0, nullptr) || !raw) {
        return false;
    }

    auto data = static_cast<const uint8_t*>(raw);
    bool ok = false;

    // RIFF....AVI
    if (size >= 12 && ReadU32(data) == CC_RIFF && ReadU32(data + 8) == CC_AVI) {
        size_t moviPayload = 0;
        size_t moviEnd = 0;
        const uint8_t* index = nullptr;
        uint32_t indexSize = 0;

        // Which stream each strl describes; strh comes before its strf.
        uint32_t streamType = 0;

        size_t pos = 12;

        while (pos + 8 <= size) {
            uint32_t tag = ReadU32(data + pos);
            uint32_t chunkSize = ReadU32(data + pos + 4);
            size_t body = pos + 8;

            if (body + chunkSize > size) {
                chunkSize = static_cast<uint32_t>(size - body);
            }

            if (tag == CC_LIST && chunkSize >= 4) {
                uint32_t listType = ReadU32(data + body);

                if (listType == CC_movi) {
                    // The index is relative to the movi chunk's fourcc, which sits four bytes
                    // before this payload -- which is why the reference adds 4 to every entry.
                    moviPayload = body + 4;
                    moviEnd = body + chunkSize;
                    pos = body + chunkSize + (chunkSize & 1);
                    continue;
                }

                if (listType == CC_hdrl || listType == CC_strl) {
                    // Descend: the stream headers are nested inside these.
                    pos = body + 4;
                    continue;
                }

                pos = body + chunkSize + (chunkSize & 1);
                continue;
            }

            if (tag == CC_strh && chunkSize >= 32) {
                streamType = ReadU32(data + body);

                if (streamType == CC_vids) {
                    uint32_t scale = ReadU32(data + body + 20);
                    uint32_t rate = ReadU32(data + body + 24);
                    movie.frameCount = ReadU32(data + body + 32);

                    if (scale) {
                        movie.frameRate = static_cast<float>(rate) / static_cast<float>(scale);
                    }
                }
            } else if (tag == CC_strf && streamType == CC_vids && chunkSize >= 40) {
                // BITMAPINFOHEADER. Height is signed and may be negative for a top-down image.
                int32_t width = static_cast<int32_t>(ReadU32(data + body + 4));
                int32_t height = static_cast<int32_t>(ReadU32(data + body + 8));

                movie.width = width;
                movie.height = height < 0 ? -height : height;
                movie.videoCodec = ReadU32(data + body + 16);
            } else if (tag == CC_strf && streamType == CC_auds && chunkSize >= 16) {
                // WAVEFORMATEX
                movie.audioFormat = ReadU16(data + body);
                movie.audioChannels = ReadU16(data + body + 2);
                movie.audioSampleRate = ReadU32(data + body + 4);
            } else if (tag == CC_idx1) {
                index = data + body;
                indexSize = chunkSize;
            }

            pos = body + chunkSize + (chunkSize & 1);
        }

        // Walk the index twice the way the reference does: once to size the two buffers, once to
        // fill them. A chunk id is "NNdc"/"NNdb" for video and "NNwb" for audio, and the reference
        // tells them apart on the third character alone.
        if (index && indexSize >= 16 && moviPayload && movie.width > 0) {
            uint32_t entries = indexSize / 16;
            uint32_t videoTotal = 0;
            uint32_t audioTotal = 0;
            uint32_t videoCount = 0;

            for (uint32_t i = 0; i < entries; i++) {
                const uint8_t* e = index + i * 16;
                char kind = static_cast<char>(e[2]);
                uint32_t chunkSize = ReadU32(e + 12);

                if (kind == 'd') {
                    videoTotal += chunkSize;
                    videoCount++;
                } else if (kind == 'w') {
                    audioTotal += chunkSize;
                }
            }

            if (videoCount) {
                movie.videoData = static_cast<uint8_t*>(SMemAlloc(videoTotal ? videoTotal : 1, __FILE__, __LINE__, 0));
                movie.videoChunks = static_cast<MovieAviChunk*>(SMemAlloc(sizeof(MovieAviChunk) * videoCount, __FILE__, __LINE__, 0));

                if (audioTotal) {
                    movie.audioData = static_cast<uint8_t*>(SMemAlloc(audioTotal, __FILE__, __LINE__, 0));
                }

                uint32_t videoAt = 0;
                uint32_t audioAt = 0;
                uint32_t chunkAt = 0;

                for (uint32_t i = 0; i < entries; i++) {
                    const uint8_t* e = index + i * 16;
                    char kind = static_cast<char>(e[2]);
                    uint32_t offset = ReadU32(e + 8);
                    uint32_t chunkSize = ReadU32(e + 12);

                    // +4 because the index is relative to the movi chunk's fourcc, which sits
                    // four bytes before this payload, and the data starts eight bytes after that.
                    // The reference adds exactly this (offset + 4 + the movi position).
                    size_t at = moviPayload + offset + 4;

                    if (!chunkSize || at + chunkSize > size || at + chunkSize > moviEnd + 8) {
                        continue;
                    }

                    if (kind == 'd') {
                        memcpy(movie.videoData + videoAt, data + at, chunkSize);
                        movie.videoChunks[chunkAt].offset = videoAt;
                        movie.videoChunks[chunkAt].size = chunkSize;
                        videoAt += chunkSize;
                        chunkAt++;
                    } else if (kind == 'w' && movie.audioData) {
                        memcpy(movie.audioData + audioAt, data + at, chunkSize);
                        audioAt += chunkSize;
                    }
                }

                movie.videoSize = videoAt;
                movie.audioSize = audioAt;
                movie.videoChunkCount = chunkAt;

                // The header's frame count is advisory; the index is what actually plays.
                if (!movie.frameCount || movie.frameCount > chunkAt) {
                    movie.frameCount = chunkAt;
                }

                ok = chunkAt != 0;
            }
        }
    }

    SFile::Unload(raw);

    if (!ok) {
        MovieAviClose(movie);
    }

    return ok;
}
