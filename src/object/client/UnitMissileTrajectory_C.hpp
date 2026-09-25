#ifndef OBJECT_CLIENT_UNIT_MISSILE_TRAJECTORY_C_HPP
#define OBJECT_CLIENT_UNIT_MISSILE_TRAJECTORY_C_HPP

#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>
#include <utility>

// How far along `delta` from `origin`, as a fraction of its length, the segment first meets one of
// `spheres` -- the first in list order that it meets, not the nearest -- or -1 when it meets none
// or is shorter than 1e-5. The list ends at the first sphere whose radius is not positive.
float TrajectorySphereListHit(const C3Vector* origin, const C3Vector* delta, const CAaSphere* spheres);

// Heap sort of `count` entries into descending order of their float `key`. The reference sorts
// 8-byte entries whose key is the second dword.
// ref: FUN_006fc7d0
template <class T>
void TrajectorySortByKeyDescending(T* entries, uint32_t count) {
    if (count < 2) {
        return;
    }

    // One-based, as the reference indexes it.
    auto at = [entries](uint32_t i) -> T& { return entries[i - 1]; };

    auto siftDown = [&](uint32_t node, uint32_t size) {
        do {
            uint32_t child = node * 2;

            if (child < size && at(child + 1).key - at(child).key < 0.0f) {
                child++;
            }

            if (0.0f <= at(child).key - at(node).key) {
                break;
            }

            std::swap(at(node), at(child));
            node = child;
        } while (node * 2 <= size);
    };

    for (uint32_t node = count / 2; node >= 1; node--) {
        siftDown(node, count);
    }

    uint32_t size = count;

    while (true) {
        std::swap(at(1), at(size));
        size--;

        if (size == 1) {
            return;
        }

        siftDown(1, size);
    }
}

#endif
