#include "model/CM2ParticleEmitter.hpp"
#include <cmath>
#include <cstring>
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

// ref: FUN_00979740
void CM2ParticleEmitter::GroundSnapParticle(Particle& p) {
    if (!g_m2ParticleHeightQuery || !this->m_groundOffset) {
        return;
    }

    float groundZ = 0.0f;

    if (!g_m2ParticleHeightQuery(p.m_position, groundZ, g_m2ParticleHeightQueryContext)) {
        return;
    }

    // This particle's own lifespan, from the draw the creator stored in it. The floor is the same
    // 0.001 the emitter uses elsewhere and keeps the division below finite.
    float life = static_cast<float>(p.m_lifeVariation) * this->m_lifespanVariation + this->m_lifespan;

    if (life < 0.001f) {
        life = 0.001f;
    }

    C2Vector range;
    M2PartTrackEval2(range, *this->m_groundOffset, p.m_age / life);

    // The larger of the pair, which is what the reference's two-way compare picks.
    p.m_position.z = (range.y < range.x ? range.x : range.y) + groundZ;
}

// Fill one new particle.
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
    if (!this->m_freeCount || !this->m_pool || !this->m_liveIndices || !this->m_freeIndices) {
        return;
    }

    // Pop the last free slot and make it live. The reference reaches the same two arrays through a
    // growable-array pop and a link; the effect is this.
    uint32_t slot = this->m_freeIndices[--this->m_freeCount];

    this->m_liveIndices[this->m_liveCount++] = slot;

    this->CreateParticle(this->m_pool[slot], dt, placement);
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

        while (this->m_freeCount && n) {
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
        while (this->m_freeCount && n) {
            this->SpawnParticle(dt, placement);
            spawned++;
            n--;
        }
    } else {
        // Interpolated placement: lay each particle at a random point along the segment the
        // emitter travelled this frame, so a moving emitter draws a trail instead of a cluster.
        C3Vector cur = { placement.d0, placement.d1, placement.d2 };
        const C3Vector& prev = this->m_prevPosition;

        while (this->m_freeCount && n) {
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
            dx = p.m_position.x - this->m_position.x;
            rest = (p.m_position.y - this->m_position.y) * vyDt
                + (p.m_position.z - this->m_position.z) * vzDt;
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
// ref: FUN_00978da0
void CM2ParticleEmitter::SetZSource(float zSource) {
    this->m_zSource = zSource;

    if (fabsf(zSource) < 0.001f) {
        this->m_zSource = 0.0f;
    }
}
