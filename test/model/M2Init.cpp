#include "model/M2Init.hpp"
#include "catch.hpp"
#include <cstring>
#include <vector>

// Loading an .m2 in place (M2Init, the reference's M2Init<T> family): every M2Array's file offset is
// checked against the file size and rewritten into a delta from the array itself, so element 0 of
// an EMPTY array is a wild pointer rather than null -- the bug class CLAUDE.md warns about. These
// build the bytes of a small model by hand.

namespace {

struct ModelFile {
    std::vector<uint8_t> bytes = std::vector<uint8_t>(sizeof(M2Data), 0);

    M2Data& Data() {
        return *reinterpret_cast<M2Data*>(this->bytes.data());
    }

    template<class T>
    void Fill(M2Array<T> M2Data::*member, const T* values, uint32_t count) {
        uint32_t offset = static_cast<uint32_t>(this->bytes.size());
        auto p = reinterpret_cast<const uint8_t*>(values);
        this->bytes.insert(this->bytes.end(), p, p + count * sizeof(T));

        (this->Data().*member).count = count;
        (this->Data().*member).offset = offset;
    }

    int32_t Init() {
        return M2Init(this->bytes.data(), static_cast<uint32_t>(this->bytes.size()), this->Data());
    }
};

} // namespace

TEST_CASE("M2Init patches arrays in place", "[model][m2]") {
    REQUIRE(CM2Model::s_loadingSequence == 0xFFFFFFFF);

    ModelFile file;
    file.Data().MD20 = 0x3032444d;
    file.Data().version = 264;

    const char name[] = "Creature\\Test\\Test.m2";
    file.Fill(&M2Data::name, name, sizeof(name));

    M2Vertex vertices[3] = {};
    for (int32_t i = 0; i < 3; i++) {
        vertices[i].position = { static_cast<float>(i), static_cast<float>(i * 2), static_cast<float>(i * 3) };
        vertices[i].texcoord[0] = { 0.5f * i, 0.25f };
    }

    file.Fill(&M2Data::vertices, vertices, 3);

    const uint16_t lookup[4] = { 3, 1, 0xffff, 7 };
    file.Fill(&M2Data::sequenceIdxHashById, lookup, 4);

    REQUIRE(file.Init() == 1);
    auto& data = file.Data();

    SECTION("an array reads its elements back through the patched delta") {
        REQUIRE(data.vertices.Count() == 3);
        CHECK(data.vertices[2].position.y == 4.0f);
        CHECK(data.vertices[2].position.z == 6.0f);
        CHECK(data.vertices[1].texcoord[0].x == 0.5f);

        CHECK(data.sequenceIdxHashById[2] == 0xffff);
        CHECK(data.sequenceIdxHashById[3] == 7);
        CHECK(strcmp(&data.name[0], name) == 0);
    }

    SECTION("an empty array is patched to a zero delta, not left pointing into the file") {
        CHECK(data.bones.Count() == 0);
        CHECK(data.bones.offset == 0);
        CHECK(data.particles.offset == 0);
    }
}

TEST_CASE("M2Init rejects arrays outside the file", "[model][m2]") {
    SECTION("an offset past the end") {
        ModelFile file;
        file.Data().vertices.count = 1;
        file.Data().vertices.offset = static_cast<uint32_t>(file.bytes.size()) + 1;
        CHECK(file.Init() == 0);
    }

    SECTION("an array that starts inside but runs past the end") {
        ModelFile file;
        M2Vertex vertex = {};
        file.Fill(&M2Data::vertices, &vertex, 1);
        file.Data().vertices.count = 2;
        CHECK(file.Init() == 0);
    }

    SECTION("exactly up to the end is fine") {
        ModelFile file;
        M2Vertex vertex = {};
        file.Fill(&M2Data::vertices, &vertex, 1);
        CHECK(file.Init() == 1);
    }
}
