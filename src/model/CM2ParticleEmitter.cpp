#include "model/CM2ParticleEmitter.hpp"
#include <cmath>
#include <tempest/Math.hpp>
#include <cstdlib>
#include <cstring>
#include "gx/Gx.hpp"
#include "util/Log.hpp"
#include "world/ParticleFx.hpp"

// Flush a velocity's denormals to zero, component by component, leaving an exact zero alone.
//
// The threshold is the 1e-8 at 0x00a9806c, read out of the binary. It matters because a particle's
// velocity is multiplied by dt every step and then damped by drag, so a component that has decayed
// into the denormal range keeps costing full denormal-arithmetic penalties on every step of every
// particle while contributing nothing that could ever be seen.
//
// ref: FUN_00978b70
static void M2ParticleFlushDenormals(C3Vector& v) {
    if (v.x != 0.0f && fabsf(v.x) < 9.99999994e-09f) {
        v.x = 0.0f;
    }

    if (v.y != 0.0f && fabsf(v.y) < 9.99999994e-09f) {
        v.y = 0.0f;
    }

    if (v.z != 0.0f && fabsf(v.z) < 9.99999994e-09f) {
        v.z = 0.0f;
    }
}

float g_m2ParticleCameraDistance = 0.0f;

M2ParticleHeightQuery g_m2ParticleHeightQuery = nullptr;
void* g_m2ParticleHeightQueryContext = nullptr;

// ref: FUN_00979330
uint32_t M2PartTrackFindKey(const M2Array<fixed16>& times, float t) {
    uint32_t lo = 0;

    // TWO high bounds, and conflating them is wrong: `last` is fixed at the final index and only
    // guards against running off the end, while `high` is the shrinking search bound. The first
    // transcription of this used one variable for both and disagreed with a linear scan on roughly
    // one lookup in eight -- caught by testing it against one rather than by reading it again.
    uint32_t last = times.Count() - 1;

    if (last == 0) {
        return 0;
    }

    uint32_t high = last;

    for (;;) {
        uint32_t mid = (high + lo) >> 1;

        if (static_cast<float>(times[mid]) <= t) {
            lo = mid + 1;

            // The reference's second test reaches Ghidra as
            // `fVar1 < param_1 == (fVar1 == param_1)`, which is the x87 compare-and-branch pattern
            // CLAUDE.md warns about rather than anything meaningful. For ordered values `a < b` and
            // `a == b` cannot both hold, so the equality is true only when both are false: it means
            // `a > b`. The key is the right one once the NEXT key's time is beyond t.
            if (last <= lo || static_cast<float>(times[mid + 1]) > t) {
                return mid;
            }
        } else {
            high = mid - 1;
        }

        if (high <= lo) {
            return lo;
        }
    }
}

// ref: FUN_009793b0
float M2PartTrackRatio(uint32_t& lo, uint32_t& hi, const M2Array<fixed16>& times,
                       uint32_t valueCount, float t) {
    // Two keys: the whole track is one span, so the normalised time IS the ratio.
    if (valueCount == 2) {
        lo = 0;
        hi = 1;

        return t;
    }

    // Three keys: one comparison decides which of the two spans t falls in, and the ratio is t
    // rescaled within it. Cheaper than entering the search for a case this common.
    if (valueCount == 3) {
        float mid = static_cast<float>(times[1]);

        if (t < mid) {
            lo = 0;
            hi = 1;

            return t / mid;
        }

        lo = 1;
        hi = 2;

        return (t - mid) / (1.0f - mid);
    }

    uint32_t key = M2PartTrackFindKey(times, t);

    lo = key;
    hi = key + 1;

    float a = static_cast<float>(times[key]);
    float b = static_cast<float>(times[key + 1]);

    return (t - a) / (b - a);
}

// ref: FUN_00979480
void M2PartTrackEval2(C2Vector& out, const M2PartTrack<C2Vector>& track, float t) {
    // A single value is the whole track.
    if (track.values.Count() == 1) {
        out = track.values[0];

        return;
    }

    uint32_t lo = 0;
    uint32_t hi = 0;
    float ratio = M2PartTrackRatio(lo, hi, track.times, track.values.Count(), t);

    const C2Vector& a = track.values[lo];
    const C2Vector& b = track.values[hi];

    out.x = (b.x - a.x) * ratio + a.x;
    out.y = (b.y - a.y) * ratio + a.y;
}

// One fixed16 part track at `t`.
//
// ref: FUN_009794f0
float M2PartTrackEvalAlpha(const M2PartTrack<fixed16>& track, float t) {
    // The 1/32767 below is fixed16's own scale (0x009ea0b4), applied to each key BEFORE the
    // interpolation rather than after. The two are the same up to rounding, and this is the order
    // the reference uses.
    if (track.values.Count() == 1) {
        return static_cast<float>(track.values[0]);
    }

    uint32_t lo = 0;
    uint32_t hi = 0;
    float ratio = M2PartTrackRatio(lo, hi, track.times, track.values.Count(), t);

    float a = static_cast<float>(track.values[lo]);
    float b = static_cast<float>(track.values[hi]);

    return a + (b - a) * ratio;
}

// One uint16 texture-cell track at `t`.
//
// The interpolation is done in INTEGER arithmetic and rounded at the end -- `fimull` on the signed
// difference, `fiaddl` on the base (0x9795aa) -- so a track stepping 0 -> 3 crosses cell 1 and 2
// rather than blending them, which is what a texture atlas needs.
//
// ref: FUN_00979560
uint32_t M2PartTrackEvalCell(const M2PartTrack<uint16_t>& track, float t) {
    if (track.values.Count() == 1) {
        return track.values[0];
    }

    uint32_t lo = 0;
    uint32_t hi = 0;
    float ratio = M2PartTrackRatio(lo, hi, track.times, track.values.Count(), t);

    int32_t a = track.values[lo];
    int32_t b = track.values[hi];

    return static_cast<uint32_t>(static_cast<int32_t>(
        static_cast<float>(b - a) * ratio + static_cast<float>(a)));
}

// ref: FUN_00978ad0
void M2ParticleToFixed16(fixed16& out, float value) {
    if (value >= 1.0f) {
        out.n = 0x7FFF;

        return;
    }

    if (value <= -1.0f) {
        out.n = static_cast<int16_t>(0x8001);

        return;
    }

    out.n = static_cast<int16_t>(value * 32767.0f);
}

// See the declaration for why the centre is 2.0 rather than the 1.5 it looks like.
float M2ParticleRandSigned(CRndSeed& seed) {
    uint32_t u = CRandom::uint32(seed);
    uint32_t bits = (u & 0x7FFFFF) | 0x3F800000;

    float f;
    memcpy(&f, &bits, sizeof(f));

    return (static_cast<int32_t>(u) < 0) ? (2.0f - f) : (f - 2.0f);
}

// Map a GxBlend back to the M2 blend index.
//
// A jump table at 0x0081ca54 over 0..10, anything outside falling to 0. It is the exact inverse of
// M2ParticleBlendToGx below, which was read independently from a different part of the binary --
// M2 3 goes out as GxBlend 10 and comes back as 3, M2 4 goes out as 3 and comes back as 4. The two
// round-tripping is the best check available on that mapping short of running the client.
//
// ref: FUN_0081ca20
uint32_t M2BlendIndexFromGx(uint32_t gxBlend) {
    switch (gxBlend) {
    case 1:  return 1;   // GxBlend_AlphaKey
    case 2:  return 2;   // GxBlend_Alpha
    case 3:  return 4;   // GxBlend_Add
    case 4:  return 5;   // GxBlend_Mod
    case 5:  return 6;   // GxBlend_Mod2x
    case 10: return 3;   // GxBlend_NoAlphaAdd
    default: return 0;   // GxBlend_Opaque, and everything with no M2 equivalent
    }
}

// Map an M2Particle blend mode onto a GxBlend. See the declaration for why mode 3 matters.
//
// An out-of-range mode falls through with blend 0 and the flags untouched -- the reference has no
// default case, so whatever `flags` was seeded with survives.
uint32_t M2ParticleBlendToGx(uint8_t blendMode, uint32_t& flags) {
    switch (blendMode) {
    case 0:
        flags |= 0x4;
        return 0;   // GxBlend_Opaque
    case 1:
        flags |= 0x4;
        return 1;   // GxBlend_AlphaKey
    case 2:
        flags &= ~0x4u;
        return 2;   // GxBlend_Alpha
    case 3:
        flags &= ~0x4u;
        return 10;  // GxBlend_NoAlphaAdd -- additive, ignoring source alpha
    case 4:
        flags &= ~0x4u;
        return 3;   // GxBlend_Add
    case 5:
        flags &= ~0x4u;
        return 4;   // GxBlend_Mod
    case 6:
        flags &= ~0x4u;
        return 5;   // GxBlend_Mod2x
    default:
        return 0;
    }
}

// A uniform draw in [1, 2) from one RNG call: the mantissa of a float with the exponent pinned
// to zero. The reference inlines this at every site that needs a unit random; frozen keeps one
// copy, because the alternative is the same six lines repeated and those six lines are exactly
// where a transcription mistake would hide.
static float M2ParticleRandUnit(CRndSeed& seed) {
    uint32_t bits = (CRandom::uint32(seed) & 0x7FFFFF) | 0x3F800000;

    float f;
    memcpy(&f, &bits, sizeof(f));

    return f;
}

// ref: FUN_004c1680
void M2ParticleRandomUnitVector(C3Vector& out, CRndSeed& seed) {
    float z = M2ParticleRandSigned(seed);

    uint32_t u = CRandom::uint32(seed);
    uint32_t bits = (u & 0x7FFFFF) | 0x3F800000;

    float f;
    memcpy(&f, &bits, sizeof(f));

    // The radius of the circle at height z, and an azimuth drawn flat over the full turn. The
    // constant is the 2pi at 0x009f193c.
    float r = sqrtf(1.0f - z * z);
    float theta = (f - 1.0f) * 6.28318548f;

    out.x = cosf(theta) * r;
    out.y = r * sinf(theta);
    out.z = z;
}

// ref: FUN_009792d0
float CM2ParticleEmitter::RandomSpeed() {
    return (M2ParticleRandSigned(this->m_seed) * this->m_variation + 1.0f) * this->m_speed;
}

// This particle's own lifespan: the emitter's base, plus its own stored draw against the
// emitter's variation, floored so nothing downstream divides by zero.
//
// The reference inlines this at both of its sites (0x979771 in the ground-snap and 0x97de9b in the
// step) with the same instruction sequence, so it is one expression written twice rather than a
// call. The ASSOCIATION is worth preserving: both multiply `raw * variation * (1/32767)`, taking
// the raw int16 straight off the particle, and NOT `float(raw)/32767 * variation`. 0x009ea0b4
// holds 3.0518509e-05, the float nearest 1/32767. Same value, different rounding.
//
// The 0.001 floor is 0x009e1134, and the step takes the larger of the two the same way.
float CM2ParticleEmitter::ParticleLifespan(const Particle& p) const {
    float life = static_cast<float>(p.m_lifeVariation.n) * this->m_lifespanVariation
        * (1.0f / 32767.0f) + this->m_lifespan;

    return life < 0.001f ? 0.001f : life;
}

// ref: FUN_00979740
void CM2ParticleEmitter::GroundSnapParticle(Particle& p) {
    // The scale track, not a dedicated offset track -- see the field. A particle with no
    // scale track cannot be lifted, so the snap does nothing, which is also what the reference
    // does when the height query fails.
    if (!g_m2ParticleHeightQuery || !this->m_scaleTrack) {
        return;
    }

    float groundZ = 0.0f;

    if (!g_m2ParticleHeightQuery(p.m_position, groundZ, g_m2ParticleHeightQueryContext)) {
        return;
    }

    float life = this->ParticleLifespan(p);

    C2Vector range;
    M2PartTrackEval2(range, *this->m_scaleTrack, p.m_age / life);

    // The larger of the pair, which is what the reference's two-way compare picks.
    p.m_position.z = (range.y < range.x ? range.x : range.y) + groundZ;
}

// ref: FUN_009813f0
CM2ParticleEmitterSphere::CM2ParticleEmitterSphere() {
    this->m_emitterType = 2;
}

// ref: FUN_00981490
void CM2ParticleEmitterSphere::SetWidth(float minRadius) {
    this->m_minRadius = minRadius;
    this->m_radiusSpan = this->m_maxRadius - minRadius;
}

// ref: FUN_009814b0
void CM2ParticleEmitterSphere::SetLength(float maxRadius) {
    this->m_maxRadius = maxRadius;
    this->m_radiusSpan = maxRadius - this->m_minRadius;
}

// The reference folded this and the plane emitter's SetLongitude into ONE function at
// 0x009813e0 -- identical bodies, one store to +0x240 -- and points both vtables at it. It is
// tagged over on the plane's SetLongitude, since a tag claims a single frozen function and only
// one of the two can carry it. Written out rather than spelled as a tag on purpose: that spelling
// creates a tag wherever it appears, comments included.
void CM2ParticleEmitterSphere::SetLatitude(float latitude) {
    this->m_latitude = latitude;
}

// ref: FUN_009814d0
void CM2ParticleEmitterSphere::SetLongitude(float longitude) {
    this->m_longitude = longitude;
}

// Fill one new particle: place it on the shell, then launch it.
//
// The position's LATITUDE IS MEASURED FROM THE XY PLANE here (z is sin(lat)), which is the
// opposite convention to the plane emitter's direction build a few lines up, where z is cos(lat).
// Both are transcribed from their own function; the mismatch is the reference's and looks like a
// bug in one of them until you check both.
//
// ref: FUN_00981950
void CM2ParticleEmitterSphere::CreateParticle(Particle& p, float dt, const C44Matrix& placement) {
    p.m_age = (M2ParticleRandUnit(this->m_seed) - 1.0f) * dt;
    p.m_randomTag = static_cast<uint16_t>(CRandom::uint32(this->m_seed));

    // Like the plane emitter, this does not write m_lifeVariation; only the abstract base's
    // creator does.

    // Uniform across the shell's thickness -- note this is uniform in RADIUS, not in volume, so
    // particles bunch toward the inner surface. That is what the reference does.
    float radius = (M2ParticleRandUnit(this->m_seed) - 1.0f) * this->m_radiusSpan
        + this->m_minRadius;

    float latitude = M2ParticleRandSigned(this->m_seed) * this->m_latitude;
    float longitude = M2ParticleRandSigned(this->m_seed) * this->m_longitude;

    float sinLat = sinf(latitude);
    float cosLat = cosf(latitude);
    float sinLon = sinf(longitude);
    float cosLon = cosf(longitude);

    // The unit direction the position is built from. The third velocity case below reuses it.
    C3Vector outward = { cosLon * cosLat, cosLat * sinLon, sinLat };

    p.m_position.x = outward.x * radius;
    p.m_position.y = outward.y * radius;
    p.m_position.z = outward.z * radius;

    C3Vector dir;

    if (this->m_zSource != 0.0f) {
        C3Vector away = { p.m_position.x, p.m_position.y, p.m_position.z - this->m_zSource };

        float lengthSq = away.x * away.x + away.y * away.y + away.z * away.z;

        if (2.384185791015625e-07f < lengthSq) {
            float scale = 1.0f / sqrtf(lengthSq);

            dir.x = away.x * scale;
            dir.y = away.y * scale;
            dir.z = away.z * scale;
        } else {
            // Degenerate: the particle landed on the z source. Use the raw difference rather than
            // dividing by nearly zero. The plane emitter has no equivalent guard.
            dir = away;
        }
    } else if (this->m_flags & 0x8000) {
        // Straight up in emitter space.
        dir = { 0.0f, 0.0f, 1.0f };
    } else {
        dir = outward;
    }

    float speed = this->RandomSpeed();

    dir.x *= speed;
    dir.y *= speed;
    dir.z *= speed;

    if (this->m_flags & 0x200) {
        p.m_velocity = dir;
    } else {
        p.m_velocity.x = placement.a0 * dir.x + placement.b0 * dir.y + placement.c0 * dir.z;
        p.m_velocity.y = placement.a1 * dir.x + placement.b1 * dir.y + placement.c1 * dir.z;
        p.m_velocity.z = placement.a2 * dir.x + placement.b2 * dir.y + placement.c2 * dir.z;
    }
}

// ref: FUN_00981310
CM2ParticleEmitterPlane::CM2ParticleEmitterPlane() {
    this->m_emitterType = 1;
}

// ref: FUN_009813b0
void CM2ParticleEmitterPlane::SetWidth(float width) {
    this->m_width = width;
}

// ref: FUN_009813c0
void CM2ParticleEmitterPlane::SetLength(float length) {
    this->m_length = length;
}

// ref: FUN_009813d0
void CM2ParticleEmitterPlane::SetLatitude(float latitude) {
    this->m_latitude = latitude;
}

// ref: FUN_009813e0
void CM2ParticleEmitterPlane::SetLongitude(float longitude) {
    this->m_longitude = longitude;
}

// Fill one new particle: place it on the rectangle, then launch it.
//
// This is the creator the reference actually runs, as against the abstract base's further down.
// Its direction build is thirty instructions of interleaved x87 with nothing named, and two
// details only came out of tracing the stack by hand:
//
//   LATITUDE IS THE POLAR ANGLE from +Z, not an elevation from the XY plane. Swapping the sin and
//   the cos would compile, would still emit plausible particles, and would aim every cone
//   sideways.
//
//   The sequence `fld1; fsub %st(4), %st; faddp %st, %st(4)` at 0x9817e8 computes
//   cos(lon) + (1 - cos(lon)), which is exactly 1.0. It reads like a term and is not: the compiler
//   is materialising a 1.0 over a dead slot so it can multiply cos(lat) by it. Transcribing it
//   literally would have invented arithmetic the source never had.
//
// ref: FUN_009815c0
void CM2ParticleEmitterPlane::CreateParticle(Particle& p, float dt, const C44Matrix& placement) {
    // Spread the batch across the step rather than stacking it on one instant, as the base does.
    p.m_age = (M2ParticleRandUnit(this->m_seed) - 1.0f) * dt;
    p.m_randomTag = static_cast<uint16_t>(CRandom::uint32(this->m_seed));

    // NOTE the omission -- it is the reference's, not a transcription slip. This creator does NOT
    // write m_lifeVariation at +0x1c; it writes only +0x1e. The abstract base's creator does write
    // it. So a particle from a plane emitter carries whatever life variation the recycled slot
    // happened to hold, and the step reads exactly that field whenever m_lifespanVariation is
    // non-zero. Adding a fresh draw here is the obvious "fix" and would change how these effects
    // die.

    // The rectangle is CENTRED: a signed draw in [-1,1] times the extent times a half, so width
    // and length are full extents rather than half-extents (the 0.5 is 0x009e2ec4).
    p.m_position.x = M2ParticleRandSigned(this->m_seed) * this->m_width * 0.5f;
    p.m_position.y = this->m_length * M2ParticleRandSigned(this->m_seed) * 0.5f;
    p.m_position.z = 0.0f;

    float speed = this->RandomSpeed();

    C3Vector dir;

    if (this->m_zSource != 0.0f) {
        // With a z source the cone is ignored entirely: the particle is pushed straight out from a
        // point m_zSource below it, at the same speed.
        C3Vector away = { p.m_position.x, p.m_position.y, p.m_position.z - this->m_zSource };

        float length = sqrtf(away.x * away.x + away.y * away.y + away.z * away.z);
        float scale = speed / length;

        dir.x = away.x * scale;
        dir.y = away.y * scale;
        dir.z = away.z * scale;
    } else {
        float latitude = M2ParticleRandSigned(this->m_seed) * this->m_latitude;
        float longitude = M2ParticleRandSigned(this->m_seed) * this->m_longitude;

        float sinLat = sinf(latitude);
        float cosLat = cosf(latitude);
        float sinLon = sinf(longitude);
        float cosLon = cosf(longitude);

        dir.x = cosLon * sinLat * speed;
        dir.y = sinLat * sinLon * speed;
        dir.z = cosLat * speed;
    }

    if (this->m_flags & 0x200) {
        // Emitter space: the direction is already in the frame the particle lives in.
        p.m_velocity = dir;
    } else {
        // Rotate into the world by the placement's 3x3. No translation -- this is a direction.
        p.m_velocity.x = placement.a0 * dir.x + placement.b0 * dir.y + placement.c0 * dir.z;
        p.m_velocity.y = placement.a1 * dir.x + placement.b1 * dir.y + placement.c1 * dir.z;
        p.m_velocity.z = placement.a2 * dir.x + placement.b2 * dir.y + placement.c2 * dir.z;
    }
}

// Construct an emitter.
//
// Nearly every field's value is a default member initialiser on the class, where the reference
// writes it here -- same result, and it keeps each value beside the field it documents. What has
// to live in the body is the part with logic: the random seed.
//
// The reference builds it from two calls to the CRT rand() at 0x0088b867, the classic
// `x = x*0x343FD + 0x269EC3` returning bits 16..30. That is FIFTEEN bits, so
// `(rand() << 16) | (rand() & 0xffff)` leaves bits 15 and 31 always clear: a 30-bit seed with two
// holes in it, not a 32-bit one. Reproduced as written rather than tidied, because an emitter's
// entire visual signature -- every launch direction, speed and lifetime -- comes out of this
// stream, and a "better" seed would be a different-looking effect.
//
// ref: FUN_0097e150
CM2ParticleEmitter::CM2ParticleEmitter() {
    uint32_t seed = (static_cast<uint32_t>(rand()) << 16) | (static_cast<uint32_t>(rand()) & 0xFFFF);

    this->m_seed = CRndSeed(seed);
}

// Slot [10].
//
// ref: FUN_0097bd80
void CM2ParticleEmitter::SetEmissionRate(float rate) {
    if (rate > 0.0f) {
        this->m_rate = rate;
    }
}

// Slots [6]..[9]. Pure virtual in the reference (the base vtable holds _purecall at 0x0040baa5,
// which aborts), and implemented only by the two concrete subclasses at 0x00aa2d30 and 0x00aa2d5c
// -- 0x00981490/0x009814b0/0x009813e0/0x009814d0 and 0x009813b0/0x009813c0/0x009813d0/0x009813e0.
// Neither subclass is ported. Reporting is closer to the reference's intent than doing nothing:
// reaching these means an emitter was built as the abstract base, which the reference cannot do.
void CM2ParticleEmitter::SetWidth(float) {
    SysMsgPrintf(SYSMSG_ERROR, "CM2ParticleEmitter::SetWidth is pure virtual in the reference; "
                               "neither concrete emitter subclass is ported");
}

void CM2ParticleEmitter::SetLength(float) {
    SysMsgPrintf(SYSMSG_ERROR, "CM2ParticleEmitter::SetLength is pure virtual in the reference; "
                               "neither concrete emitter subclass is ported");
}

void CM2ParticleEmitter::SetLatitude(float) {
    SysMsgPrintf(SYSMSG_ERROR, "CM2ParticleEmitter::SetLatitude is pure virtual in the reference; "
                               "neither concrete emitter subclass is ported");
}

void CM2ParticleEmitter::SetLongitude(float) {
    SysMsgPrintf(SYSMSG_ERROR, "CM2ParticleEmitter::SetLongitude is pure virtual in the reference; "
                               "neither concrete emitter subclass is ported");
}

// Fill one new particle.
//
// DIVERGENCE, recorded in overrides.json under 00979870. This is the ABSTRACT BASE's creator, and
// no live emitter in the reference runs it: FUN_0097d820 dispatches through vtable[2], the
// function has zero direct callers, and both concrete subclasses override the slot with their own
// implementations (0x009815c0 and 0x00981950) that do not chain to this one.
//
// Both are now ported -- CM2ParticleEmitterPlane and CM2ParticleEmitterSphere -- so nothing built
// as either runs this. It stays because the dispatch is the reference's shape, and because the
// base is still concrete here where the reference's is abstract: anything constructed as the bare
// base lands on this creator, which the reference cannot do. The remaining hole is the THIRD
// emitter class (constructor 0x009820f0, 0x40c bytes), which is a separate hierarchy.
//
// The difference is not cosmetic. Compare the two: this one writes m_lifeVariation and the plane
// one does not, and this one builds its direction from a uniform sphere sample where the plane one
// builds a latitude/longitude cone.
//
// The age is the part to keep: a new particle starts at `rand[0,1) * dt`, NOT at zero, so a batch
// spawned in one step is spread across that step instead of stacked on the same instant. Without
// it an emitter pulses once a frame rather than flowing.
//
// ref: FUN_00979870
void CM2ParticleEmitter::CreateParticle(Particle& p, float dt, const C44Matrix& placement) {
    uint32_t u = CRandom::uint32(this->m_seed);
    uint32_t bits = (u & 0x7FFFFF) | 0x3F800000;

    float f;
    memcpy(&f, &bits, sizeof(f));

    float age = (f - 1.0f) * dt;

    // This particle's draw against the emitter's lifespan variation, saturated and kept for the
    // ground-snap to read back.
    M2ParticleToFixed16(p.m_lifeVariation, M2ParticleRandSigned(this->m_seed));

    if (age < 0.0f) {
        age = 0.0f;
    }

    p.m_age = age;
    p.m_randomTag = static_cast<uint16_t>(CRandom::uint32(this->m_seed));

    p.m_position = { 0.0f, 0.0f, 0.0f };

    // Flag 0x200 leaves the particle in emitter space; otherwise it is placed into the world here,
    // and only then can it be dropped onto the ground.
    if (!(this->m_flags & 0x200)) {
        C3Vector placed;
        TransformPointInPlace(placed, p.m_position, placement);

        if (this->m_flags & 0x40000) {
            this->GroundSnapParticle(p);
        }
    }

    float speed = this->RandomSpeed();

    C3Vector dir;
    M2ParticleRandomUnitVector(dir, this->m_seed);

    p.m_velocity.x = this->m_inheritedVelocity.x + dir.x * speed;
    p.m_velocity.y = this->m_inheritedVelocity.y + dir.y * speed;
    p.m_velocity.z = dir.z * speed + this->m_inheritedVelocity.z;

    M2ParticleFlushDenormals(p.m_velocity);
}

// ref: FUN_0097d820
void CM2ParticleEmitter::SpawnParticle(float dt, const C44Matrix& placement) {
    if (!this->m_freeIndices.Count()) {
        return;
    }

    // Pop the last free slot and make it live. The reference reaches the same two arrays through a
    // growable-array pop and a link; the effect is this.
    // Pop the free list and push the live one -- the reference's Pop (0x0097d7b0) and Add
    // (0x00480fd0) on the two containers.
    uint32_t slot = this->m_freeIndices[this->m_freeIndices.Count() - 1];
    this->m_freeIndices.SetCount(this->m_freeIndices.Count() - 1);

    this->m_liveIndices.Add(1, &slot);

    this->CreateParticle(this->ParticleAt(slot), dt, placement);
}

// Spawn whatever this frame's rate calls for.
//
// NOTE that this MOVES the placement matrix's translation while it works, under flag 0x2000, and
// puts it back before returning. That is why the parameter is not const: the reference edits the
// caller's matrix in place rather than copying it per particle.
//
// ref: FUN_0097d8c0
void CM2ParticleEmitter::Emit(float dt, C44Matrix& placement) {
    // Distance thinning, unless the emitter opts out. Full rate within 50 yards, falling off
    // linearly, and never below a quarter however far away. The three constants are the 50.0 at
    // 0x009f22ec, the 0.02 at 0x009e2efc and the 0.25 at 0x009e8ce4.
    float density = 1.0f;

    if (!(this->m_flags & 0x400000)) {
        density = 1.0f - (g_m2ParticleCameraDistance - 50.0f) * 0.02f;

        if (density < 0.25f) {
            density = 0.25f;
        } else if (density >= 1.0f) {
            density = 1.0f;
        }
    }

    float rate = ParticleFxGetDensity()
        * (M2ParticleRandSigned(this->m_seed) * this->m_rateVariation + this->m_rate)
        * density;

    // A burst: everything at once, at age zero, and the bit clears itself so it fires once.
    if ((this->m_flags & 0x40) && (this->m_flags & 0x2)) {
        int32_t n = static_cast<int32_t>(nearbyintf(rate));

        while (this->m_freeIndices.Count() && n) {
            this->SpawnParticle(0.0f, placement);
            n--;
        }

        this->m_flags &= ~0x40u;
    }

    if ((this->m_flags & 0x3) != 0x3) {
        return;
    }

    this->m_emitCarry += rate * dt;

    int32_t spawned = 0;
    int32_t n = static_cast<int32_t>(nearbyintf(this->m_emitCarry + 0.5f));

    if (!(this->m_flags & 0x2000)) {
        while (this->m_freeIndices.Count() && n) {
            this->SpawnParticle(dt, placement);
            spawned++;
            n--;
        }
    } else {
        // Interpolated placement: lay each particle at a random point along the segment the
        // emitter travelled this frame, so a moving emitter draws a trail instead of a cluster.
        C3Vector cur = { placement.d0, placement.d1, placement.d2 };
        const C3Vector& prev = this->m_prevPosition;

        while (this->m_freeIndices.Count() && n) {
            uint32_t u = CRandom::uint32(this->m_seed);
            uint32_t bits = (u & 0x7FFFFF) | 0x3F800000;

            float f;
            memcpy(&f, &bits, sizeof(f));

            float along = f - 1.0f;

            placement.d0 = prev.x + (cur.x - prev.x) * along;
            placement.d1 = (cur.y - prev.y) * along + prev.y;
            placement.d2 = (cur.z - prev.z) * along + prev.z;

            this->SpawnParticle(dt, placement);
            spawned++;
            n--;
        }

        placement.d0 = cur.x;
        placement.d1 = cur.y;
        placement.d2 = cur.z;
    }

    // Only what was actually spawned comes off the carry -- a full emitter keeps its credit rather
    // than losing it.
    this->m_emitCarry -= static_cast<float>(spawned);
}

// One particle, one step. Semi-implicit: the position takes the OLD velocity, and gravity
// contributes its half-step term to z as well as changing the velocity, which is what keeps a
// ballistic arc from drifting with the step size.
//
// The order below is the reference's and matters in two places. The velocity is sampled BEFORE the
// position is moved, and those sampled values -- not the post-step ones -- are what the cull at
// the end uses. And the wind is applied before the integration rather than folded into it, so a
// particle crossing m_windTime during a step gets the whole step's wind or none of it.
//
// ref: FUN_00979bb0
bool CM2ParticleEmitter::IntegrateParticle(Particle& p, float dt) const {
    // Wind, while the particle is young enough for it.
    if (p.m_age < this->m_windTime) {
        p.m_velocity.x += this->m_wind.x * dt;
        p.m_velocity.y += this->m_wind.y * dt;
        p.m_velocity.z += this->m_wind.z * dt;

        M2ParticleFlushDenormals(p.m_velocity);
    }

    // Carry the emitter's own movement into particles that are not brand new. The age test keeps a
    // particle spawned during this step from being dragged by a motion that happened before it
    // existed.
    if ((this->m_flags & 0x80000) && dt + dt < p.m_age) {
        p.m_position.x += this->m_substepDelta.x;
        p.m_position.y += this->m_substepDelta.y;
        p.m_position.z += this->m_substepDelta.z;
    }

    float vx = p.m_velocity.x;
    float vyDt = p.m_velocity.y * dt;
    float vzDt = p.m_velocity.z * dt;

    p.m_position.x += vx * dt;
    p.m_position.y += vyDt;
    p.m_position.z = (p.m_position.z - this->m_gravity * dt * dt * 0.5f) + vzDt;

    p.m_velocity.z -= this->m_gravity * dt;

    if (this->m_drag != 0.0f) {
        float f = dt * this->m_drag;

        if (f > 1.0f) {
            f = 1.0f;
        }

        p.m_velocity.x -= p.m_velocity.x * f;
        p.m_velocity.y -= p.m_velocity.y * f;
        p.m_velocity.z -= p.m_velocity.z * f;
    }

    M2ParticleFlushDenormals(p.m_velocity);

    // The divergence cull, for emitters that carry flag 0x1000. The threshold it compares against
    // is the 0.0 at 0x009e418c, so the test is purely a sign: kill the particle the moment it
    // stops approaching the emitter and starts receding. That is what makes an implosion collapse
    // inward and stop rather than pass through and fly out the far side.
    //
    // Flag 0x200 measures from the origin instead of from the emitter's current position, which is
    // the same choice that flag makes in the placement code.
    if (this->m_flags & 0x1000) {
        float dx;
        float rest;

        if (!(this->m_flags & 0x200)) {
            // The emitter's position is its placement matrix's translation row.
            dx = p.m_position.x - this->m_placement.d0;
            rest = (p.m_position.y - this->m_placement.d1) * vyDt
                + (p.m_position.z - this->m_placement.d2) * vzDt;
        } else {
            dx = p.m_position.x;
            rest = p.m_position.y * vyDt + p.m_position.z * vzDt;
        }

        if (dx * vx * dt + rest > 0.0f) {
            return false;
        }
    }

    return true;
}

// The z source arrives from the driver as the animated value of M2Particle::zsourceTrack, and
// anything under a thousandth is stored as a hard zero rather than kept.
//
// That is not tidiness. The z source is used as a divisor downstream, so a denormal left in here
// turns into an enormous velocity rather than the "effectively off" the author meant. The constant
// is the 0.001 at 0x009e1134, read out of the binary -- the same one the reference uses to convert
// scene milliseconds to seconds, which is why it turns up in two unrelated places.
//
// Choose which of the two quads each particle draws.
//
// The interesting half is the tail: 0x4 and 0x8 are the head and tail quads, and the two counts
// this derives are four vertices and six indices per quad -- one triangle pair. A particle drawing
// both costs 8 and 12, and the draw side sizes its batch from that rather than assuming one quad.
//
// ref: FUN_00978d00
void CM2ParticleEmitter::SetHeadTail(int32_t head, int32_t tail, float tailLength,
                                     int32_t flag20000) {
    if (head) {
        this->m_flags |= 0x4;
    } else {
        this->m_flags &= ~0x4u;
    }

    if (tail) {
        this->m_flags |= 0x8;
    } else {
        this->m_flags &= ~0x8u;
    }

    if (flag20000) {
        this->m_flags |= 0x20000;
    } else {
        this->m_flags &= ~0x20000u;
    }

    this->m_tailLength = tailLength;

    this->m_verticesPerParticle = 0;
    this->m_indicesPerParticle = 0;

    // Re-read from the flag word rather than from the arguments, so the counts stay consistent
    // with the flags whatever else has touched them.
    if (this->m_flags & 0x4) {
        this->m_verticesPerParticle = 4;
        this->m_indicesPerParticle = 6;
    }

    if (this->m_flags & 0x8) {
        this->m_verticesPerParticle += 4;
        this->m_indicesPerParticle += 6;
    }
}

// Release what the emitter owns.
//
// The TSGrowableArrays free themselves, but the texture reference does not: SetMaterial takes one
// through HandleDuplicate, and the pooled buffer the emitter lives in is released wholesale
// without running anything. CM2Model's teardown calls this explicitly for exactly that reason.
CM2ParticleEmitter::~CM2ParticleEmitter() {
    if (this->m_texture) {
        HandleClose(this->m_texture);
        this->m_texture = nullptr;
    }
}

// Cache the twinkle scale as a min and a span.
//
// ref: FUN_0097ac00
void CM2ParticleEmitter::SetTwinkleScale(const CRange& range) {
    this->m_twinkleMin = range.l;
    this->m_twinkleSpan = range.h - range.l;
}

// Take the emitter's material and a counted reference to its texture.
//
// The release comes FIRST and unconditionally, before the new reference is taken. That order is
// the reference's and it is the safe one either way: re-setting an emitter to the texture it
// already holds releases and re-adds, where the other order would work too -- but only because
// the caller happens to hold a reference of its own. Kept as written.
//
// ref: FUN_00978bf0
void CM2ParticleEmitter::SetMaterial(uint32_t blendMode, uint32_t materialFlags,
                                     HTEXTURE texture) {
    if (this->m_texture) {
        HandleClose(this->m_texture);
    }

    this->m_texture = HandleDuplicate(texture);

    this->m_blendMode = blendMode;
    this->m_materialFlags = materialFlags;
}

// Set the texture atlas grid, and derive the cell geometry from it.
//
// The power-of-two requirement is the reference's and it REPORTS rather than clamping: a grid that
// fails it leaves the emitter on whatever it had, which for a fresh one is the 1x1 the constructor
// sets. That matters because the shift below is only a valid substitute for a divide when the
// count is a power of two.
//
// ref: FUN_00978c70
void CM2ParticleEmitter::SetTextureGrid(uint32_t rows, uint32_t cols) {
    if (!rows || !cols || (rows & (rows - 1)) || (cols & (cols - 1))) {
        SysMsgPrintf(SYSMSG_ERROR,
                     "CM2ParticleEmitter::SetTextureGrid: %ux%u is not a pair of non-zero powers "
                     "of two; keeping the previous grid", rows, cols);

        return;
    }

    this->m_textureRows = rows;
    this->m_textureCols = cols;

    uint32_t shift = 0;

    for (uint32_t c = cols >> 1; c; c >>= 1) {
        shift++;
    }

    this->m_cellShift = shift;

    this->m_cellWidth = 1.0f / static_cast<float>(cols);
    this->m_cellHeight = 1.0f / static_cast<float>(rows);
}

// Turn texture animation on.
//
// The guard is the point: a grid with one cell has nothing to animate over, and asking for
// animation anyway would step a single-tile emitter around one cell forever. The reference tests
// the PRODUCT of the two grid dimensions against 1, so a 1xN grid still animates.
//
// ref: FUN_00978e30
void CM2ParticleEmitter::SetTextureAnimated(int32_t animated) {
    if (this->m_textureCols * this->m_textureRows > 1 && animated) {
        this->m_flags |= 0x100000;
    } else {
        this->m_flags &= ~0x100000u;
    }
}

// Drive the emitter for one frame.
//
// ref: FUN_0097eb10
void CM2ParticleEmitter::Update(float dt, const C44Matrix& matrix, const C3Vector& cameraPosition,
                                const C44Matrix* relativeTo) {
    // The distance emission thins by, from the matrix's translation to the camera. The reference
    // parks it in a global (0x00dce68c) that emission reads rather than threading it down.
    float dx = matrix.d0 - cameraPosition.x;
    float dy = matrix.d1 - cameraPosition.y;
    float dz = matrix.d2 - cameraPosition.z;
    float distanceSq = dx * dx + dy * dy + dz * dz;

    g_m2ParticleCameraDistance = distanceSq > 0.0f ? sqrtf(distanceSq) : 0.0f;

    // Last frame's position, taken BEFORE placement overwrites the translation row. Emission
    // interpolates along the segment between the two.
    this->m_prevPosition.x = this->m_placement.d0;
    this->m_prevPosition.y = this->m_placement.d1;
    this->m_prevPosition.z = this->m_placement.d2;

    this->Place(matrix, cameraPosition, relativeTo);

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        this->m_children[c]->Place(matrix, cameraPosition, relativeTo);
    }

    // 0x009ea27c = 2^-22. A frame shorter than that is not simulated at all; the emitter records
    // that it was skipped and returns.
    if (!(fabsf(dt) >= 2.384185791015625e-07f)) {
        this->m_flags |= 0x100;

        return;
    }

    if (this->m_flags & 0x80000) {
        this->m_frameDelta.x = this->m_placement.d0 - this->m_prevPosition.x;
        this->m_frameDelta.y = this->m_placement.d1 - this->m_prevPosition.y;
        this->m_frameDelta.z = this->m_placement.d2 - this->m_prevPosition.z;

        float lengthSq = this->m_frameDelta.x * this->m_frameDelta.x
            + this->m_frameDelta.y * this->m_frameDelta.y
            + this->m_frameDelta.z * this->m_frameDelta.z;

        float speed = lengthSq > 0.0f ? sqrtf(lengthSq) / dt : 0.0f;

        // Clamped to [0,1] in that order: negative first, then the ceiling. A follow base below
        // zero is how a model says "ignore emitter motion until it is moving fast enough".
        float follow = this->m_followScale * speed + this->m_followBase;

        if (!(follow >= 0.0f)) {
            follow = 0.0f;
        } else if (follow >= 1.0f) {
            follow = 1.0f;
        }

        this->m_frameDelta.x *= follow;
        this->m_frameDelta.y *= follow;
        this->m_frameDelta.z *= follow;
    }

    if (this->m_flags & 0x800) {
        // The emitter's own velocity, resampled on a 1/30s tick rather than every frame, so that
        // what new particles inherit does not jitter with the frame rate. 0x00aa2c9c is 1/30 and
        // 0x00aa2d08 is 30; neither is written anywhere in the text section.
        this->m_time += dt;

        if ((1.0f / 30.0f) < this->m_time) {
            float elapsed = this->m_time * 30.0f;

            // An exact reset, not a carry -- see the stack trace in this file's commit message.
            this->m_time = 0.0f;

            if (this->m_liveIndices.Count() == 0) {
                this->m_inheritedVelocity = { 0.0f, 0.0f, 0.0f };
            } else {
                this->m_inheritedVelocity.x = this->m_placement.d0 - this->m_prevPosition.x;
                this->m_inheritedVelocity.y = this->m_placement.d1 - this->m_prevPosition.y;
                this->m_inheritedVelocity.z = this->m_placement.d2 - this->m_prevPosition.z;

                float scale = (1.0f / elapsed) * this->m_velocitySampleScale;

                this->m_inheritedVelocity.x *= scale;
                this->m_inheritedVelocity.y *= scale;
                this->m_inheritedVelocity.z *= scale;
            }
        }
    }

    this->Substep(dt, 0);

    // 0x80 says this emitter ran this frame, as 0x100 above says it was skipped. The draw side
    // reads them.
    this->m_flags |= 0x80;

    if (this->m_particleKind == 1) {
        // FUN_0097e8d0, the spawned-model pass for the 0x40-byte pool. Unported, and unreachable
        // while nothing allocates that pool -- the same position as its integrator.
        SysMsgPrintf(SYSMSG_ERROR,
                     "CM2ParticleEmitter: model-particle update pass is not ported "
                     "(FUN_0097e8d0); spawned models will not follow their particles");
    }
}

// Set the emitter's transform and origin for this frame.
//
// `relativeTo` is a POINTER, and the decompilation invites getting that wrong: Ghidra types it
// `int param_4` and tests it as a boolean, which reads as a flag. ECX is loaded with it at
// 0x97ac3a and never reloaded before the call at 0x97ac61, so it is that call's `this` -- the
// matrix whose inverse is taken -- and `testl %ecx,%ecx` is a null check. Read as a flag, this
// function would invert whatever happened to be in ECX: wrong everywhere, and it would still run.
//
// Flag 0x200 is emitter-space, and suppresses the re-expression: a particle that never leaves the
// emitter's frame has nothing to be made relative to.
//
// ref: FUN_0097ac20
void CM2ParticleEmitter::Place(const C44Matrix& matrix, const C3Vector& origin,
                               const C44Matrix* relativeTo) {
    this->m_origin = origin;

    if (relativeTo && !(this->m_flags & 0x200)) {
        this->m_placement = matrix * relativeTo->AffineInverse();
    } else {
        this->m_placement = matrix;
    }

    // The scale comes from the ARGUMENT's first row, not from the product just stored -- EDI holds
    // param_2 on both paths into the tail at 0x97ac89. It matters when the two differ, which is
    // exactly the case the branch above exists for.
    this->m_scale = sqrtf(matrix.a0 * matrix.a0 + matrix.a1 * matrix.a1 + matrix.a2 * matrix.a2);
}

void CM2ParticleEmitter::RetireParticle(Particle& p, uint32_t liveIndex) {
    // The kill hook, vtable[3] -- and it does nothing. Not an assumption: the slot holds
    // 0x00632050, a bare `retl $4`, in ALL THREE of the hierarchy's vtables, the two concrete
    // subclasses included. Nothing overrides it, so there is no behaviour here to port and no
    // reason to make it virtual.
    (void)p;

    uint32_t slot = this->m_liveIndices[liveIndex];
    uint32_t live = this->m_liveIndices.Count();

    // The slot goes back to the free list, and the last live entry takes the dead one's place, so
    // the walk carries on from the same index without shuffling everything down.
    //
    // The reference reaches both arrays through a growable-array Add (0x00480fd0) and Pop
    // (0x0097d7b0) on containers at +0x5c and +0x4c -- which is where m_freeCount/+0x60 and
    // m_liveCount/+0x50 come from, each being its container's count field. Frozen allocates both
    // arrays at the pool size up front, so they never grow and the push and pop are these two
    // lines. Its degenerate arm, taken when the live count is already zero, dereferences a null
    // pointer; it is unreachable from inside a loop that only runs while the count is non-zero,
    // and is not reproduced.
    this->m_freeIndices.Add(1, &slot);

    this->m_liveIndices[liveIndex] = this->m_liveIndices[live - 1];
    this->m_liveIndices.SetCount(live - 1);
}

// Integrate one particle, then let every child emitter emit from where it now is.
//
// This is where a trail is made. For each child the emitter's OWN placement translation is moved
// to the particle's position, the child emits through the parent's matrix, and the translation is
// put back -- so a child's particles appear at the parent particle and inherit the parent's
// orientation, without the child needing a placement of its own.
//
// Ghidra drops ECX at that inner Emit call, which matters: the receiver decides whether the parent
// or the child is emitting, and the two readings mean opposite things. ECX is loaded at 0x97dc4c
// as `m_children[i]` and is not reloaded before the call at 0x97dcd0, so it is the CHILD that
// emits, through the parent's matrix.
//
// ref: FUN_0097db80
bool CM2ParticleEmitter::IntegrateAndSpawnChildren(float dt, Particle& p, uint32_t liveIndex) {
    // Saved BEFORE integrating: this is where the particle was, and a child with flag 0x2000 uses
    // it as its previous position so its own interpolated placement lays particles along the
    // segment the parent particle just travelled.
    C3Vector before = p.m_position;

    bool alive;

    if (this->m_particleKind == 0) {
        alive = this->IntegrateParticle(p, dt);
    } else {
        // Safe because the pool selection above handed us an element of the 0x40 pool: a
        // ModelParticle is what is actually there.
        alive = this->IntegrateModelParticle(static_cast<ModelParticle&>(p), dt);
    }

    if (!alive) {
        this->RetireParticle(p, liveIndex);

        return false;
    }

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        CM2ParticleEmitter* child = this->m_children[c];

        C3Vector saved = { this->m_placement.d0, this->m_placement.d1, this->m_placement.d2 };

        this->m_placement.d0 = p.m_position.x;
        this->m_placement.d1 = p.m_position.y;
        this->m_placement.d2 = p.m_position.z;

        // 0x800: the child's particles start with this one's velocity, so a trail keeps moving
        // with whatever shed it.
        if (child->m_flags & 0x800) {
            child->m_inheritedVelocity = p.m_velocity;
        }

        // 0x2000: the child interpolates its spawns along the segment this particle just covered.
        if (child->m_flags & 0x2000) {
            child->m_prevPosition = before;
        }

        child->Emit(dt, this->m_placement);

        this->m_placement.d0 = saved.x;
        this->m_placement.d1 = saved.y;
        this->m_placement.d2 = saved.z;
    }

    return true;
}

// The particle in `slot`, from whichever pool is in use.
//
// ModelParticle derives from Particle and its first 0x20 bytes are that base, so returning either
// as a Particle& is the same identity the reference relies on when it hands a 0x40 element to the
// plain integrator.
// NOTE the qualification on the return type: Particle is nested in the class, and a return type
// written at namespace scope is looked up BEFORE the qualified name brings the class into scope.
// Parameters are fine unqualified because they come after it.
CM2ParticleEmitter::Particle& CM2ParticleEmitter::ParticleAt(uint32_t slot) {
    if (this->m_particleKind == 0) {
        return this->m_pool[slot];
    }

    return this->m_modelPool[slot];
}

// Spin one model particle, then integrate it like a plain one.
//
// The rotation is built from the angular velocity's own magnitude: the axis is that vector
// normalised, and the angle is `magnitude * dt`, halved for the quaternion. Folding the
// normalise and the angle together would double the spin, so the two multiplies stay separate
// here as they are in the reference.
//
// ref: FUN_0097bdb0
bool CM2ParticleEmitter::IntegrateModelParticle(ModelParticle& p, float dt) const {
    float length = sqrtf(p.m_angularVelocity.x * p.m_angularVelocity.x
        + p.m_angularVelocity.y * p.m_angularVelocity.y
        + p.m_angularVelocity.z * p.m_angularVelocity.z);

    // 0x009e8cd0. Below this the particle is not turning and the normalise would be meaningless.
    if (0.0001f < length) {
        float half = length * dt * 0.5f;

        float s = sinf(half);
        float c = cosf(half);

        float scale = (1.0f / length) * s;

        C4Quaternion delta(p.m_angularVelocity.x * scale,
                           p.m_angularVelocity.y * scale,
                           p.m_angularVelocity.z * scale,
                           c);

        p.m_orientation = p.m_orientation * delta;
    }

    return this->IntegrateParticle(p, dt);
}

// The particle system's shared index buffer, and the refcount that owns it.
//
// ONE buffer serves every particle quad in the world: 131,064 uint16s holding (0, 1, 2, 3, 2, 1)
// repeated per four vertices, 21,845 quads, written once and never touched again. A particle draw
// uploads vertices only, which is why the fill caps its count at `0x4000 / verticesPerParticle`
// instead of growing anything.
static uint32_t s_particleIndexRefs = 0;
static CGxPool* s_particleIndexPool = nullptr;
static CGxBuf* s_particleIndexBuf = nullptr;

// ref: FUN_00979170
void M2ParticleIndexBufferCreate() {
    if (s_particleIndexRefs != 0) {
        s_particleIndexRefs++;

        return;
    }

    // DROPPED BRANCH, and the reason is that it cannot run. The reference guards this on a byte
    // at 0x00dce890 -- set, and the refcount still rises while both pointers stay null. That byte
    // has exactly ONE reference in the whole 5.4MB text section, the read at 0x979179; nothing
    // writes it. Transcribing the branch would mean inventing a frozen global that is equally
    // never written, to guard a path that equally cannot be taken.

    // 0x3FFF0 bytes is 0x1FFF8 uint16s exactly. The name is the reference's own, at 0x00aa2ca8.
    s_particleIndexPool = GxPoolCreate(
        GxPoolTarget_Index,
        GxPoolUsage_Static,
        0x3FFF0,
        GxPoolHintBit_Unk2,
        const_cast<char*>("CParticleEmitter2_idx")
    );

    s_particleIndexBuf = GxBufCreate(s_particleIndexPool, 2, 0x1FFF8, 0);

    s_particleIndexRefs++;
}

// The twinkle table: 128 random floats in [0, 1), filled once by M2ParticleInitTwinkleTable.
//
// The quad writer hashes a particle into this as `((address >> 5) + round(fps * age)) & 0x7f` and
// DROPS the particle for the frame when its sample exceeds the emitter's on/off threshold. That
// is a blink, not a fade -- a twinkling particle is absent on the frames it is off, which is why
// the table is sampled before anything else is computed.
//
// The `>> 5` is load-bearing rather than arbitrary: a Particle is 0x20 bytes, so the shift
// increments by exactly one per pool slot and the hash is "slot index plus a time term". A
// 64-bit pointer changes only the high bits, which the mask discards.
//
// Zero until the init runs, and zero is a safe reading: `threshold < 0` is false for any sane
// threshold, so an uninitialised table twinkles nothing off rather than everything.
static float s_particleTwinkle[128] = {};

// Not a reference function of its own: the reference fills the table inline inside
// CM2Cache::Initialize (FUN_0081c0d0), at 0x81c240. Factored out here only so the table
// can stay file-static beside the code that reads it.
void M2ParticleInitTwinkleTable() {
    // Two draws, the FIRST supplying the high half. One rand() would give 15 bits on Windows and
    // the table would repeat.
    CRndSeed seed(static_cast<uint32_t>(rand()) << 16
                  | (static_cast<uint32_t>(rand()) & 0xFFFF));

    for (uint32_t i = 0; i < 128; i++) {
        s_particleTwinkle[i] = M2ParticleRandUnit(seed) - 1.0f;
    }
}

// The four corners of a particle quad and their texture coordinates, both STATIC INITIALISED
// DATA in the reference (0x00b2d5b4 and 0x00b2d5d4) rather than anything computed at startup --
// read straight out of the image.
//
// +y is up and v = 0 is the top, and the order matches the shared index buffer's repeating
// (0, 1, 2, 3, 2, 1): triangles (0,1,2) and (3,2,1), consistently wound.
static const C2Vector s_particleCorner[4] = {
    { -1.0f,  1.0f },
    { -1.0f, -1.0f },
    {  1.0f,  1.0f },
    {  1.0f, -1.0f }
};

static const C2Vector s_particleCornerUv[4] = {
    { 0.0f, 0.0f },
    { 0.0f, 1.0f },
    { 1.0f, 0.0f },
    { 1.0f, 1.0f }
};

// The draw basis, which SetupDrawBasis computes once per emitter and everything below reads.
//
// The reference keeps these in three globals (0x00b2d540, 0x00b2d550, 0x00b2d590) rather than
// threading them through, and they stay globals here for the same reason: the quad writer touches
// all three per VERTEX, and every function that reads them lives in this file.
//
// s_particleNormal is row 2 of the view matrix -- the camera direction -- and goes into every
// particle vertex's normal unchanged. A particle is a flat quad facing the camera, so one normal
// is right for all four corners of all of them.
static C3Vector s_particleNormal = {};
static C44Matrix s_particleSpace;
static C33Matrix s_particleBasis;

// The sink an UNLIT emitter's normal writes land in.
//
// The reference lazily zeroes its copy on first use, behind bit 0 of a flag word at 0x00dce8c0;
// zero-initialising at namespace scope reaches the same state without the flag. Recorded rather
// than left silent, because "the reference had a lazy-init here" is the kind of detail a later
// reader would otherwise have to re-derive to know nothing was missed.
static float s_particleNormalSink[3] = { 0.0f, 0.0f, 0.0f };

// Point a cursor block at a mapped vertex buffer.
//
// GxVertexAttribOffset IS the reference's FUN_00681240 -- `&DAT_00a2d5b0 + (attrib + format *
// 0xe) * 4`, where 0xe is GxVAs_Last -- so the four offsets below are the reference's four,
// computed by the same table.
//
// ref: FUN_0097a2e0
void CM2ParticleEmitter::SetupVertexCursor(char* base, EGxVertexBufferFormat format,
                                           VertexCursor& cursor) const {
    uint32_t stride = GxVertexBufferFormatSize(format);

    cursor.m_position =
        reinterpret_cast<float*>(base + GxVertexAttribOffset(format, GxVA_Position));
    cursor.m_positionStride = stride;

    // THE ONE ASYMMETRY IN THIS FUNCTION. An unlit emitter draws through GxVBF_PCT, which has no
    // normal, so asking the table for one would hand back an offset belonging to a different
    // attribute and the writer would corrupt every vertex. The reference points the cursor at a
    // shared zeroed triple with stride ZERO instead, so all of the frame's normal writes land in
    // the same scratch and are discarded -- which is what lets one writer serve both formats.
    if (this->m_materialFlags & 0x1) {
        cursor.m_normal =
            reinterpret_cast<float*>(base + GxVertexAttribOffset(format, GxVA_Normal));
        cursor.m_normalStride = stride;
    } else {
        cursor.m_normal = s_particleNormalSink;
        cursor.m_normalStride = 0;
    }

    cursor.m_color =
        reinterpret_cast<uint32_t*>(base + GxVertexAttribOffset(format, GxVA_Color0));
    cursor.m_colorStride = stride;

    cursor.m_texCoord =
        reinterpret_cast<float*>(base + GxVertexAttribOffset(format, GxVA_TexCoord0));
    cursor.m_texCoordStride = stride;
}

// Work out the matrices the quad writer draws through.
//
// Ghidra cannot render this one: the matrix multiply returns its destination in eax and the
// compiler chains three calls through partly shared argument pushes, so the decompilation shows
// the same operator* taking four arguments in one place and two in another. The push order at
// 0x97a390 is unambiguous and that is where this came from.
//
// ref: FUN_0097a390
void CM2ParticleEmitter::SetupDrawBasis(const C44Matrix* relativeTo, const C44Matrix& view) {
    // An identity whose translation row is MINUS the emitter's origin: it moves particle
    // positions into a space centred on the emitter. Built inline at 0x97a399..0x97a406 as
    // fld1/fldz stores landing exactly on a C44Matrix's diagonal.
    C44Matrix offset;

    offset.d0 = -this->m_origin.x;
    offset.d1 = -this->m_origin.y;
    offset.d2 = -this->m_origin.z;

    if (this->m_flags & 0x200) {
        // Emitter space: particles are stored relative to the emitter's own placement, so the
        // placement has to be reapplied. This is the branch the whole 0x200 family exists for.
        s_particleSpace = (this->m_placement * offset) * view;
    } else if (relativeTo) {
        s_particleSpace = (*relativeTo * offset) * view;
    } else {
        s_particleSpace = offset * view;
    }

    // Row 2 of the VIEW, not of the product: the camera direction in world terms.
    s_particleNormal = { view.c0, view.c1, view.c2 };

    if (!(this->m_flags & 0x4000)) {
        return;
    }

    s_particleBasis = C33Matrix(s_particleSpace);

    // 0x009ea27c is 2^-22, the same epsilon C3Vector::Normalize uses. Both conditions matter: a
    // zero-scale emitter would divide by zero here and fill the basis with infinities, and
    // nothing downstream checks.
    if ((this->m_flags & 0x200) && 0.00000023841858f < CMath::fabs(this->m_scale)) {
        s_particleBasis *= 1.0f / this->m_scale;
    }

    // Row 2 of the basis, normalised. Note this is read AFTER the division above, so it comes off
    // the scaled matrix -- and the normalise then undoes the scaling exactly, which makes the
    // division invisible to the axis. Reading it before the division would be identical; so
    // would hoisting the normalise. Both are noise, and saying so here is cheaper than working
    // it out twice.
    this->m_tumbleAxis = { s_particleBasis.c0, s_particleBasis.c1, s_particleBasis.c2 };
    this->m_tumbleAxis.Normalize();
}

// This particle's colour at normalised age `t`.
//
// Alpha is left at 255; SampleAppearance overwrites it. The C3Vector keys are (r, g, b) in 0..255
// and CImVector is {b, g, r, a}, so the .z key lands in .b -- which is why the assignments below
// look reversed.
//
// ref: FUN_009795d0
void CM2ParticleEmitter::SampleColor(CImVector& out, float t) const {
    const M2PartTrack<C3Vector>& track = *this->m_colorTrack;

    out.a = 0xFF;

    // A single key is the whole track and no interpolation happens -- including no override
    // lookup, which is a real asymmetry rather than an oversight: the reference reads the TRACK
    // here even when flag 0x10 is set.
    if (track.values.Count() == 1) {
        const C3Vector& only = track.values[0];

        out.r = static_cast<uint8_t>(static_cast<int32_t>(only.x));
        out.g = static_cast<uint8_t>(static_cast<int32_t>(only.y));
        out.b = static_cast<uint8_t>(static_cast<int32_t>(only.z));

        return;
    }

    uint32_t lo = 0;
    uint32_t hi = 0;
    float ratio = M2PartTrackRatio(lo, hi, track.times, track.values.Count(), t);

    // Flag 0x10 swaps the model's colour keys for the emitter's own override table, indexed by
    // the SAME lo/hi the track produced -- which only works because both are three entries long.
    //
    // UNREACHABLE TODAY. Nothing sets m_colorOverride (see its declaration), so the flag is never
    // on. Written out rather than left as a TODO because the branch is two lines and leaving it
    // out would make a later reader think the override does not exist.
    const C3Vector* keys = (this->m_flags & 0x10)
        ? this->m_colorOverride
        : &track.values[0];

    const C3Vector& a = keys[lo];
    const C3Vector& b = keys[hi];

    out.r = static_cast<uint8_t>(static_cast<int32_t>((b.x - a.x) * ratio + a.x));
    out.g = static_cast<uint8_t>(static_cast<int32_t>((b.y - a.y) * ratio + a.y));
    out.b = static_cast<uint8_t>(static_cast<int32_t>((b.z - a.z) * ratio + a.z));
}

// This particle's two spin terms.
//
// Both are base-plus-variation draws, and each variation is skipped entirely when it is zero --
// so an emitter with no spin variation consumes no randomness here and the RNG stream stays in
// step with the reference's.
//
// ref: FUN_0097a130
void CM2ParticleEmitter::SampleSpin(const Particle& p, float& initialSpin, float& spinRate) const {
    // Neither term varies: answer from the parameters and do not touch the RNG at all. The
    // reference returns before constructing the seed.
    if (this->m_initialSpinVariation == 0.0f && this->m_spinVariation == 0.0f) {
        initialSpin = this->m_initialSpin;
        spinRate = this->m_spin;

        return;
    }

    CRndSeed seed(p.m_randomTag);

    if (this->m_initialSpinVariation == 0.0f) {
        initialSpin = this->m_initialSpin;
    } else {
        initialSpin =
            M2ParticleRandSigned(seed) * this->m_initialSpinVariation + this->m_initialSpin;
    }

    if (this->m_spinVariation == 0.0f) {
        spinRate = this->m_spin;

        return;
    }

    spinRate = M2ParticleRandSigned(seed) * this->m_spinVariation + this->m_spin;
}

// Everything the quad writer needs about one particle's appearance.
//
// THE ORDER OF THE RNG DRAWS BELOW IS LOAD-BEARING. One seed is built from the particle's stored
// random tag and drawn from at most three times: once for a random texture cell, then once or
// twice for the scale variation. SampleColor does not draw. Reordering any of it would give every
// particle in the game a different appearance while still looking like it works.
//
// ref: FUN_00979e90
void CM2ParticleEmitter::SampleAppearance(const Particle& p, CImVector& color, C2Vector& size,
                                          uint32_t& headCell, uint32_t& tailCell) const {
    // The same expression ParticleLifespan computes, floor included (0.001 at 0x009e1134).
    float t = p.m_age / this->ParticleLifespan(p);

    CRndSeed seed(p.m_randomTag);

    this->SampleColor(color, t);

    // 255.0 is 0x009e30c0. m_alpha is the model's own alpha, already clamped to 0..1 by the
    // driver, so this is the only place the two alphas meet.
    float alpha = M2PartTrackEvalAlpha(*this->m_alphaTrack, t) * this->m_alpha * 255.0f;

    color.a = static_cast<uint8_t>(static_cast<int32_t>(alpha));

    M2PartTrackEval2(size, *this->m_scaleTrack, t);

    headCell = 0;
    tailCell = 0;

    if (this->m_headCellTrack->times.Count() != 0) {
        headCell = M2PartTrackEvalCell(*this->m_headCellTrack, t);
    } else if (this->m_flags & 0x100000) {
        // A random cell, uniformly over the whole grid. This is a MULTIPLY-HIGH rather than a
        // modulo -- 0x979f93 takes the 64-bit product of the draw and the cell count and keeps
        // the top half -- which is uniform where `rand % n` would not be. Ghidra prints the shift
        // as a bare `__aullshr()` with no operands, so it came off the disassembly.
        uint64_t product = static_cast<uint64_t>(CRandom::uint32(seed))
            * static_cast<uint64_t>(this->m_textureCols * this->m_textureRows);

        headCell = static_cast<uint32_t>(product >> 32);
    }

    if (this->m_tailCellTrack->times.Count() != 0) {
        tailCell = M2PartTrackEvalCell(*this->m_tailCellTrack, t);
    }

    // 0.0001 is 0x009e8cd0: a variation draw may not shrink a particle past it, and may not
    // invert it, which a draw of -1 against a variation above 1 otherwise would.
    if (this->m_flags & 0x800000) {
        // NON-UNIFORM. Two independent draws, and note which feeds which: the FIRST scales Y and
        // the SECOND scales X. Backwards from how anyone would write it, and swapping them would
        // be invisible except that every particle in the game would be a different shape.
        float fy = M2ParticleRandSigned(seed) * this->m_scaleVariation.y + 1.0f;
        float fx = M2ParticleRandSigned(seed) * this->m_scaleVariation.x + 1.0f;

        size.x *= fx < 0.0001f ? 0.0001f : fx;
        size.y *= fy < 0.0001f ? 0.0001f : fy;

        return;
    }

    // Uniform: one draw against the X variation scales both axes, so the particle keeps its
    // aspect ratio.
    float f = M2ParticleRandSigned(seed) * this->m_scaleVariation.x + 1.0f;

    f = f < 0.0001f ? 0.0001f : f;

    size.x *= f;
    size.y *= f;
}

// Write one vertex and advance the cursor.
//
// The reference inlines this at every one of its ~20 sites; it is a function here because twenty
// copies of the same eleven lines is how a transcription error hides. The bounds absorb every
// vertex as it is written, which is why Draw resets them to an inverted box first.
void CM2ParticleEmitter::WriteVertex(VertexCursor& cursor, const C3Vector& position,
                                     const CImVector& color, float u, float v) {
    cursor.m_position[0] = position.x;
    cursor.m_position[1] = position.y;
    cursor.m_position[2] = position.z;

    if (position.x < this->m_boundsMin.x) { this->m_boundsMin.x = position.x; }
    if (position.y < this->m_boundsMin.y) { this->m_boundsMin.y = position.y; }
    if (position.z < this->m_boundsMin.z) { this->m_boundsMin.z = position.z; }

    if (this->m_boundsMax.x < position.x) { this->m_boundsMax.x = position.x; }
    if (this->m_boundsMax.y < position.y) { this->m_boundsMax.y = position.y; }
    if (this->m_boundsMax.z < position.z) { this->m_boundsMax.z = position.z; }

    // One normal for every particle vertex in the frame -- see s_particleNormal. On an unlit
    // emitter the cursor points these three writes at a shared sink with stride zero.
    cursor.m_normal[0] = s_particleNormal.x;
    cursor.m_normal[1] = s_particleNormal.y;
    cursor.m_normal[2] = s_particleNormal.z;

    *cursor.m_color = color.value;

    cursor.m_texCoord[0] = u;
    cursor.m_texCoord[1] = v;

    cursor.m_position = reinterpret_cast<float*>(
        reinterpret_cast<char*>(cursor.m_position) + cursor.m_positionStride);
    cursor.m_normal = reinterpret_cast<float*>(
        reinterpret_cast<char*>(cursor.m_normal) + cursor.m_normalStride);
    cursor.m_color = reinterpret_cast<uint32_t*>(
        reinterpret_cast<char*>(cursor.m_color) + cursor.m_colorStride);
    cursor.m_texCoord = reinterpret_cast<float*>(
        reinterpret_cast<char*>(cursor.m_texCoord) + cursor.m_texCoordStride);

    cursor.m_count++;
}

// The UV of a texture-atlas cell's top-left corner.
//
// The column is masked rather than divided, which is why SetTextureGrid insists both grid
// dimensions are powers of two. Both the 2^32 fixup and the arithmetic shift are the compiler
// treating an unsigned cell index as signed; neither can fire for a real cell, and both are
// transcribed because "cannot fire" is a claim about the data, not about the code.
void CM2ParticleEmitter::CellUvBase(uint32_t cell, float& u, float& v) const {
    int32_t column = static_cast<int32_t>((this->m_textureCols - 1) & cell);

    float fColumn = static_cast<float>(column);

    if (column < 0) {
        fColumn += 4294967296.0f;
    }

    u = fColumn * this->m_cellWidth;
    v = static_cast<float>(static_cast<int32_t>(cell) >> this->m_cellShift) * this->m_cellHeight;
}

// One entry of the depth sort: a particle and how far into the scene it is.
struct M2ParticleSortEntry {
    float m_depth;
    const CM2ParticleEmitter::Particle* m_particle;
};

// The sort itself. A file-scope container because the reference's is a global at 0x00dce894 --
// one emitter draws at a time, so there is nothing to be gained by giving each its own.
static TSGrowableArray<M2ParticleSortEntry> s_particleSort;

// Resolve a live-list slot into whichever pool this emitter uses.
//
// The live list holds POOL INDICES, not particles -- ParticleAt is what picks between the two
// pools, and the reference inlines both steps at each of its four sites.
CM2ParticleEmitter::Particle& CM2ParticleEmitter::LiveParticleAt(uint32_t slot) {
    return this->ParticleAt(this->m_liveIndices[slot]);
}

// Walk this emitter's live particles into the cursor.
//
// `count` is a CAP, not the live count, and the two paths below use different bounds on purpose:
// the sorted one pushes EVERY live particle and pops only `count`, so what survives a full buffer
// is the `count` farthest particles rather than the first `count` in list order. That is the
// whole point of sorting -- when the buffer cannot hold everything, keep the ones behind.
//
// ref: FUN_0097e580
void CM2ParticleEmitter::WriteLiveParticles(VertexCursor& cursor, uint32_t count,
                                            const C44Matrix& inverse) {
    if (!(this->m_flags & 0x20)) {
        for (uint32_t i = 0; i < count; i++) {
            this->WriteParticleVertices(this->LiveParticleAt(i), cursor);
        }
    } else {
        // DIVERGED, and the reason is in the comment block on this file's draw chain: the
        // reference's global heap (push FUN_007a0f50, pop FUN_0097e080) does not agree with
        // itself about indexing -- push places 0-based, pop and the sift-up read 1-based -- so
        // the first particle pushed is never popped. One particle per emitter per frame is
        // silently dropped. Whether that is a sentinel whose base pointer is biased somewhere I
        // did not find, or an off-by-one, I could not establish, and reproducing a suspected bug
        // on a guess is worse than recording it. This sorts correctly instead, and neither heap
        // function is tagged, because a different index convention is a different function.
        s_particleSort.SetCount(0);

        for (uint32_t i = 0; i < this->m_liveIndices.Count(); i++) {
            const Particle& p = this->LiveParticleAt(i);

            // Row 2 of the particle-space matrix: the view-space Z.
            float depth = s_particleSpace.a2 * p.m_position.x
                + s_particleSpace.b2 * p.m_position.y
                + s_particleSpace.c2 * p.m_position.z
                + s_particleSpace.d2;

            M2ParticleSortEntry* entry = s_particleSort.New();

            entry->m_depth = depth;
            entry->m_particle = &p;
        }

        // Selection rather than a full sort: `count` is usually the whole list and never much
        // less, so this is the same work without a comparator, and it pops largest-first the way
        // the reference's max-heap does -- back to front, which is what alpha blending needs.
        uint32_t live = s_particleSort.Count();

        for (uint32_t written = 0; written < count && written < live; written++) {
            uint32_t best = written;

            for (uint32_t i = written + 1; i < live; i++) {
                if (s_particleSort[best].m_depth < s_particleSort[i].m_depth) {
                    best = i;
                }
            }

            M2ParticleSortEntry swap = s_particleSort[written];
            s_particleSort[written] = s_particleSort[best];
            s_particleSort[best] = swap;

            this->WriteParticleVertices(*s_particleSort[written].m_particle, cursor);
        }
    }

    if (count) {
        // The writer accumulated the bounds in VIEW space; `inverse` brings them back out into
        // the emitter's own, and the origin puts them where the emitter is.
        CAaBox bounds;

        bounds.b = this->m_boundsMin;
        bounds.t = this->m_boundsMax;

        CAaBox folded = TransformBox(bounds, inverse);

        this->m_boundsMin = {
            folded.b.x + this->m_origin.x,
            folded.b.y + this->m_origin.y,
            folded.b.z + this->m_origin.z
        };

        this->m_boundsMax = {
            folded.t.x + this->m_origin.x,
            folded.t.y + this->m_origin.y,
            folded.t.z + this->m_origin.z
        };
    }

    // How many particles actually reached the buffer, which is not `count`: the twinkle gate
    // drops some of them without writing anything.
    this->m_drawnCount = cursor.m_count / this->m_verticesPerParticle;
}

// Write one particle's quads.
//
// Read out of the disassembly at 0x97be80 rather than the decompilation; see the comment block in
// docs/ref/parity-particles.md for why, and for the frame map this was rebuilt from.
//
// ref: FUN_0097be80
bool CM2ParticleEmitter::WriteParticleVertices(const Particle& p, VertexCursor& cursor) {
    // The particle's slot in the twinkle table. The address shift is the pool slot index -- a
    // Particle is 0x20 bytes -- plus a term that advances with age, so a particle walks the table
    // at m_twinkleFps entries a second.
    //
    // lrintf, not a cast: the reference's `fistpl` rounds to nearest-even and a cast truncates.
    uint32_t twinkle = 0;

    if (this->m_twinkleOnOff < 1.0f || this->m_twinkleSpan != 0.0f) {
        int32_t step = static_cast<int32_t>(lrintf(this->m_twinkleFps * p.m_age));

        twinkle = (static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&p) >> 5) + step) & 0x7F;
    }

    // THE PARTICLE IS DROPPED, not faded: a twinkling particle is absent on its off frames, and
    // the caller sees a vertex count that did not move.
    if (this->m_twinkleOnOff < 1.0f && this->m_twinkleOnOff < s_particleTwinkle[twinkle]) {
        return false;
    }

    CImVector color;
    C2Vector size = { 0.0f, 0.0f };
    uint32_t headCell = 0;
    uint32_t tailCell = 0;

    color.value = 0;

    if (this->m_flags & 0x1000000) {
        // FUN_00979d60, the precompiled-ramp fast path. Unreachable: nothing builds the ramp at
        // emitter +0x11c. Falling through to the normal sampler would be WRONG rather than
        // approximate -- the ramp carries its own midpoint split -- so this says so instead.
        SysMsgPrintf(SYSMSG_ERROR,
                     "CM2ParticleEmitter: flag 0x1000000 wants the ramp sampler (FUN_00979d60), "
                     "which is not ported");

        return false;
    }

    this->SampleAppearance(p, color, size, headCell, tailCell);

    float initialSpin = 0.0f;
    float spinRate = 0.0f;

    this->SampleSpin(p, initialSpin, spinRate);

    // The device wants RGBA where the sampler produced ARGB: swap red and blue, nothing else.
    if (GxCaps().m_colorFormat == GxCF_rgba) {
        uint8_t red = color.r;

        color.r = color.b;
        color.b = red;
    }

    float twinkleScale = s_particleTwinkle[twinkle] * this->m_twinkleSpan + this->m_twinkleMin;

    size.x *= twinkleScale;
    size.y *= twinkleScale;

    if (this->m_flags & 0x400) {
        size.x *= this->m_scale;
        size.y *= this->m_scale;
    }

    // The particle's centre, in view space.
    C3Vector centre = p.m_position * s_particleSpace;

    uint32_t flags = this->m_flags;

    // ---------------------------------------------------------------- the head quad
    if (flags & 0x4) {
        float u0 = 0.0f;
        float v0 = 0.0f;

        this->CellUvBase(headCell, u0, v0);

        float speedSq = p.m_velocity.x * p.m_velocity.x
            + p.m_velocity.y * p.m_velocity.y
            + p.m_velocity.z * p.m_velocity.z;

        if ((flags & 0x200000) && 0.00000023841858f < speedSq) {
            // VELOCITY-ALIGNED. The quad turns to lie along the particle's motion as it appears
            // on screen, and -- the part the decompilation lost entirely -- its WIDTH is scaled
            // by |xy|/|3D|, the fraction of the velocity that is visible. A particle flying
            // straight at the camera therefore narrows to nothing rather than swelling.
            C3Vector away = { -p.m_velocity.x, -p.m_velocity.y, -p.m_velocity.z };

            // The 3x3 ONLY: this is a direction, so the translation row must not apply.
            C3Vector screen;

            TransformDirection(screen, away, s_particleSpace);

            // The reference narrows the result to a C2Vector before measuring it, and measures
            // the FULL length off the 3-vector -- so the two lengths come from different objects.
            C2Vector flat;

            flat = screen;

            float flatSq = flat.x * flat.x + flat.y * flat.y;
            float fullSq = screen.x * screen.x + screen.y * screen.y + screen.z * screen.z;

            float inv = 0.00000023841858f < flatSq ? 1.0f / CMath::sqrt(flatSq) : 0.0f;

            float dirX = flat.x * inv;
            float dirY = flat.y * inv;

            float width = size.x;

            if (0.00000023841858f < inv) {
                width = size.x * (1.0f / CMath::sqrt(fullSq)) / inv;
            }

            for (uint32_t i = 0; i < 4; i++) {
                float a = s_particleCorner[i].x * width;
                float b = s_particleCorner[i].y * size.y;

                C3Vector position = {
                    a * dirX - b * dirY + centre.x,
                    a * dirY + b * dirX + centre.y,
                    centre.z
                };

                this->WriteVertex(cursor, position, color,
                                  s_particleCornerUv[i].x * this->m_cellWidth + u0,
                                  s_particleCornerUv[i].y * this->m_cellHeight + v0);
            }
        } else if (this->m_spin == 0.0f && this->m_spinVariation == 0.0f) {
            if (!(flags & 0x4000)) {
                // PLAIN BILLBOARD. The common case by a wide margin, and the only shape that
                // leaves Z alone.
                for (uint32_t i = 0; i < 4; i++) {
                    C3Vector position = {
                        s_particleCorner[i].x * size.x + centre.x,
                        s_particleCorner[i].y * size.y + centre.y,
                        centre.z
                    };

                    this->WriteVertex(cursor, position, color,
                                      s_particleCornerUv[i].x * this->m_cellWidth + u0,
                                      s_particleCornerUv[i].y * this->m_cellHeight + v0);
                }
            } else {
                // BASIS BILLBOARD: the corners laid out on the emitter's own two axes, which is
                // a 3D plane, so this one moves Z as well.
                for (uint32_t i = 0; i < 4; i++) {
                    float a = s_particleCorner[i].x * size.x;
                    float b = s_particleCorner[i].y * size.y;

                    C3Vector position = {
                        a * s_particleBasis.a0 + b * s_particleBasis.b0 + centre.x,
                        a * s_particleBasis.a1 + b * s_particleBasis.b1 + centre.y,
                        a * s_particleBasis.a2 + b * s_particleBasis.b2 + centre.z
                    };

                    this->WriteVertex(cursor, position, color,
                                      s_particleCornerUv[i].x * this->m_cellWidth + u0,
                                      s_particleCornerUv[i].y * this->m_cellHeight + v0);
                }
            }
        } else {
            float angle = p.m_age * spinRate + initialSpin;

            // Flag 0x10000 turns every OTHER particle the opposite way, using bit 5 of the
            // particle's address -- which alternates because the pool stride is 0x20. The same
            // trick as the twinkle hash, and it survives x64 for the same reason.
            if ((flags & 0x10000) && (reinterpret_cast<uintptr_t>(&p) & 0x20)) {
                angle = -angle;
            }

            if (flags & 0x4000) {
                // TUMBLED: the basis billboard, then turned about the emitter's tumble axis. The
                // reference rebuilds the rotation matrix PER CORNER rather than once per
                // particle -- four identical matrices -- and that is transcribed rather than
                // hoisted, because hoisting it is a change in arithmetic even if it looks like
                // one in cost only.
                for (uint32_t i = 0; i < 4; i++) {
                    C33Matrix rotation =
                        C33Matrix::RotationAroundAxis(angle, this->m_tumbleAxis, true);

                    float a = s_particleCorner[i].x * size.x;
                    float b = s_particleCorner[i].y * size.y;

                    float x = a * s_particleBasis.a0 + b * s_particleBasis.b0;
                    float y = a * s_particleBasis.a1 + b * s_particleBasis.b1;
                    float z = a * s_particleBasis.a2 + b * s_particleBasis.b2;

                    C3Vector position = {
                        rotation.a0 * x + rotation.a1 * y + rotation.a2 * z + centre.x,
                        rotation.b0 * x + rotation.b1 * y + rotation.b2 * z + centre.y,
                        rotation.c0 * x + rotation.c1 * y + rotation.c2 * z + centre.z
                    };

                    this->WriteVertex(cursor, position, color,
                                      s_particleCornerUv[i].x * this->m_cellWidth + u0,
                                      s_particleCornerUv[i].y * this->m_cellHeight + v0);
                }
            } else {
                // SPUN BILLBOARD.
                //
                // DIVERGED, and recorded rather than tagged: the reference computes both terms in
                // one pass through FUN_006f7a60, a polynomial approximation with its own range
                // reduction (FUN_005fe800). That returns SINE through its second argument and
                // cosine through its third -- settled by matching the first vertex the reference
                // writes against corner 0, (-1, 1), since the signs only work out one way round.
                // frozen uses the CRT's, which differs in the last bits and in nothing else.
                //
                // The reference also unrolls the four corners into longhand vertices built from
                // the same four products. Algebraically identical, because every corner is
                // (+-1, +-1), and the loop is the form a reader can check.
                float sine = 0.0f;
                float cosine = 0.0f;

                CMath::SinCos(angle, sine, cosine);

                for (uint32_t i = 0; i < 4; i++) {
                    float a = s_particleCorner[i].x * size.x;
                    float b = s_particleCorner[i].y * size.y;

                    C3Vector position = {
                        a * cosine - b * sine + centre.x,
                        a * sine + b * cosine + centre.y,
                        centre.z
                    };

                    this->WriteVertex(cursor, position, color,
                                      s_particleCornerUv[i].x * this->m_cellWidth + u0,
                                      s_particleCornerUv[i].y * this->m_cellHeight + v0);
                }
            }
        }
    }

    // ---------------------------------------------------------------- the tail quad
    if (!(flags & 0x8)) {
        return true;
    }

    float u1 = 0.0f;
    float v1 = 0.0f;

    this->CellUvBase(tailCell, u1, v1);

    // How far back the tail reaches. Flag 0x20000 shortens it for a young particle so a trail
    // grows out of the head rather than springing into existence at full length.
    float length = this->m_tailLength;

    if ((flags & 0x20000) && p.m_age < length) {
        length = p.m_age;
    }

    C3Vector away = { -p.m_velocity.x, -p.m_velocity.y, -p.m_velocity.z };

    C3Vector tail;

    TransformDirection(tail, away, s_particleSpace);

    tail.x *= length;
    tail.y *= length;
    tail.z *= length;

    // Narrowed the same way the head's velocity path narrows its direction.
    C2Vector flat;

    flat = tail;

    float flatSq = flat.x * flat.x + flat.y * flat.y;

    // 0x00aa2cec, and note it is NOT the 2^-22 used everywhere else in this file: a tail shorter
    // than about 0.028 of a screen unit is drawn as a plain quad instead of being stretched.
    if (flatSq < 0.0007716049440205097f) {
        for (uint32_t i = 0; i < 4; i++) {
            C3Vector position = {
                s_particleCorner[i].x * size.x + centre.x,
                s_particleCorner[i].y * size.y + centre.y,
                centre.z
            };

            this->WriteVertex(cursor, position, color,
                              s_particleCornerUv[i].x * this->m_cellWidth + u1,
                              s_particleCornerUv[i].y * this->m_cellHeight + v1);
        }

        return true;
    }

    // STRETCHED. Two corners at the particle and two at the far end of the tail, each pair pushed
    // apart along a perpendicular to the tail's screen direction.
    //
    // The perpendicular is (-py, px) where px and py are the direction scaled by size -- and the
    // pairing is crossed: px takes size.x while py takes size.y, so the offset is
    // (-dy*size.y, dx*size.x). That asymmetry is the reference's, and it is the kind of thing a
    // reader would "correct" on sight.
    float inv = 1.0f / CMath::sqrt(flatSq);

    float px = tail.x * size.x * inv;
    float py = tail.y * size.y * inv;

    C3Vector far_ = { centre.x + tail.x, centre.y + tail.y, centre.z + tail.z };

    C3Vector v[4] = {
        { centre.x - py, centre.y + px, centre.z },
        { centre.x + py, centre.y - px, centre.z },
        { far_.x - py,   far_.y + px,   far_.z   },
        { far_.x + py,   far_.y - px,   far_.z   }
    };

    for (uint32_t i = 0; i < 4; i++) {
        this->WriteVertex(cursor, v[i], color,
                          s_particleCornerUv[i].x * this->m_cellWidth + u1,
                          s_particleCornerUv[i].y * this->m_cellHeight + v1);
    }

    return true;
}

// Draw this emitter's particles.
//
// The bounds reset at the top is why the constructor initialises them inverted: the quad builder
// absorbs each particle as it emits one, so the box that comes out describes the frame that just
// drew rather than accumulating across frames.
//
// ref: FUN_0097ea60
void CM2ParticleEmitter::Draw(const C44Matrix* relativeTo, void* a3, int32_t batched) {
    this->m_boundsMin = { 3.4028234663852886e+38f, 3.4028234663852886e+38f,
                          3.4028234663852886e+38f };
    this->m_boundsMax = { -3.4028234663852886e+38f, -3.4028234663852886e+38f,
                          -3.4028234663852886e+38f };

    // Note this tests THIS emitter's live list, not the subtree's -- an emitter with no particles
    // of its own returns here without descending, even if a child has some. HasLiveParticles is
    // the subtree test and the element builder uses that one instead, which is what keeps a
    // parent's element alive for its children's sake.
    if (this->m_liveIndices.Count() == 0) {
        this->m_drawFlags &= ~0x1u;
        this->m_drawnCount = 0;

        return;
    }

    this->m_drawFlags = (this->m_drawFlags & ~0x1u) | (batched & 0x1);

    if (this->m_particleKind == 0) {
        // FUN_0097e730, the plain pool's quad builder -- 22 calls into the GX layer and not
        // ported. The model pool has no branch here at all: those particles draw as models.
        SysMsgPrintf(SYSMSG_ERROR,
                     "CM2ParticleEmitter::Draw: the quad builder is not ported (FUN_0097e730); "
                     "no particle geometry is emitted");

        (void)relativeTo;
        (void)a3;
    }
}

// Does this emitter's subtree hold any live particle?
//
// Short-circuits on the first one found, and does not count -- the caller only wants to know
// whether there is anything to draw at all.
//
// ref: FUN_0097b9e0
bool CM2ParticleEmitter::HasLiveParticles() const {
    if (this->m_liveIndices.Count() != 0) {
        return true;
    }

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        if (this->m_children[c]->HasLiveParticles()) {
            return true;
        }
    }

    return false;
}

// ref: FUN_0097ba30
uint32_t CM2ParticleEmitter::CountSpawnedModels() const {
    // Only an emitter on the model pool carries models of its own; the rest still have to be
    // walked, because a plain emitter can have model-carrying children.
    uint32_t count = this->m_particleKind == 1 ? this->m_liveIndices.Count() : 0;

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        count += this->m_children[c]->CountSpawnedModels();
    }

    return count;
}

// The `index`-th spawned model in this subtree.
//
// `index` is passed by reference and CONSUMED as the walk descends -- each emitter subtracts what
// it holds before handing the remainder to its children. That is what turns a tree into a flat
// enumeration without building a list, and it is why this cannot take the index by value.
//
// ref: FUN_0097ba70
CM2Model* CM2ParticleEmitter::FindSpawnedModel(uint32_t& index) const {
    if (this->m_particleKind == 1) {
        if (index < this->m_liveIndices.Count()) {
            return this->m_modelPool[this->m_liveIndices[index]].m_model;
        }

        index -= this->m_liveIndices.Count();
    }

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        CM2Model* model = this->m_children[c]->FindSpawnedModel(index);

        if (model) {
            return model;
        }
    }

    return nullptr;
}

// Age every live particle, then recurse into the children.
//
// `fromParent` is zero only when the emitter is being stepped on its own account. A child driven
// through its parent passes one and does NOT emit here -- the parent has already emitted on its
// behalf, from each of its own particles, in IntegrateAndSpawnChildren.
//
// ref: FUN_0097dd20
void CM2ParticleEmitter::Step(float dt, int32_t fromParent) {
    // The decompiler renders this guard `dt < 0.0 != (dt == 0.0)`, which is the x87
    // compare-and-branch pattern rather than anything meaningful. As instructions (0x97dd2c) it is
    // `fcom` + `fnstsw` + `testb $0x41, %ah` + `jnp`, and jnp on the C0|C3 mask is taken when
    // exactly one bit is set -- C0 for less-than, C3 for equal. So it returns on dt < 0 OR dt == 0,
    // and a zero-length frame does not emit. Written as `<=` so a NaN dt falls through, which is
    // what the instruction does too.
    if (dt <= 0.0f) {
        return;
    }

    if ((this->m_flags & 0x3) == 0x3
            || ((this->m_flags & 0x40) && (this->m_flags & 0x2))) {
        // vtable[0]. An earlier pass left this empty, guessing it refreshed placement; it is the
        // POOL SIZING, so leaving it out meant every emitter had zero slots and spawned nothing
        // however correct the rest was. The gate is the same one that decides whether this frame
        // emits at all, so the pool is sized exactly when it is about to be needed.
        this->PrepareStep();
    }

    if (fromParent == 0) {
        this->Emit(dt, this->m_placement);
    }

    // The reference guards on the LIVE COUNT (0x97ddac tests +0x50), not on the pool pointer. An
    // earlier pass here added a defensive null check on all three arrays, which was a divergence:
    // with the containers there are no pointers to check, and an empty live list is exactly what
    // this is for.
    // WHY THIS TERMINATES, because the loop below deliberately does not always advance `i`:
    // every path that leaves `i` alone shrinks the live count -- RetireParticle pops it, and
    // IntegrateAndSpawnChildren returns false only after calling RetireParticle itself. So the
    // bound falls on exactly the iterations that do not advance. Verified against frozen's
    // TSGrowableArray::SetCount, which assigns m_count unconditionally and therefore really does
    // shrink; if that ever became grow-only, this loop would hang rather than misbehave.
    //
    // And nothing here can reallocate the pool while `p` below is a live reference into it:
    // PrepareStep sizes the pool above the loop, and emission only pops the free list. A child's
    // Emit touches the CHILD's containers, not this one's.
    if (this->m_liveIndices.Count()) {
        // Two branches over the same loop, and the split is the whole reason +0xa8 exists. With no
        // lifespan variation every particle shares one lifetime, so the comparison is hoisted out;
        // with variation each particle's own draw has to be decoded inside it. The reference
        // writes both rather than always paying for the second.
        if (this->m_lifespanVariation == 0.0f) {
            float life = this->m_lifespan < 0.001f ? 0.001f : this->m_lifespan;

            for (uint32_t i = 0; i < this->m_liveIndices.Count();) {
                Particle& p = this->ParticleAt(this->m_liveIndices[i]);

                p.m_age += dt;

                if (p.m_age < life) {
                    if (this->IntegrateAndSpawnChildren(dt, p, i)) {
                        i++;
                    }
                } else {
                    this->RetireParticle(p, i);
                }
            }
        } else {
            for (uint32_t i = 0; i < this->m_liveIndices.Count();) {
                Particle& p = this->ParticleAt(this->m_liveIndices[i]);

                p.m_age += dt;

                if (p.m_age < this->ParticleLifespan(p)) {
                    if (this->IntegrateAndSpawnChildren(dt, p, i)) {
                        i++;
                    }
                } else {
                    this->RetireParticle(p, i);
                }
            }
        }
    }

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        // Children go through the substepper rather than straight to Step, so a child of a
        // fast-moving parent is still integrated in bounded slices.
        this->m_children[c]->Substep(dt, 1);
    }
}

// How many slots an emitter with this rate and lifespan needs.
//
// Rate times lifetime is the steady-state population; the 1.15 (0x009f23cc) is headroom over it.
// The reference truncates by saving the x87 control word, OR-ing 0xc00 in to select
// round-toward-zero, converting, and restoring -- a C cast truncates by definition, so the cast
// here is the whole of it.
static int32_t M2ParticleCapacityFor(const CM2ParticleEmitter& emitter) {
    float population = (emitter.m_lifespanVariation + emitter.m_lifespan)
        * (emitter.m_rateVariation + emitter.m_rate) * 1.15f;

    return static_cast<int32_t>(population);
}

// Size this emitter's pool and its children's. vtable[0].
//
// Step calls this before emitting, and without it an emitter has no slots and spawns nothing --
// which is the whole of why the wiring produced no particles until now.
//
// A child is sized by its OWN rate-times-lifetime product multiplied by the PARENT's count,
// because every parent particle can shed a trail of its own. That product is what the 4096 cap is
// there to bound; it is applied to the children only, not to this emitter.
//
// ref: FUN_0097edf0
void CM2ParticleEmitter::PrepareStep() {
    int32_t own = M2ParticleCapacityFor(*this);

    this->SetParticleCount(own);

    for (uint32_t c = 0; c < this->m_childCount; c++) {
        CM2ParticleEmitter* child = this->m_children[c];

        int32_t count = M2ParticleCapacityFor(*child) * own;

        if (count > 0x1000) {
            count = 0x1000;
        }

        child->SetParticleCount(count);
    }
}

// Grow the emitter to hold `count` particles, and put every new slot on the free list.
//
// This is what makes an emitter usable. Reserve grows the allocation and deliberately leaves the
// pool's count alone, so after it there is memory but no slots; the loop at the end is what turns
// memory into slots, by pushing each new index onto the FREE list. SpawnParticle pops from that
// list, so an emitter that has never been through here emits nothing at all -- silently, and no
// matter how correct the simulation downstream is.
//
// ref: FUN_0097e480
void CM2ParticleEmitter::SetParticleCount(uint32_t count) {
    // The reference writes the two pools as separate branches (0x97e495 and 0x97e505) that
    // differ only in which container they touch -- same Reserve, same SetCount, same two index
    // Reserves, same free-list fill. Written once here.
    uint32_t existing = this->m_particleKind == 0
        ? this->m_pool.Count()
        : this->m_modelPool.Count();

    if (existing >= count) {
        return;
    }

    if (this->m_particleKind == 0) {
        this->Reserve(count, this->m_pool.Count(), this->m_pool.Reserved());
        this->m_pool.SetCount(count);
    } else {
        this->Reserve(count, this->m_modelPool.Count(), this->m_modelPool.Reserved());
        this->m_modelPool.SetCount(count);
    }

    this->m_liveIndices.Reserve(count, 0);
    this->m_freeIndices.Reserve(count, 0);

    for (uint32_t slot = existing; slot != count; slot++) {
        this->m_freeIndices.Add(1, &slot);
    }
}

// Make room for `capacity` particles, rounded up to a power of two.
//
// Its four blocks are each `if (need + m_count > m_alloc) ReallocData(need + m_count)`, which is
// TSGrowableArray::Reserve(need, 0) inlined -- round = 0, so no chunk rounding. The pool and both
// index arrays grow together because a slot needs an entry in each.
//
// The rounding is the only real arithmetic, and it is NOT RoundToChunk (that one is a modulo). It
// takes 2n-1 and clears its lowest set bit until a single bit remains, leaving the highest: n = 5
// gives 9 = 0b1001 -> 8, n = 9 gives 17 -> 16. The test at the top short-circuits when n is
// already a power of two, which is why an exact power passes through untouched rather than
// doubling.
//
// ref: FUN_0097e3f0
void CM2ParticleEmitter::Reserve(uint32_t capacity, uint32_t used, uint32_t spare) {
    // The sole caller passes the pool's Count() and Reserved(), which sum to its m_alloc. So this
    // compares the rounded capacity against what is already allocated, not against what is live.
    uint32_t allocated = used + spare;
    uint32_t rounded = capacity;

    if (rounded & (rounded - 1)) {
        rounded = rounded + rounded - 1;

        while (rounded & (rounded - 1)) {
            rounded &= rounded - 1;
        }
    }

    if (allocated >= rounded) {
        return;
    }

    uint32_t need = rounded - allocated;

    if (this->m_particleKind == 0) {
        this->m_pool.Reserve(need, 0);
    } else {
        this->m_modelPool.Reserve(need, 0);
    }

    this->m_liveIndices.Reserve(need, 0);
    this->m_freeIndices.Reserve(need, 0);
}

// Split a frame into fixed 0.1s slices and step each one.
//
// ref: FUN_0097acb0
void CM2ParticleEmitter::Substep(float dt, int32_t fromParent) {
    // 0x009e3004 = 0.1, the slice; 0x009e30cc = 10.0, its reciprocal.
    const float SLICE = 0.1f;

    // A frame no longer than one slice needs no splitting, and neither does a negative one -- the
    // reference passes 0.0 in that case, which Step then rejects at its own guard.
    if (!(dt >= 0.0f) || !(SLICE < dt)) {
        this->m_substepDelta = this->m_frameDelta;

        this->Step(dt < 0.0f ? 0.0f : dt, fromParent);

        return;
    }

    float slices = floorf(dt * 10.0f);

    // The remainder is taken against the UNCAPPED count, before the lifespan cap below. That is
    // deliberate: when the cap bites, the emitter simulates less than the full frame rather than
    // stretching the slices, so a short-lived emitter is not over-integrated.
    float remainder = dt - slices * SLICE;

    float lifeCap = floorf(this->m_lifespan * 10.0f);

    if (lifeCap < slices) {
        slices = lifeCap;
    }

    // `fsubs 0.5; fistpl` at 0x97ad62. fistp rounds by the current x87 mode -- round-to-nearest,
    // ties-to-even -- so this is the usual float-to-int floor idiom. Applied to a value that is
    // ALREADY an exact integer it lands exactly on a tie every time, and ties-to-even means an odd
    // count comes back one lower: 3 slices become 2, 4 stay 4. nearbyintf reads the same rounding
    // mode, so it reproduces that rather than papering over it. Do not "fix" it to slices - 1 or
    // to slices; neither matches.
    int32_t full = static_cast<int32_t>(nearbyintf(slices - 0.5f));

    // The frame's movement is divided across every step, the remainder step included -- hence
    // full + 1. The reference converts that count as UNSIGNED (it adds 2^32 when the int is
    // negative, 0x009e23ac), so the cast is to uint32_t.
    float perStep = 1.0f / static_cast<float>(static_cast<uint32_t>(full + 1));

    this->m_substepDelta.x = this->m_frameDelta.x * perStep;
    this->m_substepDelta.y = this->m_frameDelta.y * perStep;
    this->m_substepDelta.z = this->m_frameDelta.z * perStep;

    // HAZARD, and it is the reference's, not an artefact of this port. A negative m_lifespan
    // makes lifeCap negative, which makes `slices` negative, which makes `full` negative -- and
    // this counts DOWN to zero, so it would run about four billion times. The reference decrements
    // and tests non-zero in exactly the same way (0x97add5), so a real client hangs on the same
    // data, which is the argument that no shipped model has a negative life track.
    //
    // Left faithful rather than clamped: clamping would be inventing behaviour to guard against
    // data there is no evidence exists. If a hang is ever seen inside particle stepping, look
    // here first and at whatever wrote m_lifespan.
    for (; full != 0; full--) {
        this->Step(SLICE, fromParent);
    }

    this->Step(remainder, fromParent);
}

// ref: FUN_00978da0
void CM2ParticleEmitter::SetZSource(float zSource) {
    this->m_zSource = zSource;

    if (fabsf(zSource) < 0.001f) {
        this->m_zSource = 0.0f;
    }
}
