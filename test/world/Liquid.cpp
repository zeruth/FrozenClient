#include "world/map/CMapLiquidData.hpp"
#include "world/map/LiquidVertexData.hpp"
#include "catch.hpp"
#include <cstring>
#include <vector>

// MH2O and MCLQ as the reference reads them (Liquid::CVertexData, 0x008a3130..0x008a3290, and the
// CMapLiquidData accessors 0x008a3050..0x008a3110). Each test builds the bytes a tile would carry.

namespace {

// A tile body: the 256 chunk entries, then whatever the test appends.
struct Mh2oBody {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(256 * sizeof(SMLiquidChunk), 0);

    SMLiquidChunk& Chunk(uint32_t x, uint32_t y) {
        return reinterpret_cast<SMLiquidChunk*>(this->bytes.data())[y * 16 + x];
    }

    uint32_t Append(const void* data, size_t size) {
        uint32_t offset = static_cast<uint32_t>(this->bytes.size());
        auto p = static_cast<const uint8_t*>(data);
        this->bytes.insert(this->bytes.end(), p, p + size);
        return offset;
    }

    template<class T>
    uint32_t Append(const T& value) {
        return this->Append(&value, sizeof(T));
    }
};

void PutFloat(uint8_t* p, float value) {
    memcpy(p, &value, sizeof(value));
}

} // namespace

TEST_CASE("CMapLiquidData chunk and layer lookup", "[world][liquid]") {
    Mh2oBody body;

    SMLiquidInstance layers[2] = {};
    layers[0].liquidType = 2;
    layers[1].liquidType = 5;

    uint32_t layerOffset = body.Append(layers, sizeof(layers));
    body.Chunk(3, 7).instanceOffset = layerOffset;
    body.Chunk(3, 7).layerCount = 2;

    CMapLiquidData data;
    CHECK_FALSE(data.HasLiquid());

    data.SetBody(body.bytes.data());
    CHECK(data.HasLiquid());

    SECTION("chunks are row-major, sixteen to a row") {
        auto chunk = data.Chunk(3, 7);
        CHECK(reinterpret_cast<const uint8_t*>(chunk) == body.bytes.data() + (7 * 16 + 3) * sizeof(SMLiquidChunk));
        CHECK(chunk->layerCount == 2);
    }

    SECTION("a chunk's layers run from its instance offset") {
        auto chunk = data.Chunk(3, 7);
        CHECK(data.Layer(chunk, 0)->liquidType == 2);
        CHECK(data.Layer(chunk, 1)->liquidType == 5);
    }
}

TEST_CASE("CMapLiquidData masks", "[world][liquid]") {
    Mh2oBody body;
    CMapLiquidData data;

    SECTION("a chunk with no layers is wet nowhere") {
        data.SetBody(body.bytes.data());
        auto chunk = data.Chunk(0, 0);

        CHECK(data.FishableMask(chunk) == CMapLiquidData::s_maskNone);
        CHECK(data.DeepMask(chunk) == CMapLiquidData::s_maskNone);
    }

    SECTION("layers without an attribute block are ordinary everywhere") {
        body.Chunk(0, 0).layerCount = 1;
        data.SetBody(body.bytes.data());
        auto chunk = data.Chunk(0, 0);

        CHECK(data.FishableMask(chunk) == CMapLiquidData::s_maskAll);
        CHECK(data.DeepMask(chunk) == CMapLiquidData::s_maskAll);
    }

    SECTION("the attribute block is the fishable mask, then the deep mask") {
        uint8_t attributes[16];
        for (int32_t i = 0; i < 16; i++) {
            attributes[i] = static_cast<uint8_t>(0xa0 + i);
        }

        uint32_t offset = body.Append(attributes, sizeof(attributes));
        body.Chunk(1, 0).layerCount = 1;
        body.Chunk(1, 0).attributeOffset = offset;
        data.SetBody(body.bytes.data());
        auto chunk = data.Chunk(1, 0);

        CHECK(data.FishableMask(chunk)[0] == 0xa0);
        CHECK(data.DeepMask(chunk)[0] == 0xa8);
    }

    SECTION("a layer without an exists mask is wet everywhere") {
        SMLiquidInstance layer = {};
        CHECK(data.ExistsMask(&layer) == CMapLiquidData::s_maskAll);

        uint8_t exists[8] = { 0x0f };
        layer.existsOffset = body.Append(exists, sizeof(exists));
        data.SetBody(body.bytes.data());
        CHECK(data.ExistsMask(&layer)[0] == 0x0f);
    }
}

TEST_CASE("Liquid::CVertexDataMH2O", "[world][liquid]") {
    Mh2oBody body;
    SMLiquidInstance layer = {};
    layer.tileWidth = 2;
    layer.tileHeight = 1;   // 3 x 2 = 6 vertices

    const uint32_t vertices = 6;
    std::vector<uint8_t> block(vertices * 4 + vertices * 4, 0);

    for (uint32_t i = 0; i < vertices; i++) {
        PutFloat(&block[i * 4], 100.0f + static_cast<float>(i));
    }

    layer.vertexOffset = body.Append(block.data(), block.size());

    CMapLiquidData data;
    data.SetBody(body.bytes.data());

    SECTION("the vertex count is one more than the tiles each way") {
        Liquid::CVertexDataMH2O v(&data, &layer);
        CHECK(v.m_vertexCount == vertices);
    }

    SECTION("material 0: heights, then a depth byte per vertex") {
        for (uint32_t i = 0; i < vertices; i++) {
            body.bytes[layer.vertexOffset + vertices * 4 + i] = static_cast<uint8_t>(0x10 * i);
        }

        layer.material = 0;
        Liquid::CVertexDataMH2O v(&data, &layer);

        CHECK(v.GetHeight(0) == 100.0f);
        CHECK(v.GetHeight(5) == 105.0f);
        CHECK(v.GetDepth(3) == 0x30);
        CHECK(v.GetCoord(3) == Liquid::s_noCoord);
    }

    SECTION("material 1: heights, then a coordinate pair per vertex stepped as four bytes") {
        uint8_t* coords = &body.bytes[layer.vertexOffset + vertices * 4];
        coords[2 * 4] = 0x7f;
        coords[2 * 4 + 1] = 0x81;

        layer.material = 1;
        Liquid::CVertexDataMH2O v(&data, &layer);

        CHECK(v.GetHeight(2) == 102.0f);
        CHECK(v.GetDepth(2) == 0);
        CHECK(v.GetCoord(2)[0] == 0x7f);
        CHECK(v.GetCoord(2)[1] == 0x81);
    }

    SECTION("material 2: no heights, depth bytes alone") {
        body.bytes[layer.vertexOffset + 4] = 0x44;

        layer.material = 2;
        Liquid::CVertexDataMH2O v(&data, &layer);

        CHECK(v.GetHeight(4) == 0.0f);
        CHECK(v.GetDepth(4) == 0x44);
    }

    SECTION("material 2 with no vertex data at all is fully deep") {
        SMLiquidInstance ocean = layer;
        ocean.material = 2;
        ocean.vertexOffset = 0;

        Liquid::CVertexDataMH2O v(&data, &ocean);
        CHECK(v.GetDepth(0) == 0xff);
    }
}

TEST_CASE("Liquid::CVertexDataMCLQ", "[world][liquid]") {
    // Eight bytes a vertex: the layout's first four, then the height.
    uint8_t verts[3 * 8] = {};

    for (uint32_t i = 0; i < 3; i++) {
        verts[i * 8] = static_cast<uint8_t>(0x20 + i);
        verts[i * 8 + 1] = static_cast<uint8_t>(0x30 + i);
        PutFloat(&verts[i * 8 + 4], 7.5f + static_cast<float>(i));
    }

    SECTION("layout 0 (water) carries a depth and a height") {
        Liquid::CVertexDataMCLQ v(verts, 0);
        CHECK(v.GetHeight(1) == 8.5f);
        CHECK(v.GetDepth(1) == 0x21);
        CHECK(v.GetCoord(1) == Liquid::s_noCoord);
    }

    SECTION("layout 1 (magma and slime) replaces the depth with a coordinate pair") {
        Liquid::CVertexDataMCLQ v(verts, 1);
        CHECK(v.GetHeight(2) == 9.5f);
        CHECK(v.GetDepth(2) == 0);
        CHECK(v.GetCoord(2) == verts + 16);
        CHECK(v.GetCoord(2)[1] == 0x32);
    }

    SECTION("layouts 2 and 3 store no height") {
        Liquid::CVertexDataMCLQ two(verts, 2);
        Liquid::CVertexDataMCLQ three(verts, 3);

        CHECK(two.GetHeight(0) == 0.0f);
        CHECK(three.GetHeight(0) == 0.0f);
        CHECK(two.GetDepth(0) == 0x20);
        CHECK(three.GetDepth(0) == 0);
    }
}
