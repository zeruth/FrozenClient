#include "model/CM2ParticleEmitter.hpp"
#include <cmath>
#include <cstring>

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
