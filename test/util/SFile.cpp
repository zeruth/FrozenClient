#include "util/SFile.hpp"
#include "catch.hpp"
#include <cstdio>
#include <cstring>
// StormLib's error helpers are renamed to avoid colliding with Storm's (see vendor/stormlib-9.31)
#define SErrGetLastError StormLib_SErrGetLastError
#define SErrSetLastError StormLib_SErrSetLastError
#include <StormLib.h>
#undef SErrGetLastError
#undef SErrSetLastError

namespace {

// Writes a small archive containing the given files, using StormLib's write path
bool CreateTestArchive(const char* path, const char* const* names, const char* const* contents, uint32_t count) {
    remove(path);

    HANDLE archive;

    if (!SFileCreateArchive(path, MPQ_CREATE_ARCHIVE_V2, 16, &archive)) {
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        HANDLE file;
        auto size = static_cast<DWORD>(strlen(contents[i]));

        if (!SFileCreateFile(archive, names[i], 0, size, 0, MPQ_FILE_COMPRESS | MPQ_FILE_REPLACEEXISTING, &file)) {
            SFileCloseArchive(archive);
            return false;
        }

        if (!SFileWriteFile(file, contents[i], size, MPQ_COMPRESSION_ZLIB)) {
            SFileCloseFile(file);
            SFileCloseArchive(archive);
            return false;
        }

        SFileFinishFile(file);
    }

    SFileCloseArchive(archive);

    return true;
}

} // namespace

TEST_CASE("SFile::OpenArchive", "[util]") {
    const char* archivePath = "sfile-test-open.mpq";
    const char* names[] = { "Test\\Hello.txt" };
    const char* contents[] = { "hello, world" };

    REQUIRE(CreateTestArchive(archivePath, names, contents, 1));

    SECTION("opens an existing archive") {
        SArchive* archive = nullptr;

        CHECK(SFile::OpenArchive(archivePath, 0x40, 0xC00, &archive) == 1);
        REQUIRE(archive != nullptr);
        CHECK(archive->m_priority == 0x40);
        CHECK(archive->m_flags == 0xC00);

        SFile::CloseArchive(archive);
    }

    SECTION("fails on a missing archive") {
        SArchive* archive = nullptr;

        CHECK(SFile::OpenArchive("sfile-test-missing.mpq", 0x40, 0xC00, &archive) == 0);
        CHECK(archive == nullptr);
    }

    remove(archivePath);
}

TEST_CASE("SFile::OpenEx", "[util]") {
    const char* archivePath = "sfile-test-openex.mpq";
    const char* names[] = { "Test\\Hello.txt", "Test\\Empty.txt" };
    const char* contents[] = { "hello, world", "" };

    REQUIRE(CreateTestArchive(archivePath, names, contents, 2));

    SArchive* archive = nullptr;
    REQUIRE(SFile::OpenArchive(archivePath, 0x40, 0xC00, &archive) == 1);

    SECTION("opens a file from a named archive") {
        SFile* file = nullptr;

        REQUIRE(SFile::OpenEx(archive, "Test\\Hello.txt", 0, &file) == 1);
        REQUIRE(file != nullptr);
        CHECK(file->m_archive == archive);
        CHECK(SFile::GetFileSize(file, nullptr) == 12);

        SFile::Close(file);
    }

    SECTION("opens a file by searching open archives") {
        SFile* file = nullptr;

        REQUIRE(SFile::OpenEx(nullptr, "Test\\Hello.txt", 0, &file) == 1);
        CHECK(file->m_archive == archive);

        SFile::Close(file);
    }

    SECTION("accepts forward slashes") {
        SFile* file = nullptr;

        REQUIRE(SFile::OpenEx(nullptr, "Test/Hello.txt", 0, &file) == 1);

        SFile::Close(file);
    }

    SECTION("is case-insensitive") {
        SFile* file = nullptr;

        REQUIRE(SFile::OpenEx(nullptr, "test\\HELLO.TXT", 0, &file) == 1);

        SFile::Close(file);
    }

    SECTION("fails on a missing file") {
        SFile* file = nullptr;

        CHECK(SFile::OpenEx(nullptr, "Test\\Nope.txt", 0, &file) == 0);
        CHECK(file == nullptr);
    }

    SECTION("reads file contents") {
        SFile* file = nullptr;
        REQUIRE(SFile::OpenEx(nullptr, "Test\\Hello.txt", 0, &file) == 1);

        char buffer[32] = {};
        size_t bytesRead = 0;

        CHECK(SFile::Read(file, buffer, 5, &bytesRead, nullptr, nullptr) == 1);
        CHECK(bytesRead == 5);
        CHECK(strncmp(buffer, "hello", 5) == 0);

        CHECK(SFile::Read(file, buffer, sizeof(buffer), &bytesRead, nullptr, nullptr) == 1);
        CHECK(bytesRead == 7);
        CHECK(strncmp(buffer, ", world", 7) == 0);

        SFile::Close(file);
    }

    SECTION("reports the size of an empty file") {
        SFile* file = nullptr;
        REQUIRE(SFile::OpenEx(nullptr, "Test\\Empty.txt", 0, &file) == 1);
        CHECK(SFile::GetFileSize(file, nullptr) == 0);

        SFile::Close(file);
    }

    SFile::CloseArchive(archive);
    remove(archivePath);
}

TEST_CASE("SFile::Load", "[util]") {
    const char* archivePath = "sfile-test-load.mpq";
    const char* names[] = { "Test\\Hello.txt" };
    const char* contents[] = { "hello, world" };

    REQUIRE(CreateTestArchive(archivePath, names, contents, 1));

    SArchive* archive = nullptr;
    REQUIRE(SFile::OpenArchive(archivePath, 0x40, 0xC00, &archive) == 1);

    SECTION("loads a whole file") {
        void* buffer = nullptr;
        size_t bytes = 0;

        REQUIRE(SFile::Load(nullptr, "Test\\Hello.txt", &buffer, &bytes, 0, 0, nullptr) == 1);
        REQUIRE(buffer != nullptr);
        CHECK(bytes == 12);
        CHECK(memcmp(buffer, "hello, world", 12) == 0);

        SFile::Unload(buffer);
    }

    SECTION("zero-fills extra bytes") {
        void* buffer = nullptr;
        size_t bytes = 0;

        REQUIRE(SFile::Load(nullptr, "Test\\Hello.txt", &buffer, &bytes, 1, 0, nullptr) == 1);
        CHECK(bytes == 12);
        CHECK(static_cast<char*>(buffer)[12] == '\0');

        SFile::Unload(buffer);
    }

    SECTION("fails on a missing file") {
        void* buffer = nullptr;

        CHECK(SFile::Load(nullptr, "Test\\Nope.txt", &buffer, nullptr, 0, 0, nullptr) == 0);
    }

    SFile::CloseArchive(archive);
    remove(archivePath);
}

TEST_CASE("SFile::FileExists", "[util]") {
    const char* archivePath = "sfile-test-exists.mpq";
    const char* names[] = { "Test\\Hello.txt" };
    const char* contents[] = { "hello, world" };

    REQUIRE(CreateTestArchive(archivePath, names, contents, 1));

    SArchive* archive = nullptr;
    REQUIRE(SFile::OpenArchive(archivePath, 0x40, 0xC00, &archive) == 1);

    CHECK(SFile::FileExists("Test\\Hello.txt") == 1);
    CHECK(SFile::FileExists("Test\\Nope.txt") == 0);

    SFile::CloseArchive(archive);

    CHECK(SFile::FileExists("Test\\Hello.txt") == 0);

    remove(archivePath);
}

TEST_CASE("SFile archive priority", "[util]") {
    const char* lowPath = "sfile-test-low.mpq";
    const char* highPath = "sfile-test-high.mpq";
    const char* names[] = { "Test\\Shared.txt" };
    const char* lowContents[] = { "low" };
    const char* highContents[] = { "high" };

    REQUIRE(CreateTestArchive(lowPath, names, lowContents, 1));
    REQUIRE(CreateTestArchive(highPath, names, highContents, 1));

    SECTION("higher priority archive wins regardless of open order") {
        SArchive* high = nullptr;
        SArchive* low = nullptr;

        REQUIRE(SFile::OpenArchive(highPath, 0x41, 0xC00, &high) == 1);
        REQUIRE(SFile::OpenArchive(lowPath, 0x40, 0xC00, &low) == 1);

        void* buffer = nullptr;
        size_t bytes = 0;

        REQUIRE(SFile::Load(nullptr, "Test\\Shared.txt", &buffer, &bytes, 1, 0, nullptr) == 1);
        CHECK(strcmp(static_cast<char*>(buffer), "high") == 0);
        SFile::Unload(buffer);

        SFile::CloseArchive(high);

        REQUIRE(SFile::Load(nullptr, "Test\\Shared.txt", &buffer, &bytes, 1, 0, nullptr) == 1);
        CHECK(strcmp(static_cast<char*>(buffer), "low") == 0);
        SFile::Unload(buffer);

        SFile::CloseArchive(low);
    }

    SECTION("higher priority archive wins when opened second") {
        SArchive* high = nullptr;
        SArchive* low = nullptr;

        REQUIRE(SFile::OpenArchive(lowPath, 0x40, 0xC00, &low) == 1);
        REQUIRE(SFile::OpenArchive(highPath, 0x41, 0xC00, &high) == 1);

        void* buffer = nullptr;

        REQUIRE(SFile::Load(nullptr, "Test\\Shared.txt", &buffer, nullptr, 1, 0, nullptr) == 1);
        CHECK(strcmp(static_cast<char*>(buffer), "high") == 0);
        SFile::Unload(buffer);

        SFile::CloseArchive(high);
        SFile::CloseArchive(low);
    }

    remove(lowPath);
    remove(highPath);
}

TEST_CASE("SFile local files", "[util]") {
    const char* localPath = "sfile-test-local.txt";

    FILE* stream = fopen(localPath, "wb");
    REQUIRE(stream != nullptr);
    fputs("local", stream);
    fclose(stream);

    SECTION("falls back to local files when no archives are open") {
        void* buffer = nullptr;
        size_t bytes = 0;

        REQUIRE(SFile::Load(nullptr, localPath, &buffer, &bytes, 1, 0, nullptr) == 1);
        CHECK(bytes == 5);
        CHECK(strcmp(static_cast<char*>(buffer), "local") == 0);

        SFile::Unload(buffer);
    }

    SECTION("reads local files when allowed while archives are open") {
        const char* archivePath = "sfile-test-local.mpq";
        const char* names[] = { "Test\\Hello.txt" };
        const char* contents[] = { "hello, world" };

        REQUIRE(CreateTestArchive(archivePath, names, contents, 1));

        SArchive* archive = nullptr;
        REQUIRE(SFile::OpenArchive(archivePath, 0x40, 0xC00, &archive) == 1);

        SFile* file = nullptr;
        CHECK(SFile::OpenEx(nullptr, localPath, 0, &file) == 0);

        REQUIRE(SFile::OpenEx(nullptr, localPath, 1, &file) == 1);
        CHECK(file->m_archive == nullptr);
        CHECK(SFile::GetFileSize(file, nullptr) == 5);
        SFile::Close(file);

        SFile::CloseArchive(archive);
        remove(archivePath);
    }

    remove(localPath);
}
