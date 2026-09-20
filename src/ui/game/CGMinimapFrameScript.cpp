#include "ui/game/CGMinimapFrameScript.hpp"
#include "object/client/AuraCache.hpp"
#include <common/DataStore.hpp>
#include "client/ClientServices.hpp"
#include "ui/game/CGRaidInfo.hpp"
#include "ui/game/CGPartyInfo.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "object/client/SpellBook.hpp"
#include "db/Db.hpp"
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

// ref: FUN_0057ed70
// Turns a click inside the minimap into a world position and pings it.
//
// The offsets arrive in the same units SetPlayerTextureWidth above takes, and go through the same
// conversion -- NDCToDDCWidth over the aspect compensation times 1024 -- which is what identified
// it. Both axes use the WIDTH conversion; only the division afterwards differs, x by the frame's
// width and y by its height.
//
// The axes swap: the horizontal offset becomes a world Y and the vertical one a world X. That is
// the usual world convention rather than anything about the minimap, and the horizontal one is
// also negated.
//
// The packet is only sent to a group -- a solo ping still shows locally, which is why SetPing is
// called either way rather than only on the send path.
int32_t CGMinimapFrame_PingLocation(lua_State* L) {
    auto player = CGPlayer_C::GetActivePtr();

    if (!player) {
        return 0;
    }

    auto type = CGMinimapFrame::GetObjectType();
    auto frame = static_cast<CGMinimapFrame*>(FrameScript_GetObjectThis(L, type));

    // Twice the radius, because the offsets below are measured across the whole minimap.
    auto diameter = CGMinimapFrame::GetRadius() * 2.0f;

    float offsetX = 0.0f;
    float offsetY = 0.0f;

    // Both offsets or neither: the reference checks them together and pings the player's own
    // position when they are missing.
    if (lua_isnumber(L, 2) && lua_isnumber(L, 3)) {
        auto scale = CoordinateGetAspectCompensation() * 1024.0f;

        offsetY = -NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale);
        offsetX = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 3)) / scale);

        auto width = frame ? frame->GetWidth() : 0.0f;
        auto height = frame ? frame->GetHeight() : 0.0f;

        offsetX = height ? offsetX / height * diameter : 0.0f;
        offsetY = width ? offsetY / width * diameter : 0.0f;
    }

    // TODO with rotateMinimap set the reference rotates the offset by the player's facing here,
    // the same rotation the ping store applies in reverse. Not ported for the same reason.

    auto position = player->GetPosition();
    auto worldX = position.x + offsetX;
    auto worldY = position.y + offsetY;

    if (CGPartyInfo::NumMembers() || CGRaidInfo::NumMembers()) {
        CDataStore msg;
        msg.Put(static_cast<uint32_t>(MSG_MINIMAP_PING));
        msg.Put(worldX);
        msg.Put(worldY);
        msg.Finalize();

        ClientServices::Send(&msg);
    }

    CGMinimapFrame::SetPing(ClntObjMgrGetActivePlayer(), worldX, worldY);

    return 0;
}
// ref: FUN_0057efe0
// Where the last ping sits relative to the player right now, in the same units the MINIMAP_PING
// event uses. Recomputed on each call rather than stored, so it follows the player as they move.
//
// Two zeros with no player, which is what the reference pushes -- not nil, and not nothing.
int32_t CGMinimapFrame_GetPingPosition(lua_State* L) {
    float x = 0.0f;
    float y = 0.0f;

    CGMinimapFrame::GetPingOffset(&x, &y);

    lua_pushnumber(L, x);
    lua_pushnumber(L, y);

    return 2;
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
    auto count = CGMinimapFrame::GetNumTrackingSpells() + CGMinimapFrame::GetNumOtherTrackingTypes();

    lua_pushnumber(L, static_cast<double>(count));

    return 1;
}

// ref: FUN_0057f1b0
// name, texture, active, category. Tracking ids are 1-based and run over the spell half first.
int32_t Script_GetTrackingInfo(lua_State* L) {
    auto id = static_cast<uint32_t>(static_cast<int32_t>(luaL_checknumber(L, 1))) - 1;

    auto numSpells = CGMinimapFrame::GetNumTrackingSpells();

    if (id < numSpells) {
        auto spellID = CGMinimapFrame::GetTrackingSpell(id);
        auto spell = g_spellDB.GetRecord(static_cast<int32_t>(spellID));

        if (!spell) {
            return 0;
        }

        auto active = CGMinimapFrame::s_trackingSpell == spellID;

        lua_pushstring(L, spell->m_name);

        // The active icon is used only while this spell is the one being tracked, and only when it
        // has one -- a spell with no separate active icon keeps its normal one either way.
        auto iconID = (active && spell->m_activeIconID) ? spell->m_activeIconID : spell->m_spellIconID;
        auto icon = g_spellIconDB.GetRecord(iconID);

        if (icon && icon->m_textureFilename) {
            lua_pushstring(L, icon->m_textureFilename);
        } else {
            lua_pushnil(L);
        }

        if (active) {
            lua_pushnumber(L, 1.0);
        } else {
            lua_pushnil(L);
        }

        lua_pushstring(L, "spell");

        return 4;
    }

    auto type = CGMinimapFrame::GetOtherTrackingType(id - numSpells);

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
            // Tracking IS an aura, so clearing it is cancelling that aura. The server dropping it
            // is what will clear s_trackingSpell, through the same aura path that set it.
            AuraCacheCancel(CGMinimapFrame::s_trackingSpell);
        }

        CGMinimapFrame::SetOtherTracking(nullptr);

        return 0;
    }

    auto id = static_cast<uint32_t>(static_cast<int32_t>(lua_tonumber(L, 1))) - 1;

    auto numSpells = CGMinimapFrame::GetNumTrackingSpells();

    if (id < numSpells) {
        // Tracking IS the spell: selecting one casts it, and the server's aura is what makes it
        // take effect. Nothing else is set here, which is why the reference does not signal
        // MINIMAP_UPDATE_TRACKING on this path.
        SpellBookCast(CGMinimapFrame::GetTrackingSpell(id), 0);

        return 0;
    }

    auto type = CGMinimapFrame::GetOtherTrackingType(id - numSpells);

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
