#ifndef MODEL_C_M2_PARTICLE_EMITTER_HPP
#define MODEL_C_M2_PARTICLE_EMITTER_HPP

#include <cstdint>
#include "math/Types.hpp"
#include "model/M2Data.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Random.hpp>
#include <tempest/Vector.hpp>

class CM2Model;

// The reference's particle emitter object: one per M2Particle, held by CM2Model in an array of
// POINTERS at +0x2c4, beside the animated state at +0x2c0 that M2ModelParticle now mirrors.
//
// Ported field by field rather than invented, from the six functions that make up the emitter:
//
//   FUN_008309c0  CM2Model::AnimateParticleEmitter -- the driver, which writes speed, gravity,
//                 variation, lifespan, alpha and the flag word
//   FUN_0097eb10  the per-frame update: position history, frame delta, drag
//   FUN_0097ac20  placement -- writes the world origin and the scale
//   FUN_0097acb0  the fixed-timestep substepper, which splits the frame delta
//   FUN_0097dd20  the step itself: age every live particle, integrate or kill
//   FUN_0097ba30 / FUN_0097ba70  walk the emitter tree and its spawned models
//
// Every offset below is agreed by at least two of those independently. The driver writes lifespan
// at +0xa4 and the step reads its particle lifetime from +0xa4; the driver writes the flag word at
// +0x134 and the step branches on +0x134. That agreement is what makes the map a reading rather
// than a guess.
//
// NOT YET WIRED. frozen still simulates particles through the stand-in in src/world/ParticleFx.cpp,
// which works. This class is the structure the reference's own simulation needs, landed ahead of
// that simulation so the two can be swapped in one reviewable change rather than a rewrite. The
// members carry reference offsets in their comments so the remaining functions can be ported
// against them directly.
class CM2ParticleEmitter {
    public:
        // One live particle. Two sizes exist and the emitter picks between them with m_particleKind
        // below: the plain pool is 0x20 bytes per particle, and the pool used when an emitter
        // spawns MODELS is 0x40, carrying a CM2Model* at its +0x3c. FUN_0097ba70 reaches a spawned
        // model as `pool[index] * 0x40 + 0x3c`, which is what identifies both the stride and the
        // field.
        //
        // The age is the FIRST float of either: the step reads `*p`, adds the delta and compares
        // the result against the lifespan before deciding to integrate or kill.
        struct Particle {
            // +0x00. The step reads this, adds the delta and compares the result against the
            // emitter's lifespan before deciding to integrate or kill.
            float m_age;
            // +0x04 and +0x10, confirmed twice over: the integrator moves these, and the wrapper
            // above it saves +0x04..+0x0c as the pre-step position and hands +0x10..+0x18 to a
            // child emitter as its inherited velocity.
            C3Vector m_position;
            C3Vector m_velocity;
        };

        // Member variables. Offsets are the reference's.
        // +0x24: this emitter's own RNG state. Every emitter draws from its own rather than a
        // shared one, so two identical emitters side by side do not produce identical particles.
        CRndSeed m_seed = CRndSeed(0);
        // +0x34: the 0x20-byte pool, used when m_particleKind is 0
        Particle* m_pool = nullptr;
        // +0x44: the 0x40-byte pool, used otherwise -- its particles carry a spawned model
        Particle* m_modelPool = nullptr;
        // +0x50: how many particles are live, and +0x54 the indices of those particles into
        // whichever pool is in use. The step swap-removes from this array as particles die, which
        // is why it walks `i` forward only when one survives.
        uint32_t m_liveCount = 0;
        uint32_t* m_liveIndices = nullptr;
        // +0x6c: child emitters. FUN_0097ba30 counts a subtree by recursing through these, so an
        // emitter is a node rather than a leaf.
        uint32_t m_childCount = 0;
        // +0x98: which pool is in use. Zero means the plain 0x20-byte one.
        uint32_t m_particleKind = 0;
        // +0xa4 and +0xa8: the base lifespan and its variation. The driver writes the base from
        // the model's animated life track.
        //
        // +0xa8 was read as "a mode the step branches on", which is all the step can tell -- it
        // tests the field against zero to pick between two aging paths. FUN_00979740 shows what it
        // holds: it computes `m_lifespan + fixed16(particle[+0x1c]) * m_lifespanVariation`, so the
        // zero test is not a mode at all. It means "no variation, every particle dies at the same
        // age, take the simple path". The creator draws a signed random, saturates it into fixed16
        // and stores it at particle +0x1c -- exactly the value that expression reads back.
        float m_lifespan = 0.0f;
        float m_lifespanVariation = 0.0f;
        // +0xb0, +0xb4, +0xb8: written every frame by the driver from the animated tracks.
        float m_speed = 0.0f;
        float m_gravity = 0.0f;
        float m_variation = 0.0f;
        // +0xbc: the z source, written through a setter that zeroes anything under 0.001 rather
        // than storing it (FUN_00978da0). A z source that small is meant to be off, and leaving a
        // denormal there would divide badly downstream.
        float m_zSource = 0.0f;
        // +0x168: drag. Zero disables it; otherwise each step takes `min(1, drag * dt)` of the
        // velocity away, which is an exponential decay sampled per step rather than per second.
        float m_drag = 0.0f;
        // +0x16c and +0x178: a constant acceleration applied only while a particle is younger than
        // m_windTime. M2Particle carries these as windVector and windTime.
        C3Vector m_wind;
        float m_windTime = 0.0f;
        // +0x130: the model's alpha, clamped to 0..1 by the driver before it arrives.
        float m_alpha = 0.0f;
        // +0x134: the flag word everything branches on. Known bits, from the two functions that
        // test them: 0x1 and 0x2 together, or 0x40 with 0x2, make the step call its first virtual;
        // 0x200 suppresses the extra transform in placement; 0x800 and 0x80000 gate the drag and
        // the frame-delta scaling in the update. The driver raises 0x1 and 0x40 itself.
        uint32_t m_flags = 0;
        // +0x1b4 / +0x1c4 / +0x1d0: three positions. The update copies +0x1b4 into +0x1d0 before
        // anything else, so +0x1d0 is LAST frame's and +0x1b4 is this frame's; placement writes
        // +0x1c4 from the caller's vector.
        C3Vector m_position;
        C3Vector m_origin;
        C3Vector m_prevPosition;
        // +0x1dc: the accumulated time the 0x800 branch of the update advances.
        float m_time = 0.0f;
        // +0x1ec: the scale, taken as the length of the placement matrix's first row.
        float m_scale = 1.0f;
        // +0x1f4: how far the emitter moved this frame, and +0x200 that delta divided across the
        // substeps the substepper chose.
        C3Vector m_frameDelta;
        C3Vector m_substepDelta;

        // Member functions
        // ref: FUN_00978da0
        void SetZSource(float zSource);

        // Advance one particle of the plain pool by `dt`. Returns false when the particle should
        // be killed rather than kept. ref: FUN_00979bb0
        bool IntegrateParticle(Particle& particle, float dt) const;

        // A launch speed for one new particle: the emitter's speed, scaled by its variation and a
        // fresh signed draw. ref: FUN_009792d0
        float RandomSpeed();
};

// A uniform draw in [-1, 1] from one RNG call.
//
// The mantissa becomes a float in [1, 2) and the SIGN BIT chooses which way it is subtracted from
// 2.0, so one draw gives both magnitude and sign with no division. The 2.0 is the constant at
// 0x00a4040c -- worth stating because 1.5 is the value that "looks right" there and would silently
// halve every randomised quantity in the particle system.
float M2ParticleRandSigned(CRndSeed& seed);

// The index of the key whose time brackets `t` from below, by binary search over a part track's
// fixed16 times. ref: FUN_00979330
uint32_t M2PartTrackFindKey(const M2Array<fixed16>& times, float t);

// The two keys bracketing `t` and the ratio between them. Tracks of two and three keys -- most of
// them -- take closed-form paths rather than the search. ref: FUN_009793b0
float M2PartTrackRatio(uint32_t& lo, uint32_t& hi, const M2Array<fixed16>& times,
                       uint32_t valueCount, float t);

// One 2-float part track, linearly interpolated at `t`. ref: FUN_00979480
void M2PartTrackEval2(C2Vector& out, const M2PartTrack<C2Vector>& track, float t);

// Saturating float to fixed16. Anything at or beyond +/-1 clamps to the exact endpoints rather
// than wrapping -- 0x7FFF and 0x8001, which are +1 and -1 exactly under fixed16's 1/32767 scaling.
//
// The negative endpoint is 0x8001 and NOT 0x8000, which is the value a naive clamp to INT16_MIN
// would pick. 0x8000 is -32768/32767 = -1.00003, outside the range every consumer assumes.
// ref: FUN_00978ad0
void M2ParticleToFixed16(fixed16& out, float value);

// A uniform direction on the unit sphere. z is drawn flat in [-1, 1] and the azimuth flat over the
// full turn, which is the correct sampling -- taking z from a cosine would crowd the poles.
// ref: FUN_004c1680
void M2ParticleRandomUnitVector(C3Vector& out, CRndSeed& seed);

#endif
