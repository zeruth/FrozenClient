#include "ui/game/CGMinimapFrameScript.hpp"
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
