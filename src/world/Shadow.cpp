#include "gx/shader/CShaderEffect.hpp"
#include "world/Shadow.hpp"
#include "gx/Gx.hpp"
#include "gx/buffer/CGxBuf.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Draw.hpp"
#include "gx/Buffer.hpp"
#include "world/map/CMapObjGroup.hpp"
#include <tempest/Intersect.hpp>
#include "gx/Device.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Transform.hpp"
#include "gx/RenderState.hpp"
#include <tempest/Vector.hpp>
#include <tempest/Matrix.hpp>
#include "world/CWorld.hpp"
#include "console/CVar.hpp"
#include "console/Console.hpp"
#include "gx/Texture.hpp"
#include "gx/texture/CGxTex.hpp"
#include "model/CM2Model.hpp"
#include <storm/String.hpp>
#include <tempest/Box.hpp>
#include <cmath>
#include <cstdio>

// ------------------------------------------------------------------------------------------------
// The blob shadow. See Shadow.hpp for what this module is and what of it is ported.
// ------------------------------------------------------------------------------------------------

namespace {

// DAT_00d38044, DAT_00d38040, DAT_00d3803c: the blob and the two generated fade ramps.
HTEXTURE s_blobTexture = nullptr;
HTEXTURE s_addTexture = nullptr;
HTEXTURE s_modTexture = nullptr;

// DAT_00d3804c and DAT_00d38048.
CVar* s_shadowLODVar = nullptr;
CVar* s_extShadowQualityVar = nullptr;

// The ramps are 64x8 and generated, not loaded.
const uint32_t RAMP_WIDTH = 64;
const uint32_t RAMP_HEIGHT = 8;

// DAT_00af3e24, with DAT_00af3e20 holding the pixel count the size query reported. One buffer
// serves both ramps because only one of them is ever being latched at a time.
uint32_t s_rampPixels[RAMP_WIDTH * RAMP_HEIGHT];
uint32_t s_rampCount = 0;

// DAT_00a41170: what the strength is scaled by to make the decal's polygon offset. About 1/32768.
const float DECAL_OFFSET_SCALE = 3.051804378628731e-05f;

// DAT_009f98d8: the strength the blob gate hands the projector. It is a constant at the call site,
// and because it is NOT zero the blob path never takes the projector's depth-EQUAL branch.
const float BLOB_STRENGTH = 0.4f;

// DAT_009ea27c: the projector's own degenerate test, a far tighter one than DecalBuildTransforms's.
const float PROJECTOR_EPSILON = 2.384185791015625e-07f;

// DAT_009ebf34 and DAT_009f267c: the footprint is clamped into this cube, so a huge caster still
// gets a bounded blob.
const float PROJECTOR_CLAMP = 5.0f;

// DAT_00af3e14 and DAT_00af3e18: how far the projection volume reaches below and above the model's
// own z. It hangs five thirds of the caster's half-height downward and one half-height up.
const float PROJECTOR_BELOW = 1.6666666269302368f;
const float PROJECTOR_ABOVE = 1.0f;

// The bias DecalBuildTransforms is handed, and the QUERY MASK the receiver collection is handed.
// Every bit of that mask selects a branch of the query, and none of them touch the draw:
//
//   & 0x300f0  the WMO instance walk runs        (0x20020 -- yes)
//   & 0x30100  the terrain tile walk runs        (0x20100 -- yes)
//   & 0x100    the terrain hit collector runs    (yes: this is what puts a blob on the ground)
//   & 0x30000  the per-group extra collector     (0x20000 -- yes)
//
// The blob's own FLAGS word, which is what the draw reads, is a separate argument and is ZERO:
// bit 0 clear means no M2 receivers, bit 1 clear means the coloured vertex stream, bit 2 clear
// means the winding test stays on.
const float PROJECTOR_BIAS = 0.5f;
const uint32_t PROJECTOR_QUERY_MASK = 0x220122;
const uint32_t PROJECTOR_FLAGS = 0;

// DAT_009e1134: a decal whose box is thinner than a millimetre in x or y gets no transforms at all.
const float DECAL_MIN_EXTENT = 0.001f;

// DAT_009f1ff4, the quarter turn the footprint square is built with. The reference builds it as a
// rotation about (0, 0, 1) through an arbitrary-axis constructor; on an identity matrix that is
// exactly a rotation around z.
const float DECAL_TURN = -1.5707963705062866f;

// The trapezoid both ramps are built from. Along the row, t runs 0 .. RAMP_FULL; it fades up over
// the first RAMP_RISE, holds at 1 until RAMP_FALL and fades back down. The three constants were
// read out of the image on 2026-09-26: DAT_00a4040c is 2.0, DAT_009e30cc is 10.0 and DAT_00a1047c
// is 12.0, so with 64 texels the soft ends are about ten texels each.
const float RAMP_RISE = 2.0f;
const float RAMP_FALL = 10.0f;
const float RAMP_FULL = 12.0f;

float RampValue(uint32_t x, uint32_t width) {
    float t = (static_cast<float>(x) / static_cast<float>(width - 1)) * RAMP_FULL;

    if (t < RAMP_RISE) {
        return t * 0.5f;
    }

    if (t < RAMP_FALL) {
        return 1.0f;
    }

    float falling = (RAMP_FULL - t) * 0.5f;

    return falling > 0.0f ? falling : 0.0f;
}

// ref: FUN_007e36e0
// "ShadowAdd": white with the trapezoid in the alpha channel, for the additive stage.
void ShadowAddCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Lock) {
        s_rampCount = width * height;
        return;
    }

    if (cmd != GxTex_Latch || mipLevel != 0) {
        return;
    }

    stride = width * 4;
    texels = s_rampPixels;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t alpha = static_cast<uint32_t>(lroundf(RampValue(x, width) * 255.0f));

            s_rampPixels[y * width + x] = (alpha << 24) | 0x00ffffff;
        }
    }
}

// ref: FUN_007e3820
// "ShadowMod": the INVERSE of the trapezoid as a grey, for the modulating stage. Its alpha is
// opaque except where the grey has gone fully white, which the reference writes as a compare
// against 0xff rather than a second ramp.
void ShadowModCallback(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& stride, const void*& texels) {
    if (cmd == GxTex_Lock) {
        s_rampCount = width * height;
        return;
    }

    if (cmd != GxTex_Latch || mipLevel != 0) {
        return;
    }

    stride = width * 4;
    texels = s_rampPixels;

    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            uint32_t grey = static_cast<uint32_t>(lroundf((1.0f - RampValue(x, width)) * 255.0f)) & 0xff;
            uint32_t alpha = grey == 0xff ? 0x00 : 0xff;

            s_rampPixels[y * width + x] = (alpha << 24) | (grey << 16) | (grey << 8) | grey;
        }
    }
}

// The ramps' update rect, {minY, minX, maxY, maxX}, which is the whole texture.
CiRect RampRect() {
    CiRect rect;
    rect.minY = 0;
    rect.minX = 0;
    rect.maxY = static_cast<int32_t>(RAMP_HEIGHT);
    rect.maxX = static_cast<int32_t>(RAMP_WIDTH);

    return rect;
}

// ref: FUN_007e3a20
bool ShadowLODCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    uint32_t lod = 0;

    if (sscanf(value, "%d", &lod) != 1) {
        lod = 0;
    }

    if (lod < 2) {
        ShadowSetLOD(static_cast<int32_t>(lod));

        char message[256];
        snprintf(message, sizeof(message), "Shadow LOD set to %d", g_shadowLOD);
        ConsoleWrite(message, DEFAULT_COLOR);

        return true;
    }

    ConsoleWrite("Shadow LOD must be in the range (0, 1)", DEFAULT_COLOR);

    return false;
}

} // namespace

int32_t g_shadowLOD = 0;

HTEXTURE ShadowBlobTexture() {
    return s_blobTexture;
}

// ref: FUN_007e2c40
CGxTex* ShadowModGxTex() {
    return TextureGetGxTex(s_modTexture, 1, nullptr);
}

// ref: FUN_007e2c60
CGxTex* ShadowAddGxTex() {
    return TextureGetGxTex(s_addTexture, 1, nullptr);
}

// ref: FUN_007e4a40
// Called from the client's init right after CWorld::Initialize, where the reference calls it.
void ShadowInit() {
    CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

    // TextureCreate refuses a null status, as the reference's does.
    CStatus status;

    s_blobTexture = TextureCreate("Textures\\ShadowBlob.blp", flags, &status, 1);


    s_addTexture = TextureCreate(
        RAMP_WIDTH, RAMP_HEIGHT, GxTex_Argb8888, GxTex_Argb8888,
        CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), nullptr, ShadowAddCallback, "ShadowAdd", 0);

    s_modTexture = TextureCreate(
        RAMP_WIDTH, RAMP_HEIGHT, GxTex_Argb8888, GxTex_Argb8888,
        CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), nullptr, ShadowModCallback, "ShadowMod", 0);

    // The LOD CVar owns the ramps: its callback is what generates them, and its default of "1"
    // means blobs are on unless something turns them off.
    s_shadowLODVar = CVar::Register(
        "shadowLOD", "Unit shadow LOD", 0x1, "1", &ShadowLODCallback, GRAPHICS, false, nullptr, false);

    s_extShadowQualityVar = CVar::Lookup("extShadowQuality");
}

// ref: FUN_007e2c80
void ShadowDestroy() {
    if (s_blobTexture) {
        HandleClose(s_blobTexture);
    }

    if (s_modTexture) {
        HandleClose(s_modTexture);
    }

    if (s_addTexture) {
        HandleClose(s_addTexture);
    }

    s_blobTexture = nullptr;
    s_modTexture = nullptr;
    s_addTexture = nullptr;
}

// ref: FUN_007e3980
// Turning blobs on is what generates the ramps: the reference re-installs each generator on its
// texture and then latches both, rather than relying on the create-time callback firing.
void ShadowSetLOD(int32_t lod) {
    g_shadowLOD = lod;

    if (lod != 1) {
        return;
    }

    TextureSetUpdateCallback(s_addTexture, ShadowAddCallback, nullptr);
    TextureSetUpdateCallback(s_modTexture, ShadowModCallback, nullptr);

    CiRect rect = RampRect();

    auto add = TextureGetGxTex(s_addTexture, 1, nullptr);

    if (add) {
        GxTexUpdate(add, rect, 0);
    }

    auto mod = TextureGetGxTex(s_modTexture, 1, nullptr);

    if (mod) {
        GxTexUpdate(mod, rect, 0);
    }
}

// ref: FUN_007e2d60
// The two TEXTURE transforms a projected decal draws through, and the reason the decal never has to
// reproduce anything about the receiver: the receiver's own vertices are drawn as they are, and
// these two matrices turn their positions into the decal's texture coordinates. FUN_007e4370 pushes
// them as GxXform_Tex0 and GxXform_Tex1, so stage 0 gets the blob and stage 1 gets a fade ramp.
//
// It was linked to Liquid::IMaterial::Draw by the matcher and it is nothing of the kind -- it sits
// inside the blob shadow's own module. Three reference decal kinds call it.
//
//   stage 0  = Translate(offset) * diag(1/width, 1/height, 1, 1) * RotateZ(-pi/2)
//              then *= `extra` if one is given, then +0.5 on the translation's x and y
//   stage 1  = Translate(offset) * a matrix whose only non-zero terms are
//              c0 = c2 = 1/depth, d0 = d2 = `bias`, d1 = d3 = 1
//
// stage 0 is the footprint: the box's own width and height become the 0..1 texture square, turned a
// quarter turn, and the +0.5 puts the box centre at the middle of the texture. stage 1 is the fade
// ALONG the projection axis -- it maps the box's depth into u, leaves v at 1, and so reads one row
// of the 64x8 ShadowAdd / ShadowMod ramp. That is what those two textures are for.
//
// `absolute` picks the space: zero makes the offset camera-relative (camera - centre), which is what
// the decal pass wants because FUN_007e4370 draws with world and view set to identity; non-zero just
// negates the centre.
//
// Does nothing at all when the box is thinner than a millimetre in x or y (DAT_009e1134 is 0.001).
void DecalBuildTransforms(C44Matrix& stage0, C44Matrix& stage1, const CAaBox& box, const C44Matrix* extra, float bias, int32_t absolute) {
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    C3Vector centre = {
        (box.t.x + box.b.x) * 0.5f,
        (box.t.y + box.b.y) * 0.5f,
        (box.t.z + box.b.z) * 0.5f
    };

    C3Vector offset;

    if (absolute == 0) {
        offset.x = cameraPos.x - centre.x;
        offset.y = cameraPos.y - centre.y;
        offset.z = cameraPos.z - centre.z;
    } else {
        offset.x = -centre.x;
        offset.y = -centre.y;
        offset.z = -centre.z;
    }

    C44Matrix translate;
    translate.Identity();
    translate.Translate(offset);

    float width = box.t.x - box.b.x;
    float height = box.t.y - box.b.y;
    float depth = box.t.z - box.b.z;

    if (width < DECAL_MIN_EXTENT || height < DECAL_MIN_EXTENT) {
        return;
    }

    // The footprint square, a quarter turn, and the half-texel centring.
    C44Matrix scale;
    scale.Identity();
    scale.a0 = 1.0f / width;
    scale.b1 = 1.0f / height;

    stage0 = translate * scale * C44Matrix::RotationAroundZ(DECAL_TURN);

    if (extra) {
        stage0 *= *extra;
    }

    stage0.d0 += 0.5f;
    stage0.d1 += 0.5f;

    // The ramp lookup along the projection axis. Every term the reference does not write is zero,
    // including the whole first two rows, so this is built from scratch rather than from identity.
    C44Matrix ramp;
    ramp.a0 = 0.0f; ramp.a1 = 0.0f; ramp.a2 = 0.0f; ramp.a3 = 0.0f;
    ramp.b0 = 0.0f; ramp.b1 = 0.0f; ramp.b2 = 0.0f; ramp.b3 = 0.0f;
    ramp.c0 = 1.0f / depth; ramp.c1 = 0.0f; ramp.c2 = 1.0f / depth; ramp.c3 = 0.0f;
    ramp.d0 = bias; ramp.d1 = 1.0f; ramp.d2 = bias; ramp.d3 = 1.0f;

    stage1 = translate * ramp;
}

// The constant every streamed decal vertex carries in its second field, DAT_00af4644: straight up.
// It is what the texgen turns into the two stages' texture coordinates.
const C3Vector DECAL_VERTEX_NORMAL = { 0.0f, 0.0f, 1.0f };

// ref: FUN_007e2fd0
// One hit record's triangles into a vertex stream: 0x18 bytes each, the position followed by that
// constant up vector, no indices of their own -- three vertices per triangle, in order.
//
// The winding test is the receiver selection, and it is the whole reason a wall never gets a blob:
// a triangle is kept only when its XY cross product is non-negative, i.e. it faces up. Bit 2 of the
// flags skips the test; the blob's mask (0x220122) has that bit clear, so blobs are upward-only.
//
// A record with a HEIGHT ARRAY takes the other path entirely, and that is the terrain receiver's:
// one vertex per index in index order, no triangle expansion and no winding test, with each vertex's
// z read out of the array instead of out of the shared vertex table. It leaves batch.m_count alone,
// which is why the caller sets it to the index count before calling either builder.
static void DecalStreamReceiver(const CMapObjHitRecord& record, CGxBatch& batch, uint32_t flags) {
    auto stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x18, record.indexCount);

    if (!stream) {
        return;
    }

    auto vertices = reinterpret_cast<float*>(g_theGxDevicePtr->BufLock(stream));

    if (!vertices) {
        return;
    }

    if (record.heights) {
        float* out = vertices;

        for (uint32_t i = 0; i < record.indexCount; i++) {
            uint16_t v = record.indices[i];

            *out++ = record.vertices[v].x;
            *out++ = record.vertices[v].y;
            *out++ = record.heights[v];
            *out++ = DECAL_VERTEX_NORMAL.x;
            *out++ = DECAL_VERTEX_NORMAL.y;
            *out++ = DECAL_VERTEX_NORMAL.z;
        }

        g_theGxDevicePtr->BufUnlock(stream, 0);
        stream->unk1C = 1;
        GxPrimVertexPtr(stream, GxVBF_PN);

        return;
    }

    batch.m_count = 0;

    bool testWinding = (flags & 4) == 0;
    float* out = vertices;

    for (uint32_t i = 0; i + 2 < record.indexCount; i += 3) {
        const C3Vector& a = record.vertices[record.indices[i]];
        const C3Vector& b = record.vertices[record.indices[i + 1]];
        const C3Vector& c = record.vertices[record.indices[i + 2]];

        if (testWinding) {
            float cross = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);

            if (cross < 0.0f) {
                continue;
            }
        }

        const C3Vector* corner[3] = { &a, &b, &c };

        for (int32_t k = 0; k < 3; k++) {
            *out++ = corner[k]->x;
            *out++ = corner[k]->y;
            *out++ = corner[k]->z;
            *out++ = DECAL_VERTEX_NORMAL.x;
            *out++ = DECAL_VERTEX_NORMAL.y;
            *out++ = DECAL_VERTEX_NORMAL.z;
        }

        batch.m_count += 3;
    }

    g_theGxDevicePtr->BufUnlock(stream, 0);
    stream->unk1C = 1;
    GxPrimVertexPtr(stream, GxVBF_PN);
}

// The state the collection leaves for the two walks to read back, because the reference passes it
// between them in callee-saved registers across a static call chain rather than as arguments:
// DAT_00d38058..6c is the caster's box and DAT_00d38050 the query mask. Reproducing the register
// convention is neither possible nor desirable in C++, so the walks take them as arguments and this
// keeps them where the reference keeps them for anything else that reads them.
CAaBox s_decalCasterBox = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
uint32_t s_decalQueryMask = 0;

// DAT_00d38010, DAT_00d38014 and DAT_00d38054: the second receiver list, up to ten M2 models, and
// the flag that says to collect it. Only a caller whose flags have bit 0 set gets one, which the
// blob never does.
int32_t s_decalCollectM2 = 0;
void* s_decalM2Receivers[10] = { nullptr };
uint32_t s_decalM2ReceiverCount = 0;

// ref: FUN_007e35f0
// Collect the receivers under a caster: reset the pools, stash the box and the mask, run whichever
// halves the arguments ask for, and say whether anything was found. `wantM2` and `wantHits` are the
// reference's own out-parameters, and the walk reads them to decide which of its two loops to run.
//
// The five counters reset here are the hit-record pools CMapObjGroup owns, the last of them the
// cursor into the 32 placement matrices the terrain collector hands out -- a terrain chunk has no
// instance matrix of its own for a record to point at, so it gets one from there.
int32_t DecalCollectReceivers(const CAaBox& casterBox, uint32_t queryMask, uint32_t flags, uint32_t* wantM2, uint32_t* wantHits) {
    *wantM2 = flags & 1;

    s_decalM2ReceiverCount = 0;
    s_decalQueryMask = 0;
    s_decalCollectM2 = 0;

    *wantHits = queryMask != 0 ? 1 : 0;

    s_decalCasterBox = casterBox;

    // NOT PORTED: the reference copies the global at 0x00cd7690 into 0x00af3e0c here (0x007e3645).
    // Neither has a frozen counterpart, and nothing on this path reads the copy back.

    if (*wantM2) {
        s_decalCollectM2 = 1;

        // Up to ten building doodads under the caster, through the one-line wrapper FUN_0077f350.
        // The mask is a constant, 0x2000000 (doodads flagged 0x1000), not the caller's query mask
        // (0x007e3659). The blob's flags leave bit 0 clear, so blobs never take this path.
        s_decalM2ReceiverCount = MapQueryBoxModels(reinterpret_cast<CM2Model**>(s_decalM2Receivers), 10,
                                                   casterBox, 0x2000000);
    }

    if (*wantHits) {
        CMapObjGroup::s_hitFlags = 0;
        CMapObjGroup::s_hitRecordCount = 0;
        CMapObjGroup::s_hitFacePoolCount = 0;
        CMapObjGroup::s_hitIndexPoolCount = 0;
        CMapObjGroup::s_hitPlacementCount = 0;

        s_decalQueryMask = queryMask;

        // The query itself, reached in the reference through the one-line wrapper FUN_0077f340. Its
        // terrain half is ported and is what puts a blob on the ground; the map-object half is not,
        // and MapQueryBox carries the note saying what it still needs.
        //
        // The reference's three arguments all arrive in registers, the box in ESI and the mask in
        // EBX, and the third -- the owner a hit record is stamped with -- is never set on this path.
        // Only the map-object half reads it: a terrain record is stamped with its own chunk.
        MapQueryBox(s_decalCasterBox, nullptr, queryMask);
    }

    return (s_decalM2ReceiverCount != 0 || CMapObjGroup::s_hitRecordCount != 0) ? 1 : 0;
}

// ref: FUN_006c42f0
// Swap red and blue when the device wants RGBA rather than ARGB. The reference calls this on any
// colour it is about to put in a vertex stream by hand; five call sites share it.
static void DecalFixupColor(uint32_t& color) {
    if (GxCaps().m_colorFormat != GxCF_rgba) {
        return;
    }

    uint32_t b = color & 0xff;
    uint32_t g = (color >> 8) & 0xff;
    uint32_t r = (color >> 16) & 0xff;
    uint32_t a = (color >> 24) & 0xff;

    color = (a << 24) | (b << 16) | (g << 8) | r;
}

// ref: FUN_007e32f0
// The stream the BLOB takes: 0x10 bytes a vertex, position then one packed colour, which is
// GxVBF_PC. The colour is the same for every vertex -- the decal's own, already fixed up for the
// device's byte order -- so this is a flat tint, not per-vertex shading.
//
// Same two branches as the plain builder: the winding test on bit 2 of the flags, and the height-
// array path a terrain receiver takes.
static void DecalStreamReceiverColored(const CMapObjHitRecord& record, CGxBatch& batch, uint32_t color, uint32_t flags) {
    auto stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Vertex, 0x10, record.indexCount);

    if (!stream) {
        return;
    }

    auto vertices = reinterpret_cast<uint32_t*>(g_theGxDevicePtr->BufLock(stream));

    if (!vertices) {
        return;
    }

    if (record.heights) {
        uint32_t* out = vertices;

        for (uint32_t i = 0; i < record.indexCount; i++) {
            uint16_t v = record.indices[i];

            auto position = reinterpret_cast<float*>(out);
            position[0] = record.vertices[v].x;
            position[1] = record.vertices[v].y;
            position[2] = record.heights[v];
            out[3] = color;
            out += 4;
        }

        g_theGxDevicePtr->BufUnlock(stream, 0);
        stream->unk1C = 1;
        GxPrimVertexPtr(stream, GxVBF_PC);

        return;
    }

    batch.m_count = 0;

    bool testWinding = (flags & 4) == 0;
    uint32_t* out = vertices;

    for (uint32_t i = 0; i + 2 < record.indexCount; i += 3) {
        const C3Vector& a = record.vertices[record.indices[i]];
        const C3Vector& b = record.vertices[record.indices[i + 1]];
        const C3Vector& c = record.vertices[record.indices[i + 2]];

        if (testWinding) {
            float cross = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);

            if (cross < 0.0f) {
                continue;
            }
        }

        const C3Vector* corner[3] = { &a, &b, &c };

        for (int32_t k = 0; k < 3; k++) {
            auto position = reinterpret_cast<float*>(out);
            position[0] = corner[k]->x;
            position[1] = corner[k]->y;
            position[2] = corner[k]->z;
            out[3] = color;
            out += 4;
        }

        batch.m_count += 3;
    }

    g_theGxDevicePtr->BufUnlock(stream, 0);
    stream->unk1C = 1;
    GxPrimVertexPtr(stream, GxVBF_PC);
}

// ref: FUN_007e3580
// The index stream that goes with it: 0, 1, 2, ... one per streamed vertex, because the stream
// builder above already expanded the triangles.
static void DecalStreamIndices(const CMapObjHitRecord& record) {
    auto stream = g_theGxDevicePtr->BufStream(GxPoolTarget_Index, 2, record.indexCount);

    if (!stream) {
        return;
    }

    auto indices = reinterpret_cast<uint16_t*>(g_theGxDevicePtr->BufLock(stream));

    if (!indices) {
        return;
    }

    for (uint32_t i = 0; i < record.indexCount; i++) {
        indices[i] = static_cast<uint16_t>(i);
    }

    g_theGxDevicePtr->BufUnlock(stream, 0);
    stream->unk1C = 1;
    g_theGxDevicePtr->PrimIndexPtr(stream);
}

// ref: FUN_007e3e80
// The receiver walk: every hit record a query left behind, drawn in the receiver's own space with
// the decal's two texture matrices already pushed.
//
// The ramp on stage 1 is chosen by the blending mode ALREADY SET, which is how one function serves
// several decal kinds: mode 4 takes ShadowMod with ColorOp1 and AlphaOp1 at 2, anything else takes
// ShadowAdd with both at 0. ShadowProjectBlob sets mode 4, so a blob uses ShadowMod.
//
// The texgen is what makes the projection work: both stages generate their coordinates from the
// vertex position, and the matrices DecalBuildTransforms built do the rest. The receiver's own
// triangles draw untransformed, so nothing here depends on how the receiver's base pass drew them.
//
// Polygon offset comes from the strength -- twice it, scaled by about 1/32768 -- which is the
// reference's whole answer to coplanar depth on this path. The blob's 0.4 gives about 2.4e-05.
void DecalDrawReceivers(const CAaBox& casterBox, const CImVector& color, uint32_t queryMask, uint32_t flags, float strength) {
    uint32_t wantM2 = 0;
    uint32_t wantHits = 0;

    if (!DecalCollectReceivers(casterBox, queryMask, flags, &wantM2, &wantHits)) {
        return;
    }

    GxRsPush();

    // GxRs_Unk61 and GxRs_Unk62 have no name in frozen's enum yet; the reference sets both to 1
    // here, alongside the two texgens.
    if (g_theGxDevicePtr->m_appRenderStates[GxRs_BlendingMode].m_value == 4) {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, ShadowModGxTex());
        GxRsSet(GxRs_ColorOp1, 2);
        GxRsSet(GxRs_AlphaOp1, 2);
    } else {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, ShadowAddGxTex());
        GxRsSet(GxRs_ColorOp1, 0);
        GxRsSet(GxRs_AlphaOp1, 0);
    }

    if (strength != 0.0f) {
        GxRsSet(GxRs_PolygonOffset, (strength + strength) * DECAL_OFFSET_SCALE);
    }

    GxRsSet(GxRs_TexGen0, 2);
    GxRsSet(GxRs_TexGen1, 2);
    GxRsSet(GxRs_Unk61, 1);
    GxRsSet(GxRs_Unk62, 1);

    // Bit 1 of the flags decides where the colour goes, and the blob's flags are ZERO, so the blob
    // takes the first arm: the colour is fixed up for the device's byte order and written into every
    // streamed vertex. The other arm puts it in a single material constant instead.
    uint32_t streamColor = color.value;

    if ((flags & 2) == 0) {
        DecalFixupColor(streamColor);
    } else {
        GxRsSet(GxRs_MatDiffuse, color.value);
    }

    // Every receiver draws in its own placement with the camera translation folded out, which is
    // the same convention the WMO passes use.
    const C3Vector& cameraPos = CWorld::GetCameraPos();

    C44Matrix toCamera;
    toCamera.Identity();
    C3Vector back = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
    toCamera.Translate(back);

    g_theGxDevicePtr->XformPush(GxXform_World);

    for (uint32_t i = 0; wantHits && i < CMapObjGroup::s_hitRecordCount; i++) {
        const CMapObjHitRecord& record = CMapObjGroup::s_hitRecords[i];

        // The stream the expansion needs must fit; the reference caps it at 0x10000 vertices.
        if (static_cast<uint32_t>(record.indexCount) * 3 >= 0x10001) {
            continue;
        }

        if (!record.placement || !record.vertices || !record.indices) {
            continue;
        }

        GxXformSet(GxXform_World, *record.placement * toCamera);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = record.indexCount;
        batch.m_minIndex = 0;
        batch.m_maxIndex = static_cast<uint16_t>(record.indexCount - 1);

        // Bit 1 clear takes the coloured 0x10-byte stream, which is the blob's path; set takes the
        // plain 0x18-byte one with the constant up normal.
        if ((flags & 2) == 0) {
            DecalStreamReceiverColored(record, batch, streamColor, flags);
        } else {
            DecalStreamReceiver(record, batch, flags);
        }

        DecalStreamIndices(record);

        if (batch.m_count != 0) {
            GxDraw(&batch, 1);
        }
    }

    // TODO the reference's SECOND receiver list, DAT_00d38014 with its count at DAT_00d38054: up to
    // ten M2 receivers, each drawn through its own matrix at +0xb4. THE DRAW ITSELF IS NOW PORTED
    // -- FUN_00829aa0 is CM2Model::DrawReceiverGeometry -- so what is still missing here is only
    // the list and the per-receiver matrix, not the geometry.
    // It is gated on bit 0 of the flags, which the blob leaves clear, so a blob never collects M2
    // receivers at all -- that list belongs to whichever decal kind passes an odd flag word. The
    // render inventory's open "M2 receivers" item is about the M2 SHADOW pass, not this.
    (void)wantM2;

    g_theGxDevicePtr->XformPop(GxXform_World);
    GxRsPop();
}

// The colour as four floats in 0..1, red first (FUN_00984c90; CWorldScene.cpp carries the tag).
static void ImVectorToFloats(float* out, const CImVector& color) {
    out[0] = color.r / 255.0f;
    out[1] = color.g / 255.0f;
    out[2] = color.b / 255.0f;
    out[3] = color.a / 255.0f;
}

// ref: FUN_007e3aa0
// The receiver walk for a decal whose texture matrices and texgen the CALLER has already bound --
// the projected-texture path, where CM2SceneRender::DrawBatchProj sets both stages before calling
// the scene's projection callback. So unlike DecalDrawReceivers there is no state push and no
// texgen here: only stage 1's ramp (ShadowMod under blend mode 4, otherwise ShadowAdd when the
// stage is still empty), the polygon offset from the strength, restored at the end, and the two
// receiver lists -- the map's hit records in their own placements, then the M2 receivers through
// their own matrices with the colour as a material constant.
void DecalDrawBoundReceivers(const CAaBox& bounds, const CImVector& color, uint32_t queryMask, uint32_t flags, float strength) {
    uint32_t wantM2 = 0;
    uint32_t wantHits = 0;

    if (!DecalCollectReceivers(bounds, queryMask, flags, &wantM2, &wantHits)) {
        return;
    }

    if (g_theGxDevicePtr->m_appRenderStates[GxRs_BlendingMode].m_value == 4) {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, ShadowModGxTex());
    } else if (g_theGxDevicePtr->m_appRenderStates[GxRs_Texture1].m_value == static_cast<void*>(nullptr)) {
        g_theGxDevicePtr->RsSet(GxRs_Texture1, ShadowAddGxTex());
    }

    float savedOffset = 0.0f;

    if (strength != 0.0f) {
        savedOffset = static_cast<float>(g_theGxDevicePtr->m_appRenderStates[GxRs_PolygonOffset].m_value);
        GxRsSet(GxRs_PolygonOffset, (strength + strength) * DECAL_OFFSET_SCALE);
    }

    uint32_t streamColor = color.value;

    if ((flags & 2) == 0) {
        DecalFixupColor(streamColor);
    } else {
        C4Vector diffuse;
        ImVectorToFloats(reinterpret_cast<float*>(&diffuse), color);
        CShaderEffect::SetDiffuse(diffuse);
    }

    const C3Vector& cameraPos = CWorld::GetCameraPos();

    C44Matrix toCamera;
    toCamera.Identity();
    C3Vector back = { -cameraPos.x, -cameraPos.y, -cameraPos.z };
    toCamera.Translate(back);

    g_theGxDevicePtr->XformPush(GxXform_World);

    for (uint32_t i = 0; wantHits && i < CMapObjGroup::s_hitRecordCount; i++) {
        const CMapObjHitRecord& record = CMapObjGroup::s_hitRecords[i];

        if (static_cast<uint32_t>(record.indexCount) * 3 >= 0x10001) {
            continue;
        }

        if (!record.placement || !record.vertices || !record.indices) {
            continue;
        }

        GxXformSet(GxXform_World, *record.placement * toCamera);

        CGxBatch batch;
        batch.m_primType = GxPrim_Triangles;
        batch.m_start = 0;
        batch.m_count = record.indexCount;
        batch.m_minIndex = 0;
        batch.m_maxIndex = static_cast<uint16_t>(record.indexCount - 1);

        if ((flags & 2) == 0) {
            DecalStreamReceiverColored(record, batch, streamColor, flags);
        } else {
            DecalStreamReceiver(record, batch, flags);
        }

        DecalStreamIndices(record);
        CShaderEffect::SetShadersForGeometry(0);
        CShaderEffect::SetWorldViewConstants();

        if (batch.m_count != 0) {
            GxDraw(&batch, 1);
        }
    }

    for (uint32_t i = 0; wantM2 && i < s_decalM2ReceiverCount; i++) {
        auto model = static_cast<CM2Model*>(s_decalM2Receivers[i]);

        GxXformSet(GxXform_World, model->matrixB4 * toCamera);
        CShaderEffect::SetWorldViewConstants();
        C4Vector diffuse;
        ImVectorToFloats(reinterpret_cast<float*>(&diffuse), color);
        CShaderEffect::SetDiffuse(diffuse);
        model->DrawReceiverGeometry();
    }

    g_theGxDevicePtr->XformPop(GxXform_World);

    if (strength != 0.0f) {
        GxRsSet(GxRs_PolygonOffset, savedOffset);
    }
}

// ref: FUN_007e4370
// The wrapper eight reference decal kinds share: build the two texture transforms, push them as the
// stage 0 and stage 1 texture matrices, walk the receivers, pop.
//
// The identity world and view are the point. With both set to identity the receiver's vertices go
// down untransformed and the decal's whole contribution is the two texture matrices, so it never has
// to reproduce the receiver's depth -- which is exactly what frozen's deleted pass could not do.
//
// The reference pops by decrementing the device's own two push counters by hand rather than calling
// XformPop. frozen calls XformPop, which is the same thing said properly.
void DecalDrawProjected(const CAaBox& bounds, const CImVector& color, const C44Matrix& texMatrix, float bias, uint32_t queryMask, uint32_t flags, float strength) {
    C44Matrix stage0;
    stage0.Identity();

    C44Matrix stage1;
    stage1.Identity();

    DecalBuildTransforms(stage0, stage1, bounds, &texMatrix, bias, 0);

    g_theGxDevicePtr->XformPush(GxXform_Tex0, stage0);
    g_theGxDevicePtr->XformPush(GxXform_Tex1, stage1);

    // The walk runs the receiver query itself (DecalCollectReceivers, FUN_007e35f0) before it draws.
    DecalDrawReceivers(bounds, color, queryMask, flags, strength);

    g_theGxDevicePtr->XformPop(GxXform_Tex1);
    g_theGxDevicePtr->XformPop(GxXform_Tex0);
}

// ref: FUN_007e4480
// The projector: everything between "here is a caster's box" and "walk the receivers".
//
// The footprint is an ORIENTED rectangle, which is what the older notes in parity-shadows.md kept
// asking for. The caster's two half extents make a rectangle, the model's own rotation turns it, and
// the axis-aligned bounds of those four turned corners become the projection volume -- so a model
// standing at an angle gets a shadow at that angle rather than a circle.
//
// The scale comes from the length of the model matrix's FIRST ROW, so a scaled model gets a scaled
// footprint; the rotation is that matrix's 3x3 with the scale divided back out.
//
// The texture matrix that goes with it maps the axis-aligned volume back onto the turned rectangle:
// diag(boundsHeight, boundsWidth, 1) * transpose(rotation) * diag(1/xExtent, 1/yExtent, 1). The
// transpose is read off the argument order of the reference's 3x3 constructor, which picks the nine
// elements as 0 3 6 1 4 7 2 5 8. The two divisors are read as the footprint box's final extents;
// Ghidra reuses one local's name across the frame there, so that half is a reading rather than a
// measurement.
void ShadowProjectBlob(const CAaBox& casterBox, CM2Model* model, float strength) {
    float halfX = (casterBox.t.x - casterBox.b.x) * 0.5f;
    float halfY = (casterBox.t.y - casterBox.b.y) * 0.5f;

    if (fabsf(halfX) < PROJECTOR_EPSILON || fabsf(halfY) < PROJECTOR_EPSILON) {
        return;
    }

    if (fabsf(casterBox.t.z - casterBox.b.z) < PROJECTOR_EPSILON) {
        return;
    }

    // The axes swap because the footprint square is built through a quarter turn.
    CAaBox footprint;
    footprint.b.x = -halfY;
    footprint.b.y = -halfX;
    footprint.b.z = casterBox.b.z;
    footprint.t.x = halfY;
    footprint.t.y = halfX;
    footprint.t.z = casterBox.t.z;

    const C44Matrix& modelMatrix = model->matrixB4;

    float scale = sqrtf(
        modelMatrix.a0 * modelMatrix.a0 + modelMatrix.a1 * modelMatrix.a1 + modelMatrix.a2 * modelMatrix.a2);

    footprint.Scale(scale);

    // Both corners into the clamp cube, min then max, exactly as the reference does it.
    footprint.b.x = fminf(footprint.b.x, PROJECTOR_CLAMP);
    footprint.b.y = fminf(footprint.b.y, PROJECTOR_CLAMP);
    footprint.b.z = fminf(footprint.b.z, PROJECTOR_CLAMP);
    footprint.b.x = fmaxf(footprint.b.x, -PROJECTOR_CLAMP);
    footprint.b.y = fmaxf(footprint.b.y, -PROJECTOR_CLAMP);
    footprint.b.z = fmaxf(footprint.b.z, -PROJECTOR_CLAMP);

    footprint.t.x = fminf(footprint.t.x, PROJECTOR_CLAMP);
    footprint.t.y = fminf(footprint.t.y, PROJECTOR_CLAMP);
    footprint.t.z = fminf(footprint.t.z, PROJECTOR_CLAMP);
    footprint.t.x = fmaxf(footprint.t.x, -PROJECTOR_CLAMP);
    footprint.t.y = fmaxf(footprint.t.y, -PROJECTOR_CLAMP);
    footprint.t.z = fmaxf(footprint.t.z, -PROJECTOR_CLAMP);

    float halfHeight = (footprint.t.z - footprint.b.z) * 0.5f;

    C33Matrix rotation(modelMatrix);

    if (scale != 1.0f) {
        rotation *= 1.0f / scale;
    }

    C3Vector corners[4] = {
        { halfX, halfY, 0.0f },
        { halfX, -halfY, 0.0f },
        { -halfX, -halfY, 0.0f },
        { -halfX, halfY, 0.0f }
    };

    for (int32_t i = 0; i < 4; i++) {
        C3Vector p = corners[i];
        corners[i].x = p.x * rotation.a0 + p.y * rotation.b0 + p.z * rotation.c0;
        corners[i].y = p.x * rotation.a1 + p.y * rotation.b1 + p.z * rotation.c1;
        corners[i].z = p.x * rotation.a2 + p.y * rotation.b2 + p.z * rotation.c2;
    }

    CAaBox bounds;
    BoundsFromPoints(bounds, corners, 4);

    bounds.b.x += modelMatrix.d0;
    bounds.b.y += modelMatrix.d1;
    bounds.t.x += modelMatrix.d0;
    bounds.t.y += modelMatrix.d1;
    bounds.b.z = modelMatrix.d2 - PROJECTOR_BELOW * halfHeight;
    bounds.t.z = modelMatrix.d2 + PROJECTOR_ABOVE * halfHeight;

    float xExtent = footprint.t.x - footprint.b.x;
    float yExtent = footprint.t.y - footprint.b.y;

    if (fabsf(xExtent) < PROJECTOR_EPSILON || fabsf(yExtent) < PROJECTOR_EPSILON) {
        return;
    }

    C33Matrix boundsScale(
        bounds.t.y - bounds.b.y, 0.0f, 0.0f,
        0.0f, bounds.t.x - bounds.b.x, 0.0f,
        0.0f, 0.0f, 1.0f);

    C33Matrix footprintScale(
        1.0f / fabsf(xExtent), 0.0f, 0.0f,
        0.0f, 1.0f / fabsf(yExtent), 0.0f,
        0.0f, 0.0f, 1.0f);

    C44Matrix texMatrix(boundsScale * rotation.Transpose() * footprintScale);

    GxRsPush();
    GxRsSet(GxRs_BlendingMode, 4);

    // The reference names the state here rather than calling a no-argument helper; frozen's
    // GxRsSetAlphaRef is the same store.
    GxRsSetAlphaRef();

    GxRsSet(GxRs_Lighting, 0);
    GxRsSet(GxRs_Fog, 0);
    GxRsSet(GxRs_DepthWrite, 0);

    auto blob = TextureGetGxTex(s_blobTexture, 1, nullptr);
    g_theGxDevicePtr->RsSet(GxRs_Texture0, blob);

    GxRsSet(GxRs_FogColor, 0xffffffffu);
    GxRsSet(GxRs_ColorOp0, 5);
    GxRsSet(GxRs_AlphaOp0, 3);

    // The depth-EQUAL test is on ONE branch only: the reference takes it when the strength argument
    // is zero. At the blob call site the strength is 0.4, so the blob shadow does NOT use it.
    if (strength == 0.0f) {
        GxRsSet(GxRs_DepthFunc, 1);
    }

    // The per-model strength, clamped, as the alpha of a white colour. The reference reads it from
    // model+0x178, which is m_baseAlpha here -- the comment on that member already says the
    // reference keeps the base tint block at 0x178..0x194, so this is the same field.
    float modelStrength = model->m_baseAlpha;

    if (modelStrength < 0.0f) {
        modelStrength = 0.0f;
    } else if (modelStrength > 1.0f) {
        modelStrength = 1.0f;
    }

    CImVector color;
    color.b = 0xff;
    color.g = 0xff;
    color.r = 0xff;
    color.a = static_cast<uint8_t>(static_cast<int32_t>(modelStrength * 255.0f + 0.5f));

    DecalDrawProjected(bounds, color, texMatrix, PROJECTOR_BIAS, PROJECTOR_QUERY_MASK, PROJECTOR_FLAGS, strength);

    GxRsPop();
}

// ref: FUN_007e49e0
// The per-caster gate, and the five conditions are all the reference's:
//
//   the model exists and is drawable,
//   its per-frame flag 0x4000 is clear,
//   the caster's box is not degenerate,
//   the extShadowQuality CVar reads BELOW 1, and
//   the shadow LOD is exactly 1.
//
// The fourth one is worth stating plainly because it is not obvious and it decides what a player
// sees: BLOB SHADOWS AND THE SHADOW MAP ARE MUTUALLY EXCLUSIVE. Raising extShadowQuality turns
// every blob off, because the map shadow is then drawing the same shadows properly. Frozen's
// shadow quality gate landed in the same cycle as this, so the two now agree.
//
// The box comes from the CALLER, not from the model: a unit's caster box is the one its animation
// authored (CGUnit_C::GetShadowBox) and a doodad's is its current sequence's extent. The reference
// passes it in as the implicit argument, which is why this takes it first.
void ShadowDrawBlob(const CAaBox& casterBox, CM2Model* model) {
    if (!model || !model->IsDrawable(0, 0)) {
        return;
    }

    // m_flag4000, NOT a bit in m_flags: the reference reads the +0x10 state word, and the two
    // storages are not interchangeable (see the note on CM2Model::m_flags).
    if (model->m_flag4000) {
        return;
    }

    if (AaBoxIsDegenerate(casterBox)) {
        return;
    }

    if (s_extShadowQualityVar && s_extShadowQualityVar->GetInt() >= 1) {
        return;
    }

    if (g_shadowLOD != 1) {
        return;
    }

    // BLOB_STRENGTH is the constant at the reference's own call site, not a choice.
    ShadowProjectBlob(casterBox, model, BLOB_STRENGTH);
}
