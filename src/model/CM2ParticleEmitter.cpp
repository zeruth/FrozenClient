#include "model/CM2ParticleEmitter.hpp"
#include <cmath>
#include <cstring>
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
    if (!g_m2ParticleHeightQuery || !this->m_groundOffset) {
        return;
    }

    float groundZ = 0.0f;

    if (!g_m2ParticleHeightQuery(p.m_position, groundZ, g_m2ParticleHeightQueryContext)) {
        return;
    }

    float life = this->ParticleLifespan(p);

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
void CM2ParticleEmitter::RetireParticle(Particle& p, uint32_t liveIndex) {
    // The kill hook. The reference reaches it as vtable[3]; frozen has no subclass that overrides
    // it yet, so it is a direct call rather than a virtual -- the dispatch exists to let a derived
    // emitter release whatever its particles carry, and the plain pool carries nothing.
    (void)p;

    uint32_t slot = this->m_liveIndices[liveIndex];

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
    this->m_freeIndices[this->m_freeCount++] = slot;

    this->m_liveCount--;
    this->m_liveIndices[liveIndex] = this->m_liveIndices[this->m_liveCount];
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
        // The 0x40-byte pool's integrator (FUN_0097bdb0) updates the CM2Model each of its
        // particles carries and then delegates to the same arithmetic as the plain one. It is not
        // ported, and nothing in frozen allocates m_modelPool, so this cannot currently be
        // reached. Say so rather than integrating a model particle as a plain one: the pools have
        // different strides and reading one as the other walks off the end.
        SysMsgPrintf(SYSMSG_ERROR,
                     "CM2ParticleEmitter: model-pool particles are not integrated yet "
                     "(FUN_0097bdb0 unported); killing the particle instead of misreading it");

        alive = false;
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
        // vtable[0]: the reference's per-step hook, which a derived emitter uses to refresh its
        // placement before anything spawns. Nothing in frozen overrides it.
    }

    if (fromParent == 0) {
        this->Emit(dt, this->m_placement);
    }

    if (this->m_pool && this->m_liveIndices && this->m_freeIndices) {
        // Two branches over the same loop, and the split is the whole reason +0xa8 exists. With no
        // lifespan variation every particle shares one lifetime, so the comparison is hoisted out;
        // with variation each particle's own draw has to be decoded inside it. The reference
        // writes both rather than always paying for the second.
        if (this->m_lifespanVariation == 0.0f) {
            float life = this->m_lifespan < 0.001f ? 0.001f : this->m_lifespan;

            for (uint32_t i = 0; i < this->m_liveCount;) {
                Particle& p = this->m_pool[this->m_liveIndices[i]];

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
            for (uint32_t i = 0; i < this->m_liveCount;) {
                Particle& p = this->m_pool[this->m_liveIndices[i]];

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
