#include "world/ParticleFx.hpp"
#include <cstdio>
#include "world/Terrain.hpp"
#include "world/CWorld.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Shared.hpp"
#include "model/M2Data.hpp"
#include "model/M2Model.hpp"
#include "gx/Draw.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "gx/CGxDevice.hpp"
#include <tempest/Matrix.hpp>
#include <tempest/Vector.hpp>
#include <algorithm>
#include <cmath>
#include <map>
#include <common/Time.hpp>
#include <vector>

namespace {

const uint32_t MAX_PARTICLES_PER_EMITTER = 96;
// Per-frame quad ceiling. The reference has none -- it thins by the particleDensity CVar -- so this
// only exists to bound a pathological frame. It is spent NEAREST FIRST (see ParticleFxRender):
// when it was handed out in map order, which is pointer order, whichever emitters happened to sit
// first in memory took the whole 6000 and the fires in front of the camera got nothing. Which
// ones lost out changed with what was in view, so it looked like flames vanishing as the camera
// moved.
const uint32_t MAX_QUADS_PER_FRAME = 24000;

float s_particleDensity = 1.0f;

struct Particle {
    C3Vector pos;
    C3Vector vel;
    float age;
    float life;
    float seed;
};

struct EmitterState {
    std::vector<Particle> particles;
    float accumulator = 0.0f;
    M2ModelTrack<float> speed, variation, latitude, longitude, gravity, life, rate, width, length;
};

struct ModelParticles {
    std::vector<EmitterState> emitters;
    uint32_t lastFrame = 0;

    // Farthest any live particle sat from the model origin at the last step, plus its half size.
    float liveExtent = 0.0f;
};

// Static reach per M2Data, computed once from the emitter definitions.
std::map<const M2Data*, float> s_staticExtent;

// First key of a float track, or the default when the track is empty. Emitter tracks are almost
// always constant, so the first key is the value; for an animated one it is still a fair estimate.
float TrackFirstKey(const M2Track<float>& track, float def) {
    if (!track.sequenceKeys.Count()) {
        return def;
    }

    const auto& keys = track.sequenceKeys[0].keys;

    return keys.Count() ? keys[0] : def;
}

std::map<CM2Model*, ModelParticles> s_models;
uint32_t s_frame = 0;
uint32_t s_rand = 0x2545F491;

float Rand01() {
    s_rand = s_rand * 1664525u + 1013904223u;
    return static_cast<float>((s_rand >> 8) & 0xFFFF) / 65535.0f;
}

void Lerp(const C3Vector& a, const C3Vector& b, float f, C3Vector& out);
void Lerp(const C2Vector& a, const C2Vector& b, float f, C2Vector& out);
void Lerp(const fixed16& a, const fixed16& b, float f, fixed16& out);
void Lerp(const uint16_t& a, const uint16_t& b, float f, uint16_t& out);

// Sample a lifetime track (fixed16 times over 0..1) at t
template<class T>
bool SamplePartTrack(const M2PartTrack<T>& track, float t, T& out) {
    uint32_t n = track.values.Count();

    if (!n || track.times.Count() < n) {
        return false;
    }

    if (n == 1) {
        out = track.values[0];
        return true;
    }

    float tt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    for (uint32_t i = 0; i + 1 < n; i++) {
        float t0 = static_cast<float>(track.times[i]);
        float t1 = static_cast<float>(track.times[i + 1]);

        if (tt <= t1 || i + 2 == n) {
            float f = (t1 > t0) ? (tt - t0) / (t1 - t0) : 0.0f;
            if (f < 0.0f) f = 0.0f;
            if (f > 1.0f) f = 1.0f;
            out = track.values[i];
            Lerp(track.values[i], track.values[i + 1], f, out);
            return true;
        }
    }

    out = track.values[n - 1];
    return true;
}

void Lerp(const C3Vector& a, const C3Vector& b, float f, C3Vector& out) {
    out = { a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f, a.z + (b.z - a.z) * f };
}

void Lerp(const C2Vector& a, const C2Vector& b, float f, C2Vector& out) {
    out = { a.x + (b.x - a.x) * f, a.y + (b.y - a.y) * f };
}

void Lerp(const fixed16& a, const fixed16& b, float f, fixed16& out) {
    float va = static_cast<float>(a);
    float vb = static_cast<float>(b);
    float v = va + (vb - va) * f;
    out.n = static_cast<int16_t>(v * 32767.0f);
}

void Lerp(const uint16_t& a, const uint16_t& b, float f, uint16_t& out) {
    out = f < 0.5f ? a : b;
}

float TrackValue(CM2Model* model, M2ModelBone* bone, const M2Track<float>& track, M2ModelTrack<float>& state, float def) {
    // An M2Array resolves its data as (its own address + offset), so element 0 of an EMPTY array
    // is a wild pointer, not null: every access below has to be gated on Count() first. Emitter
    // tracks with no sequences are common (a constant emitter stores nothing), and indexing one
    // of those crashed the client on world entry.
    uint32_t seqCount = track.sequenceKeys.Count();

    if (!seqCount) {
        state.currentValue = def;
        return def;
    }

    uint32_t seqIndex = (bone && bone->sequence.uint4 < seqCount) ? bone->sequence.uint4 : 0;
    const auto& keys = track.sequenceKeys[seqIndex].keys;

    if (!keys.Count()) {
        state.currentValue = def;
        return def;
    }

    // Without bone state there is no sequence clock to sample against: hold the first key
    if (!bone) {
        state.currentValue = keys[0];
        return state.currentValue;
    }

    // Same sampling as M2AnimateTrack (model/M2Animate.hpp), which cannot be included here
    // because that header carries non-inline function bodies

    uint32_t nextKey = 0;
    float ratio = 0.0f;
    model->FindKey(&bone->sequence, track, state.currentKey, nextKey, ratio);

    if (state.currentKey >= keys.Count()) {
        state.currentKey = 0;
    }

    if (nextKey >= keys.Count()) {
        nextKey = state.currentKey;
    }

    if (track.trackType == 0) {
        state.currentValue = keys[state.currentKey];
    } else {
        state.currentValue = keys[state.currentKey] + (keys[nextKey] - keys[state.currentKey]) * ratio;
    }

    return state.currentValue;
}

} // namespace

void ParticleFxForgetModel(CM2Model* model) {
    s_models.erase(model);
}

float ParticleFxCullExtent(CM2Model* model, float scale) {
    if (!model || !model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data) {
        return 0.0f;
    }

    M2Data* data = model->m_shared->m_data;
    uint32_t emitterCount = data->particles.Count();

    if (!emitterCount) {
        return 0.0f;
    }

    auto cached = s_staticExtent.find(data);
    float reach;

    if (cached != s_staticExtent.end()) {
        reach = cached->second;
    } else {
        reach = 0.0f;

        for (uint32_t e = 0; e < emitterCount; e++) {
            const M2Particle& def = data->particles[e];

            float speed = TrackFirstKey(def.speedTrack, 1.0f);
            float life = TrackFirstKey(def.lifeTrack, 1.0f);
            float width = TrackFirstKey(def.widthTrack, 0.0f);
            float length = TrackFirstKey(def.lengthTrack, 0.0f);
            float offset = sqrtf(def.position.x * def.position.x + def.position.y * def.position.y + def.position.z * def.position.z);

            // Gravity and variation can carry a particle further than speed x life; the halved
            // extra covers the common case without turning every torch into a 40 yard sphere.
            float r = offset + fabsf(speed) * fabsf(life) * 1.5f + (fabsf(width) + fabsf(length)) * 0.5f;

            if (r > reach) {
                reach = r;
            }
        }

        s_staticExtent[data] = reach;
    }

    reach *= scale;

    auto live = s_models.find(model);

    if (live != s_models.end() && live->second.liveExtent > reach) {
        reach = live->second.liveExtent;
    }

    return reach;
}

void ParticleFxUpdateModel(CM2Model* model, float dt) {
    // m_data is assigned when the async read lands, but the array offsets inside it are only
    // patched by M2Init afterwards, and m_m2DataLoaded is set only once that succeeds. Touching a
    // model in between walks raw file offsets -- which crashed the client on world entry.
    if (!model || !model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data) {
        return;
    }

    M2Data* data = model->m_shared->m_data;
    uint32_t emitterCount = data->particles.Count();

    if (!emitterCount) {
        return;
    }

    ModelParticles& mp = s_models[model];
    mp.lastFrame = s_frame;

    if (mp.emitters.size() != emitterCount) {
        mp.emitters.clear();
        mp.emitters.resize(emitterCount);
    }

    const C44Matrix& M = model->matrixB4; // model -> world
    float liveExtent = 0.0f;

    for (uint32_t e = 0; e < emitterCount; e++) {
        const M2Particle& def = data->particles[e];
        EmitterState& st = mp.emitters[e];
        M2ModelBone* bone = (model->m_bones && def.boneIndex < data->bones.Count()) ? &model->m_bones[def.boneIndex] : nullptr;

        float rate = TrackValue(model, bone, def.emissionRateTrack, st.rate, 0.0f);
        float speed = TrackValue(model, bone, def.speedTrack, st.speed, 1.0f);
        float variation = TrackValue(model, bone, def.variationTrack, st.variation, 0.0f);
        float latitude = TrackValue(model, bone, def.latitudeTrack, st.latitude, 0.0f);
        float longitude = TrackValue(model, bone, def.longitudeTrack, st.longitude, 0.0f);
        float gravity = TrackValue(model, bone, def.gravityTrack, st.gravity, 0.0f);
        float lifeSpan = TrackValue(model, bone, def.lifeTrack, st.life, 1.0f);
        float width = TrackValue(model, bone, def.widthTrack, st.width, 0.0f);
        float length = TrackValue(model, bone, def.lengthTrack, st.length, 0.0f);

        // Emitter origin in world space. matrixB4 is the model's WORLD placement -- SetWorldTransform
        // writes the absolute position straight into its translation row -- so nothing needs adding
        // here. (Bone animation of the emitter itself is not applied.)
        C3Vector lp = def.position;
        C3Vector origin = {
            lp.x * M.a0 + lp.y * M.b0 + lp.z * M.c0 + M.d0,
            lp.x * M.a1 + lp.y * M.b1 + lp.z * M.c1 + M.d1,
            lp.x * M.a2 + lp.y * M.b2 + lp.z * M.c2 + M.d2
        };

        // Model axes in world space, for the plane emitter's rectangle and the launch direction
        C3Vector ax = { M.a0, M.a1, M.a2 };
        C3Vector ay = { M.b0, M.b1, M.b2 };
        C3Vector az = { M.c0, M.c1, M.c2 };

        // Age and integrate
        for (size_t i = 0; i < st.particles.size();) {
            Particle& p = st.particles[i];
            p.age += dt;

            if (p.age >= p.life) {
                st.particles[i] = st.particles.back();
                st.particles.pop_back();
                continue;
            }

            p.vel.z -= gravity * dt;
            p.pos.x += p.vel.x * dt;
            p.pos.y += p.vel.y * dt;
            p.pos.z += p.vel.z * dt;

            {
                float dx = p.pos.x - M.d0;
                float dy = p.pos.y - M.d1;
                float dz = p.pos.z - M.d2;
                float d = sqrtf(dx * dx + dy * dy + dz * dz) + (fabsf(width) + fabsf(length)) * 0.5f;

                if (d > liveExtent) {
                    liveExtent = d;
                }
            }
            i++;
        }

        if (rate <= 0.0f || lifeSpan <= 0.0f) {
            continue;
        }

        st.accumulator += rate * dt;

        while (st.accumulator >= 1.0f && st.particles.size() < MAX_PARTICLES_PER_EMITTER) {
            st.accumulator -= 1.0f;

            Particle p;
            p.age = 0.0f;
            p.life = lifeSpan * (1.0f + def.lifeVariation * (Rand01() * 2.0f - 1.0f));
            p.seed = Rand01();

            if (p.life <= 0.05f) {
                p.life = 0.05f;
            }

            float lat = latitude * Rand01();
            float lon = longitude * (Rand01() * 2.0f - 1.0f);
            float s = speed * (1.0f + variation * (Rand01() * 2.0f - 1.0f));

            // Local launch direction: up the emitter's z, tilted by latitude, spun by longitude
            float dx = sinf(lat) * cosf(lon);
            float dy = sinf(lat) * sinf(lon);
            float dz = cosf(lat);

            if (def.emitterType == 2) {
                // Sphere: start on a shell of the given radius (length) in that direction
                float r = length;
                p.pos = { origin.x + (dx * ax.x + dy * ay.x + dz * az.x) * r, origin.y + (dx * ax.y + dy * ay.y + dz * az.y) * r, origin.z + (dx * ax.z + dy * ay.z + dz * az.z) * r };
            } else {
                // Plane: random point of the width x length rectangle around the origin
                float ox = (Rand01() * 2.0f - 1.0f) * width;
                float oy = (Rand01() * 2.0f - 1.0f) * length;
                p.pos = { origin.x + ox * ax.x + oy * ay.x, origin.y + ox * ax.y + oy * ay.y, origin.z + ox * ax.z + oy * ay.z };
            }

            p.vel = { (dx * ax.x + dy * ay.x + dz * az.x) * s, (dx * ax.y + dy * ay.y + dz * az.y) * s, (dx * ax.z + dy * ay.z + dz * az.z) * s };
            st.particles.push_back(p);
        }
    }

    mp.liveExtent = liveExtent;
}

void ParticleFxEndFrame() {
    s_frame++;

    // Release simulations of models not seen for ~10 s of frames
    for (auto it = s_models.begin(); it != s_models.end();) {
        if (s_frame - it->second.lastFrame > 600) {
            it = s_models.erase(it);
        } else {
            ++it;
        }
    }
}

namespace {

struct Quad {
    C3Vector pos[4];
    C2Vector uv[4];
    CImVector color;
    HTEXTURE texture;
    uint8_t blend;
    float dist;
};

std::vector<Quad> s_quads;
std::vector<C3Vector> s_vp;
std::vector<C2Vector> s_vt;
std::vector<CImVector> s_vc;
std::vector<uint16_t> s_vi;

} // namespace

void ParticleFxRender() {
    CGxShader* vs = nullptr;
    CGxShader* ps = nullptr;
    TerrainUiShaders(vs, ps);

    if (!vs || !ps || !vs->Valid() || !ps->Valid() || s_models.empty()) {
        return;
    }

    const C3Vector& cameraPos = CWorld::GetCameraPos();
    const C3Vector& fwd = CWorld::GetCameraDir();
    C3Vector right = { fwd.y, -fwd.x, 0.0f };
    float rl = sqrtf(right.x * right.x + right.y * right.y);

    if (rl < 1e-4f) {
        right = { 1.0f, 0.0f, 0.0f };
    } else {
        right.x /= rl; right.y /= rl;
    }

    C3Vector up = { right.y * fwd.z - right.z * fwd.y, right.z * fwd.x - right.x * fwd.z, right.x * fwd.y - right.y * fwd.x };

    s_quads.clear();

    // Walk the live emitter models nearest first, so if the quad ceiling is ever hit it is the far
    // ones that go without.
    //
    // Only models whose owner updated them THIS frame are known to be alive: the map is keyed on a
    // raw CM2Model*, and a unit that despawns releases its model without telling us, so
    // dereferencing a stale key is a use-after-free. Stale entries are aged out by key alone in
    // ParticleFxEndFrame, which never touches the pointer.
    struct LiveModel {
        float dist;
        CM2Model* model;
        ModelParticles* mp;
    };

    static std::vector<LiveModel> s_live;
    s_live.clear();

    for (auto& entry : s_models) {
        if (entry.second.lastFrame != s_frame) {
            continue;
        }

        CM2Model* model = entry.first;

        if (!model->m_shared || !model->m_shared->m_m2DataLoaded || !model->m_shared->m_data || !model->m_shared->textures) {
            continue;
        }

        float dx = model->matrixB4.d0 - cameraPos.x;
        float dy = model->matrixB4.d1 - cameraPos.y;
        float dz = model->matrixB4.d2 - cameraPos.z;

        s_live.push_back({ dx * dx + dy * dy + dz * dz, model, &entry.second });
    }

    std::sort(s_live.begin(), s_live.end(), [](const LiveModel& a, const LiveModel& b) {
        return a.dist < b.dist;
    });

    for (const LiveModel& lm : s_live) {
        CM2Model* model = lm.model;
        M2Data* data = model->m_shared->m_data;
        ModelParticles& mp = *lm.mp;

        for (size_t e = 0; e < mp.emitters.size() && e < data->particles.Count(); e++) {
            const M2Particle& def = data->particles[e];
            EmitterState& st = mp.emitters[e];

            if (st.particles.empty()) {
                continue;
            }

            HTEXTURE texture = (def.textureIndex < data->textures.Count()) ? model->m_shared->textures[def.textureIndex] : nullptr;

            if (!texture) {
                continue;
            }

            uint32_t rows = def.rows ? def.rows : 1;
            uint32_t cols = def.cols ? def.cols : 1;

            for (const Particle& p : st.particles) {
                if (s_quads.size() >= MAX_QUADS_PER_FRAME) {
                    break;
                }

                float t = p.age / p.life;
                C3Vector color = { 1.0f, 1.0f, 1.0f };
                fixed16 alpha; alpha.n = 32767;
                C2Vector scale = { 1.0f, 1.0f };
                SamplePartTrack(def.colorTrack, t, color);
                SamplePartTrack(def.alphaTrack, t, alpha);
                SamplePartTrack(def.scaleTrack, t, scale);

                float a = static_cast<float>(alpha);
                if (a <= 0.01f) {
                    continue;
                }

                uint16_t cell = 0;
                SamplePartTrack(def.headCellTrack, t, cell);
                uint32_t cr = (cell / cols) % rows;
                uint32_t cc = cell % cols;
                float u0 = static_cast<float>(cc) / cols;
                float v0 = static_cast<float>(cr) / rows;
                float u1 = static_cast<float>(cc + 1) / cols;
                float v1 = static_cast<float>(cr + 1) / rows;

                float hw = scale.x * 0.5f;
                float hh = scale.y * 0.5f;

                Quad q;
                q.pos[0] = { p.pos.x - right.x * hw + up.x * hh, p.pos.y - right.y * hw + up.y * hh, p.pos.z - right.z * hw + up.z * hh };
                q.pos[1] = { p.pos.x + right.x * hw + up.x * hh, p.pos.y + right.y * hw + up.y * hh, p.pos.z + right.z * hw + up.z * hh };
                q.pos[2] = { p.pos.x + right.x * hw - up.x * hh, p.pos.y + right.y * hw - up.y * hh, p.pos.z + right.z * hw - up.z * hh };
                q.pos[3] = { p.pos.x - right.x * hw - up.x * hh, p.pos.y - right.y * hw - up.y * hh, p.pos.z - right.z * hw - up.z * hh };
                q.uv[0] = { u0, v0 }; q.uv[1] = { u1, v0 }; q.uv[2] = { u1, v1 }; q.uv[3] = { u0, v1 };

                // Colour values are 0..255 in the file
                float cr255 = color.x > 1.0f ? color.x / 255.0f : color.x;
                float cg255 = color.y > 1.0f ? color.y / 255.0f : color.y;
                float cb255 = color.z > 1.0f ? color.z / 255.0f : color.z;
                q.color.b = static_cast<uint8_t>((cb255 > 1.0f ? 1.0f : cb255) * 255.0f);
                q.color.g = static_cast<uint8_t>((cg255 > 1.0f ? 1.0f : cg255) * 255.0f);
                q.color.r = static_cast<uint8_t>((cr255 > 1.0f ? 1.0f : cr255) * 255.0f);
                q.color.a = static_cast<uint8_t>((a > 1.0f ? 1.0f : a) * 255.0f);
                q.texture = texture;
                q.blend = def.blendMode;

                float dx = p.pos.x - cameraPos.x;
                float dy = p.pos.y - cameraPos.y;
                float dz = p.pos.z - cameraPos.z;
                q.dist = dx * dx + dy * dy + dz * dz;
                s_quads.push_back(q);
            }
        }
    }

    if (s_quads.empty()) {
        return;
    }

    // Order: additive quads (3, 4 -- fire, glow) are order-independent, so they group by texture
    // and are drawn first, in as few batches as their textures allow. Everything alpha-blended
    // follows, farthest first, since for those the order is the picture. Walking the nearest
    // models first and then sorting purely by distance produced one batch per texture change --
    // thousands of draw calls a frame with a fortress of candles in view -- which was the lag.
    std::sort(s_quads.begin(), s_quads.end(), [](const Quad& a, const Quad& b) {
        bool addA = a.blend == 3 || a.blend == 4;
        bool addB = b.blend == 3 || b.blend == 4;

        if (addA != addB) {
            return addA;
        }

        if (addA) {
            if (a.texture != b.texture) {
                return a.texture < b.texture;
            }

            return a.blend < b.blend;
        }

        return a.dist > b.dist;
    });

    GxRsPush();
    GxRsSet(GxRs_DepthTest, 1);
    GxRsSet(GxRs_DepthFunc, 0);
    GxRsSet(GxRs_DepthWrite, 0);
    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, TerrainFogActive() ? 1 : 0);
    GxRsSet(GxRs_VertexShader, vs);
    GxRsSet(GxRs_PixelShader, ps);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&TerrainViewProjT()), 4);

    size_t i = 0;

    // Largest batch handed to the device at once. s_vi holds 16-bit indices, so a batch can never
    // address more than 65536 vertices; well below that, a lock of several thousand quads is what
    // used to stall the frame. A run larger than this is drawn in consecutive slices, in order --
    // the old code stopped at the cap and then skipped the REST OF THE RUN, and since the quads are
    // sorted farthest first the quads it dropped were the nearest ones. With the camera low enough
    // to see a whole fortress of candles and torches, which all share one flame texture, that was
    // every nearby fire disappearing at once.
    const size_t MAX_BATCH_QUADS = 2048;

    while (i < s_quads.size()) {
        // Run of quads sharing texture and blend, capped at one batch
        size_t j = i;
        HTEXTURE texture = s_quads[i].texture;
        uint8_t blend = s_quads[i].blend;

        while (j < s_quads.size() && j - i < MAX_BATCH_QUADS && s_quads[j].texture == texture && s_quads[j].blend == blend) {
            j++;
        }

        s_vp.clear(); s_vt.clear(); s_vc.clear(); s_vi.clear();

        for (size_t k = i; k < j; k++) {
            const Quad& q = s_quads[k];
            uint16_t base = static_cast<uint16_t>(s_vp.size());

            for (int32_t c = 0; c < 4; c++) {
                s_vp.push_back(q.pos[c]);
                s_vt.push_back(q.uv[c]);
                s_vc.push_back(q.color);
            }

            s_vi.push_back(base); s_vi.push_back(base + 1); s_vi.push_back(base + 2);
            s_vi.push_back(base); s_vi.push_back(base + 2); s_vi.push_back(base + 3);
        }

        // M2 particle blend modes: 0 opaque, 1 alpha key, 2 alpha, 3 add (no alpha), 4 add alpha,
        // 5 mod, 6 mod2x
        EGxBlend gxBlend;

        switch (blend) {
            case 0: gxBlend = GxBlend_Opaque; break;
            case 1: gxBlend = GxBlend_AlphaKey; break;
            case 3: case 4: gxBlend = GxBlend_Add; break;
            case 5: gxBlend = GxBlend_Mod; break;
            case 6: gxBlend = GxBlend_Mod2x; break;
            default: gxBlend = GxBlend_Alpha; break;
        }

        GxRsSet(GxRs_BlendingMode, gxBlend);
        GxRsSet(GxRs_AlphaRef, static_cast<int32_t>(CGxDevice::s_alphaRef[gxBlend]));
        GxRsSet(GxRs_Texture0, TextureGetGxTex(texture, 0, nullptr));

        GxPrimLockVertexPtrs(
            static_cast<uint32_t>(s_vp.size()),
            s_vp.data(), sizeof(C3Vector),
            nullptr, 0,
            s_vc.data(), sizeof(CImVector),
            nullptr, 0,
            s_vt.data(), sizeof(C2Vector),
            nullptr, 0
        );
        GxDrawLockedElements(GxPrim_Triangles, static_cast<uint32_t>(s_vi.size()), s_vi.data());
        GxPrimUnlockVertexPtrs();


        i = j;
    }

    GxRsPop();
}

// ref: FUN_00980f70
void ParticleFxSetDensity(float density) {
    s_particleDensity = 0.0f;

    if (density >= 0.0f) {
        s_particleDensity = density;

        if (density >= 1.0f) {
            s_particleDensity = 1.0f;
        }
    }
}
