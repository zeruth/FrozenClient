#ifndef WORLD_WORLD_FACETS_HPP
#define WORLD_WORLD_FACETS_HPP

#include "model/CM2Model.hpp"
#include <storm/Array.hpp>
#include <cmath>
#include <cstdint>

#if defined(_M_X64) || defined(_M_IX86) || defined(__SSE__)
#include <xmmintrin.h>
#define WORLD_FACETS_HAVE_SSE 1
#endif

// What the facet builder records beside each facet it adds from the hit records: two words the
// caller passes (zero at every call site the reference has).
struct CFacetOwner {
    uint32_t a;
    uint32_t b;
};

// What the world frustum query fills (reference: two growable arrays back to back, the camera's
// static at 0x00c24e84). `facets` are 0x34-byte triangles, a plane then three world-space
// corners, the same record CM2Model::GetCollisionTriangles appends; `owners` runs beside the
// facets the hit-record builder added.
struct CFacetList {
    TSGrowableArray<M2CollisionTriangle> facets;
    TSGrowableArray<CFacetOwner> owners;
};

// 1 / sqrt(x) as the reference takes it when CMap::s_useSse is set: rsqrtss, about twelve bits.
// A build without SSE takes the exact value.
inline float FacetRsqrt(float x) {
#if defined(WORLD_FACETS_HAVE_SSE)
    return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(x)));
#else
    return 1.0f / std::sqrt(x);
#endif
}

#endif
