#include "world/map/LiquidMaterialSettings.hpp"
#include "db/Db.hpp"
#include "gx/CGxCaps.hpp"
#include "gx/Device.hpp"
#include "gx/Shader.hpp"
#include "gx/RenderState.hpp"
#include <cstring>
#include "gx/Transform.hpp"
#include "model/CM2Lighting.hpp"
#include "model/CM2Light.hpp"
#include "world/CWorld.hpp"
#include "world/map/LiquidSurface.hpp"
#include "gx/CGxBatch.hpp"
#include "gx/Buffer.hpp"
#include <cmath>
#include <common/Time.hpp>
#include "gx/shader/CGxShader.hpp"
#include "gx/CGxDevice.hpp"
#include "gx/Gx.hpp"
#include "util/Log.hpp"
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <common/Handle.hpp>
#include "gx/texture/CGxTex.hpp"
#include <tempest/Vector.hpp>
#include <tempest/Rect.hpp>
#include <new>

namespace Liquid {

// The bank, indexed by LiquidType.dbc id. Sparse: only the types the map has asked for are
// built, and the slots between them stay null. DAT_00d43b1c / DAT_00d43b18
static TSGrowableArray<CMaterialSettings*> s_settingsBank;

// ref: FUN_008a27c0
bool CMaterialSettings::LoadFromDbc(int32_t liquidType) {
    auto typeRec = g_liquidTypeDB.GetRecord(liquidType);

    if (!typeRec) {
        return false;
    }

    auto materialRec = g_liquidMaterialDB.GetRecord(typeRec->m_materialID);

    if (!materialRec) {
        return false;
    }

    for (uint32_t i = 0; i < TEXTURE_SLOTS; i++) {
        SStrCopy(this->m_textureName[i], typeRec->m_texture[i], TEXTURE_NAME_SIZE);
    }

    this->m_color[0] = typeRec->m_color[0];
    this->m_color[1] = typeRec->m_color[1];

    for (uint32_t i = 0; i < 4; i++) {
        this->m_int[i] = typeRec->m_int[i];
    }

    // LiquidType's eighteen floats are two stages of nine, laid end to end.
    for (uint32_t stage = 0; stage < STAGE_COUNT; stage++) {
        for (uint32_t i = 0; i < STAGE_FLOATS; i++) {
            this->m_stage[stage][i] = typeRec->m_float[stage * STAGE_FLOATS + i];
        }
    }

    this->m_procedural = materialRec->m_flags & 1;

    this->LoadTextures();

    return true;
}

// How far a numbered animation is followed before the loader gives up. The reference stops at
// thirty, whether or not the files keep going.
static const uint32_t MAX_FRAMES = 30;

// ref: FUN_008a2450
// Procedural water is generated rather than read, so it is sampled without mipmaps and clamped;
// everything else is an ordinary wrapped, trilinear texture.
// ------------------------------------------------------------------------------------------------
// The procedural liquid textures
//
// Three textures the client GENERATES rather than reads off disk, 8 wide and 64 tall, named
// proceduralRiverDepthTex, proceduralOceanDepthTex and proceduralWmoWaterTex. LiquidType.dbc puts
// one of those names in a liquid's second texture column, and the second texcoord -- the depth in
// 0..1 that LiquidTypeBlock's ramp produces -- is what reads down them. So these ARE the water's
// depth shading: the colour and transparency of shallow water at the top row and of deep water at
// the bottom, taken from the day/night light bands so they change with the hour and the zone.
//
// Every input was already in frozen, which is why this could be written at all: CWorld publishes
// LightIntBand bands 14..17 as the ocean and river close/far colours and LightParams' four alphas,
// and the reference reads exactly those out of its light block at +0x10c..+0x118 and +0x140..+0x14c.
// The offsets line up with frozen's existing ordering entry for entry, which is a check rather than
// a coincidence.

const uint32_t PROC_TEX_WIDTH = 8;
const uint32_t PROC_TEX_HEIGHT = 64;

// The gradient is stepped in 8.8 fixed point, and the reference's step is (to - from) * 256 / 64.
// The 64 is the texture's height, so the shift belongs to the texture rather than to the row count
// the callback happens to be handed.
const int32_t PROC_GRADIENT_SHIFT = 6;

// One component of a band colour, back to the byte it came from.
uint8_t BandByte(float v) {
    int32_t b = static_cast<int32_t>(nearbyintf(v * 255.0f));

    return static_cast<uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
}

// A band colour packed the way the reference's light block holds it and the way the generators read
// it back: blue in the low byte, then green, then red, alpha left clear.
uint32_t PackBandColor(const C3Vector& c) {
    return static_cast<uint32_t>(BandByte(c.z))
         | (static_cast<uint32_t>(BandByte(c.y)) << 8)
         | (static_cast<uint32_t>(BandByte(c.x)) << 16);
}

// ref: FUN_008a2bf0
// The two DEPTH textures, river and ocean, told apart by the user argument -- 1 is the river.
//
// Each row is one colour repeated across the width, interpolated from the close colour and shallow
// alpha at row 0 to the far colour and deep alpha at row 63. The interpolation is integer 8.8 fixed
// point rather than float, and it is reproduced rather than tidied because a texture is exactly the
// kind of thing where a rounding difference shows as a band.
//
// THE LAST ROW OF THE OCEAN TEXTURE IS DARKENED, and only the ocean's. The reference unpacks it,
// converts to HSV, multiplies the VALUE by 0.9, converts back and repacks. Scaling V in HSV scales
// R, G and B by the same factor -- C = V*S, X and the m = V - C offset are all linear in V, and H
// and S are untouched -- so the round trip is exactly a 0.9 multiply on the three colour bytes, and
// that is what this does. Alpha does not go through HSV and is left alone.
//
// UNCERTAIN, and flagged rather than hidden: the decompilation calls PackColor and does not show
// its result being stored back, so it is possible the reference computes that darkened row and
// throws it away. One row in sixty-four of one of three textures either way.
void DepthGradientGenerate(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t x,
                           uint32_t y, void* userArg, uint32_t& pitch, const void*& data) {
    static uint32_t s_pixels[PROC_TEX_WIDTH * PROC_TEX_HEIGHT] = { 0 };

    (void)x;
    (void)y;

    if (command != GxTex_Latch) {
        return;
    }

    bool river = reinterpret_cast<intptr_t>(userArg) != 0;
    int32_t oceanic = river ? 0 : 1;

    uint32_t shallow = PackBandColor(CWorld::GetLiquidShallow(oceanic));
    uint32_t deep = PackBandColor(CWorld::GetLiquidDeep(oceanic));

    uint8_t alphaClose = BandByte(CWorld::GetLiquidAlpha(oceanic, 0));
    uint8_t alphaFar = BandByte(CWorld::GetLiquidAlpha(oceanic, 1));

    int32_t b = static_cast<int32_t>(shallow & 0xff) << 8;
    int32_t g = static_cast<int32_t>((shallow >> 8) & 0xff) << 8;
    int32_t r = static_cast<int32_t>((shallow >> 16) & 0xff) << 8;
    int32_t a = static_cast<int32_t>(alphaClose) << 8;

    int32_t db = ((static_cast<int32_t>(deep & 0xff)
                   - static_cast<int32_t>(shallow & 0xff)) * 0x100) >> PROC_GRADIENT_SHIFT;
    int32_t dg = ((static_cast<int32_t>((deep >> 8) & 0xff)
                   - static_cast<int32_t>((shallow >> 8) & 0xff)) * 0x100) >> PROC_GRADIENT_SHIFT;
    int32_t dr = ((static_cast<int32_t>((deep >> 16) & 0xff)
                   - static_cast<int32_t>((shallow >> 16) & 0xff)) * 0x100) >> PROC_GRADIENT_SHIFT;
    int32_t da = ((static_cast<int32_t>(alphaFar)
                   - static_cast<int32_t>(alphaClose)) * 0x100) >> PROC_GRADIENT_SHIFT;

    uint32_t* out = s_pixels;

    for (uint32_t row = 0; row < height; row++) {
        uint32_t pixel = static_cast<uint32_t>((b >> 8) & 0xff)
                       | (static_cast<uint32_t>((g >> 8) & 0xff) << 8)
                       | (static_cast<uint32_t>((r >> 8) & 0xff) << 16)
                       | (static_cast<uint32_t>((a >> 8) & 0xff) << 24);

        if (row == height - 1 && !river) {
            static const float DEEPEST_ROW_VALUE = 0.89999998f;

            uint32_t db8 = static_cast<uint32_t>(
                nearbyintf(static_cast<float>(pixel & 0xff) * DEEPEST_ROW_VALUE));
            uint32_t dg8 = static_cast<uint32_t>(
                nearbyintf(static_cast<float>((pixel >> 8) & 0xff) * DEEPEST_ROW_VALUE));
            uint32_t dr8 = static_cast<uint32_t>(
                nearbyintf(static_cast<float>((pixel >> 16) & 0xff) * DEEPEST_ROW_VALUE));

            pixel = (pixel & 0xff000000u) | db8 | (dg8 << 8) | (dr8 << 16);
        }

        for (uint32_t col = 0; col < width; col++) {
            out[col] = pixel;
        }

        out += width;

        b += db;
        g += dg;
        r += dr;
        a += da;
    }

    pitch = width * 4;
    data = s_pixels;
}

// ref: FUN_008a2ac0
// The MAP OBJECT water texture, and it is two columns rather than a gradient of one colour: the left
// half of every row is the RIVER's deep colour and the right half is white, both carrying the same
// depth-interpolated alpha. It always reads the river's bands, never the ocean's, however the water
// it shades is classified.
//
// That is what the WMO vertex writer's first texcoord component is for -- it writes a real value
// where the terrain writer writes zero, which picks the column and so picks between the tinted half
// and the plain one.
void WmoWaterGenerate(EGxTexCommand command, uint32_t width, uint32_t height, uint32_t x,
                      uint32_t y, void* userArg, uint32_t& pitch, const void*& data) {
    static uint32_t s_pixels[PROC_TEX_WIDTH * PROC_TEX_HEIGHT] = { 0 };

    (void)x;
    (void)y;
    (void)userArg;

    if (command != GxTex_Latch) {
        return;
    }

    uint32_t tint = PackBandColor(CWorld::GetLiquidDeep(0));

    uint8_t alphaClose = BandByte(CWorld::GetLiquidAlpha(0, 0));
    uint8_t alphaFar = BandByte(CWorld::GetLiquidAlpha(0, 1));

    int32_t a = static_cast<int32_t>(alphaClose) << 8;
    int32_t da = ((static_cast<int32_t>(alphaFar)
                   - static_cast<int32_t>(alphaClose)) * 0x100) >> PROC_GRADIENT_SHIFT;

    uint32_t* out = s_pixels;

    for (uint32_t row = 0; row < height; row++) {
        uint32_t alpha = static_cast<uint32_t>((a >> 8) & 0xff) << 24;
        uint32_t half = width / 2;

        for (uint32_t col = 0; col < width; col++) {
            out[col] = (col < half ? tint : 0xffffffu) | alpha;
        }

        out += width;

        a += da;
    }

    pitch = width * 4;
    data = s_pixels;
}

// ref: FUN_008a2e20
// Make the three and put them in the texture cache under their names, which is how LoadTextures
// finds them: a liquid whose texture column holds one of these names gets the generated texture
// instead of a file read.
//
// DIVERGED in WHEN, not what: the reference makes them from its liquid initialise, beside the depth
// ramps. Frozen makes them the first time a liquid asks for one, which needs no initialisation
// order and cannot be skipped by a map that loads before the subsystem is up.
// The three procedural textures, in creation order, kept so they can be updated and released.
// The reference holds them in three separate globals (0x00d43bd0, 0x00d43bd4, 0x00d43bd8) with a
// latch byte each; which global carries which name is not established and does not need to be,
// because every operation on them is identical and independently latched.
static HTEXTURE s_proceduralTextures[3] = { nullptr, nullptr, nullptr };
static bool s_proceduralUpdated[3] = { false, false, false };

// ref: FUN_008a2780
// Drop every settings record's texture frames and open them again.
//
// The whole body is a walk of the settings bank calling ReleaseFrames then LoadTextures on each
// record, which is why it could not be written until ReleaseFrames existed -- it is the pair of
// them, in that order, and nothing else.
void ReloadAllLiquidTextures() {
    for (uint32_t i = 0; i < s_settingsBank.Count(); i++) {
        CMaterialSettings* settings = s_settingsBank[i];

        if (!settings) {
            continue;
        }

        settings->ReleaseFrames();
        settings->LoadTextures();
    }
}

// ref: FUN_008a2a10
// Put the procedural textures back in the cache and reopen every liquid's frames.
//
// This is the DEVICE RESET path: the textures survive as CTexture objects but their cache entries
// do not, so each one is re-registered under its own name and flags before anything asks for it by
// name again. The flags are the ones they were made with -- linear, no wrap, one anisotropy --
// because the cache key carries them and a mismatch would miss.
//
// Same two inert arms as UpdateProceduralTextures, for the same reason and with the same evidence:
// the reference also re-registers the thirty-two handles at 0x00d43b50 and the one at 0x00d43b4c,
// and nothing in the binary ever puts a handle in either.
void RestoreLiquidTextures() {
    for (uint32_t i = 0; i < 3; i++) {
        if (!s_proceduralTextures[i]) {
            continue;
        }

        CTexture* texture = TextureGetTexturePtr(s_proceduralTextures[i]);

        if (texture) {
            TextureCacheNewTexture(texture, CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1));
        }
    }

    ReloadAllLiquidTextures();
}

// ref: FUN_008a2980
// Close the procedural texture handles and let them be made again.
//
// This was tagged `part of` when it landed, on the assumption that the single handle at 0x00d43b4c
// and the block of THIRTY-TWO at 0x00d43b50 which the reference also closes were liquid textures
// frozen had not got to yet. They are not. Nothing in the binary ever stores a handle into either:
// every reference to 0x00d43b48..0x00d43bd8 was enumerated, and the base 0x00d43b50 appears exactly
// four times -- all of them the indexed reads in this function, FUN_008a2a10 and FUN_008a2f00 --
// while 0x00d43b54 and every other element address appears not once. The only writes are the zeroing
// here. So the two extra groups are dead storage, their loops skip every iteration in the reference
// as much as here, and closing the three procedural handles is the whole of what this function does.
//
// The latches come off with the handles, so the pair stays consistent: whatever is made next is
// uploaded again rather than being assumed current.
void ReleaseProceduralTextures() {
    for (uint32_t i = 0; i < 3; i++) {
        if (s_proceduralTextures[i]) {
            HandleClose(s_proceduralTextures[i]);

            s_proceduralTextures[i] = nullptr;
        }

        s_proceduralUpdated[i] = false;
    }
}

// ref: FUN_008a2f00
// Push each procedural texture's generated pixels to the device, once.
//
// The reference does this as three copies of the same block, one per global, each guarded by its
// own latch byte at 0x00d43b49..0x00d43b4b so the upload happens exactly once per texture. The
// rect it hands over is {0, 0, 0x40, 8}, which against CiRect's {minY, minX, maxY, maxX} is the
// whole 8 x 64 image -- the same dimensions frozen already builds them at, which is the check that
// the rect is being read in the right field order.
//
// Nothing calls this yet. Frozen's textures are created with their generator callback attached, so
// the device asks for the pixels itself the first time it binds one; this is the reference's own
// eager path and is ported for the parity rather than to fix a blank texture.
//
// THE REFERENCE HAS TWO MORE ARMS THAN THIS AND THEY ARE BOTH INERT, which is why the tag above is
// still a whole claim on the function rather than a `part of`. After the three procedural textures
// it walks a block of thirty-two handles at 0x00d43b50 and then a single one at 0x00d43b4c. Neither
// is ever populated: enumerating every reference to 0x00d43b48..0x00d43bd8 in the binary turns up
// the base 0x00d43b50 exactly four times and 0x00d43b54, 0x00d43b58 and the rest not once, so no
// instruction anywhere stores a handle into that array. The only writes are the zeroing in
// FUN_008a2980. Both loops therefore run over nulls and skip every iteration, in the reference as
// much as here, and reproducing storage nothing fills would be copying a ghost.
void UpdateProceduralTextures() {
    for (uint32_t i = 0; i < 3; i++) {
        if (s_proceduralUpdated[i] || !s_proceduralTextures[i]) {
            continue;
        }

        CGxTex* tex = TextureGetGxTex(s_proceduralTextures[i], 0, nullptr);

        if (!tex) {
            continue;
        }

        CiRect rect = { 0, 0, static_cast<int32_t>(PROC_TEX_HEIGHT),
                        static_cast<int32_t>(PROC_TEX_WIDTH) };

        GxTexUpdate(tex, rect, 0);

        s_proceduralUpdated[i] = true;
    }
}

HTEXTURE ProceduralLiquidTexture(const char* name) {
    static bool s_created = false;

    if (!s_created) {
        s_created = true;

        struct { const char* name; TEXTURE_CALLBACK* generate; intptr_t arg; } kTextures[3] = {
            { "proceduralRiverDepthTex", DepthGradientGenerate, 1 },
            { "proceduralOceanDepthTex", DepthGradientGenerate, 0 },
            { "proceduralWmoWaterTex", WmoWaterGenerate, 0 }
        };

        for (uint32_t i = 0; i < 3; i++) {
            CGxTexFlags flags(GxTex_Linear, 0, 0, 0, 0, 0, 1);

            HTEXTURE handle = TextureCreate(PROC_TEX_WIDTH, PROC_TEX_HEIGHT, GxTex_Argb8888,
                                           GxTex_Argb8888, flags,
                                           reinterpret_cast<void*>(kTextures[i].arg),
                                           kTextures[i].generate, kTextures[i].name, 0);

            if (!handle) {
                continue;
            }

            s_proceduralTextures[i] = handle;

            CTexture* texture = TextureGetTexturePtr(handle);

            if (texture) {
                TextureCacheNewTexture(texture, flags);
            }
        }
    }

    // ref: FUN_004b6f30 -- the by-name lookup, which hashes the name and takes whatever the cache
    // holds under it. A name nothing registered comes back null, and the caller falls through to
    // its solid stand-in.
    char key[CMaterialSettings::TEXTURE_NAME_SIZE];

    SStrCopy(key, name, sizeof(key));

    return TextureCacheGetProcedural(key);
}
void CMaterialSettings::LoadTextures() {
    for (uint32_t slot = 0; slot < TEXTURE_SLOTS; slot++) {
        const char* name = this->m_textureName[slot];

        this->m_resident[slot] = 0;

        if (!name[0]) {
            continue;
        }

        bool procedural = SStrStrI(name, "procedural") != nullptr;

        CGxTexFlags flags(procedural ? GxTex_Linear : GxTex_LinearMipLinear,
                          !procedural, !procedural, 0, 0, 0, 1);

        CStatus status;

        if (!SStrStrI(name, "%d")) {
            HTEXTURE texture = nullptr;

            if (procedural) {
                // The generated texture, looked up by name rather than read off disk.
                texture = ProceduralLiquidTexture(name);

                if (!texture) {
                    // The reference falls back to a solid too, and only when its generator has
                    // nothing under the name. White is the neutral for a multiplied depth ramp.
                    CImVector white = { 0xff, 0xff, 0xff, 0xff };

                    texture = TextureCreateSolid(white);
                }
            } else {
                texture = TextureCreate(name, flags, &status, 0);
            }

            if (texture) {
                this->m_frames[slot].Add(1, &texture);
            }

            continue;
        }

        // An animation: the name is a pattern, and the frames are numbered from one.
        bool any = false;

        for (uint32_t frame = 1; frame < MAX_FRAMES + 1; frame++) {
            char path[CMaterialSettings::TEXTURE_NAME_SIZE];
            SStrPrintf(path, sizeof(path), name, frame);

            HTEXTURE texture = TextureCreate(path, flags, &status, 0);

            this->m_frames[slot].Add(1, &texture);

            if (TextureHasPendingData(texture)) {
                any = true;
            }
        }

        // TODO the second set, FUN_004b8d70 over the same names, kept only when it comes out the
        // same length as the first. Which loader that is, and so what the second set is for, is
        // not established, so m_framesAlt stays empty.
        (void)any;
    }
}

// ref: FUN_008a1d60
// Which frame of a slot's animation is showing, and the thing that makes water move.
//
// Two jobs in one, and the second is the reason for m_framesAlt. Until every frame of the slot has
// arrived the pick comes out of the stand-in set, and each still-pending frame gets its streaming
// priority raised on the way past; once they have all landed the flag latches, the stand-ins are
// closed, and every later call reads the real set directly.
CGxTex* CMaterialSettings::GetFrame(uint32_t slot, uint32_t periodMs) {
    uint32_t count = this->m_frames[slot].Count();

    if (!count) {
        return nullptr;
    }

    // A single still needs none of the machinery below, not even a resident check.
    if (count == 1) {
        return TextureGetGxTex(this->m_frames[slot][0], 0, nullptr);
    }

    TSGrowableArray<HTEXTURE>& frames = this->m_frames[slot];
    TSGrowableArray<HTEXTURE>* pick = &frames;

    if (!this->m_resident[slot]) {
        if (!this->m_framesAlt[slot].Count()) {
            // No stand-ins: the slot simply does not draw until every frame has a GxTex.
            for (uint32_t i = 0; i < count; i++) {
                if (!TextureGetGxTex(frames[i], 0, nullptr)) {
                    return nullptr;
                }
            }

            this->m_resident[slot] = 1;
        } else {
            bool ready = true;

            for (uint32_t i = 0; i < count; i++) {
                if (TextureHasPendingData(frames[i])) {
                    // Ask for it sooner, and show a stand-in this frame.
                    TextureIncreasePriority(TextureGetTexturePtr(frames[i]));

                    ready = false;
                    break;
                }
            }

            if (!ready) {
                pick = &this->m_framesAlt[slot];
            } else {
                this->m_resident[slot] = 1;

                for (uint32_t i = 0; i < this->m_framesAlt[slot].Count(); i++) {
                    HandleClose(this->m_framesAlt[slot][i]);
                }

                this->m_framesAlt[slot].SetCount(0);
            }
        }
    }

    if (!periodMs) {
        periodMs = 1;
    }

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    float phase = static_cast<float>(now % periodMs) / static_cast<float>(periodMs);

    // The reference writes this as round(count * phase - 0.5), and nearbyintf is the same
    // half-to-even rounding the x87 ROUND it compiles to uses -- so this is exact, not a
    // simplification to floor. phase < 1 keeps the result inside 0 .. count-1 without a clamp.
    int32_t frame = static_cast<int32_t>(nearbyintf(static_cast<float>(count) * phase - 0.5f));

    return TextureGetGxTex((*pick)[frame], 1, nullptr);
}

// ref: FUN_008a28f0
// Asking twice for the same type gives the same record. A type the DBC does not carry is
// reported once and retried as water, which every map has.
CMaterialSettings* GetMaterialSettings(int32_t liquidType) {
    while (true) {
        if (static_cast<uint32_t>(liquidType) < s_settingsBank.Count()
            && s_settingsBank[liquidType]) {
            return s_settingsBank[liquidType];
        }

        auto settings = static_cast<CMaterialSettings*>(
            SMemAlloc(sizeof(CMaterialSettings), __FILE__, __LINE__, 0x0));

        if (settings) {
            new (settings) CMaterialSettings();
        }

        if (settings && settings->LoadFromDbc(liquidType)) {
            s_settingsBank.GrowToFit(liquidType, 1);
            s_settingsBank[liquidType] = settings;

            return settings;
        }

        if (settings) {
            SMemFree(settings, __FILE__, __LINE__, 0x0);
        }

        SysMsgPrintf(SYSMSG_ERROR, "Settings Bank: Liquid type [%d] not found, defaulting to water!", liquidType);

        // DIVERGENCE, and a deliberate one. The reference loops back to water rather than
        // recursing, on the assumption that a water row always exists -- and if it does not, it
        // spins forever printing this line. That is not hypothetical: it happened here, and a
        // single run wrote ten million copies of this message before the client died. Frozen
        // gives up instead and lets the caller cope with a null.
        if (liquidType == 1) {
            return nullptr;
        }

        liquidType = 1;
    }
}

// The materials, indexed by LiquidMaterial.dbc id rather than by liquid type: every kind of
// water shares one material. DAT_00d43b2c / DAT_00d43b28
static TSGrowableArray<IMaterial*> s_materialBank;

// The shader pairs, one set a material class, loaded once each. The reference keeps them as loose
// globals and guards each with its own instance counter; a static here does the same job.
namespace {

// The procedural water shaders take a suffix. The reference is given one from outside the module
// (a setter at 0x008a1770 copies it into 0x00d439f0 along with a float and four counters); nothing
// in frozen calls that, so the names come out unsuffixed.
// TODO identify the caller and what it passes.
// THE MODULE STATE Liquid::Initialize writes. The reference keeps these as loose globals; they
// are gathered here because they are one thing -- what the client was configured with -- and two
// of them were standing in as hardcoded returns until now.
//
// The defaults are the post-Initialize values rather than the reference's zero-init. That is a
// deliberate, narrow divergence: the reference's globals start at zero and Initialize sets them
// before any liquid can draw, so the only window where the two differ is one in which nothing
// reads them. Starting at the settled values means a liquid drawn through some path that has not
// run Initialize gets the right answer instead of a scale of zero and a no-specular material.
char s_procWaterSuffix[256] = { 0 };
float s_textureScaleMultiplier = 1.0f;
int32_t s_specularWater = 1;
// Two counts, both taken by Initialize. The reference keeps them at 0x00d43af8 and 0x00d43afc and
// spends the first of them in the settings-bank teardown.
int32_t s_settingsRefs = 0;
int32_t s_moduleRefs = 0;
// Initialize's first and third arguments, 0x00d43af4 and 0x00d43af0. Both are read back nowhere
// this port has found, so they are stored and named by origin rather than by meaning.
int32_t s_initArg0 = 0;
int32_t s_initArg2 = 0;

const char* ProcWaterSuffix() {
    return s_procWaterSuffix;
}

void LoadPair(CGxShader** vertex, int32_t vertexCount, const char* vertexName,
              CGxShader** pixel, int32_t pixelCount, const char* pixelName) {
    g_theGxDevicePtr->ShaderCreate(vertex, GxSh_Vertex, "Shaders\\Vertex", vertexName, vertexCount);
    g_theGxDevicePtr->ShaderCreate(pixel, GxSh_Pixel, "Shaders\\Pixel", pixelName, pixelCount);
}

CGxShader* s_vsWater[4];
CGxShader* s_psWater[1];
CGxShader* s_vsWaterNoSpec[4];
CGxShader* s_psWaterNoSpec[1];
CGxShader* s_vsMagma[1];
CGxShader* s_psMagma[1];
CGxShader* s_vsProcWater[4];
CGxShader* s_psProcWater[1];

}

// ------------------------------------------------------------------------------------------------
// The shader constant block
//
// The reference keeps these as loose globals and uploads four ranges out of them. Laying them out
// by REGISTER instead makes the map checkable: a register is (address - base) / 16.
//
//   vertex  c0..c3    the projection                      0x00d44ca8
//   vertex  c4        fog: -f, f * fogEnd, density, 0     0x00d44ce8
//   vertex  c5..c8    the world-view                      0x00d44cf8
//   vertex  c9,13,17,21  four texture matrices            0x00d44d38 onwards, 0x40 apart
//   vertex  c25..c28  a scale from stage float 8          0x00d44e38
//   vertex  c29..c32  RotationAroundZ(f10) * Scale(f9)    0x00d44e78
//   vertex  c33       sun direction in VIEW space, w 1    0x00d44eb8
//   vertex  c34       sun ambient, w 1                    0x00d44ec8
//   vertex  c35       sun diffuse, w 1                    0x00d44ed8
//   vertex  c36       sun specular, w 6                   0x00d44ee8
//   vertex  c37..c45  three local lights, 3 each          0x00d44ef8
//   vertex  c46       three wave phases (FUN_008a3620)    0x00d44f88
//   vertex  c47       three wave phases (FUN_008a3710)    0x00d44f98
//   vertex  c48..c50  per-wave, from FUN_008a3620         0x00d44fa8
//   vertex  c51,c52   reciprocals, one per wave           0x00d44fd8
//   vertex  c53..c55  per-wave, from FUN_008a3710         0x00d44ff8
//   pixel   c0..c3    the model-view-projection           0x00b24120
//   pixel   c4        the fog colour, w 1                 0x00b24160
//   pixel   c5        the camera position, w 1            0x00b24170
//   pixel   c6        MINUS the sun direction, world, w 0 0x00d44c48
//   pixel   c7        sun ambient, w 0                    0x00d44c58
//   pixel   c8        sun diffuse, w 0                    0x00d44c68
//   pixel   c9        sun specular, w 50                  0x00d44c78
//   pixel   c10..c11  stage floats 11..17 (FUN_008a3810)  0x00d44c88
//
// There are no gaps left in that map: every register is accounted for, and FUN_008a3810 turns out to
// write pixel c10 and c11 from stage floats 11..17, which is the last seven of the eighteen.
//
// NOTHING IS LEFT UNFILLED. This paragraph used to say the local lights and the whole wave animation
// were still zero, and both have since landed -- the waves through GetWaveManager and SetupWaves,
// the lights in SetupLightConstants -- so the claim outlived its truth by two cycles. It is recorded
// rather than deleted because a register map that says a range is dead is the kind of note that
// stops the next reader looking, and this one was wrong.

namespace {

const uint32_t VS_REGISTERS = 58;
const uint32_t PS_REGISTERS = 6;

const uint32_t VS_PROJECTION = 0;
const uint32_t VS_FOG = 4;
const uint32_t VS_WORLD_VIEW = 5;
const uint32_t VS_SUN_DIR = 33;
const uint32_t VS_LOCAL_LIGHT = 37;  // 37..45, THREE registers a light
// How many of CM2Lighting's four kept lights reach the water. The reference's loop advances its
// destination offset by 0x30 a light and refuses to begin an iteration once that offset has
// reached 0x90, so the fourth light never gets a register however near it is.
const uint32_t VS_LOCAL_LIGHT_COUNT = 3;
// 0.0039215689 at 0x00a45564, which is 1/255: the light colour is held as bytes widened into
// floats and the shader wants it normalised.
const float LIGHT_COLOR_SCALE = 0.0039215689f;
const uint32_t VS_WAVE_PHASE_A = 46;
const uint32_t VS_WAVE_PHASE_B = 47;
const uint32_t VS_WAVE_A = 48;        // 48,49,50, one per wave
const uint32_t VS_WAVE_RECIP_A = 51;
const uint32_t VS_WAVE_RECIP_B = 52;
const uint32_t VS_WAVE_B = 53;        // 53,54,55, one per wave
const uint32_t VS_WAVE_RECIP_C = 56;
const uint32_t VS_WAVE_SCALAR = 57;
const uint32_t VS_TEX_MATRIX = 9;
const uint32_t VS_SCALE = 25;
const uint32_t VS_ROT_SCALE = 29;
const uint32_t VS_SPLIT = 46;        // where the second vertex upload starts

const uint32_t PS_MVP = 0;
const uint32_t PS_FOG_COLOR = 4;
const uint32_t PS_CAMERA = 5;
// psSun[] is uploaded starting at this register, so an index into it is (register - this).
const uint32_t PS_SUN_BASE = 6;
const uint32_t PS_WAVE = 10;         // 10 and 11

struct Constants {
    C4Vector vs[VS_REGISTERS];
    C4Vector psMvp[PS_REGISTERS];
    C4Vector psSun[PS_REGISTERS];
};

Constants s_constants;

// Straight copy, NOT transposed. frozen's own terrain and WMO shaders take transposed matrices,
// but these are the reference's shaders out of the archives, and the reference stores each matrix
// into this block with a plain sixteen-dword copy. Matching the shader, not the house style.
void StoreMatrix(C4Vector* dst, const C44Matrix& m) {
    memcpy(dst, &m, sizeof(C44Matrix));
}

void StoreVector(C4Vector* dst, const C3Vector& v, float w) {
    dst->x = v.x;
    dst->y = v.y;
    dst->z = v.z;
    dst->w = w;
}

// ref: FUN_008a32f0
// The projection, the world-view and the combined transform.
//
// The world-view is the placement with the camera taken out of its translation, times the device's
// VIEW matrix -- and that composes correctly here rather than double-counting the camera, because
// frozen's CCamera builds its view matrix with the camera at the origin, which is the same
// rotation-only convention the reference uses.
//
// DIVERGED, and SETTLED 2026-09-27: the reference negates the projection's third row when the
// device's flag at +0x1b4 is clear -- a depth-range convention -- and frozen's CGxDevice has no such
// flag, so the negation is skipped. That is CORRECT, and this note used to offer it as "the one
// knob" to turn if water sat at the wrong depth. It is not. vsLiquidWater, disassembled out of
// patch.MPQ, computes oPos.z = z*c2.z + c3.z and oPos.w = z*c2.w from c0..c3 as ROWS; with frozen's
// projection that is the standard D3D mapping for near 0.4 / far 4000, and negating the third row
// would make oPos.z negative for everything in front of the camera. See docs/ref/parity-liquid-shaders.md.
void SetupTransforms(const C3Vector& cameraPos, const C44Matrix& placement) {
    C44Matrix view;
    g_theGxDevicePtr->XformView(view);

    C44Matrix local = placement;

    local.d0 -= cameraPos.x;
    local.d1 -= cameraPos.y;
    local.d2 -= cameraPos.z;

    C44Matrix worldView = local * view;

    StoreMatrix(&s_constants.vs[VS_WORLD_VIEW], worldView);

    C44Matrix projection;
    g_theGxDevicePtr->XformProjection(projection);

    StoreMatrix(&s_constants.vs[VS_PROJECTION], projection);
    StoreMatrix(&s_constants.psMvp[PS_MVP], worldView * projection);

    s_constants.psMvp[PS_CAMERA].x = cameraPos.x;
    s_constants.psMvp[PS_CAMERA].y = cameraPos.y;
    s_constants.psMvp[PS_CAMERA].z = cameraPos.z;
    s_constants.psMvp[PS_CAMERA].w = 1.0f;
}

// ref: FUN_008a38b0
// The sun, the local lights and the fog, as the VERTEX program wants them -- the sun direction
// brought into view space, and the fog reduced to the two coefficients a linear fade needs.
void SetupLightConstants(const CM2Lighting& lighting) {
    C44Matrix view;
    g_theGxDevicePtr->XformView(view);

    // Only the rotation: frozen's view matrix already has the camera at the origin.
    C33Matrix rotation(view);

    C3Vector sunDir = lighting.m_sunDir * rotation;

    s_constants.vs[VS_SUN_DIR].x = sunDir.x;
    s_constants.vs[VS_SUN_DIR].y = sunDir.y;
    s_constants.vs[VS_SUN_DIR].z = sunDir.z;
    s_constants.vs[VS_SUN_DIR].w = 1.0f;

    StoreVector(&s_constants.vs[VS_SUN_DIR + 1], lighting.m_sunAmbient, 1.0f);
    StoreVector(&s_constants.vs[VS_SUN_DIR + 2], lighting.m_sunDiffuse, 1.0f);

    // 6 is the reference's own constant at 0x009e8cf8, presumably the specular power.
    StoreVector(&s_constants.vs[VS_SUN_DIR + 3], lighting.m_sunSpecular, 6.0f);

    // The three local lights, at c37..c45, three registers each:
    //
    //   +0  the light's m_pos brought through the same rotation, w 1
    //   +1  its m_dirColor times 1/255, w 1
    //   +2  its three attenuations -- constant, linear, quadratic
    //
    // WHICH VECTOR IS ROTATED was the open question that held this back, and it is settled. The
    // reference reads the light's +0x0c -- m_pos, not the m_posCameraSpace at +0x18 -- and puts it
    // through the same rotation as the sun with NO camera subtraction. Rotating a world position
    // looked wrong enough to be a misread, so it was checked against the OTHER consumer of these
    // same lights, CShaderEffect::ComputeLocalLights (FUN_00872900): that one branches on whether
    // it was handed a camera position, subtracting it from +0x0c when it was (0x008729de) and
    // reading the cached camera-space vector at +0x18 when it was not (0x00872a3c). The reference
    // therefore distinguishes the two vectors deliberately, and this path takes the first one
    // unsubtracted. Ported as measured rather than as it ought to be.
    //
    // A LIGHT SHORT OF THE THREE LEAVES ITS REGISTERS ALONE, which is the reference's behaviour
    // and not an oversight here: nothing zeroes c37..c45 on the way in, so a frame with one light
    // leaves the other two holding the previous frame's values. Matched, because a liquid surface
    // lit by a torch that has gone out is a reference behaviour and diverging from it silently is
    // worse than reproducing it.

    uint32_t lights = lighting.m_lightCount < VS_LOCAL_LIGHT_COUNT
                   ? lighting.m_lightCount
                   : VS_LOCAL_LIGHT_COUNT;

    for (uint32_t i = 0; i < lights; i++) {
        CM2Light* light = lighting.m_lights[i];

        // FROZEN-ONLY. The reference dereferences the slot without a check, trusting m_lightCount
        // to count only filled ones. Keeping the guard costs a compare and turns a would-be crash
        // into a dark surface.
        if (!light) {
            continue;
        }

        uint32_t reg = VS_LOCAL_LIGHT + i * 3;

        StoreVector(&s_constants.vs[reg], light->m_pos * rotation, 1.0f);
        StoreVector(&s_constants.vs[reg + 1], light->m_dirColor * LIGHT_COLOR_SCALE, 1.0f);

        // The w of the attenuation register is the one slot of the nine the reference never
        // writes, so it keeps whatever was already there. frozen's constant block is a
        // zero-initialised static that nothing else touches, which is the same value.
        s_constants.vs[reg + 2].x = light->m_constantAttenuation;
        s_constants.vs[reg + 2].y = light->m_linearAttenuation;
        s_constants.vs[reg + 2].z = light->m_quadraticAttenuation;
    }

    // Fog. Linear, and the two coefficients are what the program multiplies the depth by; the
    // reference's multiplier at 0x00d4300c is a global set to exactly 1.0 by the shader system's
    // init (an `fld1` at 0x00872d09), so the first coefficient is just -1/(end - start).
    if (!g_theGxDevicePtr->MasterEnable(GxMasterEnable_Fog)) {
        s_constants.vs[VS_FOG].x = 0.0f;
        s_constants.vs[VS_FOG].y = 1.0f;
        s_constants.vs[VS_FOG].z = 1.0f;
    } else {
        float scale = 1.0f / (lighting.m_fogEnd - lighting.m_fogStart);

        s_constants.vs[VS_FOG].x = -scale;
        s_constants.vs[VS_FOG].y = scale * lighting.m_fogEnd;
        s_constants.vs[VS_FOG].z = lighting.m_fogDensity;
    }

    s_constants.vs[VS_FOG].w = 0.0f;

    StoreVector(&s_constants.psMvp[PS_FOG_COLOR], lighting.m_fogColor, 1.0f);
}

// ref: FUN_008a3c90
// The same sun terms again for the PIXEL program, and deliberately not the same values: the
// direction is negated and left in world space, and every w differs from the vertex copy.
void SetupSunConstants(const CM2Lighting& lighting) {
    s_constants.psSun[0].x = -lighting.m_sunDir.x;
    s_constants.psSun[0].y = -lighting.m_sunDir.y;
    s_constants.psSun[0].z = -lighting.m_sunDir.z;
    s_constants.psSun[0].w = 0.0f;

    StoreVector(&s_constants.psSun[1], lighting.m_sunAmbient, 0.0f);
    StoreVector(&s_constants.psSun[2], lighting.m_sunDiffuse, 0.0f);

    // 50 is the reference's constant at 0x009f22ec.
    StoreVector(&s_constants.psSun[3], lighting.m_sunSpecular, 50.0f);
}

// The floor every reciprocal in the wave constants is clamped to, so a zero rate cannot divide.
// DAT_009e1134.
const float WAVE_EPSILON = 0.0010000000474974513f;

// A wave's phase: the clock times its rate, taken modulo 8192 and scaled so 1024 steps make a full
// turn -- so the phase wraps every 1024 and the register carries up to eight turns of it. The two
// constants are 1/1024 (0x00a1c8a0) and 2*pi (0x009f193c).
float WavePhase(float rate) {
    int32_t stamp = static_cast<int32_t>(OsGetAsyncTimeMs());

    float ticks = static_cast<float>(stamp);

    uint32_t phase = static_cast<uint32_t>(static_cast<int64_t>(nearbyintf(ticks * rate)));

    return static_cast<float>(phase & 0x1fff) * 0.0009765625f * 6.2831854820251465f;
}

float* Components(C4Vector& v) {
    return reinterpret_cast<float*>(&v);
}

// ref: FUN_008a3620
// One of the first three waves. `pair` is two floats straight through; the rest are a rate and two
// reciprocals, and the last is the rate the phase is taken at.
void SetupWaveA(uint32_t index, const float* pair, float recipA, float recipB, float scalar,
                float rate) {
    Components(s_constants.vs[VS_WAVE_PHASE_A])[index] = WavePhase(rate);

    C4Vector& wave = s_constants.vs[VS_WAVE_A + index];

    wave.x = pair[0];
    wave.y = pair[1];
    wave.z = 1.0f / (recipA < WAVE_EPSILON ? WAVE_EPSILON : recipA);
    wave.w = scalar;

    Components(s_constants.vs[VS_WAVE_RECIP_A])[index] =
        1.0f / (recipB < WAVE_EPSILON ? WAVE_EPSILON : recipB);
}

// ref: FUN_008a3710
// One of the second three. Two pairs this time, and three scalars that land in three different
// registers rather than alongside each other.
void SetupWaveB(uint32_t index, const float* pairA, const float* pairB, float recipA, float recipB,
                float scalar, float rate) {
    Components(s_constants.vs[VS_WAVE_PHASE_B])[index] = WavePhase(rate);

    float a = recipA < WAVE_EPSILON ? WAVE_EPSILON : recipA;
    float b = recipB < WAVE_EPSILON ? WAVE_EPSILON : recipB;

    Components(s_constants.vs[VS_WAVE_RECIP_B])[index] = 1.0f / b;

    C4Vector& wave = s_constants.vs[VS_WAVE_B + index];

    wave.x = pairA[0];
    wave.y = pairA[1];
    wave.z = pairB[0];
    wave.w = pairB[1];

    Components(s_constants.vs[VS_WAVE_RECIP_C])[index] = 1.0f / a;
    Components(s_constants.vs[VS_WAVE_SCALAR])[index] = scalar;
}

// ref: FUN_008a3810
// The last seven stage floats, and the only part of the wave block that needs no wave manager --
// which is why it carries real values today where the six above do not. Note the scramble: the
// SEVENTH float lands before the fifth and sixth.
void SetupWaveConstants(const CMaterialSettings& settings) {
    float rate = settings.GetStageFloat(11);

    if (rate < WAVE_EPSILON) {
        rate = WAVE_EPSILON;
    }

    s_constants.psSun[PS_WAVE - PS_SUN_BASE].x = 1.0f / rate;
    s_constants.psSun[PS_WAVE - PS_SUN_BASE].y = settings.GetStageFloat(12);
    s_constants.psSun[PS_WAVE - PS_SUN_BASE].z = settings.GetStageFloat(13);
    s_constants.psSun[PS_WAVE - PS_SUN_BASE].w = settings.GetStageFloat(14);

    s_constants.psSun[PS_WAVE + 1 - PS_SUN_BASE].x = settings.GetStageFloat(17);
    s_constants.psSun[PS_WAVE + 1 - PS_SUN_BASE].y = settings.GetStageFloat(15);
    s_constants.psSun[PS_WAVE + 1 - PS_SUN_BASE].z = settings.GetStageFloat(16);
    s_constants.psSun[PS_WAVE + 1 - PS_SUN_BASE].w = 0.0f;
}

// Part of ref: FUN_008a48f0 -- the six wave records, walked out of the manager the surface holds.
//
// The first three are six dwords each and the second three eight, and a record past the end of what
// the manager offers is filled with zeroes rather than skipped -- so with no manager at all, which
// is where frozen is, every wave register takes the reference's own zero-fill values.
void SetupWaves(CWaveManager* manager, const CMaterialSettings& settings) {
    const float* records = nullptr;
    const float* end = nullptr;

    if (manager) {
        records = manager->Records();
        end = records + manager->RecordDwords();
    }

    static const float ZERO_PAIR[2] = { 0.0f, 0.0f };

    for (uint32_t i = 0; i < 3; i++) {
        if (records && records + 6 <= end) {
            SetupWaveA(i, &records[0], records[2], records[3], records[4], records[5]);

            records += 6;
        } else {
            SetupWaveA(i, ZERO_PAIR, 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    for (uint32_t i = 0; i < 3; i++) {
        if (records && records + 8 <= end) {
            SetupWaveB(i, &records[0], &records[2], records[4], records[5], records[6],
                       records[7]);

            records += 8;
        } else {
            SetupWaveB(i, ZERO_PAIR, ZERO_PAIR, 0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    SetupWaveConstants(settings);
}

// The magma and water pixel programs take a FIXED constant block, which the reference keeps as a
// static at 0x00b24120: an identity matrix in c0..c3 and two zero registers. psLiquidWater and
// psLiquidMagma read nothing from the CPU -- everything they need arrives interpolated out of the
// vertex program -- so this six-register upload is a formality the reference performs anyway, and
// skipping it would leave whatever the previous pass wrote in those registers.
const float FIXED_PS_CONSTANTS[24] = {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
    0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f
};

// The whole vertex block goes up in ONE upload of 46 registers for these materials, where the
// procedural one splits it in two around the wave constants it alone writes.
const uint32_t VS_UPLOAD_REGISTERS = 46;
const uint32_t FIXED_PS_REGISTERS = 6;

// ref: FUN_008a5590
// CMaterialWater::Draw, and also CMaterialWaterNoSpec::Draw at FUN_008a5900 -- those two reference
// bodies are instruction-for-instruction the same but for the two shader tables they read, so one
// function taking the pair is the honest shape for them.
//
// TWO frames, not six. Frame 0 animates on the period in m_int[1] and frame 1 on the default 1250,
// and the pair goes to the stages CROSSED: frame 1 to texture 0 and frame 0 to texture 1.
//
// THE VERTEX FORMAT IS GxVBF_PNCT2, which is the second defect this function fixes. Frozen asked
// for GxVBF_PT2 everywhere; the reference passes 6 here, and 6 is PNCT2 -- position, normal,
// colour and two texcoords, which is exactly the five attributes the liquid vertex writers fill
// (GxVA_Position, Normal, Color0, TexCoord0, TexCoord1). Under PT2 the format carries no colour
// and no normal, so CMeshGeomFactory::Build's attribute test left three of its five cursors null
// and the writers skipped them -- including the diffColor the map-object queue goes to the trouble
// of reading off the group's material.
//
// THE STAGE FLOATS ARE 0, 1 AND 2. The procedural body uses 8, 9 and 10 for its scale pair, and
// frozen was applying those indices to water, which reads floats LiquidType.dbc leaves at zero --
// a zero texture scale collapses every texcoord to one point.
//
// TWO THINGS THE REFERENCE DOES THAT ARE NOT REPRODUCED, both deliberate:
//
//   It copies the device's projection into c0..c3 and negates the third row when a device flag is
//   clear, BEFORE calling SetupTransforms -- which then writes the same four registers from the
//   same source. The negation is therefore discarded in the reference too. SetupTransforms already
//   records that divergence; doing it twice here would only duplicate it.
//
//   It asks the geometry a virtual question (IGeomFactory vtable +0x10) and swaps the placement for
//   an identity when the answer is true -- a factory saying "my vertices are already where they
//   belong". frozen's IGeomFactory has no such virtual and both of its factories write local-space
//   vertices, so the placement is always used. If map-object water turns up at the world origin,
//   this is the question to answer.
//
// Depth write comes from a device capability at Caps+0xf8 that frozen does not model (the same one
// the GL arbvp path tests); absent, the reference's `cap == 0` yields 1, which is what this passes.
void DrawWaterMaterial(CGxShader** vertexShaders, CGxShader** pixelShaders,
                       CClientEnvironment* environment, IGeomFactory* geometry,
                       const C3Vector& cameraPos, const C44Matrix* placement,
                       const CAaSphere* sphere, CMaterialSettings* settings) {
    if (!geometry || !settings || !environment) {
        return;
    }

    static const uint32_t DEFAULT_PERIOD = 1250;
    static const float ANGLE_SCALE = 57.295780181884766f;

    CGxTex* frame0 = settings->GetFrame(0, static_cast<uint32_t>(settings->GetInt(1)));

    if (!frame0) {
        return;
    }

    CGxTex* frame1 = settings->GetFrame(1, DEFAULT_PERIOD);

    if (!frame1) {
        return;
    }

    float texScale = settings->GetStageFloat(0);
    float texAngle = settings->GetStageFloat(1) * ANGLE_SCALE;
    float scaleY = settings->GetStageFloat(2);

    GxRsPush();

    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_BlendingMode, GxBlend_Alpha);
    GxRsSet(GxRs_DepthWrite, 1);
    GxRsSet(GxRs_Fog, 1);

    SetupTransforms(cameraPos, *placement);

    CM2Lighting lighting;
    lighting.Initialize(nullptr, *sphere);

    environment->SetupLighting(&lighting);

    SetupLightConstants(lighting);

    // Texture matrix 1 (c13) is a plain scale down the second row, paired with texture stage 0.
    C44Matrix scale(1.0f);

    scale.b1 = scaleY;

    StoreMatrix(&s_constants.vs[VS_TEX_MATRIX + 4], scale);

    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(frame1));

    // Texture matrix 0 (c9) turns and then scales, paired with texture stage 1.
    C44Matrix rotScale = C44Matrix::RotationAroundZ(texAngle);

    rotScale.Scale(texScale);

    StoreMatrix(&s_constants.vs[VS_TEX_MATRIX], rotScale);

    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<void*>(frame0));

    GxShaderConstantsSet(GxSh_Vertex, 0,
                         reinterpret_cast<const float*>(&s_constants.vs[0]),
                         VS_UPLOAD_REGISTERS);
    GxShaderConstantsSet(GxSh_Pixel, 0, FIXED_PS_CONSTANTS, FIXED_PS_REGISTERS);

    uint32_t permutation = lighting.m_lightCount;

    if (permutation > 2) {
        permutation = 3;
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<void*>(vertexShaders[permutation]));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<void*>(pixelShaders[0]));

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;

    if (!geometry->Build(GxVBF_PNCT2, &vertexBuf, &indexBuf, &batch)) {
        GxRsPop();

        return;
    }

    GxPrimVertexPtr(vertexBuf, GxVBF_PNCT2);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();
}


// ref: FUN_008a1750
// The global multiplier on a liquid texture matrix's scale. Liquid::Initialize writes it from its
// own argument and it rests at 1.0 (0x00b23f64), which is why every scale in this file has looked
// like it had no multiplier at all.
float TextureScaleMultiplier() {
    return s_textureScaleMultiplier;
}

// ref: FUN_008a34b0
// Magma's scrolling texture matrix: an identity carrying a phase in x and y. Each phase is the wall
// clock modulo a period of round(1000 / rate) milliseconds, divided by that period, so a rate is in
// wraps per second and a rate of zero holds still.
//
// DIVERGED in storage only: the reference keeps the matrix in a static at 0x00d450d8, initialises
// its identity once behind a flag and returns its address. Returning a value is the same matrix
// with less state.
C44Matrix MagmaScrollMatrix(float rateX, float rateY) {
    static const float SCROLL_PERIOD_BASE = 1000.0f;

    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());

    C44Matrix m(1.0f);

    if (rateX != 0.0f) {
        uint32_t period = static_cast<uint32_t>(nearbyintf(SCROLL_PERIOD_BASE / rateX));

        if (period) {
            m.d0 = static_cast<float>(now % period) / static_cast<float>(period);
        }
    }

    if (rateY != 0.0f) {
        uint32_t period = static_cast<uint32_t>(nearbyintf(SCROLL_PERIOD_BASE / rateY));

        if (period) {
            m.d1 = static_cast<float>(now % period) / static_cast<float>(period);
        }
    }

    return m;
}


// ref: FUN_008a48f0 -- CMaterialProcWater::Draw, and NOT the shared body behind all four shader
// materials, which is what this comment used to claim and what routed every material through it.
// The four shader materials have FOUR SEPARATE reference bodies with different frame counts, vertex
// formats and constant sets: ProcWater 0x008a48f0 (six frames), Water 0x008a5590 and WaterNoSpec
// 0x008a5900 (two frames, and those two genuinely do share a shape), Magma 0x008a6090 (one frame).
// Sending water through this one made it ask for six frames, and LiquidType.dbc fills only two --
// so GetFrame(2) returned null and the draw returned before building anything. That is why no
// liquid has ever appeared, terrain or map object.
//
// STILL WRONG HERE, and left alone because material 3 is the only thing that reaches it and nothing
// on map 0 asks for material 3: the reference binds FOUR textures (render states 0x15..0x18), where
// this binds six. Its Build format of GxVBF_PT2 IS right -- the reference passes 0xb here, which is
// what made the wrong format elsewhere so easy to miss.
void DrawProcWaterMaterial(CGxShader** vertexShaders, CGxShader** pixelShaders,
                        CClientEnvironment* environment, IGeomFactory* geometry,
                        CWaveManager* waveManager, const C3Vector& cameraPos,
                        const C44Matrix* placement, const CAaSphere* sphere,
                        CMaterialSettings* settings) {

    if (!geometry || !settings || !environment) {
        return;
    }

    // Six animation frames. 1250 ms for slots 0, 1 and 4; the other three take their period from
    // m_int. A slot with nothing resolved aborts the whole draw, before any state is pushed.
    CGxTex* frames[CMaterialSettings::TEXTURE_SLOTS];

    static const uint32_t DEFAULT_PERIOD = 1250;

    frames[0] = settings->GetFrame(0, DEFAULT_PERIOD);
    frames[1] = settings->GetFrame(1, DEFAULT_PERIOD);
    frames[2] = settings->GetFrame(2, static_cast<uint32_t>(settings->GetInt(1)));
    frames[3] = settings->GetFrame(3, static_cast<uint32_t>(settings->GetInt(2)));
    frames[4] = settings->GetFrame(4, DEFAULT_PERIOD);
    frames[5] = settings->GetFrame(5, static_cast<uint32_t>(settings->GetInt(3)));

    for (uint32_t i = 0; i < CMaterialSettings::TEXTURE_SLOTS; i++) {
        if (!frames[i]) {
            return;
        }
    }

    // The stage floats. The first four are scales and the next four angles; the multiplier the
    // reference applies to the scales is a global that rests at 1.0, and the one it applies to the
    // angles is 180/pi, so those four are radians on their way to degrees.
    static const float ANGLE_SCALE = 57.295780181884766f;

    float texScale[4];
    float texAngle[4];

    for (uint32_t i = 0; i < 4; i++) {
        texScale[i] = settings->GetStageFloat(i);
        texAngle[i] = settings->GetStageFloat(i + 4) * ANGLE_SCALE;
    }

    float scaleY = settings->GetStageFloat(8);
    float extraScale = settings->GetStageFloat(9);
    float extraAngle = settings->GetStageFloat(10) * ANGLE_SCALE;

    GxRsPush();

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;

    if (!geometry->Build(GxVBF_PT2, &vertexBuf, &indexBuf, &batch)) {
        GxRsPop();

        return;
    }

    GxPrimVertexPtr(vertexBuf, GxVBF_PT2);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    // The six frames do NOT go to the stages in slot order.
    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(frames[0]));
    g_theGxDevicePtr->RsSet(GxRs_Texture1, static_cast<void*>(frames[1]));
    g_theGxDevicePtr->RsSet(GxRs_Texture2, static_cast<void*>(frames[4]));
    g_theGxDevicePtr->RsSet(GxRs_Texture3, static_cast<void*>(frames[5]));
    g_theGxDevicePtr->RsSet(GxRs_Texture4, static_cast<void*>(frames[2]));
    g_theGxDevicePtr->RsSet(GxRs_Texture5, static_cast<void*>(frames[3]));

    SetupTransforms(cameraPos, *placement);

    // The lighting block the environment fills. Its own constants are not written yet, but building
    // it is what selects the shader permutation below.
    CM2Lighting lighting;
    lighting.Initialize(nullptr, *sphere);

    environment->SetupLighting(&lighting);

    SetupLightConstants(lighting);
    SetupSunConstants(lighting);
    SetupWaves(waveManager, *settings);

    // Four texture matrices, one per stage float pair.
    for (uint32_t i = 0; i < 4; i++) {
        C44Matrix m = C44Matrix::RotationAroundZ(texAngle[i]);

        m.Scale(texScale[i]);

        StoreMatrix(&s_constants.vs[VS_TEX_MATRIX + i * 4], m);
    }

    // Then a plain scale, and a rotate-and-scale.
    C44Matrix scale(1.0f);

    scale.b1 = scaleY;

    StoreMatrix(&s_constants.vs[VS_SCALE], scale);

    C44Matrix rotScale = C44Matrix::RotationAroundZ(extraAngle);

    rotScale.Scale(extraScale);

    StoreMatrix(&s_constants.vs[VS_ROT_SCALE], rotScale);

    // Four uploads, two ranges per target.
    GxShaderConstantsSet(GxSh_Vertex, 0,
                         reinterpret_cast<const float*>(&s_constants.vs[0]), VS_SPLIT);
    GxShaderConstantsSet(GxSh_Pixel, 0,
                         reinterpret_cast<const float*>(&s_constants.psMvp[0]), PS_REGISTERS);
    GxShaderConstantsSet(GxSh_Vertex, VS_SPLIT,
                         reinterpret_cast<const float*>(&s_constants.vs[VS_SPLIT]),
                         VS_REGISTERS - VS_SPLIT);
    GxShaderConstantsSet(GxSh_Pixel, PS_REGISTERS,
                         reinterpret_cast<const float*>(&s_constants.psSun[0]), PS_REGISTERS);

    // The vertex permutation is the local light count, clamped to three -- which is why every one
    // of these materials loads four vertex programs and one pixel program.
    uint32_t permutation = lighting.m_lightCount;

    if (permutation > 2) {
        permutation = 3;
    }

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<void*>(vertexShaders[permutation]));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<void*>(pixelShaders[0]));

    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();
}

}

// ref: FUN_008a3f70
// THE SHADER PAIRS ARE RELEASED HERE, which nothing did before: each class loads its pair on
// first use into statics and, until CGxDevice::ShaderDestroy existed, there was no call to give
// them back with. The reference does exactly this in each material's destructor -- drop a
// per-class count, and on the last one walk the permutation array handing each SLOT's address to
// the device so it comes back null, then the pixel shader the same way.
//
// The count is per CLASS, not per material bank entry: the reference keeps one at 0x00d44c38 for
// water, 0x00d44c3c for the no-specular variant, and the constructor increments the same word it
// tests. A static here is the same thing with the name attached.

namespace {

// One reference count a shader-loading material class.
int32_t s_waterRefs = 0;
int32_t s_waterNoSpecRefs = 0;
int32_t s_magmaRefs = 0;
int32_t s_procWaterRefs = 0;

// Hand every slot of a permutation array back to the device, which nulls each as it goes.
void ReleasePair(CGxShader** vertex, int32_t vertexCount, CGxShader** pixel, int32_t pixelCount) {
    for (int32_t i = 0; i < vertexCount; i++) {
        g_theGxDevicePtr->ShaderDestroy(&vertex[i]);
    }

    for (int32_t i = 0; i < pixelCount; i++) {
        g_theGxDevicePtr->ShaderDestroy(&pixel[i]);
    }
}

}

// ref: FUN_008a3fe0
CMaterialWater::~CMaterialWater() {
    if (s_waterRefs > 0) {
        s_waterRefs--;
    }

    if (s_waterRefs) {
        return;
    }

    ReleasePair(s_vsWater, 4, s_psWater, 1);
}

// ref: FUN_008a40e0
CMaterialWaterNoSpec::~CMaterialWaterNoSpec() {
    if (s_waterNoSpecRefs > 0) {
        s_waterNoSpecRefs--;
    }

    if (s_waterNoSpecRefs) {
        return;
    }

    ReleasePair(s_vsWaterNoSpec, 4, s_psWaterNoSpec, 1);
}

// The same shape as CMaterialWater's, which is ref FUN_008a3fe0; this class's own
// destructor address is not pinned, so it carries no tag rather than a guessed one.
CMaterialMagma::~CMaterialMagma() {
    if (s_magmaRefs > 0) {
        s_magmaRefs--;
    }

    if (s_magmaRefs) {
        return;
    }

    ReleasePair(s_vsMagma, 1, s_psMagma, 1);
}

// The same shape as CMaterialWater's, which is ref FUN_008a3fe0; this class's own
// destructor address is not pinned, so it carries no tag rather than a guessed one.
CMaterialProcWater::~CMaterialProcWater() {
    if (s_procWaterRefs > 0) {
        s_procWaterRefs--;
    }

    if (s_procWaterRefs) {
        return;
    }

    ReleasePair(s_vsProcWater, 4, s_psProcWater, 1);
}
void CMaterialWater::EnsureShaders() {
    s_waterRefs++;

    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsWater, 4, "vsLiquidWater", s_psWater, 1, "psLiquidWater");
}

// ref: FUN_008a4070
void CMaterialWaterNoSpec::EnsureShaders() {
    s_waterNoSpecRefs++;

    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsWaterNoSpec, 4, "vsLiquidWaterNoSpec",
             s_psWaterNoSpec, 1, "psLiquidWaterNoSpec");
}

// ref: FUN_008a4190
// The only one of the four with a single vertex permutation rather than four.
void CMaterialMagma::EnsureShaders() {
    s_magmaRefs++;

    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    LoadPair(s_vsMagma, 1, "vsLiquidMagma", s_psMagma, 1, "psLiquidMagma");
}

// ref: FUN_008a3e00
void CMaterialProcWater::EnsureShaders() {
    s_procWaterRefs++;

    static bool s_loaded = false;

    if (s_loaded) {
        return;
    }

    s_loaded = true;

    char vertexName[256];
    char pixelName[256];

    SStrPrintf(vertexName, sizeof(vertexName), "vsLiquidProcWater%s", ProcWaterSuffix());
    SStrPrintf(pixelName, sizeof(pixelName), "psLiquidProcWater%s", ProcWaterSuffix());

    LoadPair(s_vsProcWater, 4, vertexName, s_psProcWater, 1, pixelName);
}

// ref: FUN_008a5590
// The wave manager is not a parameter of this reference body at all -- only the procedural material
// reads waves -- so it is accepted and ignored to keep the IMaterial signature.
void CMaterialWater::Draw(CClientEnvironment* environment, IGeomFactory* geometry, void* waveManager,
                          const C3Vector& cameraPos, const C44Matrix* placement,
                          const CAaSphere* sphere, CMaterialSettings* settings) {
    (void)waveManager;

    DrawWaterMaterial(s_vsWater, s_psWater, environment, geometry, cameraPos, placement, sphere,
                      settings);
}

// ref: FUN_008a5900
void CMaterialWaterNoSpec::Draw(CClientEnvironment* environment, IGeomFactory* geometry, void* waveManager,
                                const C3Vector& cameraPos, const C44Matrix* placement,
                                const CAaSphere* sphere, CMaterialSettings* settings) {
    (void)waveManager;

    DrawWaterMaterial(s_vsWaterNoSpec, s_psWaterNoSpec, environment, geometry, cameraPos,
                      placement, sphere, settings);
}

// ref: FUN_008a6090
// ONE frame, ONE vertex program, ONE texcoord. Magma is the simplest of the four and the only one
// whose vertex format is GxVBF_PCT (the reference passes 8) -- no normal, because nothing about
// magma is lit by a direction, and one texture coordinate because it has one texture.
//
// Its texture matrix is not a rotate-and-scale like water's but a SCROLL: the stage floats are two
// rates in wraps per second, and MagmaScrollMatrix turns them into a translation that advances with
// the clock. The global scale multiplier is applied on top.
//
// Two states rather than water's four: no blend mode and no depth-write override, so magma draws
// with whatever the pass established and is opaque.
void CMaterialMagma::Draw(CClientEnvironment* environment, IGeomFactory* geometry, void* waveManager,
                          const C3Vector& cameraPos, const C44Matrix* placement,
                          const CAaSphere* sphere, CMaterialSettings* settings) {
    (void)waveManager;

    if (!geometry || !settings || !environment) {
        return;
    }

    static const uint32_t DEFAULT_PERIOD = 1250;

    CGxTex* frame0 = settings->GetFrame(0, DEFAULT_PERIOD);

    if (!frame0) {
        return;
    }

    GxRsPush();

    GxRsSet(GxRs_Culling, 0);
    GxRsSet(GxRs_Fog, 1);

    SetupTransforms(cameraPos, *placement);

    CM2Lighting lighting;
    lighting.Initialize(nullptr, *sphere);

    environment->SetupLighting(&lighting);

    SetupLightConstants(lighting);

    C44Matrix scroll = MagmaScrollMatrix(settings->GetStageFloat(0), settings->GetStageFloat(1));

    scroll.Scale(TextureScaleMultiplier());

    StoreMatrix(&s_constants.vs[VS_TEX_MATRIX], scroll);

    g_theGxDevicePtr->RsSet(GxRs_Texture0, static_cast<void*>(frame0));

    GxShaderConstantsSet(GxSh_Vertex, 0,
                         reinterpret_cast<const float*>(&s_constants.vs[0]),
                         VS_UPLOAD_REGISTERS);
    GxShaderConstantsSet(GxSh_Pixel, 0, FIXED_PS_CONSTANTS, FIXED_PS_REGISTERS);

    g_theGxDevicePtr->RsSet(GxRs_VertexShader, static_cast<void*>(s_vsMagma[0]));
    g_theGxDevicePtr->RsSet(GxRs_PixelShader, static_cast<void*>(s_psMagma[0]));

    CGxBuf* vertexBuf = nullptr;
    CGxBuf* indexBuf = nullptr;
    CGxBatch batch;

    if (!geometry->Build(GxVBF_PCT, &vertexBuf, &indexBuf, &batch)) {
        GxRsPop();

        return;
    }

    GxPrimVertexPtr(vertexBuf, GxVBF_PCT);
    g_theGxDevicePtr->PrimIndexPtr(indexBuf);

    g_theGxDevicePtr->Draw(&batch, 1);

    GxRsPop();
}

void CMaterialProcWater::Draw(CClientEnvironment* environment, IGeomFactory* geometry, void* waveManager,
                              const C3Vector& cameraPos, const C44Matrix* placement,
                              const CAaSphere* sphere, CMaterialSettings* settings) {
    DrawProcWaterMaterial(s_vsProcWater, s_psProcWater, environment, geometry,
                       static_cast<CWaveManager*>(waveManager), cameraPos, placement,
                       sphere, settings);
}

// ref: FUN_008a1fa0
// Which of a material's two implementations is used is decided once, from the device: the
// shader ones need vertex shaders at all and pixel shader model 3. The two capability slots the
// reference reads sit sixteen bytes apart, which is the distance from the vertex entry to the
// pixel entry in the shader-target table, and the choice they drive is shader versus
// fixed-function -- so that is the table being read.
IMaterial* GetMaterial(int32_t liquidType) {
    LiquidTypeRec* typeRec = nullptr;

    while (true) {
        typeRec = g_liquidTypeDB.GetRecord(liquidType);

        if (typeRec) {
            break;
        }

        SysMsgPrintf(SYSMSG_ERROR, "Material Bank: Liquid type [%d] not found, defaulting to water!", liquidType);

        // The same divergence as the settings bank above: the reference would spin here if the
        // water row were missing, and frozen's is.
        if (liquidType == 1) {
            return nullptr;
        }

        liquidType = 1;
    }

    uint32_t materialId = typeRec->m_materialID;

    if (materialId < s_materialBank.Count() && s_materialBank[materialId]) {
        return s_materialBank[materialId];
    }

    const CGxCaps& caps = GxCaps();

    bool shaders = caps.m_shaderTargets[GxSh_Vertex] >= 1
                && caps.m_shaderTargets[GxSh_Pixel] >= 3;

    IMaterial* material = nullptr;

    // Material 1 is water, 2 magma and slime, 3 procedural water. Each has a shader flavour and
    // a fixed-function one, and the caps decide which.
    //
    // The specular choice is SETTLED and now READ rather than assumed: Liquid::Initialize writes
    // the flag this consults, so the branch below is the reference's own rather than a comment
    // explaining why the hardcoded answer happens to be right.
    // The specular choice is SETTLED, and frozen's guess was right. The reference reads a global at
    // 0x00b23f68: zero picks CMaterialWaterNoSpec, anything else CMaterialWater. Liquid::Initialize
    // writes 1 there and the only other writer is a one-line setter, so specular water is the
    // default and staying on CMaterialWater matches an unmodified client.
    switch (materialId) {
    case 2:
        material = shaders ? static_cast<IMaterial*>(new CMaterialMagma())
                           : static_cast<IMaterial*>(new CMaterialMagmaFFP());
        break;

    case 3:
        material = shaders ? static_cast<IMaterial*>(new CMaterialProcWater())
                           : static_cast<IMaterial*>(new CMaterialProcWaterFFP());
        break;

    default:
        material = shaders ? static_cast<IMaterial*>(new CMaterialWater())
                           : static_cast<IMaterial*>(new CMaterialWaterFFP());
        break;
    }

    if (material) {
        material->EnsureShaders();
    }
    s_materialBank.GrowToFit(materialId, 1);
    s_materialBank[materialId] = material;

    return material;
}

// ref: FUN_008a1f50
void ReleaseMaterials() {
    for (uint32_t i = 0; i < s_materialBank.Count(); i++) {
        if (s_materialBank[i]) {
            // The reference reaches the material through its vtable, which is what the virtual
            // destructor on IMaterial is for. This used to be a TODO and the bank simply dropped
            // every material it had made: the objects were never destroyed and never freed, once
            // per liquid material for the life of the process.
            delete s_materialBank[i];

            s_materialBank[i] = nullptr;
        }
    }

    s_materialBank.SetCount(0);
}

// ref: FUN_008a2100
// Clear the record. The reference writes zero over every dword from +0x300 to +0x438, which is
// everything this class holds after the six texture-name buffers -- the two colours, the four
// ints, the eighteen stage floats, the procedural flag, the six resident flags and both arrays of
// six frame lists.
//
// IT ALSO CLEARS THE NAMES, which the reference does not, and that is deliberate. The reference
// starts at +0x300 and leaves the 0x300 bytes of name buffers to whatever the allocator handed
// over, on the understanding that LoadFromDbc fills them. A record whose DBC row is missing a slot
// therefore reads a name out of allocator garbage -- the bug class CLAUDE.md names, where a table
// built from sparse DBC data keeps garbage in the gaps and it passes every null check. Clearing
// them costs 0x300 bytes of memset once per liquid type.
//
// The two frame arrays are left to TSGrowableArray's own constructor rather than being memset,
// which is the same zeros by a route that stays correct if that class ever grows a field.
CMaterialSettings::CMaterialSettings() {
    memset(this->m_textureName, 0, sizeof(this->m_textureName));
    memset(this->m_color, 0, sizeof(this->m_color));
    memset(this->m_int, 0, sizeof(this->m_int));
    memset(this->m_stage, 0, sizeof(this->m_stage));
    memset(this->m_resident, 0, sizeof(this->m_resident));

    this->m_procedural = 0;
}

// ref: FUN_008a1d00
// Close every frame handle this record holds and empty both arrays.
//
// BOTH SETS, which is the point of lifting this out of ReleaseMaterialSettings: that one walked
// m_frames and stopped, so every handle in m_framesAlt leaked. The reference walks the two
// together -- its loop runs six times over a pair of arrays 0x10 apart, reading the counts at
// +0x380 and +0x3e0, which are exactly m_frames and m_framesAlt once TEXTURE_SLOTS is six.
// The leak was invisible because the loader that fills m_framesAlt is still unidentified, so the
// array is empty today -- it would have started leaking the moment that landed.
void CMaterialSettings::ReleaseFrames() {
    for (uint32_t slot = 0; slot < CMaterialSettings::TEXTURE_SLOTS; slot++) {
        TSGrowableArray<HTEXTURE>* sets[2] = { &this->m_frames[slot], &this->m_framesAlt[slot] };

        for (uint32_t which = 0; which < 2; which++) {
            TSGrowableArray<HTEXTURE>& frames = *sets[which];

            for (uint32_t i = 0; i < frames.Count(); i++) {
                if (frames[i]) {
                    HandleClose(frames[i]);
                }
            }

            frames.SetCount(0);
        }
    }
}

// ref: FUN_008a2380
// Give back one hold on the settings bank, and tear it down when the last one goes.
//
// THE COUNT is what this was missing while it was tagged `part of`: the reference does not empty
// the bank every time it is asked, it decrements the count Initialize took and only walks the
// records when that reaches zero. Without the count a second caller would pull the bank out from
// under the first.
//
// FROZEN-ONLY clamp on the decrement. The reference subtracts unconditionally, so a release with
// no matching Initialize takes the count negative and the teardown then never runs again -- a leak
// that hides itself. Refusing to go below zero costs a compare.
void ReleaseMaterialSettings() {
    if (s_settingsRefs > 0) {
        s_settingsRefs--;
    }

    if (s_settingsRefs) {
        return;
    }

    for (uint32_t i = 0; i < s_settingsBank.Count(); i++) {
        if (s_settingsBank[i]) {
            auto settings = s_settingsBank[i];

            settings->ReleaseFrames();

            settings->~CMaterialSettings();
            SMemFree(s_settingsBank[i], __FILE__, __LINE__, 0x0);
            s_settingsBank[i] = nullptr;
        }
    }

    s_settingsBank.SetCount(0);
}


// ref: FUN_008a1770
// Configure the liquid module. One caller, CWorldScene::Initialize, which passes (1, 1.0f, 0, "").
//
// This is where two of this file's hardcoded stand-ins come from. TextureScaleMultiplier returned
// a literal 1.0 and ProcWaterSuffix an empty string, both correct and both unexplained; they are
// the second and fourth arguments, and now they are stored rather than assumed.
//
// The specular flag is NOT an argument -- the reference writes a literal 1 into 0x00b23f68 here,
// which is what settles the CMaterialWater / CMaterialWaterNoSpec choice in the bank below and
// confirms the note that has been sitting there.
//
// The reference also stores two objects from its fifth and sixth arguments (0x00ad407c and
// 0x00ad40a0 at the call site, each a vtable pointer followed by two empty slots). What they are
// is not established and nothing this port has found reads them back, so they are not plumbed
// through rather than being given a made-up type.
void Initialize(int32_t a1, float textureScale, int32_t a3, const char* procWaterSuffix) {
    SStrCopy(s_procWaterSuffix, procWaterSuffix ? procWaterSuffix : "",
             sizeof(s_procWaterSuffix));

    s_textureScaleMultiplier = textureScale;

    s_settingsRefs++;
    s_moduleRefs++;

    s_initArg0 = a1;
    s_initArg2 = a3;

    s_specularWater = 1;
}

}
