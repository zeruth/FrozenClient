#include "ui/CScriptRegionScript.hpp"
#include "gx/Coordinate.hpp"
#include "ui/CFramePoint.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/FrameScript_Object.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>
#include <tempest/Rect.hpp>

int32_t CScriptRegion_IsProtected(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CScriptRegion_CanChangeProtectedState(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CScriptRegion_SetParent(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO

        return 0;
    }

    if (lua_type(L, 2) == LUA_TNIL) {
        if (region->IsA(CSimpleFontString::GetObjectType()) || region->IsA(CSimpleTexture::GetObjectType())) {
            return luaL_error(L, "%s:SetParent(): Cannot set a 'nil' parent for fonts or textures", region->GetDisplayName());
        }

        region->SetParent(nullptr);

        return 0;
    }

    CScriptObject* parent = nullptr;

    if (lua_isstring(L, 2)) {
        parent = CScriptObject::GetScriptObjectByName(lua_tostring(L, 2), CSimpleFrame::GetObjectType());
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);
        parent = static_cast<CScriptObject*>(lua_touserdata(L, -1));
        lua_settop(L, -2);

        if (!parent) {
            return luaL_error(L, "%s:SetParent(): Couldn't find 'this' in parent object", region->GetDisplayName());
        }

        if (!parent->IsA(CSimpleFrame::GetObjectType())) {
            return luaL_error(L, "%s:SetParent(): Wrong parent object type, expected Frame", region->GetDisplayName());
        }
    }

    if (!parent) {
        return luaL_error(L, "%s:SetParent(): Couldn't find region named '%s'", region->GetDisplayName(), lua_tostring(L, 2));
    }

    for (auto p = parent; p; p = p->GetScriptObjectParent()) {
        if (p == region) {
            return luaL_error(L, "%s:SetParent(): Would create a loop parenting to %s", region->GetDisplayName(), p->GetName());
        }
    }

    region->SetParent(static_cast<CSimpleFrame*>(parent));

    return 0;
}

// ref: FUN_0049ce50
// Left, bottom, width and height, in the same units GetCenter and GetLeft already report.
//
// The reference resolves the rect and then makes four identical conversions, and the decompiler
// lost their arguments, so the ORDER is not recoverable from the disassembly. It comes from the
// shipped interface instead: RestrictedFrames.lua and SecureHoverDriver.lua both spell it
// "local l, b, w, h = frame:GetRect()", at four call sites between them.
//
// A region with no rect answers with no values at all, which is what the reference returns; that
// differs from GetCenter next door, which pushes two nils to keep its arity.
int32_t CScriptRegion_GetRect(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (!region->GetRect(&rect)) {
        return 0;
    }

    auto scale = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(scale * (rect.minX / region->m_layoutScale)));
    lua_pushnumber(L, DDCToNDCWidth(scale * (rect.minY / region->m_layoutScale)));
    lua_pushnumber(L, DDCToNDCWidth(scale * ((rect.maxX - rect.minX) / region->m_layoutScale)));
    lua_pushnumber(L, DDCToNDCWidth(scale * ((rect.maxY - rect.minY) / region->m_layoutScale)));

    return 4;
}

int32_t CScriptRegion_GetCenter(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (region->GetRect(&rect)) {
        float width = rect.maxX - rect.minX;
        float v5 = CoordinateGetAspectCompensation() * 1024.0f * ((width * 0.5f + rect.minX) / region->m_layoutScale);
        float v6 = DDCToNDCWidth(v5);
        lua_pushnumber(L, v6);

        float height = rect.maxY - rect.minY;
        float v7 = CoordinateGetAspectCompensation() * 1024.0f * ((height * 0.5f + rect.minY) / region->m_layoutScale);
        float v8 = DDCToNDCWidth(v7);
        lua_pushnumber(L, v8);
    } else {
        lua_pushnil(L);
        lua_pushnil(L);
    }

    return 2;
}

int32_t CScriptRegion_GetLeft(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (region->GetRect(&rect)) {
        float ddc = CoordinateGetAspectCompensation() * 1024.0f * (rect.minX / region->m_layoutScale);
        lua_pushnumber(L, DDCToNDCWidth(ddc));
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CScriptRegion_GetRight(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (region->GetRect(&rect)) {
        float ddc = CoordinateGetAspectCompensation() * 1024.0f * (rect.maxX / region->m_layoutScale);
        lua_pushnumber(L, DDCToNDCWidth(ddc));
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CScriptRegion_GetTop(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (region->GetRect(&rect)) {
        float ddc = CoordinateGetAspectCompensation() * 1024.0f * (rect.maxY / region->m_layoutScale);
        lua_pushnumber(L, DDCToNDCWidth(ddc));
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CScriptRegion_GetBottom(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->IsResizePending()) {
        region->Resize(1);
    }

    CRect rect;

    if (region->GetRect(&rect)) {
        float ddc = CoordinateGetAspectCompensation() * 1024.0f * (rect.minY / region->m_layoutScale);
        lua_pushnumber(L, DDCToNDCWidth(ddc));
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CScriptRegion_GetWidth(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    float width = region->GetWidth();

    if (width == 0.0 && !StringToBOOL(L, 2, 0)) {
        if (region->IsResizePending()) {
            region->Resize(1);
        }

        CRect rect = { 0.0, 0.0, 0.0, 0.0 };

        if (region->GetRect(&rect)) {
            width = (rect.maxX - rect.minX) / region->m_layoutScale;
        }
    }

    float ddcWidth = CoordinateGetAspectCompensation() * 1024.0 * width;
    float ndcWidth = DDCToNDCWidth(ddcWidth);
    lua_pushnumber(L, ndcWidth);

    return 1;
}

int32_t CScriptRegion_SetWidth(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetWidth(width)", region->GetDisplayName());
    }

    float width = lua_tonumber(L, 2);
    float ndcWidth = width / (CoordinateGetAspectCompensation() * 1024.0f);
    float ddcWidth = NDCToDDCWidth(ndcWidth);

    region->SetWidth(ddcWidth);

    return 0;
}

int32_t CScriptRegion_GetHeight(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    float height = region->GetHeight();

    if (height == 0.0f && !StringToBOOL(L, 2, 0)) {
        if (region->IsResizePending()) {
            region->Resize(1);
        }

        CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };

        if (region->GetRect(&rect)) {
            height = (rect.maxY - rect.minY) / region->m_layoutScale;
        }
    }

    float ddcHeight = CoordinateGetAspectCompensation() * 1024.0f * height;
    float ndcHeight = DDCToNDCWidth(ddcHeight);
    lua_pushnumber(L, ndcHeight);

    return 1;
}

int32_t CScriptRegion_SetHeight(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetHeight(height)", region->GetDisplayName());
    }

    float height = lua_tonumber(L, 2);
    float ndcHeight = height / (CoordinateGetAspectCompensation() * 1024.0f);
    float ddcHeight = NDCToDDCWidth(ndcHeight);

    region->SetHeight(ddcHeight);

    return 0;
}

int32_t CScriptRegion_SetSize(lua_State* L) {
    auto type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO disallowed logic
        return 0;
    }

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetSize(width, height)", region->GetDisplayName());
        return 0;
    }

    auto ndcWidth = static_cast<float>(lua_tonumber(L, 2)) / (CoordinateGetAspectCompensation() * 1024.0f);
    auto ddcWidth = NDCToDDCWidth(ndcWidth);

    auto ndcHeight = static_cast<float>(lua_tonumber(L, 3)) / (CoordinateGetAspectCompensation() * 1024.0f);
    auto ddcHeight = NDCToDDCWidth(ndcHeight);

    region->SetSize(ddcWidth, ddcHeight);

    return 0;
}

// Width and height together, in the same units GetWidth and GetHeight report them. FrameXML calls
// this far more often than the singular pair, and a stub returned nothing at all -- so every caller
// unpacked two nils and did arithmetic on them.
int32_t CScriptRegion_GetSize(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    float width = region->GetWidth();
    float height = region->GetHeight();

    // A region that has never been laid out reports zero; resolve it the way GetWidth does rather
    // than handing back a zero that is merely "not measured yet".
    if ((width == 0.0f || height == 0.0f) && !StringToBOOL(L, 2, 0)) {
        if (region->IsResizePending()) {
            region->Resize(1);
        }

        CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };

        if (region->GetRect(&rect)) {
            if (width == 0.0f) {
                width = (rect.maxX - rect.minX) / region->m_layoutScale;
            }

            if (height == 0.0f) {
                height = (rect.maxY - rect.minY) / region->m_layoutScale;
            }
        }
    }

    float aspect = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(aspect * width));
    lua_pushnumber(L, DDCToNDCWidth(aspect * height));

    return 2;
}

int32_t CScriptRegion_GetNumPoints(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    int32_t count = 0;

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        if (region->m_points[i]) {
            count++;
        }
    }

    lua_pushnumber(L, count);

    return 1;
}

// GetPoint(index) -> point, relativeTo, relativePoint, offsetX, offsetY.
//
// The index is 1-based and counts only the points that are actually set, so it is a position in the
// occupied subset rather than a FRAMEPOINT value -- m_points is a sparse array indexed by the point
// itself.
int32_t CScriptRegion_GetPoint(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    int32_t wanted = lua_isnumber(L, 2) ? static_cast<int32_t>(lua_tonumber(L, 2)) : 1;

    if (wanted < 1) {
        return luaL_error(L, "%s:GetPoint(): Invalid point index", region->GetDisplayName());
    }

    CFramePoint* found = nullptr;
    FRAMEPOINT foundPoint = FRAMEPOINT_TOPLEFT;
    int32_t seen = 0;

    for (int32_t i = 0; i < FRAMEPOINT_NUMPOINTS; i++) {
        if (!region->m_points[i]) {
            continue;
        }

        if (++seen == wanted) {
            found = region->m_points[i];
            foundPoint = static_cast<FRAMEPOINT>(i);
            break;
        }
    }

    if (!found) {
        return 0;
    }

    lua_pushstring(L, FramePointToString(foundPoint));

    auto relative = found->GetRelative();

    // CSimpleTop is the other CLayoutFrame subclass and is NOT a CScriptRegion, so casting one to
    // CScriptRegion* would adjust the pointer past a base that isn't there and read a garbage
    // lua_objectRef. It is the internal root above UIParent and is not exposed to script at all, so
    // nil is the honest answer for it.
    if (relative && relative != static_cast<CLayoutFrame*>(CSimpleTop::s_instance)) {
        auto region = static_cast<CScriptRegion*>(relative);

        if (!region->lua_registered) {
            region->RegisterScriptObject(0);
        }

        lua_rawgeti(L, LUA_REGISTRYINDEX, region->lua_objectRef);
    } else {
        lua_pushnil(L);
    }

    lua_pushstring(L, FramePointToString(static_cast<FRAMEPOINT>(found->m_framePoint)));

    // SetPoint converts the script's offsets into DDC on the way in; undo exactly that, so a value
    // handed to SetPoint comes back out of GetPoint unchanged.
    float aspect = CoordinateGetAspectCompensation() * 1024.0f;

    lua_pushnumber(L, DDCToNDCWidth(found->m_offset.x) * aspect);
    lua_pushnumber(L, DDCToNDCWidth(found->m_offset.y) * aspect);

    return 5;
}

int32_t CScriptRegion_SetPoint(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetPoint(\"point\" [, region or nil] [, \"relativePoint\"] [, offsetX, offsetY])", region->GetDisplayName());
    }

    auto relative = region->GetLayoutParent();

    const char* pointStr = lua_tostring(L, 2);
    FRAMEPOINT point;

    if (!StringToFramePoint(pointStr, point)) {
        return luaL_error(L, "%s:SetPoint(): Unknown region point", region->GetDisplayName());
    }

    // FROZEN_TRACE_LUA=1 names the Lua caller of every SetPoint. The reference anchors ~1 region a
    // frame; frozen was seen doing 161 (tools/recomp call trace), which means some OnUpdate script
    // re-lays out every frame, and the caller's file:line is the fastest way to find which.
    static int32_t s_traceLua = -1;

    if (s_traceLua < 0) {
        s_traceLua = getenv("FROZEN_TRACE_LUA") ? 1 : 0;
    }

    if (s_traceLua) {
        lua_Debug ar;

        if (lua_getstack(L, 1, &ar) && lua_getinfo(L, "Sl", &ar)) {
            fprintf(stderr, "SetPoint %s <- %s:%d\n", region->GetDisplayName(), ar.short_src, ar.currentline);
        }
    }

    int32_t argsIndex = 3;

    if (lua_type(L, 3) == LUA_TSTRING) {
        const char* name = lua_tostring(L, 3);
        relative = region->GetLayoutFrameByName(name);

        argsIndex++;
    } else if (lua_type(L, 3) == LUA_TTABLE) {
        lua_rawgeti(L, 3, 0);

        auto r = reinterpret_cast<CScriptRegion*>(lua_touserdata(L, -1));
        relative = r ? static_cast<CLayoutFrame*>(r) : nullptr;

        lua_settop(L, -2);

        argsIndex++;
    } else if (lua_type(L, 3) == LUA_TNIL) {
        relative = CSimpleTop::s_instance;

        argsIndex++;
    }

    if (!relative) {
        const char* name = lua_tostring(L, 3);
        return luaL_error(L, "%s:SetPoint(): Couldn't find region named '%s'", region->GetDisplayName(), name);
    }

    if (relative == region) {
        return luaL_error(L, "%s:SetPoint(): trying to anchor to itself", region->GetDisplayName());
    }

    if (relative->IsResizeDependency(region)) {
        // Same hazard as in GetPoint: only a CScriptRegion has a display name, and CSimpleTop is not
        // one. This is an error path, so it would have crashed exactly when something already had.
        const char* relativeName = relative == static_cast<CLayoutFrame*>(CSimpleTop::s_instance)
            ? "UIParent"
            : static_cast<CScriptRegion*>(relative)->GetDisplayName();

        return luaL_error(L, "%s:SetPoint(): %s is dependent on this", region->GetDisplayName(), relativeName);
    }

    FRAMEPOINT relativePoint = point;

    if (lua_type(L, argsIndex) == LUA_TSTRING) {
        const char* relativePointStr = lua_tostring(L, argsIndex);

        if (!StringToFramePoint(relativePointStr, relativePoint)) {
            return luaL_error(L, "%s:SetPoint(): Unknown region point", region->GetDisplayName());
        }

        argsIndex++;
    }

    float offsetX = 0.0f;
    float offsetY = 0.0f;

    if (lua_isnumber(L, argsIndex) && lua_isnumber(L, argsIndex + 1)) {
        float x = lua_tonumber(L, argsIndex);
        float ndcX = x / (CoordinateGetAspectCompensation() * 1024.0f);
        float ddcX = NDCToDDCWidth(ndcX);

        float y = lua_tonumber(L, argsIndex + 1);
        float ndcY = y / (CoordinateGetAspectCompensation() * 1024.0f);
        float ddcY = NDCToDDCWidth(ndcY);

        offsetX = ddcX;
        offsetY = ddcY;
    }

    region->SetPoint(point, relative, relativePoint, offsetX, offsetY, 1);

    return 0;
}

int32_t CScriptRegion_SetAllPoints(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (!region->ProtectedFunctionsAllowed()) {
        // TODO
        // - disallowed logic

        return 0;
    }

    auto relative = region->GetLayoutParent();

    if (lua_isstring(L, 2)) {
        const char* name = lua_tostring(L, 2);
        relative = region->GetLayoutFrameByName(name);
    } else if (lua_type(L, 2) == LUA_TTABLE) {
        lua_rawgeti(L, 2, 0);

        auto r = reinterpret_cast<CScriptRegion*>(lua_touserdata(L, -1));
        relative = r ? static_cast<CLayoutFrame*>(r) : nullptr;

        lua_settop(L, -2);
    } else if (lua_type(L, 2) == LUA_TNIL) {
        relative = CSimpleTop::s_instance;
    }

    if (!relative) {
        const char* name = lua_tostring(L, 2);
        return luaL_error(L, "%s:SetAllPoints(): Couldn't find region named '%s'", region->GetDisplayName(), name);
    }

    if (relative == region) {
        return luaL_error(L, "%s:SetAllPoints(): trying to anchor to itself", region->GetDisplayName());
    }

    if (relative->IsResizeDependency(region)) {
        return luaL_error(L, "%s:SetAllPoints(): %s is dependent on this", region->GetDisplayName(), static_cast<CScriptRegion*>(relative)->GetDisplayName());
    }

    region->SetAllPoints(relative, 1);

    return 0;
}

int32_t CScriptRegion_ClearAllPoints(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    if (region->ProtectedFunctionsAllowed()) {
        region->ClearAllPoints();
    } else {
        // TODO
        // void* v3 = CSimpleTop::s_instance->Function4692;

        // if (v3) {
        //     v3(object);
        // }
    }

    return 0;
}

int32_t CScriptRegion_CreateAnimationGroup(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CScriptRegion_GetAnimationGroups(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CScriptRegion_StopAnimating(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0049e0b0
int32_t CScriptRegion_IsDragging(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    lua_pushboolean(L, region->IsDragging());

    return 1;
}

// IsMouseOver([top, bottom, left, right]) -> is the cursor inside this region.
//
// The optional arguments are edge offsets in the units SetPoint takes, added to their own edge, so
// FrameXML's IsMouseOver(1, -1, -1, 1) grows the rect by one unit all round. Callers use this only
// in conditions, so while it was a stub it read as false and those branches -- buff consolidation,
// chat fade -- simply never fired.
//
// The mouse position and a region's rect are in the same space: CSimpleTop stores the mouse event
// straight into m_mousePosition, and CSimpleFrame::TestHitRect compares that event's x/y against
// m_hitRect without converting. So the only conversion needed is on the offsets coming from script.
int32_t CScriptRegion_IsMouseOver(lua_State* L) {
    int32_t type = CScriptRegion::GetObjectType();
    auto region = static_cast<CScriptRegion*>(FrameScript_GetObjectThis(L, type));

    CRect rect = { 0.0f, 0.0f, 0.0f, 0.0f };

    if (!CSimpleTop::s_instance || !region->GetRect(&rect)) {
        lua_pushboolean(L, 0);

        return 1;
    }

    float aspect = CoordinateGetAspectCompensation() * 1024.0f;

    // Same conversion SetPoint applies to its offsets, so an inset means the same thing here as the
    // offset that placed the region.
    auto offset = [&](int32_t index) {
        return lua_isnumber(L, index)
            ? NDCToDDCWidth(static_cast<float>(lua_tonumber(L, index)) / aspect)
            : 0.0f;
    };

    rect.maxY += offset(2);
    rect.minY += offset(3);
    rect.minX += offset(4);
    rect.maxX += offset(5);

    C2Vector point = { CSimpleTop::s_instance->m_mousePosition.x,
                       CSimpleTop::s_instance->m_mousePosition.y };

    lua_pushboolean(L, rect.IsPointInside(point));

    return 1;
}

FrameScript_Method ScriptRegionMethods[NUM_SCRIPT_REGION_SCRIPT_METHODS] = {
    { "IsProtected",                &CScriptRegion_IsProtected },
    { "CanChangeProtectedState",    &CScriptRegion_CanChangeProtectedState },
    { "SetParent",                  &CScriptRegion_SetParent },
    { "GetRect",                    &CScriptRegion_GetRect },
    { "GetCenter",                  &CScriptRegion_GetCenter },
    { "GetLeft",                    &CScriptRegion_GetLeft },
    { "GetRight",                   &CScriptRegion_GetRight },
    { "GetTop",                     &CScriptRegion_GetTop },
    { "GetBottom",                  &CScriptRegion_GetBottom },
    { "GetWidth",                   &CScriptRegion_GetWidth },
    { "SetWidth",                   &CScriptRegion_SetWidth },
    { "GetHeight",                  &CScriptRegion_GetHeight },
    { "SetHeight",                  &CScriptRegion_SetHeight },
    { "SetSize",                    &CScriptRegion_SetSize },
    { "GetSize",                    &CScriptRegion_GetSize },
    { "GetNumPoints",               &CScriptRegion_GetNumPoints },
    { "GetPoint",                   &CScriptRegion_GetPoint },
    { "SetPoint",                   &CScriptRegion_SetPoint },
    { "SetAllPoints",               &CScriptRegion_SetAllPoints },
    { "ClearAllPoints",             &CScriptRegion_ClearAllPoints },
    { "CreateAnimationGroup",       &CScriptRegion_CreateAnimationGroup },
    { "GetAnimationGroups",         &CScriptRegion_GetAnimationGroups },
    { "StopAnimating",              &CScriptRegion_StopAnimating },
    { "IsDragging",                 &CScriptRegion_IsDragging },
    { "IsMouseOver",                &CScriptRegion_IsMouseOver }
};
