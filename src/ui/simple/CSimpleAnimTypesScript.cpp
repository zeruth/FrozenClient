#include "ui/simple/CSimpleAnimTypesScript.hpp"
#include "ui/simple/CSimpleAnimTypes.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/CScriptObject.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScript_Object.hpp"
#include "ui/Util.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <storm/Memory.hpp>
#include <cmath>
#include <cstdint>

// Every message below is the reference's own, read out of the binary at 009ed8ab-009edb78, and
// each embeds the object name with the "<unnamed>" fallback the reference uses.
static const char* AnimName(CScriptObject* object) {
    const char* name = object->GetName();

    return name ? name : "<unnamed>";
}

template <class T>
static T* AnimThisOf(lua_State* L) {
    return static_cast<T*>(FrameScript_GetObjectThis(L, T::GetObjectType()));
}

// ---------------------------------------------------------------------------- Translation

int32_t CSimpleTranslationAnim_SetOffset(lua_State* L) {
    auto anim = AnimThisOf<CSimpleTranslationAnim>(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetOffset(x, y)", AnimName(anim));

        return 0;
    }

    anim->m_offsetX = static_cast<float>(lua_tonumber(L, 2));
    anim->m_offsetY = static_cast<float>(lua_tonumber(L, 3));

    return 0;
}

int32_t CSimpleTranslationAnim_GetOffset(lua_State* L) {
    auto anim = AnimThisOf<CSimpleTranslationAnim>(L);

    lua_pushnumber(L, anim->m_offsetX);
    lua_pushnumber(L, anim->m_offsetY);

    return 2;
}

// ---------------------------------------------------------------------------- Rotation

// ref: FUN_004a61f0
// All three arguments are required: a point NAME, then the two offsets. The reference checks the
// string and both numbers before touching anything, and raises the one usage message if any fails.
int32_t CSimpleRotationAnim_SetOrigin(lua_State* L) {
    auto anim = AnimThisOf<CSimpleRotationAnim>(L);

    FRAMEPOINT point;

    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)
        || !StringToFramePoint(lua_tostring(L, 2), point)) {
        luaL_error(L, "Usage: %s:SetOrigin(point, offsetX, offsetY)", AnimName(anim));

        return 0;
    }

    anim->m_originPoint = point;
    anim->m_originX = static_cast<float>(lua_tonumber(L, 3));
    anim->m_originY = static_cast<float>(lua_tonumber(L, 4));

    return 0;
}

int32_t CSimpleRotationAnim_GetOrigin(lua_State* L) {
    auto anim = AnimThisOf<CSimpleRotationAnim>(L);

    lua_pushstring(L, FramePointToString(anim->m_originPoint));
    lua_pushnumber(L, anim->m_originX);
    lua_pushnumber(L, anim->m_originY);

    return 3;
}

int32_t CSimpleRotationAnim_SetDegrees(lua_State* L) {
    auto anim = AnimThisOf<CSimpleRotationAnim>(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetDegrees(degrees)", AnimName(anim));

        return 0;
    }

    anim->m_radians = static_cast<float>(lua_tonumber(L, 2)) * 0.017453292519943295f;

    return 0;
}

int32_t CSimpleRotationAnim_GetDegrees(lua_State* L) {
    lua_pushnumber(L, AnimThisOf<CSimpleRotationAnim>(L)->m_radians * 57.29577951308232);

    return 1;
}

int32_t CSimpleRotationAnim_SetRadians(lua_State* L) {
    auto anim = AnimThisOf<CSimpleRotationAnim>(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetRadians(radians)", AnimName(anim));

        return 0;
    }

    anim->m_radians = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleRotationAnim_GetRadians(lua_State* L) {
    lua_pushnumber(L, AnimThisOf<CSimpleRotationAnim>(L)->m_radians);

    return 1;
}

// ---------------------------------------------------------------------------- Scale

int32_t CSimpleScaleAnim_SetOrigin(lua_State* L) {
    auto anim = AnimThisOf<CSimpleScaleAnim>(L);

    FRAMEPOINT point;

    if (!lua_isstring(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)
        || !StringToFramePoint(lua_tostring(L, 2), point)) {
        luaL_error(L, "Usage: %s:SetOrigin(point, offsetX, offsetY)", AnimName(anim));

        return 0;
    }

    anim->m_originPoint = point;
    anim->m_originX = static_cast<float>(lua_tonumber(L, 3));
    anim->m_originY = static_cast<float>(lua_tonumber(L, 4));

    return 0;
}

int32_t CSimpleScaleAnim_GetOrigin(lua_State* L) {
    auto anim = AnimThisOf<CSimpleScaleAnim>(L);

    lua_pushstring(L, FramePointToString(anim->m_originPoint));
    lua_pushnumber(L, anim->m_originX);
    lua_pushnumber(L, anim->m_originY);

    return 3;
}

// Two arguments, not one: Scale's usage string is "SetScale(x, y)" where a frame's is
// "SetScale(scale)". They are different strings in the binary, at 009ed93b and 009ece4f.
int32_t CSimpleScaleAnim_SetScale(lua_State* L) {
    auto anim = AnimThisOf<CSimpleScaleAnim>(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetScale(x, y)", AnimName(anim));

        return 0;
    }

    anim->m_scaleX = static_cast<float>(lua_tonumber(L, 2));
    anim->m_scaleY = static_cast<float>(lua_tonumber(L, 3));

    return 0;
}

int32_t CSimpleScaleAnim_GetScale(lua_State* L) {
    float x;
    float y;

    AnimThisOf<CSimpleScaleAnim>(L)->GetScale(x, y);

    lua_pushnumber(L, x);
    lua_pushnumber(L, y);

    return 2;
}

// ---------------------------------------------------------------------------- Alpha

int32_t CSimpleAlphaAnim_SetChange(lua_State* L) {
    auto anim = AnimThisOf<CSimpleAlphaAnim>(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetChange(change)", AnimName(anim));

        return 0;
    }

    // Clamping and scaling both live in SetChange; the XML loader takes its own path because it
    // reports an out-of-range value rather than clamping it.
    anim->SetChange(static_cast<float>(lua_tonumber(L, 2)));

    return 0;
}

int32_t CSimpleAlphaAnim_GetChange(lua_State* L) {
    lua_pushnumber(L, AnimThisOf<CSimpleAlphaAnim>(L)->m_change * (1.0f / 255.0f));

    return 1;
}

// ---------------------------------------------------------------------------- ControlPoint

// ref: FUN_004a6790
// Same three-way shape as Animation:SetParent, with Path in place of AnimationGroup: nil first,
// then a name, then an object.
int32_t CSimpleControlPoint_SetParent(lua_State* L) {
    auto point = AnimThisOf<CSimpleControlPoint>(L);

    if (lua_type(L, 2) == LUA_TNIL) {
        luaL_error(L, "%s:SetParent(): Cannot set a 'nil' parent for control points",
                   AnimName(point));

        return 0;
    }

    if (lua_isstring(L, 2)) {
        const char* name = lua_tostring(L, 2);
        auto found = CScriptObject::GetScriptObjectByName(name, CSimplePathAnim::GetObjectType());

        if (!found) {
            luaL_error(L, "%s:SetParent(): Couldn't find Path named '%s'", AnimName(point), name);

            return 0;
        }

        point->SetParentPath(static_cast<CSimplePathAnim*>(found));

        return 0;
    }

    if (lua_type(L, 2) != LUA_TTABLE) {
        luaL_error(L, "%s:SetParent(): Wrong parent object type, expected Path", AnimName(point));

        return 0;
    }

    lua_rawgeti(L, 2, 0);
    auto object = static_cast<FrameScript_Object*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!object) {
        luaL_error(L, "%s:SetParent(): Couldn't find 'this' in parent object", AnimName(point));

        return 0;
    }

    if (!object->IsA(CSimplePathAnim::GetObjectType())) {
        luaL_error(L, "%s:SetParent(): Wrong parent object type, expected Path", AnimName(point));

        return 0;
    }

    point->SetParentPath(static_cast<CSimplePathAnim*>(object));

    return 0;
}

int32_t CSimpleControlPoint_SetOffset(lua_State* L) {
    auto point = AnimThisOf<CSimpleControlPoint>(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetOffset(x, y)", AnimName(point));

        return 0;
    }

    point->m_offsetX = static_cast<float>(lua_tonumber(L, 2));
    point->m_offsetY = static_cast<float>(lua_tonumber(L, 3));

    return 0;
}

int32_t CSimpleControlPoint_GetOffset(lua_State* L) {
    auto point = AnimThisOf<CSimpleControlPoint>(L);

    lua_pushnumber(L, point->m_offsetX);
    lua_pushnumber(L, point->m_offsetY);

    return 2;
}

int32_t CSimpleControlPoint_SetOrder(lua_State* L) {
    auto point = AnimThisOf<CSimpleControlPoint>(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetOrder(order)", AnimName(point));

        return 0;
    }

    point->m_order = static_cast<int8_t>(lua_tointeger(L, 2) - 1);

    return 0;
}

int32_t CSimpleControlPoint_GetOrder(lua_State* L) {
    lua_pushnumber(L, AnimThisOf<CSimpleControlPoint>(L)->m_order + 1);

    return 1;
}

// ---------------------------------------------------------------------------- Path

// ref: FUN_004a6c10
// An unrecognised curve name is an error, as with SetSmoothing and SetLooping.
int32_t CSimplePathAnim_SetCurve(lua_State* L) {
    auto path = AnimThisOf<CSimplePathAnim>(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetCurve(\"curveType\")", AnimName(path));

        return 0;
    }

    ANIM_CURVE curve;

    if (!AnimCurveFromName(lua_tostring(L, 2), curve)) {
        luaL_error(L, "%s:SetCurve(): Unknown curve type specified", AnimName(path));

        return 0;
    }

    path->m_curve = curve;

    return 0;
}

// ref: FUN_004a6cd0
int32_t CSimplePathAnim_GetCurve(lua_State* L) {
    lua_pushstring(L, AnimCurveName(AnimThisOf<CSimplePathAnim>(L)->m_curve));

    return 1;
}

int32_t CSimplePathAnim_GetMaxOrder(lua_State* L) {
    lua_pushnumber(L, AnimThisOf<CSimplePathAnim>(L)->GetMaxOrder());

    return 1;
}

int32_t CSimplePathAnim_GetControlPoints(lua_State* L) {
    auto path = AnimThisOf<CSimplePathAnim>(L);
    uint32_t count = path->m_controlPoints.Count();

    if (!lua_checkstack(L, static_cast<int32_t>(count))) {
        luaL_error(L, "%s:GetControlPoints(): Stack overflow", AnimName(path));

        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        CSimpleControlPoint* point = path->m_controlPoints[i];

        point->GetScriptMetaTable();
        lua_rawgeti(L, LUA_REGISTRYINDEX, point->lua_objectRef);
    }

    return static_cast<int32_t>(count);
}

// ref: FUN_004a7bf0
// TODO the inheritsFrom argument, as with CreateAnimation and CreateAnimationGroup: it needs the
// XML template table that stage 3 brings. The reference raises "Couldn't find inherited node" or
// "Recursively inherited node" there.
int32_t CSimplePathAnim_CreateControlPoint(lua_State* L) {
    auto path = AnimThisOf<CSimplePathAnim>(L);

    const char* name = lua_isstring(L, 2) ? lua_tostring(L, 2) : nullptr;

    CSimpleControlPoint* point = path->CreateControlPoint(name);

    if (!point) {
        lua_pushnil(L);

        return 1;
    }

    point->GetScriptMetaTable();
    lua_rawgeti(L, LUA_REGISTRYINDEX, point->lua_objectRef);

    return 1;
}

// ---------------------------------------------------------------------------- tables

FrameScript_Method SimpleTranslationAnimMethods[NUM_SIMPLE_TRANSLATION_ANIM_SCRIPT_METHODS] = {
    { "SetOffset",              &CSimpleTranslationAnim_SetOffset },
    { "GetOffset",              &CSimpleTranslationAnim_GetOffset }
};

FrameScript_Method SimpleRotationAnimMethods[NUM_SIMPLE_ROTATION_ANIM_SCRIPT_METHODS] = {
    { "SetOrigin",              &CSimpleRotationAnim_SetOrigin },
    { "GetOrigin",              &CSimpleRotationAnim_GetOrigin },
    { "SetDegrees",             &CSimpleRotationAnim_SetDegrees },
    { "GetDegrees",             &CSimpleRotationAnim_GetDegrees },
    { "SetRadians",             &CSimpleRotationAnim_SetRadians },
    { "GetRadians",             &CSimpleRotationAnim_GetRadians }
};

FrameScript_Method SimpleScaleAnimMethods[NUM_SIMPLE_SCALE_ANIM_SCRIPT_METHODS] = {
    { "SetOrigin",              &CSimpleScaleAnim_SetOrigin },
    { "GetOrigin",              &CSimpleScaleAnim_GetOrigin },
    { "SetScale",               &CSimpleScaleAnim_SetScale },
    { "GetScale",               &CSimpleScaleAnim_GetScale }
};

FrameScript_Method SimpleControlPointMethods[NUM_SIMPLE_CONTROL_POINT_SCRIPT_METHODS] = {
    { "SetParent",              &CSimpleControlPoint_SetParent },
    { "SetOffset",              &CSimpleControlPoint_SetOffset },
    { "GetOffset",              &CSimpleControlPoint_GetOffset },
    { "SetOrder",               &CSimpleControlPoint_SetOrder },
    { "GetOrder",               &CSimpleControlPoint_GetOrder }
};

FrameScript_Method SimplePathAnimMethods[NUM_SIMPLE_PATH_ANIM_SCRIPT_METHODS] = {
    { "SetCurve",               &CSimplePathAnim_SetCurve },
    { "GetCurve",               &CSimplePathAnim_GetCurve },
    { "GetControlPoints",       &CSimplePathAnim_GetControlPoints },
    { "CreateControlPoint",     &CSimplePathAnim_CreateControlPoint },
    { "GetMaxOrder",            &CSimplePathAnim_GetMaxOrder }
};

FrameScript_Method SimpleAlphaAnimMethods[NUM_SIMPLE_ALPHA_ANIM_SCRIPT_METHODS] = {
    { "SetChange",              &CSimpleAlphaAnim_SetChange },
    { "GetChange",              &CSimpleAlphaAnim_GetChange }
};
