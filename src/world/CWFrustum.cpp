#include "world/CWFrustum.hpp"
#include <cstring>

#include <storm/Memory.hpp>
#include <tempest/Vector.hpp>

// The records handed out to visible groups, returned at the end of the map object pass. The
// reference keeps this as a WorldScene.cpp file static at 0x00adf6a8.
static STORM_EXPLICIT_LIST(CWFrustum, link) s_freeFrustums;

// ref: FUN_00601650
CWFrustum::CWFrustum() {
    // A (0, 0, 1) normal with distance 0 -- the reference writes 1.0 into the third float of each
    // plane and leaves the rest zero, which is this.
    for (int32_t i = 0; i < 6; i++) {
        this->planes[i].n.x = 0.0f;
        this->planes[i].n.y = 0.0f;
        this->planes[i].n.z = 1.0f;
        this->planes[i].d = 0.0f;
    }

    for (int32_t i = 0; i < 8; i++) {
        this->corners[i].x = 0.0f;
        this->corners[i].y = 0.0f;
        this->corners[i].z = 0.0f;
    }

    memset(this->unknownC0, 0, sizeof(this->unknownC0));
}

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

// ref: FUN_007ba960
// Flip every plane, turning the frustum inside out: what was in front of each face is now behind
// it, so a test that reported "inside" reports "outside" and the other way round.
//
// The reference negates all twenty-four floats straight through, which is the six planes' normals
// AND their distances -- both halves, or the plane would move rather than turn.
//
// Its callers are the SHADOW MAP's frustum setup, and they all run it in the same place: build the
// corners, SetCorners to derive the planes from them, negate, then carry on. That is the tell for
// what it is for -- those frusta come from GxuXformCreateOrtho, whose handedness is the opposite
// of the one SetCorners assumes, so the planes come out facing the wrong way and this turns them
// back rather than reworking the derivation.
void CWFrustum::NegatePlanes() {
    for (int32_t i = 0; i < 6; i++) {
        this->planes[i].n.x = -this->planes[i].n.x;
        this->planes[i].n.y = -this->planes[i].n.y;
        this->planes[i].n.z = -this->planes[i].n.z;
        this->planes[i].d = -this->planes[i].d;
    }
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
