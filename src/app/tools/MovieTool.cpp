// Decodes frames of a cinematic to .ppm files, so the decoder can be checked by looking at it
// rather than by trusting it.
//
//   MovieTool <client dir> <movie path> <out dir> [frame count] [stride]
//
// e.g. MovieTool "<install>" "Interface\Cinematics\Logo_1024.avi" out 6 40
//
// The client directory is the working directory the archives resolve against; the movies
// themselves are loose on disk under Data/<locale>, which SFile finds the same way.

#include "client/Archive.hpp"
#include "console/CVar.hpp"
#include "console/Types.hpp"
#include "util/SFile.hpp"
#include "ui/simple/MovieAvi.hpp"
#include "ui/simple/MovieDecoder.hpp"
#include "util/Filesystem.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <storm/String.hpp>

namespace {

bool WritePpm(const char* path, const uint8_t* bgra, int32_t width, int32_t height) {
    FILE* f = fopen(path, "wb");

    if (!f) {
        return false;
    }

    fprintf(f, "P6\n%d %d\n255\n", width, height);

    for (int32_t i = 0; i < width * height; i++) {
        // ppm is RGB; the decoder hands back BGRA
        uint8_t rgb[3] = { bgra[i * 4 + 2], bgra[i * 4 + 1], bgra[i * 4 + 0] };
        fwrite(rgb, 1, 3, f);
    }

    fclose(f);

    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 4) {
        printf("usage: MovieTool <client dir> <movie path> <out dir> [count] [stride]\n");
        return 1;
    }

    const char* clientDir = argv[1];
    const char* moviePath = argv[2];
    const char* outDir = argv[3];
    int32_t count = argc > 4 ? atoi(argv[4]) : 1;
    int32_t stride = argc > 5 ? atoi(argv[5]) : 1;

    if (stride < 1) {
        stride = 1;
    }

    if (!OsChangeDirectory(clientDir)) {
        printf("could not change to %s\n", clientDir);
        return 1;
    }

    SFile::SetBasePath(clientDir);

    // The archive loader reads the locale from this cvar, and the locale is also what puts the
    // cinematics on the search path -- they live under Data/<locale>. Without it registered this
    // crashed before printing anything.
    CVar::Register("locale", "", 0, "enUS", nullptr, DEFAULT);

    // Returns nothing; the movies are loose on disk anyway, so a missing archive is not fatal.
    ClientOpenArchives();

    // The cinematics are loose files under Data/<locale>, and that is the last of the four paths
    // SFile searches. The client sets this during startup (Client.cpp); a tool has to do it too or
    // every movie looks missing.
    char localePath[260];
    SStrPrintf(localePath, sizeof(localePath), "Data\\%s", "enUS");
    SFile::SetLocalePath(localePath);

    MovieAvi movie;

    if (!MovieAviOpen(moviePath, movie)) {
        printf("could not demux %s\n", moviePath);
        return 1;
    }

    printf("%s\n", moviePath);
    printf("  %dx%d  codec %s  %.2f fps  %u frames\n",
           movie.width, movie.height, MovieAviFourCC(movie.videoCodec),
           movie.frameRate, movie.videoChunkCount);
    printf("  video %u bytes, audio %u bytes (format 0x%04x, %u ch, %u Hz)\n",
           movie.videoSize, movie.audioSize, movie.audioFormat,
           movie.audioChannels, movie.audioSampleRate);

    auto decoder = MovieDecoderCreate(movie);

    if (!decoder) {
        printf("  no decoder for this stream\n");
        MovieAviClose(movie);
        return 1;
    }

    printf("  decoder reports %dx%d\n", MovieDecoderWidth(decoder), MovieDecoderHeight(decoder));

    if (!OsDirectoryExists(outDir)) {
        OsCreateDirectory(outDir, 0);
    }

    int32_t written = 0;
    int32_t failed = 0;

    // Decoded in order: every frame but a keyframe is coded against the one before it.
    for (uint32_t i = 0; i < movie.videoChunkCount && written < count; i++) {
        if (!MovieDecoderFrame(decoder, movie, i)) {
            failed++;
            continue;
        }

        if (i % stride) {
            continue;
        }

        char path[512];
        snprintf(path, sizeof(path), "%s/frame%05u.ppm", outDir, i);

        if (WritePpm(path, MovieDecoderPixels(decoder), MovieDecoderWidth(decoder), MovieDecoderHeight(decoder))) {
            printf("  wrote %s\n", path);
            written++;
        }
    }

    printf("  %d frames written, %d failed to decode\n", written, failed);

    MovieDecoderDestroy(decoder);
    MovieAviClose(movie);

    return 0;
}
