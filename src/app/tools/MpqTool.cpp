// Developer tool: lists and extracts files from a client's Data directory using the same archive
// load order as the client. Usage:
//
//   MpqTool <client dir> list <pattern>
//   MpqTool <client dir> extract <archive path> <output file>

#include "client/Archive.hpp"
#include "console/CVar.hpp"
#include "console/Types.hpp"
#include "util/SFile.hpp"
#include <storm/String.hpp>
#include <cstdio>
#include <cstring>

#define STORMLIB_NO_AUTO_LINK
#define SErrGetLastError StormLib_SErrGetLastError
#define SErrSetLastError StormLib_SErrSetLastError
#include "util/Filesystem.hpp"
#include <StormLib.h>
#undef SErrGetLastError
#undef SErrSetLastError

int main(int argc, char** argv) {
    if (argc < 4) {
        printf("usage: MpqTool <client dir> list <pattern>\n       MpqTool <client dir> extract <archive path> <output file>\n");
        return 1;
    }

    SFile::SetBasePath(argv[1]);

    // ClientOpenArchives looks for "Data" RELATIVE to the working directory, not under the base
    // path it was just given, so without this the tool only worked when run from inside the client
    // directory -- and reported "could not open the client archives under <the path you passed>",
    // which points away from the actual cause. Change directory rather than teach the archive
    // opener a second path: every other consumer of it is the client itself, already running there.
    if (!OsChangeDirectory(argv[1])) {
        printf("could not enter %s\n", argv[1]);
        return 1;
    }

    // The archive loader reads the locale from its cvar
    CVar::Register("locale", "", 0, "enUS", nullptr, DEFAULT);

    ClientOpenArchives();

    if (!SFile::FirstArchive()) {
        printf("no client archives under %s -- expected a Data directory holding the MPQs\n",
               argv[1]);
        return 1;
    }

    if (!strcmp(argv[2], "list")) {
        // Walk every archive in priority order, reporting each name once
        uint32_t total = 0;

        for (auto archive = SFile::FirstArchive(); archive; archive = archive->m_next) {
            SFILE_FIND_DATA find;
            HANDLE handle = SFileFindFirstFile(archive->m_handle, argv[3], &find, nullptr);

            if (!handle) {
                continue;
            }

            do {
                printf("%s\t%s\t%u\n", archive->m_path, find.cFileName, find.dwFileSize);
                total++;
            } while (SFileFindNextFile(handle, &find));

            SFileFindClose(handle);
        }

        printf("%u entries\n", total);
        return 0;
    }

    if (!strcmp(argv[2], "extract") && argc >= 5) {
        void* data;
        size_t size;

        if (!SFile::Load(nullptr, argv[3], &data, &size, 0, 0, nullptr)) {
            printf("could not open %s\n", argv[3]);
            return 1;
        }

        FILE* out = fopen(argv[4], "wb");

        if (!out) {
            printf("could not create %s\n", argv[4]);
            return 1;
        }

        fwrite(data, 1, size, out);
        fclose(out);

        printf("wrote %zu bytes to %s\n", size, argv[4]);
        return 0;
    }

    printf("unknown command %s\n", argv[2]);
    return 1;
}
