#include "world/Clouds.hpp"
#include "world/CWorld.hpp"
#include "gx/Draw.hpp"
#include "gx/Buffer.hpp"
#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Texture.hpp"
#include "util/CStatus.hpp"
#include <tempest/Matrix.hpp>
#include <cmath>
#include <cstdlib>

// Cloud sheet. See docs/ref/parity-sky-bodies.md section 2 for the recovered reference behaviour;
// the structure here follows it, with the noise constants lifted and the shading simplified.

namespace {

// LOD 0, the client's default (SkyCloudLOD defaults to 0)
const int32_t CLOUD_DIM = 128;
const int32_t CLOUD_MASK = CLOUD_DIM - 1;
const int32_t CLOUD_OCTAVES = 4;
const int32_t CLOUD_ROWS_PER_FRAME = 8;
const float CLOUD_ANIM_SPEED = 2.0f;
const float CLOUD_RAMP_BASE = 0.96f;

// Per-octave frequency step in 8.8 fixed point, for LOD 0. Octave i contributes amplitude 1/(1<<i).
const int32_t CLOUD_STEP[CLOUD_OCTAVES] = { 16, 32, 64, 128 };

// The dome: 12 rings of vertices at these zenith angles (turns of pi), 16 segments around. It stops
// at 45 degrees from the zenith, and the last three rings fade out so the sheet never reaches the
// horizon line.
const int32_t CLOUD_RINGS = 12;
const int32_t CLOUD_SEGS = 16;
const float CLOUD_RING_ZENITH[CLOUD_RINGS] = {
    0.0f, 0.025f, 0.05f, 0.075f, 0.1f, 0.125f, 0.15f, 0.175f, 0.205f, 0.23f, 0.245f, 0.25f
};
const uint8_t CLOUD_RING_ALPHA[CLOUD_RINGS] = { 255, 255, 255, 255, 255, 255, 255, 255, 255, 128, 0, 0 };
const float CLOUD_RADIUS = 140.0f; // inside the sky dome's radius so it sits under the gradient

const int32_t CLOUD_VERTS = CLOUD_RINGS * (CLOUD_SEGS + 1);
const int32_t CLOUD_INDICES = (CLOUD_RINGS - 1) * CLOUD_SEGS * 6;

// --- generated sheet -------------------------------------------------------------------------

CImVector s_texels[CLOUD_DIM * CLOUD_DIM];
uint8_t s_height[CLOUD_DIM * CLOUD_DIM];
HTEXTURE s_tex[2] = { nullptr, nullptr };
int32_t s_front = 0;
int32_t s_rowCursor = 0;
uint16_t s_phase = 0;
float s_timeAccum = 0.0f;
bool s_built = false;

// --- noise tables ----------------------------------------------------------------------------

float s_value[256];  // per-lattice random value, [-1, 1]
float s_fade[256];   // cosine interpolation weight indexed by an 8-bit fraction
uint8_t s_perm[256]; // permutation
uint8_t s_ramp[256]; // density response
uint8_t s_threshold = 128;
bool s_tablesBuilt = false;

void BuildTables() {
    if (s_tablesBuilt) {
        return;
    }

    s_tablesBuilt = true;

    // The reference seeds these from rand(); the exact values do not matter as long as the value
    // table is uniform in [-1, 1] and the permutation is a permutation. Generated deterministically
    // here so the sky looks the same every run.
    uint32_t seed = 0x1337BEEF;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (seed >> 8) & 0x7FFF;
    };

    for (int32_t i = 0; i < 256; i++) {
        s_value[i] = 1.0f - 2.0f * (static_cast<float>(next()) / 32767.0f);
        s_fade[i] = (1.0f - cosf(i * 3.14159265f / 256.0f)) * 0.5f;
        s_perm[i] = static_cast<uint8_t>(i);
    }

    for (int32_t i = 255; i > 0; i--) {
        int32_t j = next() % (i + 1);
        uint8_t t = s_perm[i];
        s_perm[i] = s_perm[j];
        s_perm[j] = t;
    }
}

// Density response ramp: 255 - 255 * base^x, x stepping by (255 - threshold)/256 per entry.
void BuildRamp(uint8_t threshold) {
    float step = static_cast<float>(255 - threshold) * (1.0f / 256.0f);
    float x = 0.0f;

    for (int32_t k = 0; k < 256; k++) {
        float v = 255.0f - 255.0f * powf(CLOUD_RAMP_BASE, x);
        s_ramp[k] = static_cast<uint8_t>(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
        x += step;
    }
}

float LatticeValue(int32_t x, int32_t y, int32_t z) {
    return s_value[s_perm[(s_perm[(s_perm[x & 255] + y) & 255] + z) & 255]];
}

// 3D value noise with cosine interpolation, coordinates in 8.8 fixed point.
float Noise3(int32_t xf, int32_t yf, int32_t zf) {
    int32_t x0 = (xf >> 8) & 255;
    int32_t y0 = (yf >> 8) & 255;
    int32_t z0 = (zf >> 8) & 255;

    float fx = s_fade[xf & 255];
    float fy = s_fade[yf & 255];
    float fz = s_fade[zf & 255];

    float c000 = LatticeValue(x0, y0, z0);
    float c100 = LatticeValue(x0 + 1, y0, z0);
    float c010 = LatticeValue(x0, y0 + 1, z0);
    float c110 = LatticeValue(x0 + 1, y0 + 1, z0);
    float c001 = LatticeValue(x0, y0, z0 + 1);
    float c101 = LatticeValue(x0 + 1, y0, z0 + 1);
    float c011 = LatticeValue(x0, y0 + 1, z0 + 1);
    float c111 = LatticeValue(x0 + 1, y0 + 1, z0 + 1);

    float x00 = c000 + (c100 - c000) * fx;
    float x10 = c010 + (c110 - c010) * fx;
    float x01 = c001 + (c101 - c001) * fx;
    float x11 = c011 + (c111 - c011) * fx;

    float y0v = x00 + (x10 - x00) * fy;
    float y1v = x01 + (x11 - x01) * fy;

    return y0v + (y1v - y0v) * fz;
}

void CloudTexCallback(EGxTexCommand cmd, uint32_t w, uint32_t h, uint32_t d, uint32_t mip, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd != GxTex_Latch) {
        return;
    }

    // Answer for EVERY level, not just level 0. The device resets `texels` to null before each
    // latch and blits straight from whatever comes back, so a level this callback declines to
    // answer is a read from a null pointer, which is exactly what it used to be: the client died on
    // entering the world, in a 256-byte row copy that turned out to be mip level 1 of this 128-wide
    // sheet. The sheet has no mips, so hand back the base image with the base stride; a smaller
    // level then reads a cropped corner of it, which is in bounds and harmless.
    stride = 4 * CLOUD_DIM;
    texels = userArg;
}

void EnsureTextures() {
    if (s_tex[0]) {
        return;
    }

    for (int32_t i = 0; i < CLOUD_DIM * CLOUD_DIM; i++) {
        s_texels[i].b = 0xFF;
        s_texels[i].g = 0xFF;
        s_texels[i].r = 0xFF;
        s_texels[i].a = 0x00;
    }

    auto flags = CGxTexFlags(GxTex_Linear, 1, 1, 0, 0, 0, 1);

    for (int32_t i = 0; i < 2; i++) {
        // The last argument is NOT a line number: a non-zero value tells TextureCreate to discard
        // the filter chosen above and substitute the global CTexture::s_filterMode, which is
        // GxTex_LinearMipNearest. That quietly turned this into a mipmapped texture and made the
        // device upload eight levels of a sheet that only ever has one.
        s_tex[i] = TextureCreate(CLOUD_DIM, CLOUD_DIM, GxTex_Argb8888, GxTex_Argb8888, flags, s_texels, &CloudTexCallback, __FILE__, 0);
    }
}

// --- mesh ------------------------------------------------------------------------------------

C3Vector s_pos[CLOUD_VERTS];
C2Vector s_uv[CLOUD_VERTS];
CImVector s_col[CLOUD_VERTS];
uint16_t s_idx[CLOUD_INDICES];
bool s_meshBuilt = false;

void BuildMesh() {
    if (s_meshBuilt) {
        return;
    }

    s_meshBuilt = true;
    const float PI = 3.14159265f;
    int32_t v = 0;

    for (int32_t ring = 0; ring < CLOUD_RINGS; ring++) {
        float theta = CLOUD_RING_ZENITH[ring] * PI;
        float st = sinf(theta);
        float ct = cosf(theta);

        // Planar projection: the texture radius runs 0 .. 0.5 over the rings
        float r = static_cast<float>(ring) * (1.0f / (CLOUD_RINGS - 1)) * 0.5f;

        for (int32_t seg = 0; seg <= CLOUD_SEGS; seg++) {
            float az = static_cast<float>(seg) / CLOUD_SEGS * 2.0f * PI;
            s_pos[v].x = st * cosf(az) * CLOUD_RADIUS;
            s_pos[v].y = st * sinf(az) * CLOUD_RADIUS;
            s_pos[v].z = ct * CLOUD_RADIUS;
            s_uv[v].x = sinf(az) * r + 0.5f;
            s_uv[v].y = cosf(az) * r + 0.5f;
            s_col[v].b = 0xFF;
            s_col[v].g = 0xFF;
            s_col[v].r = 0xFF;
            s_col[v].a = CLOUD_RING_ALPHA[ring];
            v++;
        }
    }

    int32_t n = 0;

    for (int32_t ring = 0; ring < CLOUD_RINGS - 1; ring++) {
        for (int32_t seg = 0; seg < CLOUD_SEGS; seg++) {
            uint16_t i0 = static_cast<uint16_t>(ring * (CLOUD_SEGS + 1) + seg);
            uint16_t i1 = static_cast<uint16_t>(i0 + 1);
            uint16_t i2 = static_cast<uint16_t>(i0 + (CLOUD_SEGS + 1));
            uint16_t i3 = static_cast<uint16_t>(i2 + 1);

            s_idx[n++] = i0; s_idx[n++] = i2; s_idx[n++] = i1;
            s_idx[n++] = i1; s_idx[n++] = i2; s_idx[n++] = i3;
        }
    }
}

} // namespace

void CloudsUpdate(float dt) {
    BuildTables();
    EnsureTextures();

    if (!s_tex[0] || !s_tex[1]) {
        return;
    }

    s_timeAccum += dt;

    // Cloud cover comes from the light data; without it the sky would be permanently overcast.
    float density = CWorld::GetCloudDensity();
    uint8_t threshold = static_cast<uint8_t>((1.0f - (density < 0.0f ? 0.0f : (density > 1.0f ? 1.0f : density))) * 255.0f);

    if (threshold != s_threshold || !s_built) {
        s_threshold = threshold;
        BuildRamp(threshold);
    }

    // Cloud colour: lit by the outdoor light, so the sheet warms and darkens with the day.
    const C3Vector& amb = CWorld::GetOutdoorAmbient();
    const C3Vector& dif = CWorld::GetOutdoorDiffuse();
    float lr = amb.x + dif.x * 0.65f;
    float lg = amb.y + dif.y * 0.65f;
    float lb = amb.z + dif.z * 0.65f;

    int32_t rows = s_built ? CLOUD_ROWS_PER_FRAME : CLOUD_DIM; // fill the whole sheet the first time
    int32_t zHigh = (s_phase >> 8) & 255;
    int32_t zFrac = s_phase & 255;

    for (int32_t i = 0; i < rows; i++) {
        int32_t row = (s_rowCursor + i) & CLOUD_MASK;

        for (int32_t x = 0; x < CLOUD_DIM; x++) {
            float n = 0.0f;
            float amplitude = 1.0f;

            for (int32_t o = 0; o < CLOUD_OCTAVES; o++) {
                int32_t step = CLOUD_STEP[o];
                n += Noise3(x * step, row * step, (zHigh * step) + zFrac) * amplitude;
                amplitude *= 0.5f;
            }

            float bf = n * 64.0f + 128.0f;
            int32_t b = static_cast<int32_t>(bf + 0.5f);

            if (b < 0) b = 0;
            if (b > 255) b = 255;

            int32_t k = b - s_threshold;
            uint8_t d = k >= 0 ? s_ramp[k] : 0;
            int32_t t = row * CLOUD_DIM + x;
            s_height[t] = d;

            if (!d) {
                s_texels[t].a = 0;
                continue;
            }

            // Shade: a base lift plus the light, weighted by density the way the reference does
            float shade = (static_cast<float>((~d >> 1) + 64)) / 255.0f;
            float r = lr * shade;
            float g = lg * shade;
            float bl = lb * shade;

            s_texels[t].r = static_cast<uint8_t>((r > 1.0f ? 1.0f : r) * 255.0f);
            s_texels[t].g = static_cast<uint8_t>((g > 1.0f ? 1.0f : g) * 255.0f);
            s_texels[t].b = static_cast<uint8_t>((bl > 1.0f ? 1.0f : bl) * 255.0f);
            s_texels[t].a = d;
        }
    }

    // Push the rows just written into the back texture, then flip when the sheet completes.
    int32_t back = s_front ^ 1;
    CGxTex* gxTex = TextureGetGxTex(s_tex[back], 0, nullptr);

    if (gxTex) {
        GxTexUpdate(gxTex, 0, 0, CLOUD_DIM, CLOUD_DIM, 1);
    }

    if (!s_built) {
        s_built = true;
        s_rowCursor = 0;
        s_front = back;
        return;
    }

    s_rowCursor += CLOUD_ROWS_PER_FRAME;

    if (s_rowCursor >= CLOUD_DIM) {
        s_rowCursor = 0;
        s_phase = static_cast<uint16_t>(static_cast<int32_t>(CLOUD_ANIM_SPEED * s_timeAccum * 256.0f));
        s_front = back;
    }
}

void CloudsRender(const C44Matrix& viewProjT, const C3Vector& cameraPos, CGxShader* vs, CGxShader* ps) {
    // Same one-shot question as the sky bodies: is this pass reached at all, and does it have what
    // it needs? Clouds were reported missing from a real session.
    static bool reported = false;

    if (!reported) {
        reported = true;
        fprintf(stderr, "Clouds: reached; built %d front tex %s vs %s ps %s density %.3f\n",
                s_built ? 1 : 0, s_tex[s_front] ? "ok" : "MISSING",
                vs ? "ok" : "MISSING", ps ? "ok" : "MISSING", CWorld::GetCloudDensity());
    }

    if (!s_built || !s_tex[s_front] || !vs || !ps) {
        return;
    }


    BuildMesh();

    // The sheet is centred on the camera like the rest of the sky, and the caller has already put
    // the view at the origin, so the mesh needs no translation.
    (void)cameraPos;

    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_AlphaRef, 0);
    GxRsSet(GxRs_VertexShader, vs);
    GxRsSet(GxRs_PixelShader, ps);
    GxShaderConstantsSet(GxSh_Vertex, 0, reinterpret_cast<const float*>(&viewProjT), 4);
    GxRsSet(GxRs_Texture0, TextureGetGxTex(s_tex[s_front], 0, nullptr));

    GxPrimLockVertexPtrs(
        CLOUD_VERTS,
        s_pos, sizeof(C3Vector),
        nullptr, 0,
        s_col, sizeof(CImVector),
        nullptr, 0,
        s_uv, sizeof(C2Vector),
        nullptr, 0
    );
    GxDrawLockedElements(GxPrim_Triangles, CLOUD_INDICES, s_idx);
    GxPrimUnlockVertexPtrs();
}

void CloudsRelease() {
    for (int32_t i = 0; i < 2; i++) {
        if (s_tex[i]) {
            HandleClose(s_tex[i]);
            s_tex[i] = nullptr;
        }
    }

    s_built = false;
    s_rowCursor = 0;
    s_front = 0;
}
