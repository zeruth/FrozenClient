#include "ui/game/CGMinimapFrameScript.hpp"
#include <storm/String.hpp>
#include "ui/game/Types.hpp"
#include "ui/game/CGMinimapFrame.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScript_Object.hpp"
#include "ui/Types.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "gx/Coordinate.hpp"
#include "gx/Texture.hpp"
#include "util/CStatus.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cmath>
#include <cstdint>

namespace {

// The reference builds the same texture flags for every minimap overlay: linear filtering, no
// wrapping, no mip generation, one anisotropy sample.
CGxTexFlags MinimapTexFlags() {
    return CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1);
}

// ref: FUN_00583860
int32_t CGMinimapFrame_SetMaskTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetMaskTexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_maskTexture) {
        HandleClose(CGMinimapFrame::s_maskTexture);
    }

    CGMinimapFrame::s_maskTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetMaskTexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_00583ce0
int32_t CGMinimapFrame_SetIconTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetIconTexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_iconTexture) {
        HandleClose(CGMinimapFrame::s_iconTexture);
    }

    CGMinimapFrame::s_iconTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetIconTexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_00583e00
int32_t CGMinimapFrame_SetBlipTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetBlipTexture(\"file\")", frame->GetDisplayName());
    }

    // Alone among the overlay setters, this one also rejects an empty file name instead of handing
    // it to the texture cache.
    auto fileName = lua_tostring(L, 2);

    if (!fileName || !*fileName) {
        return luaL_error(L, "Usage: %s:SetBlipTexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_blipTexture) {
        HandleClose(CGMinimapFrame::s_blipTexture);
    }

    CGMinimapFrame::s_blipTexture = TextureCreate(fileName, MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetBlipTexture(): Couldn't load the file %s", frame->GetDisplayName(), fileName);
    }

    return 0;
}

// ref: FUN_00583f60
int32_t CGMinimapFrame_SetClassBlipTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetClassBlipTexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_classBlipTexture) {
        HandleClose(CGMinimapFrame::s_classBlipTexture);
    }

    CGMinimapFrame::s_classBlipTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetClassBlipTexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_00583980
int32_t CGMinimapFrame_SetPOIArrowTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    // The reference's own messages name this method SetPOITexture, not SetPOIArrowTexture.
    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetPOITexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_poiArrowTexture) {
        HandleClose(CGMinimapFrame::s_poiArrowTexture);
    }

    CGMinimapFrame::s_poiArrowTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetPOITexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_00583aa0
int32_t CGMinimapFrame_SetStaticPOIArrowTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetStaticPOITexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_staticPOIArrowTexture) {
        HandleClose(CGMinimapFrame::s_staticPOIArrowTexture);
    }

    CGMinimapFrame::s_staticPOIArrowTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetStaticPOITexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_00583bc0
int32_t CGMinimapFrame_SetCorpsePOIArrowTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetCorpsePOITexture(\"file\")", frame->GetDisplayName());
    }

    CStatus status;

    if (CGMinimapFrame::s_corpsePOIArrowTexture) {
        HandleClose(CGMinimapFrame::s_corpsePOIArrowTexture);
    }

    CGMinimapFrame::s_corpsePOIArrowTexture = TextureCreate(lua_tostring(L, 2), MinimapTexFlags(), &status, 3);

    // Corspe is the reference's own typo in this message; kept so the strings match.
    if (status.m_maxSeverity > STATUS_WARNING) {
        return luaL_error(L, "%s:SetCorspePOITexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    return 0;
}

// ref: FUN_0057e100
int32_t CGMinimapFrame_SetPlayerTexture(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetPlayerTexture(\"file\")", frame->GetDisplayName());
    }

    // The reference sets the arrow region's texture unconditionally; the null check is Frozen's,
    // because nothing creates the region yet and the member is still always null.
    //
    // The attribute does not create it. CGMinimapFrame::PostLoadXML (0057bea0) dereferences
    // m_playerTexture to call SetTexture on it, so the region already exists by then -- but not
    // from the shipped XML, which gives the Minimap element no Texture child. What creates it is
    // still unfound; see parity-minimap.md 2a-iii. The attribute only
    // names the file, and the shipped Minimap.xml never sets it, so the reference always uses the
    // fallback Interface\Minimap\MinimapArrow.tga. See docs/ref/parity-minimap.md 2a-i and 2a-ii.
    if (frame->m_playerTexture) {
        if (!frame->m_playerTexture->SetTexture(lua_tostring(L, 2), false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI)) {
            return luaL_error(L, "%s:SetPlayerTexture(): Couldn't load the file %s", frame->GetDisplayName(), lua_tostring(L, 2));
        }
    }

    return 0;
}

// ref: FUN_0057e1c0
int32_t CGMinimapFrame_SetPlayerTextureHeight(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetPlayerTextureHeight(height)", frame->GetDisplayName());
    }

    // The argument arrives in interface pixels: dividing by the screen width in those units gives a
    // fraction of the screen, which then goes into DDC.
    auto height = static_cast<float>(lua_tonumber(L, 2));
    height = NDCToDDCWidth(height / (CoordinateGetAspectCompensation() * 1024.0f));

    if (frame->m_playerTexture) {
        frame->m_playerTexture->SetHeight(height);
    }

    return 0;
}

// ref: FUN_0057e280
int32_t CGMinimapFrame_SetPlayerTextureWidth(lua_State* L) {
    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    // The reference capitalises the argument in this one usage message.
    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetPlayerTextureWidth(Width)", frame->GetDisplayName());
    }

    auto width = static_cast<float>(lua_tonumber(L, 2));
    width = NDCToDDCWidth(width / (CoordinateGetAspectCompensation() * 1024.0f));

    if (frame->m_playerTexture) {
        frame->m_playerTexture->SetWidth(width);
    }

    return 0;
}

// ref: FUN_0057bf50
int32_t CGMinimapFrame_GetZoomLevels(lua_State* L) {
    // The reference converts the count as unsigned before pushing it.
    lua_pushnumber(L, static_cast<double>(CGMinimapFrame::GetZoomLevels()));

    return 1;
}

// ref: FUN_0057bf90
int32_t CGMinimapFrame_GetZoom(lua_State* L) {
    lua_pushnumber(L, static_cast<double>(CGMinimapFrame::GetZoom()));

    return 1;
}

// ref: FUN_0057bfd0
int32_t CGMinimapFrame_SetZoom(lua_State* L) {
    // Alone among the minimap bindings this one neither takes nor names a frame: the zoom is module
    // state, so the usage message carries no object name.
    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: SetZoom(level)");
    }

    // The reference rounds on the x87 stack, which rounds half to even, then truncates the result
    // to 32 bits.
    auto level = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(lua_tonumber(L, 2))));

    CGMinimapFrame::SetZoom(level);

    return 0;
}

// FUN_0057ed70. Not ported: the ping itself is a game system Frozen does not have. The reference
// turns the click into a world position with the active player object and the rotateMinimap CVar's
// facing matrix, then hands it to the ping store (FUN_0057eb80), which names the pinging unit,
// broadcasts the ping and raises MINIMAP_PING. None of that exists here, so the binding stays a
// stub rather than pretending to record a ping.
int32_t CGMinimapFrame_PingLocation(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// FUN_0057efe0. Not ported for the same reason: it reads back the position the ping store wrote
// (DAT_00beba8c / DAT_00beba90) relative to the active player. With no ping store there is nothing
// to read, and answering (0, 0) would look like a ping at the player's feet.
int32_t CGMinimapFrame_GetPingPosition(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

namespace {

// The reference builds this path twice with two different buffer sizes (0x80 in GetTrackingInfo,
// 0x104 in GetTrackingTexture); the shared helper keeps the larger one, since the difference only
// ever mattered to its stack layout.
void TrackingTexturePath(char* buffer, size_t size, const char* name) {
    SStrPrintf(buffer, size, "%s\\%s", "Interface\\Minimap\\Tracking", name);
}

} // namespace

// ref: FUN_0057f170
int32_t Script_GetNumTrackingTypes(lua_State* L) {
    auto count = CGMinimapFrame::s_numTrackingSpells + CGMinimapFrame::GetNumOtherTrackingTypes();

    lua_pushnumber(L, static_cast<double>(count));

    return 1;
}

// ref: FUN_0057f1b0
// name, texture, active, category. Tracking ids are 1-based and run over the spell half first.
int32_t Script_GetTrackingInfo(lua_State* L) {
    auto id = static_cast<uint32_t>(static_cast<int32_t>(luaL_checknumber(L, 1))) - 1;

    if (id < CGMinimapFrame::s_numTrackingSpells) {
        // TODO the spell half. The reference reads the spell record, picks the active icon when the
        // spell is the one being tracked and the normal icon otherwise, resolves it through
        // SpellIcon.dbc and reports the category as "spell". s_numTrackingSpells is zero until the
        // spellbook side lands, so this branch is unreachable rather than wrong.
        return 0;
    }

    auto type = CGMinimapFrame::GetOtherTrackingType(id - CGMinimapFrame::s_numTrackingSpells);

    if (!type) {
        return 0;
    }

    lua_pushstring(L, FrameScript_GetText(type->name, -1, GENDER_NOT_APPLICABLE));

    char texture[260];
    TrackingTexturePath(texture, sizeof(texture), type->texture);
    lua_pushstring(L, texture);

    // Active is 1 or nil rather than true or false, which is what MiniMapTrackingDropDown checks.
    if (CGMinimapFrame::s_otherTracking == type) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    lua_pushstring(L, "other");

    return 4;
}

// ref: FUN_0057f380
// With no argument, tracking is cleared. With one, the id selects a spell to cast or a row of the
// table to switch to -- either way only one thing is tracked at a time.
int32_t Script_SetTracking(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        if (CGMinimapFrame::s_trackingSpell) {
            // TODO cancel the tracking aura. Unreachable while nothing sets s_trackingSpell.
            CGMinimapFrame::s_trackingSpell = 0;

            FrameScript_SignalEvent(SCRIPT_MINIMAP_UPDATE_TRACKING, nullptr);
        }

        CGMinimapFrame::SetOtherTracking(nullptr);

        return 0;
    }

    auto id = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))) - 1;

    if (id < CGMinimapFrame::s_numTrackingSpells) {
        // TODO cast the tracking spell. Unreachable for the same reason.
        return 0;
    }

    auto type = CGMinimapFrame::GetOtherTrackingType(id - CGMinimapFrame::s_numTrackingSpells);

    if (type) {
        // Picking a table row cancels any tracking spell: the two halves share one slot.
        CGMinimapFrame::SetOtherTracking(type);
    }

    return 0;
}

// ref: FUN_0057f4f0
// Always returns a path. With nothing tracked that path is the "None" icon, which is what the
// minimap button falls back to rather than showing an empty square.
int32_t Script_GetTrackingTexture(lua_State* L) {
    auto name = "None";

    if (CGMinimapFrame::s_otherTracking) {
        name = CGMinimapFrame::s_otherTracking->texture;
    }

    // TODO with a tracking spell active the reference uses that spell's SpellIcon.dbc texture
    // directly instead of a name under Tracking\\, and returns early. Not reachable yet.

    char texture[260];
    TrackingTexturePath(texture, sizeof(texture), name);
    lua_pushstring(L, texture);

    return 1;
}

FrameScript_Method CGMinimapFrameScriptFunctions[] = {
    { "GetNumTrackingTypes",    &Script_GetNumTrackingTypes },
    { "GetTrackingInfo",        &Script_GetTrackingInfo },
    { "SetTracking",            &Script_SetTracking },
    { "GetTrackingTexture",     &Script_GetTrackingTexture },
};

// These four sit next to CGMinimapFrameMethods in the reference's .rdata but are not methods on the
// frame: FrameXML calls every one of them bare (Minimap.lua:409, :421, :427, :430) and never as
// Minimap:GetTrackingInfo(). So they register globally.
void CGMinimapFrameScriptRegisterFunctions() {
    for (auto& func : CGMinimapFrameScriptFunctions) {
        FrameScript_RegisterFunction(func.name, func.method);
    }
}

FrameScript_Method CGMinimapFrameMethods[] = {
    { "SetMaskTexture",             &CGMinimapFrame_SetMaskTexture },
    { "SetIconTexture",             &CGMinimapFrame_SetIconTexture },
    { "SetBlipTexture",             &CGMinimapFrame_SetBlipTexture },
    { "SetClassBlipTexture",        &CGMinimapFrame_SetClassBlipTexture },
    { "SetPOIArrowTexture",         &CGMinimapFrame_SetPOIArrowTexture },
    { "SetStaticPOIArrowTexture",   &CGMinimapFrame_SetStaticPOIArrowTexture },
    { "SetCorpsePOIArrowTexture",   &CGMinimapFrame_SetCorpsePOIArrowTexture },
    { "SetPlayerTexture",           &CGMinimapFrame_SetPlayerTexture },
    { "SetPlayerTextureHeight",     &CGMinimapFrame_SetPlayerTextureHeight },
    { "SetPlayerTextureWidth",      &CGMinimapFrame_SetPlayerTextureWidth },
    { "GetZoomLevels",              &CGMinimapFrame_GetZoomLevels },
    { "GetZoom",                    &CGMinimapFrame_GetZoom },
    { "SetZoom",                    &CGMinimapFrame_SetZoom },
    { "PingLocation",               &CGMinimapFrame_PingLocation },
    { "GetPingPosition",            &CGMinimapFrame_GetPingPosition },
};
