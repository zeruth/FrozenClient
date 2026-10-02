#ifndef WORLD_C_W_FRUSTUM_HPP
#define WORLD_C_W_FRUSTUM_HPP

#include <cstdint>
#include <storm/List.hpp>
#include <tempest/Box.hpp>
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
        // friends). reference FUN_007983b0
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
        // and a closed hole rather than a fix. reference FUN_00601650
        CWFrustum();
        explicit CWFrustum(const C3Vector* corners);
        void SetCorners(const C3Vector* corners);
        void ComputePlanes();
        // Move the whole volume into another space, corners first and planes after. The map
        // object pass uses it to bring the view into a building's own space, where the batch
        // boxes already live. reference FUN_00983f40
        void Transform(const C44Matrix& matrix);
        // Turn the frustum inside out -- every plane's normal and distance negated. The shadow
        // map's ortho frusta need it right after SetCorners. reference FUN_007ba960
        void NegatePlanes();
        int32_t SphereInside(const CAaSphere& sphere);
        // The box around the eight corners. reference FUN_00983990
        void GetBounds(CAaBox& bounds) const;
        // Swap the corners across x: 0 with 3, 1 with 2, 4 with 7, 5 with 6. The planes stay as
        // they are. The shadow map does this after turning an ortho frustum inside out.
        // reference FUN_007baaa0
        void MirrorCorners();
        // Move the volume by `offset`: corners and the two points after them, then each plane's
        // distance. reference FUN_00983ae0
        void Translate(const C3Vector& offset);
};

// The eight corners of the volume a view and a projection describe, in the space the view maps
// from: near face first, each face (-x,-y) (-x,+y) (+x,+y) (+x,-y). reference FUN_006bf6d0
void FrustumCorners(const C44Matrix& view, const C44Matrix& proj, C3Vector* corners);

// The matrix taking a point (relative to corner 0) into the unit cube the frustum spans: its
// rows are corner 3, corner 1 and corner 4 less corner 0, inverted. 0, and identity, when the
// volume is degenerate. `translate` folds corner 0 in as the translation first.
// reference FUN_00791640 (WorldScene.cpp)
int32_t FrustumUnitBasis(const CWFrustum& frustum, C44Matrix& out, int32_t translate);

// Clip a polygon to the unit cube, plane by plane (x 0 and 1, y 0 and 1, z 0 and 1): 0 when
// nothing survives, otherwise `clipped` points at the surviving points, new ones included.
// The arrays are the module's own statics and stay valid until the next call.
// reference FUN_00791380 (WorldScene.cpp)
int32_t ClipToUnitCube(const C3Vector* points, uint32_t count, const C3Vector* const** clipped, uint32_t* clippedCount);

#endif
