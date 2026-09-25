#include "world/CWFrustum.hpp"

#include <storm/Memory.hpp>
#include <tempest/Vector.hpp>

// The records handed out to visible groups, returned at the end of the map object pass. The
// reference keeps this as a WorldScene.cpp file static at 0x00adf6a8.
static STORM_EXPLICIT_LIST(CWFrustum, link) s_freeFrustums;

// ref: FUN_007983b0
CWFrustum* CWFrustum::Alloc() {
    auto frustum = s_freeFrustums.Head();

    if (!frustum) {
        frustum = STORM_NEW(CWFrustum);

        if (!frustum) {
            return nullptr;
        }

        return frustum;
    }

    s_freeFrustums.UnlinkNode(frustum);

    return frustum;
}

void CWFrustum::Free(CWFrustum* frustum) {
    if (!frustum) {
        return;
    }

    s_freeFrustums.LinkToTail(frustum);
}

// ref: FUN_00983f40
void CWFrustum::Transform(const C44Matrix& matrix) {
    for (int32_t i = 0; i < 8; i++) {
        C3Vector out;
        TransformPointInPlace(out, this->corners[i], matrix);
    }

    this->ComputePlanes();

    // The two points just past the corners, which the occlusion code keeps there.
    C3Vector out;
    TransformPointInPlace(out, *reinterpret_cast<C3Vector*>(&this->unknownC0[0]), matrix);
    TransformPointInPlace(out, *reinterpret_cast<C3Vector*>(&this->unknownC0[3]), matrix);
}
