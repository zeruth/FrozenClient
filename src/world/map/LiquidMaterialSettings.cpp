#include "world/map/LiquidMaterialSettings.hpp"
#include "db/Db.hpp"
#include "util/Log.hpp"
#include <storm/Array.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <common/Handle.hpp>
#include "gx/texture/CGxTex.hpp"
#include <tempest/Vector.hpp>
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
void CMaterialSettings::LoadTextures() {
    for (uint32_t slot = 0; slot < TEXTURE_SLOTS; slot++) {
        const char* name = this->m_textureName[slot];

        this->m_current[slot] = nullptr;

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
                // TODO FUN_004b6f30: the generated texture, looked up by name hash rather than
                // read off disk. Without it the still falls through to the solid below, which
                // is what the reference does only when the generator has nothing for the name.
                CImVector green = { 0, 0xff, 0, 0xff };

                texture = TextureCreateSolid(green);
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

        // Water. The reference loops rather than recursing, so a missing water row would spin;
        // it never happens, because no map loads without one.
        liquidType = 1;
    }
}

// ref: part of FUN_008a2380
void ReleaseMaterialSettings() {
    for (uint32_t i = 0; i < s_settingsBank.Count(); i++) {
        if (s_settingsBank[i]) {
            auto settings = s_settingsBank[i];

            for (uint32_t slot = 0; slot < CMaterialSettings::TEXTURE_SLOTS; slot++) {
                for (uint32_t frame = 0; frame < settings->m_frames[slot].Count(); frame++) {
                    if (settings->m_frames[slot][frame]) {
                        HandleClose(settings->m_frames[slot][frame]);
                    }
                }

                settings->m_frames[slot].SetCount(0);
            }

            settings->~CMaterialSettings();
            SMemFree(s_settingsBank[i], __FILE__, __LINE__, 0x0);
            s_settingsBank[i] = nullptr;
        }
    }

    s_settingsBank.SetCount(0);
}

}
