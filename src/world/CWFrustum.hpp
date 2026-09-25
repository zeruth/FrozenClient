#ifndef WORLD_C_W_FRUSTUM_HPP
#define WORLD_C_W_FRUSTUM_HPP

#include <cstdint>
#include <storm/List.hpp>
#include <tempest/Plane.hpp>
#include <tempest/Sphere.hpp>
#include <tempest/Vector.hpp>

// A view volume: six planes and the eight corners they were built from, near face first
// (corners 0-3), far face after (4-7), each face going (-x,-y) (-x,+y) (+x,+y) (+x,-y) in
// clip space.
//
// The scene keeps a stack of these, one per level of portal recursion, and the traversal
// tests against whichever the current depth names. A group the traversal reaches also gets a
// copy hung off it, so the render pass can clip that group to exactly what the doorway it
// came through left of the view.
//
// The name is the reference's own, recovered from the RTTI string on the pooled record.
class CWFrustum {
    public:
        // Static functions
        // Take a record off the free list, or make one. The list itself is a file static of
        // CWFrustum.cpp, as it is of the reference's WorldScene.cpp (0x00adf6a8 and
        // friends). ref: FUN_007983b0
        static CWFrustum* Alloc();
        // Put one back.
        static void Free(CWFrustum* frustum);

        // Member variables
        C4Plane planes[6];                  // +0x00: side, side, side, side, far, near
        C3Vector corners[8];                // +0x60
        // +0xc0..+0xf4. The reference copies 0xf4 bytes when it hands a frustum to a
        // group, which is what fixes the data at thirteen words here; the 0xfc stride
        // of its own stack array is this plus the link.
        uint32_t unknownC0[13];
        TSLink<CWFrustum> link;             // +0xf4

        // Member functions
        CWFrustum() = default;
        explicit CWFrustum(const C3Vector* corners);
        void SetCorners(const C3Vector* corners);
        void ComputePlanes();
        int32_t SphereInside(const CAaSphere& sphere);
};

#endif
