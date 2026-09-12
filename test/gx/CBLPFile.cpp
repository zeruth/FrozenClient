#include "gx/blp/CBLPFile.hpp"
#include "gx/Types.hpp"
#include "catch.hpp"
#include <cstring>
#include <vector>

namespace {

// Builds a 2x2 palettized BLP2 image in memory with the given alpha depth and alpha plane
std::vector<unsigned char> MakePalettizedBlp(char alphaSize, const unsigned char* alphaPlane, size_t alphaBytes) {
    BLPHeader header;
    memset(&header, 0, sizeof(header));

    header.magic = 0x32504C42; // BLP2
    header.formatVersion = 1;
    header.colorEncoding = COLOR_PAL;
    header.alphaSize = alphaSize;
    header.preferredFormat = PIXEL_UNSPECIFIED;
    header.hasMips = 0;
    header.width = 2;
    header.height = 2;
    header.mipOffsets[0] = sizeof(header);
    header.mipSizes[0] = static_cast<uint32_t>(4 + alphaBytes);

    // Palette entries are stored as B, G, R, pad
    const unsigned char colors[4][3] = {
        { 0x10, 0x20, 0x30 },
        { 0x40, 0x50, 0x60 },
        { 0x70, 0x80, 0x90 },
        { 0xA0, 0xB0, 0xC0 },
    };

    for (int i = 0; i < 4; i++) {
        header.extended.palette[i].b = static_cast<char>(colors[i][0]);
        header.extended.palette[i].g = static_cast<char>(colors[i][1]);
        header.extended.palette[i].r = static_cast<char>(colors[i][2]);
        header.extended.palette[i].pad = 0;
    }

    std::vector<unsigned char> file(sizeof(header) + 4 + alphaBytes);
    memcpy(file.data(), &header, sizeof(header));

    unsigned char* indices = file.data() + sizeof(header);
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    indices[3] = 3;

    if (alphaBytes) {
        memcpy(indices + 4, alphaPlane, alphaBytes);
    }

    return file;
}

} // namespace

TEST_CASE("CBLPFile palettized decode", "[gx]") {
    SECTION("expands palette entries to ARGB8888 with opaque alpha") {
        auto file = MakePalettizedBlp(0, nullptr, 0);

        CBLPFile image;
        REQUIRE(image.Source(file.data()) == 1);

        unsigned char out[16];
        uint32_t stride = 0;
        REQUIRE(image.Lock2("test", PIXEL_ARGB8888, 0, out, stride) == 1);

        // Texel 1 -> palette[1] as B, G, R, A
        CHECK(out[4] == 0x40);
        CHECK(out[5] == 0x50);
        CHECK(out[6] == 0x60);
        CHECK(out[7] == 0xFF);

        CHECK(out[12] == 0xA0);
        CHECK(out[15] == 0xFF);
    }

    SECTION("reads a 1 bit alpha plane") {
        unsigned char alpha[1] = { 0x05 }; // texels 0 and 2 opaque
        auto file = MakePalettizedBlp(1, alpha, sizeof(alpha));

        CBLPFile image;
        REQUIRE(image.Source(file.data()) == 1);

        unsigned char out[16];
        uint32_t stride = 0;
        REQUIRE(image.Lock2("test", PIXEL_ARGB8888, 0, out, stride) == 1);

        CHECK(out[3] == 0xFF);
        CHECK(out[7] == 0x00);
        CHECK(out[11] == 0xFF);
        CHECK(out[15] == 0x00);
    }

    SECTION("reads a 4 bit alpha plane") {
        unsigned char alpha[2] = { 0xF0, 0x08 }; // texel 0 = 0, texel 1 = 15, texel 2 = 8, texel 3 = 0
        auto file = MakePalettizedBlp(4, alpha, sizeof(alpha));

        CBLPFile image;
        REQUIRE(image.Source(file.data()) == 1);

        unsigned char out[16];
        uint32_t stride = 0;
        REQUIRE(image.Lock2("test", PIXEL_ARGB8888, 0, out, stride) == 1);

        CHECK(out[3] == 0x00);
        CHECK(out[7] == 0xFF);
        CHECK(out[11] == 0x88);
        CHECK(out[15] == 0x00);
    }

    SECTION("reads an 8 bit alpha plane") {
        unsigned char alpha[4] = { 0x00, 0x40, 0x80, 0xFF };
        auto file = MakePalettizedBlp(8, alpha, sizeof(alpha));

        CBLPFile image;
        REQUIRE(image.Source(file.data()) == 1);

        unsigned char out[16];
        uint32_t stride = 0;
        REQUIRE(image.Lock2("test", PIXEL_ARGB8888, 0, out, stride) == 1);

        CHECK(out[3] == 0x00);
        CHECK(out[7] == 0x40);
        CHECK(out[11] == 0x80);
        CHECK(out[15] == 0xFF);
    }

    SECTION("rejects 16 bit destination formats for now") {
        auto file = MakePalettizedBlp(0, nullptr, 0);

        CBLPFile image;
        REQUIRE(image.Source(file.data()) == 1);

        unsigned char out[16];
        uint32_t stride = 0;
        CHECK(image.Lock2("test", PIXEL_RGB565, 0, out, stride) == 0);
    }
}
