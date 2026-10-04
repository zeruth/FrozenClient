#include "world/map/CMapChunk.hpp"
#include "catch.hpp"
#include <cstring>
#include <vector>

// The reference's chunk geometry (Map.cpp): a chunk is 33.333 yards on a side, eight cells of
// 4.1666665 (DAT_00a3fda8), and its 145 vertices alternate nine-vertex outer rows with eight-vertex
// inner rows offset by half a cell (FUN_007c3c60).

namespace {

const float CELL = 4.166666507720947f;
const float HALF = 2.0833332538604736f;

// The index of outer vertex (row, col) and inner vertex (row, col) in the 9/8 interleave.
uint32_t Outer(uint32_t row, uint32_t col) {
    return row * 17 + col;
}

uint32_t Inner(uint32_t row, uint32_t col) {
    return row * 17 + 9 + col;
}

struct ChunkFixture {
    CMapChunk chunk;
    SMChunk header = {};

    ChunkFixture() {
        this->chunk.m_header = &this->header;
    }
};

} // namespace

TEST_CASE("CMapChunk::BuildVertexTable", "[world][terrain]") {
    CMapChunk::BuildVertexTable();
    auto& v = CMapChunk::s_vertexTable;

    SECTION("the first outer row starts at the chunk origin and runs negative in y") {
        CHECK(v[Outer(0, 0)][0] == 0.0f);
        CHECK(v[Outer(0, 0)][1] == 0.0f);
        CHECK(v[Outer(0, 1)][1] == Approx(-CELL));
        CHECK(v[Outer(0, 8)][1] == Approx(-8.0f * CELL));
    }

    SECTION("inner vertices sit half a cell in on both axes") {
        CHECK(v[Inner(0, 0)][0] == Approx(-HALF));
        CHECK(v[Inner(0, 0)][1] == Approx(-HALF));
        CHECK(v[Inner(3, 5)][0] == Approx(-3.0f * CELL - HALF));
        CHECK(v[Inner(3, 5)][1] == Approx(-5.0f * CELL - HALF));
    }

    SECTION("the last vertex is the far corner, one chunk away on both axes") {
        CHECK(v[144][0] == Approx(-8.0f * CELL));
        CHECK(v[144][1] == Approx(-8.0f * CELL));
        CHECK(v[144][0] == Approx(-33.333332f));
    }

    SECTION("every outer row shares its x and every outer column its y") {
        for (uint32_t row = 0; row < 9; row++) {
            for (uint32_t col = 0; col < 9; col++) {
                CHECK(v[Outer(row, col)][0] == Approx(-static_cast<float>(row) * CELL).margin(1e-5));
                CHECK(v[Outer(row, col)][1] == Approx(-static_cast<float>(col) * CELL).margin(1e-5));
            }
        }
    }

    SECTION("the cell scale the collision code divides by") {
        CMapChunk::s_invCellSize = -1.0f / v[1][1];
        CHECK(CMapChunk::s_invCellSize == Approx(1.0f / CELL));
    }
}

TEST_CASE("CMapChunk::BuildIndices", "[world][terrain]") {
    ChunkFixture f;
    std::vector<uint16_t> indices(64 * 12 + 12, 0xdead);

    SECTION("a whole chunk is 64 cells of four triangles fanned around the inner vertex") {
        int16_t count = f.chunk.BuildIndices(indices.data(), 0);
        REQUIRE(count == 64 * 12);

        // Cell (0, 0): centre 9, corners 0, 1, 17, 18, in the reference's order.
        const uint16_t first[12] = { 9, 0, 17, 9, 1, 0, 9, 18, 1, 9, 17, 18 };

        for (int32_t i = 0; i < 12; i++) {
            CHECK(indices[i] == first[i]);
        }

        // Nothing is written past the count.
        CHECK(indices[64 * 12] == 0xdead);
    }

    SECTION("every triangle uses its cell's inner vertex as the fan centre") {
        f.chunk.BuildIndices(indices.data(), 0);

        uint32_t cell = 0;

        for (uint32_t row = 0; row < 8; row++) {
            for (uint32_t col = 0; col < 8; col++, cell++) {
                for (uint32_t tri = 0; tri < 4; tri++) {
                    CHECK(indices[cell * 12 + tri * 3] == Inner(row, col));
                }
            }
        }
    }

    SECTION("the base vertex offsets every index") {
        std::vector<uint16_t> shifted(64 * 12);
        f.chunk.BuildIndices(indices.data(), 0);
        f.chunk.BuildIndices(shifted.data(), 1000);

        for (size_t i = 0; i < shifted.size(); i++) {
            CHECK(shifted[i] == indices[i] + 1000);
        }
    }

    SECTION("a hole bit removes the 2x2 block of cells it covers") {
        // Bit 0x0001 is the first 2x2 block; bit 0x0020 is block (1, 1).
        f.header.holes = 0x0001;
        CHECK(f.chunk.BuildIndices(indices.data(), 0) == 60 * 12);

        // The first surviving cell is (0, 2): its fan centre is inner vertex (0, 2).
        CHECK(indices[0] == Inner(0, 2));

        f.header.holes = 0x0001 | 0x0020;
        CHECK(f.chunk.BuildIndices(indices.data(), 0) == 56 * 12);

        f.header.holes = 0xffff;
        CHECK(f.chunk.BuildIndices(indices.data(), 0) == 0);
    }
}

TEST_CASE("CMapChunk::DecompressAlphaRow", "[world][terrain]") {
    uint8_t out[64];

    SECTION("a run byte (high bit set) repeats the next byte") {
        const uint8_t src[] = { 0x80 | 64, 0x7f };
        memset(out, 0, sizeof(out));

        auto next = CMapChunk::DecompressAlphaRow(out, 64, src);

        CHECK(next == src + 2);

        for (auto texel : out) {
            CHECK(texel == 0x7f);
        }
    }

    SECTION("a copy byte (high bit clear) copies that many literals") {
        const uint8_t src[] = { 4, 10, 20, 30, 40, 0x80 | 60, 0xff };
        auto next = CMapChunk::DecompressAlphaRow(out, 64, src);

        CHECK(next == src + sizeof(src));
        CHECK(out[0] == 10);
        CHECK(out[1] == 20);
        CHECK(out[2] == 30);
        CHECK(out[3] == 40);
        CHECK(out[4] == 0xff);
        CHECK(out[63] == 0xff);
    }

    SECTION("consecutive rows read back to back from one stream") {
        const uint8_t src[] = { 0x80 | 64, 1, 0x80 | 64, 2 };
        auto second = CMapChunk::DecompressAlphaRow(out, 64, src);
        CHECK(out[63] == 1);

        CMapChunk::DecompressAlphaRow(out, 64, second);
        CHECK(out[0] == 2);
        CHECK(out[63] == 2);
    }

    SECTION("an empty run consumes its two bytes and writes nothing") {
        const uint8_t src[] = { 0x80, 0x55, 0x80 | 64, 9 };
        memset(out, 0, sizeof(out));

        auto next = CMapChunk::DecompressAlphaRow(out, 64, src);

        CHECK(next == src + 4);
        CHECK(out[0] == 9);
    }
}

TEST_CASE("CMapChunk::UnpackShadowBits", "[world][terrain]") {
    // One bit per texel, low bit first: a set bit is in shadow and reads as 0xffff.
    uint8_t shadow[8 * 8 / 8] = {};
    shadow[0] = 0x05;   // texels 0 and 2
    shadow[7] = 0x80;   // texel 63

    uint16_t out[64];
    CMapChunk::UnpackShadowBits(out, 8, shadow);

    CHECK(out[0] == 0xffff);
    CHECK(out[1] == 0x0000);
    CHECK(out[2] == 0xffff);
    CHECK(out[3] == 0x0000);
    CHECK(out[62] == 0x0000);
    CHECK(out[63] == 0xffff);
}

TEST_CASE("CMapChunk::PackNibbleRow", "[world][terrain]") {
    // Three classic 4-bit layers and the shadow into one 4444 texel per column: the shadow's lit
    // byte in A (0xf when lit, 0 in shadow), then layers 1, 2, 3 in R, G, B.
    uint8_t layer1[4] = { 0x21, 0x43, 0x65, 0x87 };
    uint8_t layer2[4] = { 0xff, 0x00, 0xf0, 0x0f };
    uint8_t layer3[4] = { 0x10, 0x32, 0x54, 0x76 };
    const uint8_t* rows[4] = { nullptr, layer1, layer2, layer3 };

    uint8_t shadowRow[1] = { 0x02 };   // texel 1 is in shadow

    uint16_t out[8];
    memset(out, 0, sizeof(out));
    CMapChunk::PackNibbleRow(out, 8, rows, shadowRow);

    // Texel 0: lit, layer nibbles 1, f, 0.
    CHECK(out[0] == 0xf1f0);
    // Texel 1: shadowed, layer nibbles 2, f, 1.
    CHECK(out[1] == 0x02f1);
    // Texel 2: lit, layer nibbles 3, 0, 2.
    CHECK(out[2] == 0xf302);

    SECTION("the last texel repeats the one before it") {
        CHECK(out[7] == out[6]);
    }
}
