#include "model/M2Internal.hpp"
#include "model/M2Sort.hpp"
#include "catch.hpp"
#include <algorithm>
#include <vector>

// M2HeapSort (FUN_0083dcf0) orders the scene's draw lists through a comparator over indices;
// M2EnsureComboPair (FUN_0082c970) keeps the specialized-shader pass's sorted list of texture and
// transform pairs.

namespace {

int32_t CompareValues(uint32_t a, uint32_t b, const void* arg) {
    auto values = static_cast<const float*>(arg);

    if (values[a] < values[b]) {
        return -1;
    }

    return values[a] > values[b] ? 1 : 0;
}

} // namespace

TEST_CASE("M2HeapSort", "[model][sort]") {
    SECTION("indices come out ascending by the comparator") {
        const float values[] = { 5.0f, -1.0f, 3.5f, 9.0f, 0.0f, 2.0f, 7.0f, 3.5f, -8.0f };
        std::vector<uint32_t> indices = { 0, 1, 2, 3, 4, 5, 6, 7, 8 };

        M2HeapSort(&CompareValues, indices.data(), static_cast<uint32_t>(indices.size()), values);

        for (size_t i = 1; i < indices.size(); i++) {
            CHECK(values[indices[i - 1]] <= values[indices[i]]);
        }

        // A permutation: nothing lost, nothing duplicated.
        std::vector<uint32_t> sorted = indices;
        std::sort(sorted.begin(), sorted.end());
        for (uint32_t i = 0; i < sorted.size(); i++) {
            CHECK(sorted[i] == i);
        }
    }

    SECTION("zero and one element lists are left alone") {
        uint32_t one = 7;
        M2HeapSort(&CompareValues, &one, 1, nullptr);
        CHECK(one == 7);
        M2HeapSort(&CompareValues, nullptr, 0, nullptr);
    }

    SECTION("a long reversed list sorts") {
        std::vector<float> values(257);
        std::vector<uint32_t> indices(values.size());

        for (uint32_t i = 0; i < values.size(); i++) {
            values[i] = static_cast<float>(values.size() - i);
            indices[i] = i;
        }

        M2HeapSort(&CompareValues, indices.data(), static_cast<uint32_t>(indices.size()), values.data());

        for (size_t i = 1; i < indices.size(); i++) {
            CHECK(values[indices[i - 1]] <= values[indices[i]]);
        }
    }
}

TEST_CASE("M2EnsureComboPair", "[model][shader]") {
    int16_t storage[32] = {};
    M2ComboPairList list = { 0, storage };

    SECTION("pairs are inserted in order and duplicates are ignored") {
        M2EnsureComboPair(0x0302, list, 0);   // (2, 3)
        M2EnsureComboPair(0x0101, list, 0);   // (1, 1)
        M2EnsureComboPair(0x0302, list, 0);   // again
        M2EnsureComboPair(0x0102, list, 0);   // (2, 1)

        REQUIRE(list.count == 6);
        CHECK(storage[0] == 1);
        CHECK(storage[1] == 1);
        CHECK(storage[2] == 2);
        CHECK(storage[3] == 1);
        CHECK(storage[4] == 2);
        CHECK(storage[5] == 3);
    }

    SECTION("the unbias undoes the plus-one shift, and no transform sorts last") {
        M2EnsureComboPair(0x0000, list, 1);   // (-1, -1)
        M2EnsureComboPair(0x0201, list, 1);   // (0, 1)

        REQUIRE(list.count == 4);
        CHECK(storage[0] == 0);
        CHECK(storage[1] == 1);
        CHECK(storage[2] == -1);
        CHECK(storage[3] == -1);
    }

    SECTION("the duplicate search looks at every position, not just pair starts") {
        // (5, 6) followed by (7, 8): the straddling (6, 7) counts as already present.
        storage[0] = 5;
        storage[1] = 6;
        storage[2] = 7;
        storage[3] = 8;
        list.count = 4;

        M2EnsureComboPair(0x0706, list, 0);
        CHECK(list.count == 4);
    }
}
