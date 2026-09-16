#include "ui/simple/CSimpleFrameScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/CBackdropGenerator.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameXML.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleFont.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleFrame.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <storm/Memory.hpp>
#include <algorithm>
#include <cstdint>
#include <limits>

int32_t CSimpleFrame_GetTitleRegion(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_CreateTitleRegion(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_CreateTexture(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    const char* name = nullptr;
    if (lua_isstring(L, 2)) {
        name = lua_tostring(L, 2);
    }

    int32_t drawlayer = DRAWLAYER_ARTWORK;
    if (lua_isstring(L, 3)) {
        auto drawlayerStr = lua_tostring(L, 3);
        StringToDrawLayer(drawlayerStr, drawlayer);
    }

    XMLNode* inheritNode = nullptr;

    if (lua_type(L, 4) == LUA_TSTRING) {
        auto inheritName = lua_tostring(L, 4);
        const char* tainted;
        bool locked;

        inheritNode = FrameXML_AcquireHashNode(inheritName, tainted, locked);

        if (!inheritNode) {
            luaL_error(L, "%s:CreateTexture(): Couldn't find inherited node \"%s\"", frame->GetDisplayName(), inheritName);
            return 0;
        }

        if (locked) {
            luaL_error(L, "%s:CreateTexture(): Recursively inherited node \"%s\"", frame->GetDisplayName(), inheritName);
            return 0;
        }
    }

    // TODO CDataAllocator::GetData
    auto texture = STORM_NEW(CSimpleTexture)(frame, drawlayer, true);

    if (name && *name) {
        texture->SetName(name);
    }

    if (inheritNode) {
        CStatus status;

        texture->LoadXML(inheritNode, &status);
        texture->PostLoadXML(inheritNode, &status);

        auto inheritName = lua_tostring(L, 4);
        FrameXML_ReleaseHashNode(inheritName);
    }

    // TODO anim related logic?

    if (!texture->lua_registered) {
        texture->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, texture->lua_objectRef);

    return 1;
}

int32_t CSimpleFrame_CreateFontString(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    const char* name = nullptr;
    if (lua_isstring(L, 2)) {
        name = lua_tostring(L, 2);
    }

    int32_t drawlayer = DRAWLAYER_ARTWORK;
    if (lua_isstring(L, 3)) {
        auto drawlayerStr = lua_tostring(L, 3);
        StringToDrawLayer(drawlayerStr, drawlayer);
    }

    CSimpleFont* inheritFont = nullptr;
    XMLNode* inheritNode = nullptr;

    if (lua_type(L, 4) == LUA_TSTRING) {
        auto inheritName = lua_tostring(L, 4);

        inheritFont = CSimpleFont::GetFont(inheritName, 0);

        if (!inheritFont) {
            const char* tainted;
            bool locked;
            inheritNode = FrameXML_AcquireHashNode(inheritName, tainted, locked);

            if (!inheritNode) {
                luaL_error(L, "%s:CreateFontString(): Couldn't find inherited node \"%s\"", frame->GetDisplayName(), inheritName);
                return 0;
            }

            if (locked) {
                luaL_error(L, "%s:CreateFontString(): Recursively inherited node \"%s\"", frame->GetDisplayName(), inheritName);
                return 0;
            }
        }
    }

    // TODO CDataAllocator::GetData
    auto string = STORM_NEW(CSimpleFontString)(frame, drawlayer, true);

    if (name && *name) {
        string->SetName(name);
    }

    if (inheritFont) {
        string->SetFontObject(inheritFont);
    } else if (inheritNode) {
        CStatus status;

        string->LoadXML(inheritNode, &status);
        string->PostLoadXML(inheritNode, &status);

        auto inheritName = lua_tostring(L, 4);
        FrameXML_ReleaseHashNode(inheritName);
    }

    // TODO anim related logic?

    if (!string->lua_registered) {
        string->RegisterScriptObject(nullptr);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, string->lua_objectRef);

    return 1;
}

int32_t CSimpleFrame_GetBoundsRect(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    CRect bounds = {
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        0.0f,
        0.0f
    };

    if (!frame->GetBoundsRect(bounds)) {
        return 0;
    }

    float ooScale = 1.0f / frame->m_layoutScale;

    float ddcTop = CoordinateGetAspectCompensation() * 1024.0f * ooScale * bounds.minX;
    float ndcTop = DDCToNDCWidth(ddcTop);
    lua_pushnumber(L, ndcTop);

    float ddcLeft = CoordinateGetAspectCompensation() * 1024.0f * ooScale * bounds.minY;
    float ndcLeft = DDCToNDCWidth(ddcLeft);
    lua_pushnumber(L, ndcLeft);

    float ddcWidth = CoordinateGetAspectCompensation() * 1024.0f * ooScale * (bounds.maxX - bounds.minX);
    float ndcWidth = DDCToNDCWidth(ddcWidth);
    lua_pushnumber(L, ndcWidth);

    float ddcHeight = CoordinateGetAspectCompensation() * 1024.0f * ooScale * (bounds.maxY - bounds.minY);
    float ndcHeight = DDCToNDCWidth(ddcHeight);
    lua_pushnumber(L, ndcHeight);

    return 4;
}

int32_t CSimpleFrame_GetNumRegions(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    int32_t count = 0;

    for (auto region = frame->m_regions.Head(); region; region = frame->m_regions.Next(region)) {
        count++;
    }

    lua_pushnumber(L, count);

    return 1;
}

int32_t CSimpleFrame_GetRegions(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    int32_t count = 0;

    for (auto region = frame->m_regions.Head(); region; region = frame->m_regions.Next(region)) {
        if (!region->lua_registered) {
            region->RegisterScriptObject(0);
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, region->lua_objectRef);
        count++;
    }

    return count;
}

int32_t CSimpleFrame_GetNumChildren(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    int32_t count = 0;

    for (auto node = frame->m_children.Head(); node; node = node->Next()) {
        count++;
    }

    lua_pushnumber(L, count);

    return 1;
}

int32_t CSimpleFrame_GetChildren(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // Every child pushed as a separate return value, which is what FrameXML's
    // `local a, b, c = f:GetChildren()` and `{ f:GetChildren() }` both expect.
    int32_t count = 0;

    for (auto node = frame->m_children.Head(); node; node = node->Next()) {
        auto child = node->frame;

        if (!child) {
            continue;
        }

        if (!child->lua_registered) {
            child->RegisterScriptObject(0);
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, child->lua_objectRef);
        count++;
    }

    return count;
}

int32_t CSimpleFrame_GetFrameStrata(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    const char* name = FrameStrataToString(frame->m_strata);

    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFrame_SetFrameStrata(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetFrameStrata(\"strata\")", frame->GetDisplayName());
    }

    FRAME_STRATA strata;

    if (!StringToFrameStrata(lua_tostring(L, 2), strata)) {
        return luaL_error(L, "%s:SetFrameStrata(): Unknown strata %s", frame->GetDisplayName(), lua_tostring(L, 2));
    }

    frame->SetFrameStrata(strata);

    return 0;
}

int32_t CSimpleFrame_GetFrameLevel(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, frame->m_level);

    return 1;
}

int32_t CSimpleFrame_SetFrameLevel(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetFrameLevel(level)", frame->GetDisplayName());
    }

    int32_t level = lua_tonumber(L, 2);

    if (level < 0) {
        return luaL_error(L, "%s:SetFrameLevel(): Passed negative frame level: %d", frame->GetDisplayName(), level);
    }

    frame->SetFrameLevel(level, 1);

    return 0;
}

int32_t CSimpleFrame_HasScript(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetScript(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    return frame->GetScript(L);
}

int32_t CSimpleFrame_SetScript(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    return frame->SetScript(L);
}

int32_t CSimpleFrame_HookScript(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_RegisterEvent(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:RegisterEvent(\"event\")", frame->GetDisplayName());
    }

    const char* event = lua_tostring(L, 2);

    frame->RegisterScriptEvent(event);

    return 0;
}

int32_t CSimpleFrame_UnregisterEvent(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:UnregisterEvent(\"event\")", frame->GetDisplayName());
        return 0;
    }

    auto eventName = lua_tostring(L, 2);

    frame->UnregisterScriptEvent(eventName);

    return 0;
}

int32_t CSimpleFrame_RegisterAllEvents(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_UnregisterAllEvents(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_IsEventRegistered(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:IsEventRegistered(\"event\")", frame->GetDisplayName());
    }

    // The registry is per-event: each entry keeps the list of objects listening to it, which is the
    // same list RegisterScriptEvent appends to.
    auto event = FrameScript::s_scriptEventsHash.Ptr(lua_tostring(L, 2));
    int32_t registered = 0;

    if (event) {
        for (auto node = event->listeners.Head(); node; node = node->Next()) {
            if (node->listener == frame) {
                registered = 1;
                break;
            }
        }
    }

    lua_pushboolean(L, registered);

    return 1;
}

int32_t CSimpleFrame_AllowAttributeChanges(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_CanChangeAttributes(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetAttribute(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // 3 argument form

    if (lua_gettop(L) == 4 && lua_isstring(L, 3)) {
        size_t prefixLen, nameLen, suffixLen;
        auto prefix = lua_tolstring(L, 2, &prefixLen);
        auto name = lua_tolstring(L, 3, &nameLen);
        auto suffix = lua_tolstring(L, 4, &suffixLen);

        char buffer[256];
        char* write;
        size_t remaining;
        size_t copyLen;

        int32_t luaRef;

        // Attempt 1: prefix + name + suffix

        write = buffer;
        remaining = 255;

        if (prefixLen > 0) {
            copyLen = (prefixLen < remaining) ? prefixLen : remaining;
            memcpy(write, prefix, copyLen);
            write += copyLen;
            remaining -= copyLen;
        }

        if (nameLen > 0) {
            copyLen = (nameLen < remaining) ? nameLen : remaining;
            memcpy(write, name, copyLen);
            write += copyLen;
            remaining -= copyLen;
        }

        if (suffixLen > 0) {
            copyLen = (suffixLen < remaining) ? suffixLen : remaining;
            memcpy(write, suffix, copyLen);
            write += copyLen;
        }

        *write = '\0';

        if (frame->GetAttribute(buffer, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Attempt 2: "*" + name + suffix

        write = buffer;
        *write++ = '*';
        remaining = 254;

        if (nameLen > 0) {
            copyLen = (nameLen < remaining) ? nameLen : remaining;
            memcpy(write, name, copyLen);
            write += copyLen;
            remaining -= copyLen;
        }

        if (suffixLen > 0) {
            copyLen = (suffixLen < remaining) ? suffixLen : remaining;
            memcpy(write, suffix, copyLen);
            write += copyLen;
        }

        *write = '\0';

        if (frame->GetAttribute(buffer, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Attempt 3: prefix + name + "*"

        write = buffer;
        remaining = 254;

        if (prefixLen > 0) {
            copyLen = (prefixLen < remaining) ? prefixLen : remaining;
            memcpy(write, prefix, copyLen);
            write += copyLen;
            remaining -= copyLen;
        }

        if (nameLen > 0) {
            copyLen = (nameLen < remaining) ? nameLen : remaining;
            memcpy(write, name, copyLen);
            write += copyLen;
        }

        *write++ = '*';
        *write = '\0';

        if (frame->GetAttribute(buffer, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Attempt 4: "*" + name + "*"

        write = buffer;
        *write++ = '*';
        remaining = 253;

        if (nameLen > 0) {
            copyLen = (nameLen < remaining) ? nameLen : remaining;
            memcpy(write, name, copyLen);
            write += copyLen;
        }

        *write++ = '*';
        *write = '\0';

        if (frame->GetAttribute(buffer, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Attempt 5: name

        if (frame->GetAttribute(name, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Not found

        lua_pushnil(L);
        return 1;
    }

    // 1 argument form

    if (lua_isstring(L, 2)) {
        auto attrName = lua_tostring(L, 2);
        int32_t luaRef;

        if (frame->GetAttribute(attrName, luaRef)) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, luaRef);
            return 1;
        }

        // Not found

        lua_pushnil(L);
        return 1;
    }

    // Invalid call

    luaL_error(L, "Usage: %s:GetAttribute(\"name\")", frame->GetDisplayName());
    return 0;
}

int32_t CSimpleFrame_SetAttribute(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed() && !frame->AttributeChangesAllowed()) {
        // TODO disallowed logic

        return 0;
    }

    lua_settop(L, 3);

    if (!lua_isstring(L, 2) || lua_type(L, 3) == LUA_TNONE) {
        luaL_error(L, "Usage: %s:SetAttribute(\"name\", value)", frame->GetDisplayName());
        return 0;
    }

    auto attrName = lua_tostring(L, 2);
    int32_t luaRef;

    if (frame->GetAttribute(attrName, luaRef)) {
        luaL_unref(L, LUA_REGISTRYINDEX, luaRef);
    }

    // TODO taint management

    luaRef = luaL_ref(L, LUA_REGISTRYINDEX);

    // TODO taint management

    frame->SetAttribute(attrName, luaRef);

    return 0;
}

int32_t CSimpleFrame_GetEffectiveScale(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // The frame's own scale with every parent's folded in, which is what the layout already keeps.
    lua_pushnumber(L, frame->m_layoutScale);

    return 1;
}

int32_t CSimpleFrame_GetScale(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, frame->m_frameScale);

    return 1;
}

int32_t CSimpleFrame_SetScale(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetScale(scale)", frame->GetDisplayName());
    }

    auto scale = lua_tonumber(L, 2);

    if (scale <= 0.0) {
        return luaL_error(L, "%s:SetScale(): Scale must be > 0", frame->GetDisplayName());
    }

    frame->SetFrameScale(scale, false);

    return 0;
}

int32_t CSimpleFrame_GetEffectiveAlpha(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetAlpha(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // SetAlpha stores the value as a byte; script works in 0..1.
    lua_pushnumber(L, frame->m_alpha / 255.0f);

    return 1;
}

int32_t CSimpleFrame_SetAlpha(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetAlpha(alpha 0 to 1)", frame->GetDisplayName());
    }

    float alpha = lua_tonumber(L, 2);
    alpha = std::max(std::min(alpha, 1.0f), 0.0f);
    frame->SetFrameAlpha(static_cast<uint8_t>(alpha * 255.0f));

    return 0;
}

int32_t CSimpleFrame_GetID(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, frame->m_id);

    return 1;
}

int32_t CSimpleFrame_SetID(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetID(ID)", frame->GetDisplayName());
    }

    frame->m_id = lua_tonumber(L, 2);

    return 0;
}

int32_t CSimpleFrame_SetToplevel(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    frame->SetFrameFlag(FRAME_FLAG_TOPLEVEL, StringToBOOL(L, 2, 1));

    return 0;
}

int32_t CSimpleFrame_IsToplevel(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_flags & FRAME_FLAG_TOPLEVEL) != 0);

    return 1;
}

int32_t CSimpleFrame_EnableDrawLayer(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    int32_t layer;

    if (!lua_isstring(L, 2) || !StringToDrawLayer(lua_tostring(L, 2), layer)) {
        return luaL_error(L, "Usage: %s:EnableDrawLayer(\"layer\")", frame->GetDisplayName());
    }

    frame->EnableDrawLayer(layer);

    return 0;
}

int32_t CSimpleFrame_DisableDrawLayer(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    int32_t layer;

    if (!lua_isstring(L, 2) || !StringToDrawLayer(lua_tostring(L, 2), layer)) {
        return luaL_error(L, "Usage: %s:DisableDrawLayer(\"layer\")", frame->GetDisplayName());
    }

    frame->DisableDrawLayer(layer);

    return 0;
}

int32_t CSimpleFrame_Show(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (frame->ProtectedFunctionsAllowed()) {
        frame->Show();
    } else {
        // TODO
        // - disallowed logic
    }

    return 0;
}

int32_t CSimpleFrame_Hide(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (frame->ProtectedFunctionsAllowed()) {
        frame->Hide();
    } else {
        // TODO
        // - disallowed logic
    }

    return 0;
}

int32_t CSimpleFrame_IsVisible(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (frame->m_visible) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFrame_IsShown(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (frame->m_shown) {
        lua_pushnumber(L, 1.0);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleFrame_Raise(lua_State* L) {
    int32_t type = CSimpleFrame::GetObjectType();
    CSimpleFrame* frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    frame->Raise();

    return 0;
}

int32_t CSimpleFrame_Lower(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetHitRectInsets(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetHitRectInsets(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4) || !lua_isnumber(L, 5)) {
        return luaL_error(L, "Usage: %s:SetHitRectInsets(left, right, top, bottom)", frame->GetDisplayName());
    }

    float scale = CoordinateGetAspectCompensation() * 1024.0f;
    float left = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 2)) / scale);
    float right = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 3)) / scale);
    float top = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 4)) / scale);
    float bottom = NDCToDDCWidth(static_cast<float>(lua_tonumber(L, 5)) / scale);

    frame->SetHitRectInsets(left, right, top, bottom);

    return 0;
}

int32_t CSimpleFrame_GetClampRectInsets(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetClampRectInsets(lua_State* L) {
    // Clamping keeps a movable frame on screen; movement is not ported
    return 0;
}

int32_t CSimpleFrame_GetMinResize(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetMinResize(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetMaxResize(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetMaxResize(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetMovable(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    frame->SetFrameFlag(FRAME_FLAG_MOVABLE, StringToBOOL(L, 2, 1));

    return 0;
}

int32_t CSimpleFrame_IsMovable(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_flags & FRAME_FLAG_MOVABLE) != 0);

    return 1;
}

int32_t CSimpleFrame_SetDontSavePosition(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetDontSavePosition(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetResizable(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    frame->SetFrameFlag(FRAME_FLAG_RESIZABLE, StringToBOOL(L, 2, 1));

    return 0;
}

int32_t CSimpleFrame_IsResizable(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_flags & FRAME_FLAG_RESIZABLE) != 0);

    return 1;
}

int32_t CSimpleFrame_StartMoving(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_StartSizing(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_StopMovingOrSizing(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetUserPlaced(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // The reference refuses this unless the frame is movable or resizable, with this exact message
    // (FUN_004a0c70 tests flags & 0x300 first).
    if (!(frame->m_flags & (FRAME_FLAG_MOVABLE | FRAME_FLAG_RESIZABLE))) {
        return luaL_error(L, "Frame %s is not movable or resizable", frame->GetDisplayName());
    }

    frame->SetFrameFlag(FRAME_FLAG_USER_PLACED, StringToBOOL(L, 2, 1));

    return 0;
}

int32_t CSimpleFrame_IsUserPlaced(lua_State* L) {
    lua_pushnil(L);

    return 1;
}

int32_t CSimpleFrame_SetClampedToScreen(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_IsClampedToScreen(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_RegisterForDrag(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    uint32_t buttons = 0;

    for (int32_t i = 2; lua_isstring(L, i); i++) {
        auto name = lua_tostring(L, i);

        if (!SStrCmpI(name, "LeftButton", STORM_MAX_STR)) {
            buttons |= MOUSE_BUTTON_LEFT;
        } else if (!SStrCmpI(name, "RightButton", STORM_MAX_STR)) {
            buttons |= MOUSE_BUTTON_RIGHT;
        } else if (!SStrCmpI(name, "MiddleButton", STORM_MAX_STR)) {
            buttons |= MOUSE_BUTTON_MIDDLE;
        } else if (!SStrCmpI(name, "Button4", STORM_MAX_STR)) {
            buttons |= MOUSE_BUTTON_XBUTTON1;
        } else if (!SStrCmpI(name, "Button5", STORM_MAX_STR)) {
            buttons |= MOUSE_BUTTON_XBUTTON2;
        }
    }

    frame->m_lookForDrag = buttons;

    return 0;
}

int32_t CSimpleFrame_EnableKeyboard(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (lua_toboolean(L, 2)) {
        frame->EnableEvent(SIMPLE_EVENT_KEY, -1);
    } else {
        frame->DisableEvent(SIMPLE_EVENT_KEY);
    }

    return 0;
}

int32_t CSimpleFrame_IsKeyboardEnabled(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_eventmask & (1 << SIMPLE_EVENT_KEY)) != 0);

    return 1;
}

int32_t CSimpleFrame_EnableMouse(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (!frame->ProtectedFunctionsAllowed()) {
        // TODO disallowed logic

        return 0;
    }

    if (StringToBOOL(L, 2, 1)) {
        frame->EnableEvent(SIMPLE_EVENT_MOUSE, -1);
    } else {
        frame->DisableEvent(SIMPLE_EVENT_MOUSE);
    }

    return 0;
}

int32_t CSimpleFrame_IsMouseEnabled(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_eventmask & (1 << SIMPLE_EVENT_MOUSE)) != 0);

    return 1;
}

int32_t CSimpleFrame_EnableMouseWheel(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    if (lua_toboolean(L, 2)) {
        frame->EnableEvent(SIMPLE_EVENT_MOUSEWHEEL, -1);
    } else {
        frame->DisableEvent(SIMPLE_EVENT_MOUSEWHEEL);
    }

    return 0;
}

int32_t CSimpleFrame_IsMouseWheelEnabled(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, (frame->m_eventmask & (1 << SIMPLE_EVENT_MOUSEWHEEL)) != 0);

    return 1;
}

int32_t CSimpleFrame_EnableJoystick(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_IsJoystickEnabled(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    // There is no joystick input path in this client, so it can never be enabled.
    lua_pushboolean(L, 0);

    return 1;
}

int32_t CSimpleFrame_GetBackdrop(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetBackdrop(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetBackdropColor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetBackdropColor(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    CImVector color = { 0x00 };
    FrameScript_GetColor(L, 2, color);

    if (frame->m_backdrop) {
        frame->m_backdrop->SetVertexColor(color);
    }

    return 0;
}

int32_t CSimpleFrame_GetBackdropBorderColor(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_SetBackdropBorderColor(lua_State* L) {
    auto type = CSimpleFrame::GetObjectType();
    auto frame = static_cast<CSimpleFrame*>(FrameScript_GetObjectThis(L, type));

    CImVector color = { 0x00 };
    FrameScript_GetColor(L, 2, color);

    if (frame->m_backdrop) {
        frame->m_backdrop->SetBorderVertexColor(color);
    }

    return 0;
}

int32_t CSimpleFrame_SetDepth(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetDepth(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_GetEffectiveDepth(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleFrame_IgnoreDepth(lua_State* L) {
    // Depth ignore only affects 3D-projected frames, which are not ported
    return 0;
}

int32_t CSimpleFrame_IsIgnoringDepth(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

FrameScript_Method SimpleFrameMethods[NUM_SIMPLE_FRAME_SCRIPT_METHODS] = {
    { "GetTitleRegion",             &CSimpleFrame_GetTitleRegion },
    { "CreateTitleRegion",          &CSimpleFrame_CreateTitleRegion },
    { "CreateTexture",              &CSimpleFrame_CreateTexture },
    { "CreateFontString",           &CSimpleFrame_CreateFontString },
    { "GetBoundsRect",              &CSimpleFrame_GetBoundsRect },
    { "GetNumRegions",              &CSimpleFrame_GetNumRegions },
    { "GetRegions",                 &CSimpleFrame_GetRegions },
    { "GetNumChildren",             &CSimpleFrame_GetNumChildren },
    { "GetChildren",                &CSimpleFrame_GetChildren },
    { "GetFrameStrata",             &CSimpleFrame_GetFrameStrata },
    { "SetFrameStrata",             &CSimpleFrame_SetFrameStrata },
    { "GetFrameLevel",              &CSimpleFrame_GetFrameLevel },
    { "SetFrameLevel",              &CSimpleFrame_SetFrameLevel },
    { "HasScript",                  &CSimpleFrame_HasScript },
    { "GetScript",                  &CSimpleFrame_GetScript },
    { "SetScript",                  &CSimpleFrame_SetScript },
    { "HookScript",                 &CSimpleFrame_HookScript },
    { "RegisterEvent",              &CSimpleFrame_RegisterEvent },
    { "UnregisterEvent",            &CSimpleFrame_UnregisterEvent },
    { "RegisterAllEvents",          &CSimpleFrame_RegisterAllEvents },
    { "UnregisterAllEvents",        &CSimpleFrame_UnregisterAllEvents },
    { "IsEventRegistered",          &CSimpleFrame_IsEventRegistered },
    { "AllowAttributeChanges",      &CSimpleFrame_AllowAttributeChanges },
    { "CanChangeAttribute",         &CSimpleFrame_CanChangeAttributes },
    { "GetAttribute",               &CSimpleFrame_GetAttribute },
    { "SetAttribute",               &CSimpleFrame_SetAttribute },
    { "GetEffectiveScale",          &CSimpleFrame_GetEffectiveScale },
    { "GetScale",                   &CSimpleFrame_GetScale },
    { "SetScale",                   &CSimpleFrame_SetScale },
    { "GetEffectiveAlpha",          &CSimpleFrame_GetEffectiveAlpha },
    { "GetAlpha",                   &CSimpleFrame_GetAlpha },
    { "SetAlpha",                   &CSimpleFrame_SetAlpha },
    { "GetID",                      &CSimpleFrame_GetID },
    { "SetID",                      &CSimpleFrame_SetID },
    { "SetToplevel",                &CSimpleFrame_SetToplevel },
    { "IsToplevel",                 &CSimpleFrame_IsToplevel },
    { "EnableDrawLayer",            &CSimpleFrame_EnableDrawLayer },
    { "DisableDrawLayer",           &CSimpleFrame_DisableDrawLayer },
    { "Show",                       &CSimpleFrame_Show },
    { "Hide",                       &CSimpleFrame_Hide },
    { "IsVisible",                  &CSimpleFrame_IsVisible },
    { "IsShown",                    &CSimpleFrame_IsShown },
    { "Raise",                      &CSimpleFrame_Raise },
    { "Lower",                      &CSimpleFrame_Lower },
    { "GetHitRectInsets",           &CSimpleFrame_GetHitRectInsets },
    { "SetHitRectInsets",           &CSimpleFrame_SetHitRectInsets },
    { "GetClampRectInsets",         &CSimpleFrame_GetClampRectInsets },
    { "SetClampRectInsets",         &CSimpleFrame_SetClampRectInsets },
    { "GetMinResize",               &CSimpleFrame_GetMinResize },
    { "SetMinResize",               &CSimpleFrame_SetMinResize },
    { "GetMaxResize",               &CSimpleFrame_GetMaxResize },
    { "SetMaxResize",               &CSimpleFrame_SetMaxResize },
    { "SetMovable",                 &CSimpleFrame_SetMovable },
    { "IsMovable",                  &CSimpleFrame_IsMovable },
    { "SetDontSavePosition",        &CSimpleFrame_SetDontSavePosition },
    { "GetDontSavePosition",        &CSimpleFrame_GetDontSavePosition },
    { "SetResizable",               &CSimpleFrame_SetResizable },
    { "IsResizable",                &CSimpleFrame_IsResizable },
    { "StartMoving",                &CSimpleFrame_StartMoving },
    { "StartSizing",                &CSimpleFrame_StartSizing },
    { "StopMovingOrSizing",         &CSimpleFrame_StopMovingOrSizing },
    { "SetUserPlaced",              &CSimpleFrame_SetUserPlaced },
    { "IsUserPlaced",               &CSimpleFrame_IsUserPlaced },
    { "SetClampedToScreen",         &CSimpleFrame_SetClampedToScreen },
    { "IsClampedToScreen",          &CSimpleFrame_IsClampedToScreen },
    { "RegisterForDrag",            &CSimpleFrame_RegisterForDrag },
    { "EnableKeyboard",             &CSimpleFrame_EnableKeyboard },
    { "IsKeyboardEnabled",          &CSimpleFrame_IsKeyboardEnabled },
    { "EnableMouse",                &CSimpleFrame_EnableMouse },
    { "IsMouseEnabled",             &CSimpleFrame_IsMouseEnabled },
    { "EnableMouseWheel",           &CSimpleFrame_EnableMouseWheel },
    { "IsMouseWheelEnabled",        &CSimpleFrame_IsMouseWheelEnabled },
    { "EnableJoystick",             &CSimpleFrame_EnableJoystick },
    { "IsJoystickEnabled",          &CSimpleFrame_IsJoystickEnabled },
    { "GetBackdrop",                &CSimpleFrame_GetBackdrop },
    { "SetBackdrop",                &CSimpleFrame_SetBackdrop },
    { "GetBackdropColor",           &CSimpleFrame_GetBackdropColor },
    { "SetBackdropColor",           &CSimpleFrame_SetBackdropColor },
    { "GetBackdropBorderColor",     &CSimpleFrame_GetBackdropBorderColor },
    { "SetBackdropBorderColor",     &CSimpleFrame_SetBackdropBorderColor },
    { "SetDepth",                   &CSimpleFrame_SetDepth },
    { "GetDepth",                   &CSimpleFrame_GetDepth },
    { "GetEffectiveDepth",          &CSimpleFrame_GetEffectiveDepth },
    { "IgnoreDepth",                &CSimpleFrame_IgnoreDepth },
    { "IsIgnoringDepth",            &CSimpleFrame_IsIgnoringDepth }
};
