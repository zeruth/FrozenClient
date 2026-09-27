#ifndef WORLD_C_W_FRUSTUM_HPP
#define WORLD_C_W_FRUSTUM_HPP

#include <cstdint>
#include <storm/List.hpp>
#include <tempest/Matrix.hpp>
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
        // The reference has a real constructor for this and calls it at all ten of its
        // construction sites: six planes set to a (0, 0, 1) normal with d 0, and every other byte
        // zeroed. Alloc() hands out recycled records from a free list, so without it a fresh
        // frustum carries whatever the last user left. Nothing today depends on that -- the one
        // Alloc() site assigns the whole object immediately -- so this is the reference's behaviour
        // and a closed hole rather than a fix. ref: FUN_00601650
        CWFrustum();
        explicit CWFrustum(const C3Vector* corners);
        void SetCorners(const C3Vector* corners);
        void ComputePlanes();
        // Move the whole volume into another space, corners first and planes after. The map
        // object pass uses it to bring the view into a building's own space, where the batch
        // boxes already live. ref: FUN_00983f40
        void Transform(const C44Matrix& matrix);
        // Turn the frustum inside out -- every plane's normal and distance negated. The shadow
        // map's ortho frusta need it right after SetCorners. ref: FUN_007ba960
        void NegatePlanes();
        int32_t SphereInside(const CAaSphere& sphere);
};

#endif
