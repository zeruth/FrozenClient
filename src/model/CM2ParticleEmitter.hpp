#ifndef MODEL_C_M2_PARTICLE_EMITTER_HPP
#define MODEL_C_M2_PARTICLE_EMITTER_HPP

#include <cstdint>
#include "math/Types.hpp"
#include "model/M2Data.hpp"
#include "gx/Buffer.hpp"
#include "gx/Texture.hpp"
#include "storm/array/TSGrowableArray.hpp"
#include <tempest/Quaternion.hpp>
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
            // +0x1c: this particle's own draw against the emitter's lifespan variation, saturated
            // into fixed16 by the creator and read back by the ground-snap as
            // `lifespan + fixed * lifespanVariation`.
            fixed16 m_lifeVariation;
            // +0x1e: a raw random word the creator draws and stores unmodified.
            uint16_t m_randomTag;

            // NOTE, a small and deliberate divergence. This struct has no default member
            // initialisers, so TSGrowableArray::SetCount value-initialises a freshly grown slot
            // and every field starts at zero. The reference's SetCount zeroes only +0x04..+0x18 --
            // position and velocity -- and leaves age, lifeVariation and randomTag holding
            // whatever the allocation gave it.
            //
            // It shows in exactly one field: neither concrete creator writes m_lifeVariation, so
            // on a slot's FIRST use the reference reads allocator garbage where frozen reads 0.
            // After that both read the previous life's value and agree. Frozen's is the better
            // defined of the two and this is not worth "fixing" toward the reference.
        };

        // The 0x40-byte pool's element. Its first 0x20 bytes ARE a Particle -- the reference
        // extends the same layout rather than defining a second one, which is why the plain
        // integrator can be delegated to unchanged -- and the rest closes it at exactly 0x40:
        //
        //   +0x20  orientation      a quaternion, spun each step by the velocity below
        //   +0x30  angularVelocity  its magnitude IS the rotation rate, in radians per second
        //   +0x3c  model            the CM2Model this particle carries
        //
        // FUN_0097ba70 is what pins +0x3c: it reaches a spawned model as
        // `pool[index] * 0x40 + 0x3c`.
        struct ModelParticle : Particle {
            C4Quaternion m_orientation;
            C3Vector m_angularVelocity;
            CM2Model* m_model;
        };

        // Member variables. Offsets are the reference's.
        // +0x24: this emitter's own RNG state. Every emitter draws from its own rather than a
        // shared one, so two identical emitters side by side do not produce identical particles.
        //
        // GAP: the reference SEEDS it randomly. The base constructor (FUN_0097e150) ends by
        // building a 32-bit value out of two 16-bit rand() calls and seeding this with it, which
        // is what actually makes those two emitters differ -- a separate stream is not enough if
        // both start from the same number. Frozen starts every emitter at 0 because nothing
        // constructs one yet; whatever ports the constructor must carry the random seed over, or
        // every emitter in a scene will emit in lockstep.
        CRndSeed m_seed = CRndSeed(0);
        // +0x2c: the 0x20-byte pool, used when m_particleKind is 0. A TSGrowableArray, which is
        // what the reference has: TSBaseArray is {m_alloc, m_count, m_data} at +0x0/+0x4/+0x8 and
        // TSGrowableArray adds m_chunk at +0xc for 0x10 bytes total -- exactly the spacing between
        // this and the two index containers at +0x4c and +0x5c. The pointer earlier recorded at
        // +0x34 is this container's m_data.
        TSGrowableArray<Particle> m_pool;
        // +0x3c: the 0x40-byte pool, used when m_particleKind is not 0. Its element carries a
        // spawned model; see ModelParticle above. Getting this element type wrong is not a subtle
        // error -- at 0x20 rather than 0x40 every element past the first overlaps its neighbour.
        TSGrowableArray<ModelParticle> m_modelPool;
        // +0x20: which concrete emitter this is. The base constructor leaves it 0 and the plane
        // subclass's sets it to 1 (0x98132c), so it is a type tag rather than a state flag.
        uint32_t m_emitterType = 0;
        // +0x08: the fractional emission carry. A rate of 2.5 a second does not round to 2 or 3
        // -- the remainder stays here and is spent on a later frame.
        float m_emitCarry = 0.0f;
        // +0x4c: the indices of the live particles into whichever pool is in use. The step
        // swap-removes from this array as particles die, which is why it walks `i` forward only
        // when one survives. What earlier passes recorded as a separate m_liveCount at +0x50 is
        // this container's m_count.
        TSGrowableArray<uint32_t> m_liveIndices;
        // +0x5c: the slots not currently in use. Spawning pops one; the step pushes one back
        // when a particle dies. Every spawn loop in the reference stops the moment this is empty,
        // so a full emitter silently emits nothing rather than growing mid-frame -- growth happens
        // only in Reserve.
        TSGrowableArray<uint32_t> m_freeIndices;
        // +0x9c and +0xa0: emission rate and its variation, both written by the driver from the
        // model's animated tracks.
        float m_rate = 0.0f;
        float m_rateVariation = 0.0f;
        // +0x6c and +0x70: child emitters. FUN_0097ba30 counts a subtree by recursing through
        // these, so an emitter is a node rather than a leaf. The step walks the array at +0x70 as
        // `CM2ParticleEmitter*[]` -- 0x97df56 loads `(%ebx)` straight into ECX for a thiscall, and
        // the subtree counter at 0x97ba50 does the same, so two functions agree on the layout.
        //
        // The base constructor appears to contradict that with `movl $0x4, 0x6c(%esi)` near its
        // start. It does not: its last act before returning is `movl %edi, 0x6c(%esi)` with EDI
        // still zero from the top, so an emitter is born with no children and a null array. These
        // defaults already match; the 4 is not worth chasing a second time.
        uint32_t m_childCount = 0;
        CM2ParticleEmitter** m_children = nullptr;
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
        // Defaults to 0.1, from FUN_00979250 -- not zero.
        float m_variation = 0.1f;
        // +0xbc: the z source, written through a setter that zeroes anything under 0.001 rather
        // than storing it (FUN_00978da0). A z source that small is meant to be off, and leaving a
        // denormal there would divide badly downstream.
        float m_zSource = 0.0f;
        // +0xc0 through +0xcc: the spin parameters, copied straight out of the record.
        float m_initialSpin = 0.0f;
        float m_initialSpinVariation = 0.0f;
        float m_spin = 0.0f;
        float m_spinVariation = 0.0f;
        // +0x13c, +0x140, and +0x144/+0x148: the twinkle parameters. The scale arrives as a
        // CRange and is cached as a min and a SPAN, the same idiom the sphere emitter uses for
        // its radius range.
        float m_twinkleFps = 0.0f;
        float m_twinkleOnOff = 0.0f;
        float m_twinkleMin = 0.0f;
        float m_twinkleSpan = 0.0f;
        // +0x150 through +0x164: the tumble box, as three INTERLEAVED (min, span) pairs -- which
        // is why this is not two C3Vectors. Same caching idiom again.
        struct Range {
            float min;
            float span;
        };

        Range m_tumble[3] = {};
        // +0x168: drag. Zero disables it; otherwise each step takes `min(1, drag * dt)` of the
        // velocity away, which is an exponential decay sampled per step rather than per second.
        float m_drag = 0.0f;
        // +0x16c and +0x178: a constant acceleration applied only while a particle is younger than
        // m_windTime. M2Particle carries these as windVector and windTime.
        C3Vector m_wind;
        float m_windTime = 0.0f;
        // +0xd8 through +0xf0: pointers to five of the M2Particle's part-tracks, cached at
        // construction, plus the scale variation pair by value.
        //
        // +0xe0 was called m_groundOffset until 2026-09-24, because GroundSnapParticle is the
        // only thing that reads it and it uses it as a z offset above the terrain. It is the
        // SCALE track. The snap samples the particle's own size at its current age and lifts it
        // by the larger component, so a particle sits ON the ground instead of halfway through
        // it. The behaviour was right; the name was not, and a name like that is how a later
        // reader concludes the field is unused when the draw wants it too.
        // const because the emitter only ever samples them -- they point into the shared model
        // data, which no emitter owns or may modify.
        const M2PartTrack<C3Vector>* m_colorTrack = nullptr;
        const M2PartTrack<fixed16>* m_alphaTrack = nullptr;
        const M2PartTrack<C2Vector>* m_scaleTrack = nullptr;
        C2Vector m_scaleVariation = {};
        const M2PartTrack<uint16_t>* m_headCellTrack = nullptr;
        const M2PartTrack<uint16_t>* m_tailCellTrack = nullptr;
        // +0xf4 through +0x117: three RGB triples, each 0..255 held as floats, that REPLACE the
        // colour track's three keys when flag 0x10 is set -- the ParticleColor.dbc override.
        // SampleColor indexes this with the same lo/hi the track lookup produces, which only
        // works because the override is exactly three entries and so is the track.
        //
        // NOTHING SETS IT YET, so flag 0x10 is never on and SampleColor's override branch is
        // unreachable. Its setter is FUN_0097a990, which writes this AND a ramp through a pointer
        // at +0x11c; the getter is FUN_0097ab10, its exact inverse (verified byte for byte at
        // 0x97ab16..0x97abd8). CM2Model::InitializeLoaded calls the setter at 0x833f94 to inherit
        // the colours from the corresponding emitter on model30, and FUN_00825410 is a runtime
        // setter that matches on a particle id. The field is here so the branch can be written
        // truthfully rather than left out of the transcription.
        C3Vector m_colorOverride[3] = {};
        // +0x8c and +0x90: how many vertices and indices ONE particle costs. Not stored
        // parameters -- SetHeadTail derives them, four vertices and six indices per quad, one
        // quad each for the head and the tail. A particle drawing both costs 8 and 12. The draw
        // side sizes its batch from these.
        uint32_t m_verticesPerParticle = 0;
        uint32_t m_indicesPerParticle = 0;
        // +0xac: set alongside the head/tail flags by the same setter. Born 1.0.
        float m_tailLength = 1.0f;
        // +0x128: the texture the emitter's particles draw with, held as a counted reference.
        // DrawParticle resolves it through TextureGetGxTex and bails if it is null.
        HTEXTURE m_texture = nullptr;
        // +0xd0 and +0xd4: the emitter's material -- the GxBlend the particles draw with, and a
        // flags word derived alongside it. The element builder's pass selection reads the blend,
        // which is why it has to be the MAPPED value and not the file's blendMode byte.
        uint32_t m_blendMode = 0;
        uint32_t m_materialFlags = 0;
        // +0x0c, +0x10 and +0x14: the atlas cell geometry SetTextureGrid derives from the grid
        // below -- the shift for unpacking a cell index, and the cell's width and height in UV.
        // The constructor's defaults are this function's output for a 1x1 grid, which is how the
        // three were identified.
        uint32_t m_cellShift = 0;
        float m_cellWidth = 1.0f;
        float m_cellHeight = 1.0f;
        // +0x120 and +0x124: the texture tile grid. Their PRODUCT is what decides whether texture
        // animation is possible at all -- see SetTextureAnimated.
        // Both born 1, so the product is 1 and texture animation starts off -- which is what
        // SetTextureAnimated's guard is testing. Zeroes here would make that guard trivially true
        // in the wrong direction and leave a 1x1 emitter looking animated.
        uint32_t m_textureRows = 1;
        uint32_t m_textureCols = 1;
        // +0x130: the model's alpha, clamped to 0..1 by the driver before it arrives. Born
        // OPAQUE -- an emitter starting at zero alpha draws nothing until the driver writes it.
        float m_alpha = 1.0f;
        // +0x14c: scales the emitter velocity the update samples every 1/30s and hands to new
        // particles. Read only there.
        float m_velocitySampleScale = 0.0f;
        // +0x17c and +0x180: how much of the emitter's own movement this frame carries into its
        // particles, as a ramp against the emitter's speed -- `clamp01(m_followScale * speed +
        // m_followBase)`, applied to m_frameDelta under flag 0x80000. A stationary emitter gets
        // m_followBase; a fast one saturates at 1 and its particles are dragged along completely.
        float m_followBase = 0.0f;
        float m_followScale = 0.0f;
        // +0x134: the flag word everything branches on. Known bits, from the two functions that
        // test them: 0x1 and 0x2 together, or 0x40 with 0x2, make the step call its first virtual;
        // 0x200 suppresses the extra transform in placement; 0x800 and 0x80000 gate the drag and
        // the frame-delta scaling in the update. The driver raises 0x1 and 0x40 itself.
        //
        // Born as 6, not 0, and that matters: Step emits only when `(flags & 3) == 3`, the
        // constructor supplies the 0x2 and the driver raises the 0x1. Starting at zero means the
        // driver's bit never completes the pair and the emitter silently never emits. The 0x4 is
        // the head quad, so a new emitter draws one quad per particle.
        uint32_t m_flags = 0x6;
        // +0x184: the emitter's placement matrix -- and the emitter's POSITION is its translation
        // row. 0x184 + 0x30 is 0x1b4, which an earlier pass recorded as a separate m_position
        // field; it never was one. FOUR things agree: the step passes `this + 0x184` to Emit,
        // whose parameter is a matrix; m_origin below lands at exactly 0x184 + 0x40, immediately
        // past the matrix's sixteen floats; the integrate wrapper saves +0x1b4/+0x1b8/+0x1bc
        // as a unit, overwrites them with a particle's position and puts them back; and the base
        // constructor zeroes 0x150..0x1bc and then writes 1.0 to exactly +0x184, +0x198, +0x1ac
        // and +0x1c0 -- the diagonal of a C44Matrix based at 0x184, an identity written as one.
        //
        // So position and placement are one thing here, and writing the matrix moves the emitter
        // by construction.
        C44Matrix m_placement;
        // +0x1c4: where placement puts the emitter's own origin, from the caller's vector.
        C3Vector m_origin;
        // +0x1d0: last frame's position, which is what lets emission interpolate along the segment
        // travelled. The integrate wrapper also writes it on a CHILD emitter, from the parent
        // particle's pre-step position, so a trail follows the particle that sheds it.
        C3Vector m_prevPosition;
        // +0x1dc: the accumulated time the 0x800 branch of the update advances.
        float m_time = 0.0f;
        // +0x1ec: the scale, taken as the length of the placement matrix's first row.
        float m_scale = 1.0f;
        // +0x1e0: the velocity a new particle inherits. The integrate wrapper writes it into a
        // CHILD emitter from the parent particle's own velocity, which is how a trail keeps moving
        // with whatever shed it.
        C3Vector m_inheritedVelocity;
        // +0x138: a SECOND flags word, distinct from m_flags at +0x134. The constructor does
        // not touch it and only Draw and the quad builder read it; bit 0 is the batched-draw bit
        // Draw folds in from its caller.
        uint32_t m_drawFlags = 0;
        // +0x1c: how many particles the last draw emitted. Draw zeroes it when there is nothing
        // to draw.
        uint32_t m_drawnCount = 0;
        // +0x20c: the axis a tumbling particle turns about -- row 2 of the billboard basis,
        // normalised. Written by SetupDrawBasis when flag 0x4000 is set and read only by the
        // quad writer's tumble path, so it is draw state living on the emitter rather than a
        // property of it.
        C3Vector m_tumbleAxis = {};
        // +0x218 and +0x224: the emitter's bounds, born INVERTED -- min at +FLT_MAX and max at
        // -FLT_MAX (0x009ea8fc and 0x00a37f1c), the usual empty-box convention so that the first
        // point absorbed sets both corners. +0x230 is the last base field; the two concrete
        // subclasses put their own first field at +0x234, which fixes the base size at 0x234.
        C3Vector m_boundsMin = { 3.4028234663852886e+38f, 3.4028234663852886e+38f,
                                 3.4028234663852886e+38f };
        C3Vector m_boundsMax = { -3.4028234663852886e+38f, -3.4028234663852886e+38f,
                                 -3.4028234663852886e+38f };
        // +0x1f4: how far the emitter moved this frame, and +0x200 that delta divided across the
        // substeps the substepper chose.
        C3Vector m_frameDelta;
        C3Vector m_substepDelta;

        // Construct an emitter with the reference's defaults and a randomly seeded RNG.
        // ref: FUN_0097e150
        CM2ParticleEmitter();

        // NOT defaulted: the emitter holds a counted reference to its texture, and the pooled
        // buffer it lives in is freed wholesale, so nothing else would ever release it. One
        // leaked texture reference per emitter per model is enough to keep every particle
        // texture in the cache alive forever.
        virtual ~CM2ParticleEmitter();

        // The reference's vtable, recovered by scanning .rdata for runs of pointers into the
        // particle module and reading the strings beside them. The class is CParticleEmitter2 and
        // its file is ParticleSystem2.cpp; both names come from that region.
        //
        // There are exactly THREE vtables in the hierarchy: an abstract base at 0x00aa2cc0 whose
        // slots [6]..[9] are _purecall (0x0040baa5 aborts -- it is not a nullsub), and two
        // concrete subclasses at 0x00aa2d30 and 0x00aa2d5c that override [2], [4] and those four.
        //
        //   [ 0] +0x00  0x0097edf0  the per-step hook Step calls
        //   [ 1] +0x04  0x009799c0
        //   [ 2] +0x08  CreateParticle -- base 0x00979870, overridden by BOTH subclasses
        //   [ 3] +0x0c  0x00632050, a bare `retl $4`: the kill hook is a no-op in all three,
        //               which is why RetireParticle has nothing to call
        //   [ 4] +0x10  pure in the base
        //   [ 5] +0x14  0x0097d890, the deleting destructor
        //   [ 6] +0x18  SetWidth      \
        //   [ 7] +0x1c  SetLength      |  pure in the base; the driver FUN_008309c0 calls all
        //   [ 8] +0x20  SetLatitude    |  four from the model's animated tracks
        //   [ 9] +0x24  SetLongitude  /
        //   [10] +0x28  SetEmissionRate -- 0x0097bd80, NOT pure and not overridden
        //
        // Nothing here overrides [3] or [10], so those are plain members below.

        // Member functions
        // ref: FUN_00978da0
        void SetZSource(float zSource);

        // Slot [10]. The guard is the whole function: a non-positive rate is ignored rather than
        // stored, so a track that dips to zero leaves the last good rate in place instead of
        // stopping the emitter. ref: FUN_0097bd80
        void SetEmissionRate(float rate);

        // Slots [6]..[9], pure virtual in the reference. Both concrete subclasses are unported,
        // so these report rather than aborting the way _purecall does.
        virtual void SetWidth(float width);
        virtual void SetLength(float length);
        virtual void SetLatitude(float latitude);
        virtual void SetLongitude(float longitude);

        // Advance one particle of the plain pool by `dt`. Returns false when the particle should
        // be killed rather than kept. ref: FUN_00979bb0
        bool IntegrateParticle(Particle& particle, float dt) const;

        // Choose which of the two quads each particle draws, and size a particle's geometry
        // from that. ref: FUN_00978d00
        void SetHeadTail(int32_t head, int32_t tail, float tailLength, int32_t flag20000);

        // Cache a CRange as a min and a span. ref: FUN_0097ac00
        void SetTwinkleScale(const CRange& range);

        // Take the emitter's material and a counted reference to its texture. The material is
        // a two-dword pair, blend then flags, which the reference builds as a local at its call
        // site. ref: FUN_00978bf0
        void SetMaterial(uint32_t blendMode, uint32_t materialFlags, HTEXTURE texture);

        // Set the texture atlas grid. Both must be non-zero powers of two; anything else is
        // reported and nothing is stored. ref: FUN_00978c70
        void SetTextureGrid(uint32_t rows, uint32_t cols);

        // Turn texture animation on, if the tile grid has more than one cell to animate over.
        // ref: FUN_00978e30
        void SetTextureAnimated(int32_t animated);

        // Drive the emitter for one frame: measure the camera distance, place this emitter and
        // its children, derive what the particles inherit from the emitter's own motion, and
        // substep. This is the entry point the model calls. ref: FUN_0097eb10
        void Update(float dt, const C44Matrix& matrix, const C3Vector& cameraPosition,
                    const C44Matrix* relativeTo);

        // Set the emitter's transform and origin for this frame. When `relativeTo` is given and
        // the emitter is not in emitter space (flag 0x200), the stored transform is re-expressed
        // relative to it. ref: FUN_0097ac20
        void Place(const C44Matrix& matrix, const C3Vector& origin, const C44Matrix* relativeTo);

        // Make room for `capacity` particles, rounded UP to a power of two. `used` and `spare`
        // are the pool's count and its unused allocation, which sum to its total allocation -- so
        // this grows only when the rounded capacity exceeds what is already allocated. Grows the
        // pool and both index arrays together, since a slot needs an entry in each.
        // ref: FUN_0097e3f0
        void Reserve(uint32_t capacity, uint32_t used, uint32_t spare);

        // Size this emitter's pool, and its children's, from the rate and lifespan they
        // currently hold. This is vtable[0], the hook Step calls before emitting.
        // ref: FUN_0097edf0
        void PrepareStep();

        // Grow the emitter to hold `count` particles and put every new slot on the free list.
        // Nothing downstream works until this has run: SpawnParticle pops from that list, so an
        // emitter that has never been through here silently emits nothing. ref: FUN_0097e480
        void SetParticleCount(uint32_t count);

        // Split `dt` into fixed 0.1s slices and step each, so a fast-moving emitter integrates
        // in bounded increments rather than one long jump. ref: FUN_0097acb0
        void Substep(float dt, int32_t fromParent);

        // The particle in `slot`, from whichever pool is in use. The reference picks between
        // the two inline at every site that touches one (0x97ddc0 in Step, 0x97d844 in
        // SpawnParticle, 0x97dbc0 in the integrate wrapper); this is those four copies as one.
        Particle& ParticleAt(uint32_t slot);

        // Work out the matrices the quad writer draws through, and leave them in the three
        // file-scope globals the reference uses for the same purpose. `view` is the view matrix
        // the caller saved off the device. ref: FUN_0097a390
        void SetupDrawBasis(const C44Matrix* relativeTo, const C44Matrix& view);

        // This particle's colour at normalised age `t`, as a CImVector with alpha left at 255
        // for the caller to overwrite. ref: FUN_009795d0
        void SampleColor(CImVector& out, float t) const;

        // This particle's two spin terms: the angle it was born at, and the rate it turns.
        // The draw combines them as `age * rate + initial`. ref: FUN_0097a130
        void SampleSpin(const Particle& p, float& initialSpin, float& spinRate) const;

        // Everything the quad writer needs about one particle's appearance, sampled at its own
        // normalised age. ref: FUN_00979e90
        void SampleAppearance(const Particle& p, CImVector& color, C2Vector& size,
                              uint32_t& headCell, uint32_t& tailCell) const;

        // Spin one model particle's orientation by `dt`, then integrate it like a plain one.
        // ref: FUN_0097bdb0
        bool IntegrateModelParticle(ModelParticle& particle, float dt) const;

        // Where the quad builder writes one vertex's four attributes, and how far each moves
        // for the next vertex. The reference keeps this as nine bare dwords on the fill's stack:
        // four pointers, four strides, and the running count. It is a struct here because the
        // writer advances all four in lockstep and a named field is the difference between
        // reading that loop and decoding it.
        struct VertexCursor {
            // Three floats. GxVA_Position.
            float* m_position = nullptr;
            // Three floats. GxVA_Normal, and every particle vertex in a frame gets the SAME
            // value -- the camera-facing normal the basis setup parks in a global.
            float* m_normal = nullptr;
            // One packed dword. GxVA_Color0.
            uint32_t* m_color = nullptr;
            // Two floats. GxVA_TexCoord0.
            float* m_texCoord = nullptr;

            uint32_t m_positionStride = 0;
            // ZERO when the emitter is unlit -- see SetupVertexCursor.
            uint32_t m_normalStride = 0;
            uint32_t m_colorStride = 0;
            uint32_t m_texCoordStride = 0;

            // How many vertices have been written. The fill divides it by m_verticesPerParticle
            // to get the drawn count.
            uint32_t m_count = 0;
        };

        // Point a cursor block at a mapped vertex buffer. ref: FUN_0097a2e0
        void SetupVertexCursor(char* base, EGxVertexBufferFormat format, VertexCursor& cursor) const;

        // Write one vertex through a cursor, absorb it into the emitter's running bounds, and
        // advance all four pointers. Inline at every one of its ~20 sites in the reference.
        void WriteVertex(VertexCursor& cursor, const C3Vector& position, const CImVector& color,
                         float u, float v);

        // The UV of a texture-atlas cell's top-left corner.
        void CellUvBase(uint32_t cell, float& u, float& v) const;

        // Write one particle's quads -- a head, a tail, or both. Returns false when the particle
        // twinkled off this frame and nothing was written. ref: FUN_0097be80
        bool WriteParticleVertices(const Particle& p, VertexCursor& cursor);

        // Draw this emitter's particles. `relativeTo` is the matrix the owning model is placed
        // relative to, and `batched` becomes bit 0 of m_drawFlags. ref: FUN_0097ea60
        void Draw(const C44Matrix* relativeTo, void* a3, int32_t batched);

        // Does this emitter's SUBTREE hold any live particle? The gate the whole draw path
        // opens on -- CM2Scene's element builder refuses to emit an element without it.
        // ref: FUN_0097b9e0
        bool HasLiveParticles() const;

        // How many spawned models this emitter's subtree holds. Only emitters using the model
        // pool contribute their own; every emitter recurses into its children. ref: FUN_0097ba30
        uint32_t CountSpawnedModels() const;

        // The `index`-th spawned model in this subtree, or null. `index` is consumed as the walk
        // descends, which is how the recursion stays a flat enumeration. ref: FUN_0097ba70
        CM2Model* FindSpawnedModel(uint32_t& index) const;

        // Age every live particle by `dt`, integrating or killing each, then recurse into the
        // children. `fromParent` is non-zero when a parent is driving this emitter, and suppresses
        // emission because the parent has already emitted on its behalf. ref: FUN_0097dd20
        void Step(float dt, int32_t fromParent);

        // Integrate one particle and, if it survived, let every child emitter emit from where it
        // now is. Returns false when the particle was killed and retired, so the caller must not
        // advance its index. ref: FUN_0097db80
        bool IntegrateAndSpawnChildren(float dt, Particle& particle, uint32_t liveIndex);

        // Kill one live particle: the kill hook, then swap-remove it from the live array and
        // return its slot to the free list.
        void RetireParticle(Particle& particle, uint32_t liveIndex);

        // This particle's own lifespan, floored. See the definition for why the multiply order
        // matters. ref: inlined at 0x979771 and 0x97de9b
        float ParticleLifespan(const Particle& particle) const;

        // Take a free slot and fill it. Does nothing when the emitter is full.
        // ref: FUN_0097d820
        void SpawnParticle(float dt, const C44Matrix& placement);

        // Spawn whatever this frame's rate calls for. `placement` is the emitter's world transform,
        // and this may move its translation while spawning -- see the definition. ref: FUN_0097d8c0
        void Emit(float dt, C44Matrix& placement);

        // Slot [2]. VIRTUAL in the reference -- FUN_0097d820 dispatches through vtable[2] and
        // FUN_00979870 has no direct callers at all -- so the dispatch is kept even though frozen
        // has only the one implementation. See the DIVERGENCE note at the definition.
        // ref: FUN_00979870
        virtual void CreateParticle(Particle& particle, float dt, const C44Matrix& placement);

        // Drop a particle onto the ground, offset by the range sampled at its age. Does nothing
        // when no height query is registered, which is also what the reference does when the query
        // fails. ref: FUN_00979740
        void GroundSnapParticle(Particle& particle);

        // A launch speed for one new particle: the emitter's speed, scaled by its variation and a
        // fresh signed draw. ref: FUN_009792d0
        float RandomSpeed();
};

// The plane emitter: vtable 0x00aa2d30, the first of CParticleEmitter2's two concrete
// subclasses. It lays particles on a rectangle and launches them into a latitude/longitude cone.
//
// Named by behaviour -- only the base's name appears in the binary's strings -- and it is the
// plane one because its creator places particles across two independent extents on the emitter's
// z = 0 plane. The other subclass (0x00aa2d5c) caches min/max differences instead and is not
// ported.
//
// Its four fields are identified by the driver: FUN_008309c0 calls vtable slots [6], [7], [8] and
// [9] with the model's width, length, latitude and longitude tracks, and those slots store here.
class CM2ParticleEmitterPlane : public CM2ParticleEmitter {
    public:
        // +0x234 and +0x238: the rectangle's FULL extents. The creator multiplies each by a
        // signed random and by a half, so a particle lands within +/- half of each.
        float m_width = 0.0f;
        float m_length = 0.0f;
        // +0x23c and +0x240: the launch cone, in radians. Latitude is the POLAR angle from +Z.
        float m_latitude = 0.0f;
        float m_longitude = 0.0f;

        CM2ParticleEmitterPlane();

        void SetWidth(float width) override;
        void SetLength(float length) override;
        void SetLatitude(float latitude) override;
        void SetLongitude(float longitude) override;

        void CreateParticle(Particle& particle, float dt, const C44Matrix& placement) override;
};

// The sphere emitter: vtable 0x00aa2d5c, the second concrete subclass. Particles are placed on
// a shell between an inner and an outer radius.
//
// It is the sphere one because its two extents are a RADIUS RANGE rather than two independent
// widths: the setters keep min and max and cache their difference, and the creator draws a radius
// uniformly across that span. Its emitter type tag is 2 where the plane's is 1, and CM2Model's
// construction loop switches on `M2Particle::emitterType` (+0x29) to pick between them.
class CM2ParticleEmitterSphere : public CM2ParticleEmitter {
    public:
        // +0x234, +0x238 and +0x23c: the radius range and its cached span. The span is the one
        // field the constructor leaves alone -- both setters recompute it, and the driver calls
        // both, so it is written before anything reads it.
        float m_minRadius = 0.0f;
        float m_maxRadius = 0.0f;
        float m_radiusSpan = 0.0f;
        // +0x240 and +0x244: the launch cone, in radians.
        float m_latitude = 0.0f;
        float m_longitude = 0.0f;

        CM2ParticleEmitterSphere();

        // The two radius setters are deliberately asymmetric: each writes its own end and then
        // recomputes the cached span from the other.
        void SetWidth(float minRadius) override;
        void SetLength(float maxRadius) override;
        void SetLatitude(float latitude) override;
        void SetLongitude(float longitude) override;

        void CreateParticle(Particle& particle, float dt, const C44Matrix& placement) override;
};

// Map a GxBlend back to the M2 blend index. The exact inverse of M2ParticleBlendToGx below,
// and the reason the round trip exists: an emitter stores the GxBlend, and the material key
// DrawParticle builds wants the M2 index again. ref: FUN_0081ca20
uint32_t M2BlendIndexFromGx(uint32_t gxBlend);

// Map an M2Particle blend mode onto the GxBlend the emitter draws with, and fold the
// accompanying bit into `flags`. From the reference's jump table at 0x008344dc.
//
// Mode 3 is the one worth knowing: it becomes GxBlend_NoAlphaAdd, additive IGNORING source alpha,
// where mode 4 becomes GxBlend_Add which weights by it. Collapsing the two is an easy mistake and
// was live in the particle stand-in until 2026-09-24.
uint32_t M2ParticleBlendToGx(uint8_t blendMode, uint32_t& flags);

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

// One fixed16 part track, linearly interpolated at `t` and converted to a float.
// ref: FUN_009794f0
float M2PartTrackEvalAlpha(const M2PartTrack<fixed16>& track, float t);

// One uint16 part track -- a texture cell index -- interpolated at `t` and rounded. Free rather
// than a member even though every caller loads ecx first: the reference's is `retl $0x8` and
// never reads it.  ref: FUN_00979560
uint32_t M2PartTrackEvalCell(const M2PartTrack<uint16_t>& track, float t);

// Create the ONE index buffer every particle quad in the world draws through, and take a
// reference to it. Refcounted; the matching release (FUN_009791e0) is not ported. Called from
// CM2Cache::Initialize, which is the reference's only caller. ref: FUN_00979170
void M2ParticleIndexBufferCreate();

// Fill the particle twinkle table: 128 random floats in [0, 1), seeded from rand() so no two
// runs and no two emitters blink alike. The reference does this inline in CM2Cache::Initialize
// (0x81c240), which is the only place that may call it -- the table is read every frame by every
// emitter and refilling it mid-run would make every particle in the world blink at once.
void M2ParticleInitTwinkleTable();

// How far the camera is from the emitter being updated. The reference keeps this in a global
// (0x00dce68c) that its per-frame update writes before emission reads it, rather than passing it
// down. Emission is the only consumer.
extern float g_m2ParticleCameraDistance;

// The terrain height query the ground-snap calls through, and its context. The reference keeps
// both as globals (0x00dce8c8 and 0x00dce8c4) that the world installs, and every call site checks
// the return rather than assuming a hit -- so leaving this unset is a supported state, not a hole:
// emitters simply do not snap to ground until the world registers one.
typedef int32_t (*M2ParticleHeightQuery)(const C3Vector& at, float& groundZ, void* context);

extern M2ParticleHeightQuery g_m2ParticleHeightQuery;
extern void* g_m2ParticleHeightQueryContext;

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
