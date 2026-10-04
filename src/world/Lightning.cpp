#include "world/Lightning.hpp"
#include "db/Db.hpp"
#include "gx/Buffer.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Device.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Transform.hpp"
#include "ui/game/CGCamera.hpp"
#include "world/CWorldScene.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/random/CRandom.hpp>
#include <common/Handle.hpp>
#include <storm/Error.hpp>
#include <storm/Memory.hpp>
#include <cmath>
#include <cstring>

// Particulates.cpp
float PolySin(float x);

CLightningSystem* g_lightningSystem;

namespace {

// The bolts' random stream (0x00dcecf8).
CRndSeed s_seed(0);

// The frame of reference the spine builders share (0x00dcec98 onward): a perpendicular, the
// direction itself and a second perpendicular for the whole bolt, the same three for the segment
// being built, and the wave and arc offsets.
C3Vector s_perp0;           // 0x00dcec98
C3Vector s_dir;             // 0x00dceca4
C3Vector s_perp1;           // 0x00dcecb0
C3Vector s_segPerp0;        // 0x00dcecbc
C3Vector s_segDir;          // 0x00dcecc8
C3Vector s_segPerp1;        // 0x00dcecd4
C3Vector s_wave;            // 0x00dcece0
C3Vector s_arc;             // 0x00dcecec

// The normal every vertex shares (0x00dcec88), built on first use.
C3Vector s_normal = { 0.0f, 0.0f, 1.0f };

// [0, 1): the mantissa of a random dword with the exponent of 1.0, less 1.0.
float Random01() {
    uint32_t r = CRandom::uint32(s_seed);
    uint32_t bits = (r & 0x7FFFFF) | 0x3F800000;
    float f;
    memcpy(&f, &bits, 4);

    return f - 1.0f;
}

// (-1, 1): the same mantissa, its sign taken from the dword's top bit.
float RandomSigned() {
    uint32_t r = CRandom::uint32(s_seed);
    uint32_t bits = (r & 0x7FFFFF) | 0x3F800000;
    float f;
    memcpy(&f, &bits, 4);

    return static_cast<int32_t>(r) < 0 ? 2.0f - f : f - 2.0f;
}

// ref: FUN_009a8e80
float RandomRange(float lo, float hi) {
    return lo + Random01() * (hi - lo);
}

// ref: FUN_009a9260
// A perpendicular to `dir` in the horizontal plane (or straight along y when `dir` is vertical),
// and a second one perpendicular to both, scaled down by |dir|.
void Perpendiculars(C3Vector& perp, const C3Vector& dir, C3Vector& perp2) {
    perp.x = dir.y;
    perp.z = 0.0f;

    if (fabsf(perp.x) >= 2.384185791015625e-07f) {
        perp.y = -dir.x;

        float inv = 1.0f / sqrtf(perp.x * perp.x + perp.y * perp.y);
        perp.x *= inv;
        perp.y *= inv;
        perp.z = inv * 0.0f;
    } else if (dir.x <= 0.0f) {
        perp.y = 1.0f;
    } else {
        perp.y = -1.0f;
    }

    float inv = 1.0f / (sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z) + 9.99999993922529e-09f);

    perp2.x = inv * (perp.y * dir.z - perp.z * dir.y);
    perp2.y = (dir.x * perp.z - perp.x * dir.z) * inv;
    perp2.z = (perp.x * dir.y - perp.y * dir.x) * inv;
}

bool IsUnset(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);

    return bits == 0xFFFFFFFF;
}

void Grow(CAaBox& box, const C3Vector& p) {
    if (p.x < box.b.x) { box.b.x = p.x; }
    if (p.y < box.b.y) { box.b.y = p.y; }
    if (p.z < box.b.z) { box.b.z = p.z; }
    if (box.t.x < p.x) { box.t.x = p.x; }
    if (box.t.y < p.y) { box.t.y = p.y; }
    if (box.t.z < p.z) { box.t.z = p.z; }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// CLightning
// ------------------------------------------------------------------------------------------------

// ref: FUN_009a97b0
CLightning::CLightning() {
    this->m_start = { 0.0f, 0.0f, 0.0f };
    this->m_end = { 0.0f, 0.0f, 0.0f };
    this->m_color.value = 0;
    this->m_bounds = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
}

// ref: FUN_009a9860
CLightning::~CLightning() {
    if (this->m_texture) {
        HandleClose(this->m_texture);
    }
}

// ref: FUN_009a8cb0
void CLightning::SetTexture(HTEXTURE texture) {
    if (this->m_texture) {
        HandleClose(this->m_texture);
    }

    this->m_texture = HandleDuplicate(texture);
}

// ref: FUN_009a8ec0
void CLightning::Restart() {
    auto rec = this->m_rec;

    this->m_flags &= ~0x5u;
    this->m_waveAngle = rec->m_minWaveAngle + Random01() * (rec->m_maxWaveAngle - rec->m_minWaveAngle);
    this->m_waveSpin = rec->m_minWaveSpin + Random01() * (rec->m_maxWaveSpin - rec->m_minWaveSpin);

    if ((rec->m_flags & 0x400) == 0) {
        this->m_wavePhase = Random01() * 6.2831854820251465f;
    } else {
        this->m_wavePhase = rec->m_wavePhase;
    }

    this->m_arcAngle = rec->m_minArcAngle + Random01() * (rec->m_maxArcAngle - rec->m_minArcAngle);
    this->m_arcSpin = rec->m_minArcSpin + Random01() * (rec->m_maxArcSpin - rec->m_minArcSpin);
    this->m_flickerTimer = rec->m_minFlickerOnDuration
        + Random01() * (rec->m_maxFlickerOnDuration - rec->m_minFlickerOnDuration);
    this->m_pulse = 0.0f;

    if (rec->m_pulseSpeed < 0.0f) {
        this->m_flags |= 0x8;
    }

    this->m_randomTexOffset = Random01();
}

// ref: FUN_009a90c0
// A joint pushed a random amount off the line in the horizontal plane, wandering at the record's
// speed. A bolt being rebuilt (bit 0 set) waits up to the longest interval before the joint moves;
// a new one waits somewhere in the record's range.
void CLightning::NewJoint(LightningJoint& joint, float radius) {
    float a = RandomSigned();
    float b = RandomSigned();

    joint.m_offset = { b * radius, 0.0f, radius * a };

    float c = RandomSigned();
    float d = RandomSigned();
    float speed = this->m_rec->m_jointMoveSpeed;

    joint.m_velocity = { speed * d, 0.0f, speed * c };

    if (this->m_flags & 0x1) {
        joint.m_moveTimer = Random01() * this->m_rec->m_maxDurationBetweenJoints;
    } else {
        float lo = this->m_rec->m_minDurationBetweenJoints;
        float hi = this->m_rec->m_maxDurationBetweenJoints;
        joint.m_moveTimer = lo + Random01() * (hi - lo);
    }
}

// ref: FUN_009a9350
// A point `t` along the current segment from `base`, pushed off the line by the joint's smoothed
// offset in the segment's two perpendiculars.
void CLightning::PlaceJoint(C3Vector& out, const C3Vector& base, float t, LightningJoint& joint, float scale) {
    C3Vector s;

    if (IsUnset(joint.m_smoothed.x)) {
        s = joint.m_offset;
    } else {
        float k = this->m_rec->m_jointSmoothness;
        float inv = 1.0f - k;

        s.x = joint.m_offset.x * inv + joint.m_smoothed.x * k;
        s.y = joint.m_offset.y * inv + joint.m_smoothed.y * k;
        s.z = joint.m_offset.z * inv + joint.m_smoothed.z * k;
    }

    joint.m_smoothed = s;

    out.x = (s_segPerp0.x * s.x + s_segPerp1.x * s.z) * scale + s_segDir.x * t;
    out.y = (s_segPerp0.y * s.x + s_segPerp1.y * s.z) * scale + s_segDir.y * t;
    out.z = (s_segPerp0.z * s.x + s_segPerp1.z * s.z) * scale + s_segDir.z * t;

    out.x = base.x + out.x;
    out.y = base.y + out.y;
    out.z = out.z + base.z;
}

// ref: FUN_009a9490
// The pulse: a bright band running down the bolt, with a fade-in ahead of it and a fade-out
// behind. Writes the alpha of every vertex pair and says which run of the strip is lit.
void CLightning::BuildPulse(int32_t* first, int32_t* count) {
    auto rec = this->m_rec;

    if ((rec->m_flags & 0x20) == 0) {
        *first = 0;
        *count = static_cast<int32_t>(this->m_vertices.Count());

        return;
    }

    float segment = sqrtf((this->m_end.z - this->m_start.z) * (this->m_end.z - this->m_start.z)
        + (this->m_end.y - this->m_start.y) * (this->m_end.y - this->m_start.y)
        + (this->m_end.x - this->m_start.x) * (this->m_end.x - this->m_start.x))
        / static_cast<float>(static_cast<int32_t>(this->m_points.Count()) - 1);

    float pulseEnd = this->m_pulse - rec->m_pulseFadeLength;
    float onEnd = pulseEnd - rec->m_pulseOnLength;
    float fadeStart = onEnd - rec->m_pulseFadeLength;
    float fadeIn = 255.0f / (onEnd - fadeStart);
    float alpha = static_cast<float>(this->m_color.a) * 0.003921568859368563f;

    *first = 0;

    int32_t colors = static_cast<int32_t>(this->m_colors.Count());
    float d = 0.0f;
    int32_t i = 0;

    for (; i < colors; i += 2) {
        if (fadeStart <= d) {
            if (onEnd <= d) {
                if (d < pulseEnd) {
                    this->m_colors[i].a = this->m_color.a;
                } else {
                    if (this->m_pulse <= d) {
                        this->m_colors[i].a = 0;
                        this->m_colors[i + 1].a = 0;
                        *count = (i + 1) - *first;

                        return;
                    }

                    this->m_colors[i].a = static_cast<uint8_t>(static_cast<int32_t>(lrintf(
                        (this->m_pulse - d) * alpha * (255.0f / (this->m_pulse - pulseEnd)))));
                }
            } else {
                this->m_colors[i].a = static_cast<uint8_t>(static_cast<int32_t>(lrintf((d - fadeStart) * alpha * fadeIn)));
            }
        } else {
            this->m_colors[i].a = 0;
            *first = i;
        }

        d += segment;
        this->m_colors[i + 1].a = this->m_colors[i].a;
    }

    *count = i - *first;
}

// ref: FUN_009a9900
void CLightning::SetRecord(const SpellChainEffectsRec* rec) {
    this->m_rec = rec;

    this->m_color.a = rec->m_alpha;
    this->m_color.r = rec->m_red;
    this->m_color.g = rec->m_green;
    this->m_color.b = rec->m_blue;

    if ((this->m_rec->m_flags & 0x20) == 0) {
        for (uint32_t i = 0; i < this->m_colors.Count(); i++) {
            this->m_colors[i] = this->m_color;
        }
    }

    this->m_flags &= ~0x30u;

    this->Restart();
}

// ref: FUN_009a9980
void CLightning::ComputeArcAndWave() {
    s_dir = { this->m_end.x - this->m_start.x, this->m_end.y - this->m_start.y, this->m_end.z - this->m_start.z };
    Perpendiculars(s_perp0, s_dir, s_perp1);

    auto rec = this->m_rec;

    if (fabsf(rec->m_waveHeight) >= 2.384185791015625e-07f && (rec->m_flags & 0x8) == 0) {
        float c = CGCamera::SineEase(this->m_waveAngle) * rec->m_waveHeight;
        float s = PolySin(this->m_waveAngle) * rec->m_waveHeight;

        s_wave.x = s_perp0.x * s + s_perp1.x * c;
        s_wave.y = s_perp0.y * s + s_perp1.y * c;
        s_wave.z = s_perp0.z * s + c * s_perp1.z;
    }

    if (fabsf(rec->m_arcHeight) >= 2.384185791015625e-07f) {
        float c = CGCamera::SineEase(this->m_arcAngle) * rec->m_arcHeight;
        float s = PolySin(this->m_arcAngle) * rec->m_arcHeight;

        s_arc.x = s_perp0.x * s + s_perp1.x * c;
        s_arc.y = s_perp0.y * s + s_perp1.y * c;
        s_arc.z = s_perp0.z * s + c * s_perp1.z;
    }
}

// ref: FUN_009a9b30
// The wave, enveloped to zero at both ends. Bit 3 spins it round the line (a helix); otherwise it
// swings in the one plane ComputeArcAndWave chose.
void CLightning::ApplyWave(C3Vector& point, float t, float length) {
    auto rec = this->m_rec;

    float centred = t - 0.5f;
    float envelope = 1.0f - centred * centred * 4.0f;
    float phase = rec->m_waveFreq * t * length + this->m_wavePhase;

    if (rec->m_flags & 0x8) {
        float amplitude = rec->m_waveHeight * envelope;
        float s = PolySin(phase) * amplitude;
        float c = CGCamera::SineEase(phase) * amplitude;

        point.x = point.x + s_perp0.x * c + s_perp1.x * s;
        point.y = point.y + s_perp0.y * c + s_perp1.y * s;
        point.z = s_perp0.z * c + s * s_perp1.z + point.z;

        return;
    }

    float v = sinf(phase) * envelope;

    point.x = point.x + s_wave.x * v;
    point.y = s_wave.y * v + point.y;
    point.z = s_wave.z * v + point.z;
}

// ref: FUN_009a9ca0
// The plain spine: the two ends, and every joint between placed along the whole line.
void CLightning::BuildSpine() {
    int32_t last = static_cast<int32_t>(this->m_joints.Count()) - 1;

    this->m_points[0] = this->m_start;
    this->m_points[last] = this->m_end;

    this->ComputeArcAndWave();

    s_segPerp0 = s_perp0;
    s_segDir = s_dir;
    s_segPerp1 = s_perp1;

    for (int32_t i = 1; i < last; i++) {
        this->PlaceJoint(this->m_points[i], this->m_points[0],
                         static_cast<float>(i) * (1.0f / static_cast<float>(last)),
                         this->m_joints[i], 1.0f);
    }
}

// ref: FUN_009a9dc0
// The jointed spine, in three passes: the major joints along the whole line at the major scale;
// then the minor joints along each major segment at the minor scale; then every remaining joint
// along each minor segment at full scale.
void CLightning::BuildJointedSpine() {
    int32_t last = static_cast<int32_t>(this->m_joints.Count()) - 1;

    this->m_points[0] = this->m_start;
    this->m_points[last] = this->m_end;

    this->ComputeArcAndWave();

    auto rec = this->m_rec;
    auto points = &this->m_points[0];

    auto segmentFrom = [&](int32_t from, int32_t to) {
        s_segDir = { points[to].x - points[from].x, points[to].y - points[from].y, points[to].z - points[from].z };
        Perpendiculars(s_segPerp0, s_segDir, s_segPerp1);
    };

    segmentFrom(0, last);

    int32_t minor = rec->m_jointsPerMinorJoint;
    int32_t major = rec->m_minorJointsPerMajorJoint * minor;
    float inv = 1.0f / static_cast<float>(last);

    // Major joints.
    for (int32_t i = major; i < last; i += major) {
        this->PlaceJoint(points[i], points[0], static_cast<float>(i) * inv, this->m_joints[i], rec->m_majorJointScale);
    }

    // Minor joints, each major segment in turn.
    int32_t next = major < last ? major : last;
    C3Vector base = points[0];

    segmentFrom(0, next);

    float step = static_cast<float>(minor) / static_cast<float>(next);
    float t = step;
    int32_t segmentEnd = last;

    for (int32_t j = minor; j < last; j += minor) {
        if (j == next) {
            next = j + major >= last ? last : j + major;
            base = points[j];

            segmentFrom(j, next);

            step = static_cast<float>(minor) / static_cast<float>(next - j);
            t = step;
        } else {
            this->PlaceJoint(points[j], base, t, this->m_joints[j], rec->m_minorJointScale);
            t += step;
        }

        segmentEnd = minor;
    }

    // Every other joint, each minor segment in turn.
    base = points[0];

    segmentFrom(0, segmentEnd);

    step = 1.0f / static_cast<float>(segmentEnd);
    t = step;

    for (int32_t k = 1; k < last; k++) {
        if (k == segmentEnd) {
            segmentEnd = k + minor >= last ? last : k + minor;
            base = points[k];

            segmentFrom(k, segmentEnd);

            step = 1.0f / static_cast<float>(segmentEnd - k);
            t = step;
        } else {
            this->PlaceJoint(points[k], base, t, this->m_joints[k], 1.0f);
            t += step;
        }
    }
}

// ref: FUN_009aa210
// Rebuild the strip and draw it. `elapsed` is what the owner's update callback is handed; the
// system passes the bolt's index here, as the reference does (0x009ab24a pushes ESI, the index),
// so the callback sees the index's bits.
void CLightning::Render(float elapsed, const C3Vector& cameraPos) {
    this->m_flags &= ~0x30u;

    if ((this->m_flags & 0x2) == 0 || (this->m_flags & 0x4) != 0) {
        return;
    }

    if (this->m_update) {
        C3Vector start = this->m_start;
        C3Vector end = this->m_end;

        this->m_update(this->m_updateParam, elapsed, &start, &end);

        this->m_start = start;
        this->m_end = end;
    }

    if ((this->m_rec->m_flags & 0x2) == 0) {
        this->BuildSpine();
    } else {
        this->BuildJointedSpine();
    }

    auto rec = this->m_rec;
    auto verts = &this->m_vertices[0];
    int32_t points = static_cast<int32_t>(this->m_points.Count());

    verts[0] = { verts[0].x * 0.0f, verts[0].y * 0.0f, verts[0].z * 0.0f };
    verts[1] = { verts[1].x * 0.0f, verts[1].y * 0.0f, verts[1].z * 0.0f };

    C3Vector prev = this->m_points[0];

    this->m_bounds.b = prev;
    this->m_bounds.t = prev;

    bool hasWave = rec->m_waveHeight != 0.0f;
    bool hasArc = rec->m_arcHeight != 0.0f;

    float length = sqrtf((this->m_end.y - this->m_start.y) * (this->m_end.y - this->m_start.y)
        + (this->m_end.z - this->m_start.z) * (this->m_end.z - this->m_start.z)
        + (this->m_end.x - this->m_start.x) * (this->m_end.x - this->m_start.x));

    float width = rec->m_width;
    float invSegments = 1.0f / static_cast<float>(points - 1);

    auto bend = [&](C3Vector& p, float t) {
        if (hasWave) {
            this->ApplyWave(p, t, length);
        }

        if (hasArc) {
            float centred = t - 0.5f;
            float envelope = 1.0f - centred * centred * 4.0f;

            p.x = s_arc.x * envelope + p.x;
            p.y = p.y + envelope * s_arc.y;
            p.z = p.z + envelope * s_arc.z;
        }
    };

    if ((rec->m_flags & 0x80) == 0) {
        // Camera-facing: each segment's side is perpendicular to the segment and to the eye, and a
        // vertex pair takes the average of the two segments it joins.
        uint32_t pairs = static_cast<uint32_t>(points * 2 - 2);
        float t = invSegments;
        uint32_t offset = 0;

        for (uint32_t k = 2; k <= pairs; k += 2, offset += 2) {
            C3Vector p = this->m_points[k >> 1];

            bend(p, t);

            C3Vector toCam = {
                (p.x + prev.x) * 0.5f - cameraPos.x,
                (p.y + prev.y) * 0.5f - cameraPos.y,
                (p.z + prev.z) * 0.5f - cameraPos.z
            };

            C3Vector seg = { p.x - prev.x, p.y - prev.y, p.z - prev.z };
            C3Vector side = {
                seg.y * toCam.z - seg.z * toCam.y,
                seg.z * toCam.x - toCam.z * seg.x,
                toCam.y * seg.x - seg.y * toCam.x
            };

            float len = sqrtf(side.y * side.y + side.x * side.x + side.z * side.z);

            if (0.0010000000474974513f < len) {
                float inv = 1.0f / len;
                side.x *= inv;
                side.y *= inv;
                side.z *= inv;
            }

            side.x *= width;
            side.y *= width;
            side.z *= width;

            C3Vector& a = verts[offset];
            C3Vector& b = verts[offset + 1];

            a.x += (side.x + prev.x) * 0.5f;
            a.y += (side.y + prev.y) * 0.5f;
            a.z += (side.z + prev.z) * 0.5f;

            b.x += (prev.x - side.x) * 0.5f;
            b.y += (prev.y - side.y) * 0.5f;
            b.z += (prev.z - side.z) * 0.5f;

            verts[offset + 2] = { (side.x + p.x) * 0.5f, (side.y + p.y) * 0.5f, (side.z + p.z) * 0.5f };
            verts[offset + 3] = { (p.x - side.x) * 0.5f, (p.y - side.y) * 0.5f, (p.z - side.z) * 0.5f };

            Grow(this->m_bounds, a);
            Grow(this->m_bounds, b);

            prev = p;
            t += invSegments;
        }
    } else {
        // Flat: one side vector for the whole bolt, perpendicular to the line and to the eye at its
        // middle.
        C3Vector mid = {
            (this->m_end.x + this->m_start.x) * 0.5f - cameraPos.x,
            (this->m_end.y + this->m_start.y) * 0.5f - cameraPos.y,
            0.5f * (this->m_end.z + this->m_start.z) - cameraPos.z
        };

        C3Vector dir = { this->m_end.x - this->m_start.x, this->m_end.y - this->m_start.y, this->m_end.z - this->m_start.z };
        C3Vector side = {
            dir.y * mid.z - dir.z * mid.y,
            dir.z * mid.x - mid.z * dir.x,
            mid.y * dir.x - dir.y * mid.x
        };

        float len = sqrtf(side.x * side.x + side.y * side.y + side.z * side.z);

        if (0.0010000000474974513f < len) {
            float inv = 1.0f / len;
            side.x *= inv;
            side.y *= inv;
            side.z *= inv;
        }

        side.x *= width;
        side.y *= width;
        side.z *= width;

        float t = 0.0f;

        for (uint32_t k = 0; k < static_cast<uint32_t>(points * 2); k += 2) {
            C3Vector p = this->m_points[k >> 1];

            bend(p, t);

            verts[k] = { side.x + p.x, side.y + p.y, side.z + p.z };
            verts[k + 1] = { p.x - side.x, p.y - side.y, p.z - side.z };

            Grow(this->m_bounds, verts[k]);
            Grow(this->m_bounds, verts[k + 1]);

            t += invSegments;
        }
    }

    // The two ends are pinned to the bolt's ends.
    verts[1] = this->m_points[0];
    verts[0] = verts[1];
    verts[points * 2 - 1] = this->m_points[points - 1];
    verts[points * 2 - 2] = verts[points * 2 - 1];

    Grow(this->m_bounds, verts[points * 2 - 2]);

    int32_t first = 0;
    int32_t count = 0;
    this->BuildPulse(&first, &count);

    if (first == 0) {
        this->m_flags |= 0x10;
    }

    if (static_cast<int32_t>(this->m_vertices.Count()) <= first + count) {
        this->m_flags |= 0x20;
    }

    if (count <= 2 || CWorldScene::BoxOutsideFrustum(this->m_bounds)) {
        return;
    }

    // The texture runs along the bolt, scrolling with the record's speed; bit 8 tiles it by the
    // bolt's length, and bit 9 starts the tiling from the far end.
    uint32_t texFlags = rec->m_flags & 0x100;

    C44Matrix texMatrix;
    texMatrix.a0 = texFlags ? length / rec->m_textureLength : 1.0f;

    float scroll = this->m_randomTexOffset + this->m_texOffset;

    if (texFlags && (rec->m_flags & 0x200)) {
        scroll = length / rec->m_textureLength + scroll;
    }

    texMatrix.d0 = -scroll;

    GxXformSet(GxXform_Tex0, texMatrix);

    CImVector emissive = this->m_color;
    emissive.a = 0xFF;
    GxRsSet(GxRs_MatEmissive, emissive.value);
    GxRsSet(GxRs_DepthWrite, rec->m_blendMode == 2 ? 1 : 0);
    GxRsSet(GxRs_BlendingMode, rec->m_blendMode);
    GxRsSet(GxRs_AlphaRef, CGxDevice::s_alphaRef[rec->m_blendMode]);

    auto gxTex = TextureGetGxTex(this->m_texture, 0, nullptr);

    if (!gxTex) {
        return;
    }

    g_theGxDevicePtr->RsSet(GxRs_Texture0, gxTex);
    GxRsSet(GxRs_Unk61, 1);

    GxPrimLockVertexPtrs(this->m_vertices.Count(), &this->m_vertices[0], 0xc, &s_normal, 0,
                         &this->m_colors[0], 4, nullptr, 0, &this->m_texCoords[0], 8, nullptr, 0);
    GxDrawLockedElements(GxPrim_TriangleStrip, static_cast<uint32_t>(count), &this->m_indices[first]);
    GxPrimUnlockVertexPtrs();
}

// ref: FUN_009aaf10
void CLightning::SetJointCount(uint32_t count) {
    if (count == this->m_joints.Count()) {
        return;
    }

    uint32_t old = this->m_joints.Count();

    this->m_joints.SetCount(count);

    for (uint32_t i = old; i < count; i++) {
        this->m_joints[i] = LightningJoint();
    }
}

// ref: FUN_009ab2e0
// Size the spine to the bolt -- a fixed count when bit 0 says so, else one joint per average
// segment plus the ends -- and give every new joint its first offset.
void CLightning::BuildJoints(float length, float radius) {
    auto rec = this->m_rec;
    int32_t count;

    if ((rec->m_flags & 0x1) == 0) {
        count = static_cast<int32_t>(static_cast<int32_t>(length / rec->m_avgSegLen)) + 2;
    } else {
        count = rec->m_jointCount;
    }

    count++;

    if (count > 1000) {
        count = 1000;
    } else if (count < 2) {
        count = 2;
    }

    int32_t start = (this->m_flags & 0x1) ? static_cast<int32_t>(this->m_joints.Count()) : 0;

    this->SetJointCount(static_cast<uint32_t>(count));
    this->m_points.SetCount(static_cast<uint32_t>(count));

    for (int32_t i = start; i < count; i++) {
        this->NewJoint(this->m_joints[i], radius);

        uint32_t unset = 0xFFFFFFFF;
        memcpy(&this->m_joints[i].m_smoothed.x, &unset, 4);
    }

    this->m_flags |= 0x1;
}

// ref: FUN_009ab3b0
void CLightning::Update(float elapsed) {
    if ((this->m_flags & 0x2) == 0) {
        return;
    }

    this->m_flickerTimer -= elapsed;

    if ((this->m_flags & 0x4) == 0) {
        if ((this->m_rec->m_flags & 0x10) && this->m_flickerTimer <= 0.0f) {
            this->m_flickerTimer = RandomRange(this->m_rec->m_minFlickerOffDuration, this->m_rec->m_maxFlickerOffDuration);
            this->m_flags |= 0x4;
        }
    } else {
        if (0.0f < this->m_flickerTimer) {
            return;
        }

        this->Restart();
    }

    auto rec = this->m_rec;

    // _CIfmod: the scroll wraps at one texture.
    this->m_texOffset = rec->m_texCoordScale != 0.0f
        ? fmodf(rec->m_texCoordScale * elapsed + this->m_texOffset, 1.0f)
        : 0.0f;

    float length = sqrtf((this->m_end.z - this->m_start.z) * (this->m_end.z - this->m_start.z)
        + (this->m_end.y - this->m_start.y) * (this->m_end.y - this->m_start.y)
        + (this->m_end.x - this->m_start.x) * (this->m_end.x - this->m_start.x));

    if (10000.0f < length) {
        length = 10000.0f;
    }

    float radius = rec->m_noiseScale * length + rec->m_jointOffsetRadius;

    this->BuildJoints(length, radius);

    uint32_t joints = this->m_joints.Count();
    uint32_t vertexCount = joints * 2;
    float denom = static_cast<float>(static_cast<int32_t>(vertexCount) - 2);

    if (vertexCount != this->m_texCoords.Count()) {
        this->m_vertices.SetCount(vertexCount);
        this->m_texCoords.SetCount(vertexCount);
        this->m_colors.SetCount(vertexCount);
        this->m_indices.SetCount(vertexCount);

        for (uint32_t k = 0; k < vertexCount; k += 2) {
            float u = static_cast<float>(k) * (1.0f / denom);

            this->m_texCoords[k] = { u, 0.0f };
            this->m_texCoords[k + 1] = { u, 1.0f };
            this->m_colors[k] = this->m_color;
            this->m_colors[k + 1] = this->m_color;
            this->m_indices[k] = static_cast<uint16_t>(k);
            this->m_indices[k + 1] = static_cast<uint16_t>(k + 1);
        }

        this->m_texCoords[1] = { 0.0f, 0.5f };
        this->m_texCoords[0] = { 0.0f, 0.5f };
        this->m_texCoords[joints * 2 - 1] = { 1.0f, 0.5f };
        this->m_texCoords[joints * 2 - 2] = { 1.0f, 0.5f };
    }

    for (uint32_t i = 1; i < joints; i++) {
        auto& joint = this->m_joints[i];

        joint.m_moveTimer -= elapsed;

        if ((rec->m_flags & 0x4) == 0 || 0.0f <= joint.m_moveTimer) {
            joint.m_offset.x = elapsed * joint.m_velocity.x + joint.m_offset.x;
            joint.m_offset.y = joint.m_velocity.y * elapsed + joint.m_offset.y;
            joint.m_offset.z = joint.m_velocity.z * elapsed + joint.m_offset.z;
        } else {
            this->NewJoint(joint, radius);
        }
    }

    this->m_waveAngle = this->m_waveSpin * elapsed + this->m_waveAngle;
    this->m_wavePhase = this->m_wavePhase - rec->m_waveSpeed * elapsed;
    this->m_arcAngle = this->m_arcSpin * elapsed + this->m_arcAngle;

    if (rec->m_flags & 0x20) {
        float pulse = elapsed * rec->m_pulseSpeed + this->m_pulse;
        this->m_pulse = pulse;

        if ((this->m_flags & 0x8) || (rec->m_flags & 0x40)) {
            float span = rec->m_pulseFadeLength + rec->m_pulseFadeLength + rec->m_pulseOnLength;

            if (length <= pulse - span) {
                this->Restart();
                this->m_flags &= ~0x8u;

                return;
            }

            if (pulse < 0.0f) {
                this->Restart();
                this->m_flags &= ~0x8u;
                this->m_pulse = span + length;

                return;
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------
// CLightningSystem
// ------------------------------------------------------------------------------------------------

// ref: FUN_009aae80
CLightningSystem::CLightningSystem() {
}

// ref: FUN_009aaea0
CLightningSystem::~CLightningSystem() {
    for (uint32_t i = this->m_slots.Count(); i != 0; i--) {
        auto bolt = reinterpret_cast<CLightning*>(this->m_slots[i - 1] & ~static_cast<uintptr_t>(1));

        if (bolt) {
            bolt->~CLightning();
            SMemFree(bolt, __FILE__, __LINE__, 0);
        }
    }
}

// ref: FUN_009aafb0
int32_t CLightningSystem::Create(CLightning::UPDATEFN update, void* param, const SpellChainEffectsRec* rec) {
    int32_t index;

    if (this->m_free.Count() == 0) {
        index = static_cast<int32_t>(this->m_slots.Count());

        void* mem = SMemAlloc(sizeof(CLightning), "..\\..\\..\\Common\\Lightning.cpp", 0x390, 0);
        auto bolt = mem ? new (mem) CLightning() : nullptr;

        uintptr_t slot = reinterpret_cast<uintptr_t>(bolt);
        this->m_slots.Add(1, &slot);
    } else {
        index = this->m_free[this->m_free.Count() - 1];
        this->m_slots[index] &= ~static_cast<uintptr_t>(1);
        this->m_free.SetCount(this->m_free.Count() - 1);
    }

    auto bolt = reinterpret_cast<CLightning*>(this->m_slots[index]);

    bolt->SetRecord(rec);
    bolt->m_update = update;
    bolt->m_updateParam = param;

    return index;
}

// ref: FUN_009ab2b0
void CLightningSystem::Destroy(int32_t index) {
    this->m_slots[index] |= 1;
    this->m_free.Add(1, &index);
}

// ref: FUN_009a96e0
CLightning* CLightningSystem::Get(int32_t index) {
    if (index == -1 || static_cast<uint32_t>(index) >= this->m_slots.Count() || (this->m_slots[index] & 1)) {
        return nullptr;
    }

    return reinterpret_cast<CLightning*>(this->m_slots[index]);
}

// ref: FUN_009a9710
void CLightningSystem::SetEnds(int32_t index, const C3Vector* start, const C3Vector* end) {
    if (!start && !end) {
        // SErrSetLastError(ERROR_INVALID_PARAMETER) in the reference.
        return;
    }

    auto bolt = reinterpret_cast<CLightning*>(this->m_slots[index]);

    if (start) {
        bolt->m_start = *start;
    }

    if (end) {
        bolt->m_end = *end;
    }
}

// ref: FUN_009a9770
void CLightningSystem::SetVisible(int32_t index, int32_t visible) {
    auto bolt = this->Get(index);

    if (!bolt) {
        return;
    }

    if (visible) {
        bolt->m_flags |= 0x2;
    } else {
        bolt->m_flags &= ~0x2u;
    }
}

// ref: FUN_009ab730
void CLightningSystem::Update(float elapsed) {
    for (uint32_t i = this->m_slots.Count(); i != 0; i--) {
        if ((this->m_slots[i - 1] & 1) == 0) {
            reinterpret_cast<CLightning*>(this->m_slots[i - 1])->Update(elapsed);
        }
    }
}

// ref: FUN_009ab070
void CLightningSystem::Draw(const C3Vector& cameraPos) {
    g_theGxDevicePtr->XformPush(GxXform_World);
    g_theGxDevicePtr->XformPush(GxXform_Tex0);

    // The world is the camera's: the bolts are built in world space and drawn relative to the eye.
    C44Matrix world;
    g_theGxDevicePtr->XformWorld(world);
    C3Vector back = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
    world.Translate(back);
    g_theGxDevicePtr->XformSet(GxXform_World, world);

    GxRsPush();

    GxRsSet(GxRs_MatDiffuse, 0xFF000000);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_ColorOp0, 0);
    GxRsSet(GxRs_AlphaOp0, 0);
    GxRsSet(GxRs_ColorWrite, 0xF);

    for (int32_t layer = 0; layer < 4; layer++) {
        for (uint32_t i = this->m_slots.Count(); i != 0; i--) {
            uint32_t index = i - 1;

            if (this->m_slots[index] & 1) {
                continue;
            }

            auto bolt = reinterpret_cast<CLightning*>(this->m_slots[index]);

            if (!bolt->m_rec || bolt->m_rec->m_renderLayer != layer) {
                continue;
            }

            float elapsed;
            memcpy(&elapsed, &index, 4);

            bolt->Render(elapsed, cameraPos);
        }
    }

    GxRsPop();

    g_theGxDevicePtr->XformPop(GxXform_World);
    g_theGxDevicePtr->XformPop(GxXform_Tex0);
}
