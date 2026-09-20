#include "ui/simple/CSimpleAnimScript.hpp"
#include "ui/simple/CSimpleAnim.hpp"
#include "ui/simple/CSimpleAnimGroup.hpp"
#include "ui/CScriptRegion.hpp"
#include "ui/FrameScript.hpp"
#include "ui/FrameScript_Object.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <storm/String.hpp>
#include <cstdint>

// Every usage and error message below is the reference's own text, read out of the binary at
// 009ecce8-009edc30. They all embed the object's name, falling back to "<unnamed>" when it has
// none -- that fallback is the reference's, not a convenience here, and FrameXML error output
// depends on it.
static const char* AnimName(CScriptObject* object) {
    const char* name = object->GetName();

    return name ? name : "<unnamed>";
}

static CSimpleAnim* AnimThis(lua_State* L) {
    return static_cast<CSimpleAnim*>(FrameScript_GetObjectThis(L, CSimpleAnim::GetObjectType()));
}

static CSimpleAnimGroup* GroupThis(lua_State* L) {
    return static_cast<CSimpleAnimGroup*>(
        FrameScript_GetObjectThis(L, CSimpleAnimGroup::GetObjectType())
    );
}

// ---------------------------------------------------------------------------- Animation

// ref: FUN_004a4f40
int32_t CSimpleAnim_Play(lua_State* L) {
    AnimThis(L)->Play();

    return 0;
}

int32_t CSimpleAnim_Pause(lua_State* L) {
    AnimThis(L)->Pause();

    return 0;
}

int32_t CSimpleAnim_Stop(lua_State* L) {
    AnimThis(L)->Stop();

    return 0;
}

// ref: FUN_004a5000
int32_t CSimpleAnim_IsDone(lua_State* L) {
    lua_pushboolean(L, AnimThis(L)->IsDone());

    return 1;
}

int32_t CSimpleAnim_IsPlaying(lua_State* L) {
    auto anim = AnimThis(L);

    lua_pushboolean(L, anim->m_playing && !anim->m_paused);

    return 1;
}

int32_t CSimpleAnim_IsPaused(lua_State* L) {
    lua_pushboolean(L, AnimThis(L)->m_paused);

    return 1;
}

int32_t CSimpleAnim_IsStopped(lua_State* L) {
    lua_pushboolean(L, AnimThis(L)->IsStopped());

    return 1;
}

int32_t CSimpleAnim_IsDelaying(lua_State* L) {
    lua_pushboolean(L, AnimThis(L)->IsDelaying());

    return 1;
}

int32_t CSimpleAnim_GetElapsed(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_elapsed);

    return 1;
}

int32_t CSimpleAnim_SetStartDelay(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetStartDelay(delaySec)", AnimName(anim));

        return 0;
    }

    anim->m_startDelay = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleAnim_GetStartDelay(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_startDelay);

    return 1;
}

int32_t CSimpleAnim_SetEndDelay(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetEndDelay(delaySec)", AnimName(anim));

        return 0;
    }

    anim->m_endDelay = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleAnim_GetEndDelay(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_endDelay);

    return 1;
}

// ref: FUN_004a5320
int32_t CSimpleAnim_SetDuration(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetDuration(durationSec)", AnimName(anim));

        return 0;
    }

    anim->m_duration = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleAnim_GetDuration(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_duration);

    return 1;
}

// The reference derives the smooth progress from the plain progress through the smoothing curve,
// and SetSmoothProgress drives the animation to the matching point. With no driver there is no
// curve evaluation to do, so frozen stores the value and hands it back. Stage 4 has to make this
// and m_progress consistent; until then setting one does not move the other.
int32_t CSimpleAnim_GetSmoothProgress(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_smoothProgress);

    return 1;
}

int32_t CSimpleAnim_SetSmoothProgress(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetSmoothProgress(smoothProgress)", AnimName(anim));

        return 0;
    }

    anim->m_smoothProgress = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

// ref: FUN_004a54b0
int32_t CSimpleAnim_GetProgress(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_progress);

    return 1;
}

int32_t CSimpleAnim_GetProgressWithDelay(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_progressWithDelay);

    return 1;
}

int32_t CSimpleAnim_SetMaxFramerate(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetMaxFramerate(framesPerSec)", AnimName(anim));

        return 0;
    }

    anim->m_maxFramerate = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleAnim_GetMaxFramerate(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_maxFramerate);

    return 1;
}

// ref: FUN_004a5660
// Lua's order is 1-based and the stored one is 0-based: the reference passes (order - 1) through.
int32_t CSimpleAnim_SetOrder(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isnumber(L, 2)) {
        luaL_error(L, "Usage: %s:SetOrder(order)", AnimName(anim));

        return 0;
    }

    anim->m_order = static_cast<int8_t>(lua_tointeger(L, 2) - 1);

    return 0;
}

int32_t CSimpleAnim_GetOrder(lua_State* L) {
    lua_pushnumber(L, AnimThis(L)->m_order + 1);

    return 1;
}

int32_t CSimpleAnim_SetSmoothing(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetSmoothing(\"smoothingType\")", AnimName(anim));

        return 0;
    }

    ANIM_SMOOTHING smoothing;

    // An unrecognised name is an ERROR, not a silent no-op. Confirmed at 009ed788; the first pass
    // through this file ignored it, which would have let a typo in FrameXML pass unnoticed.
    if (!AnimSmoothingFromName(lua_tostring(L, 2), smoothing)) {
        luaL_error(L, "%s:SetSmoothing(): Unknown smoothing type specified", AnimName(anim));

        return 0;
    }

    anim->m_smoothing = smoothing;

    return 0;
}

// ref: FUN_004a5810
int32_t CSimpleAnim_GetSmoothing(lua_State* L) {
    lua_pushstring(L, AnimSmoothingName(AnimThis(L)->m_smoothing));

    return 1;
}

// ref: FUN_004a5880
// Accepts an AnimationGroup object or the NAME of one, and refuses nil outright -- an animation
// always belongs to a group. All three messages are the reference's.
//
// The order matters and the first pass here had it wrong: the reference tests for NIL first, then
// for a string, then for a table. Testing "not a table" as the nil case, as this did, reported a
// number or a boolean as a nil parent instead of letting it fall through to the type error.
int32_t CSimpleAnim_SetParent(lua_State* L) {
    auto anim = AnimThis(L);

    if (lua_type(L, 2) == LUA_TNIL) {
        luaL_error(L, "%s:SetParent(): Cannot set a 'nil' parent for animations", AnimName(anim));

        return 0;
    }

    if (lua_isstring(L, 2)) {
        const char* name = lua_tostring(L, 2);
        auto found = CScriptObject::GetScriptObjectByName(name, CSimpleAnimGroup::GetObjectType());

        if (!found) {
            luaL_error(L, "%s:SetParent(): Couldn't find animation group named '%s'",
                       AnimName(anim), name);

            return 0;
        }

        anim->SetParentGroup(static_cast<CSimpleAnimGroup*>(found));

        return 0;
    }

    if (lua_type(L, 2) != LUA_TTABLE) {
        luaL_error(L, "%s:SetParent(): Wrong parent object type, expected AnimationGroup",
                   AnimName(anim));

        return 0;
    }

    lua_rawgeti(L, 2, 0);
    auto object = static_cast<FrameScript_Object*>(lua_touserdata(L, -1));
    lua_settop(L, -2);

    if (!object) {
        luaL_error(L, "%s:SetParent(): Couldn't find 'this' in parent object", AnimName(anim));

        return 0;
    }

    if (!object->IsA(CSimpleAnimGroup::GetObjectType())) {
        luaL_error(L, "%s:SetParent(): Wrong parent object type, expected AnimationGroup",
                   AnimName(anim));

        return 0;
    }

    anim->SetParentGroup(static_cast<CSimpleAnimGroup*>(object));

    return 0;
}

// The REGION the animation ultimately animates, which is its group's parent rather than its own.
int32_t CSimpleAnim_GetRegionParent(lua_State* L) {
    auto region = AnimThis(L)->GetRegionParent();

    if (!region) {
        lua_pushnil(L);

        return 1;
    }

    region->GetScriptMetaTable();
    lua_rawgeti(L, LUA_REGISTRYINDEX, region->lua_objectRef);

    return 1;
}

int32_t CSimpleAnim_HasScript(lua_State* L) {
    auto anim = AnimThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:HasScript(\"type\")", AnimName(anim));

        return 0;
    }

    FrameScript_Object::ScriptData data;
    lua_pushboolean(L, anim->GetScriptByName(lua_tostring(L, 2), data) != nullptr);

    return 1;
}

int32_t CSimpleAnim_GetScript(lua_State* L) {
    return AnimThis(L)->GetScript(L);
}

int32_t CSimpleAnim_SetScript(lua_State* L) {
    return AnimThis(L)->SetScript(L);
}

// TODO HookScript, which chains a second handler in front of the stored one. Frozen has no
// chaining helper yet and every other widget class here leaves it unimplemented for the same
// reason, so this matches them rather than inventing a local mechanism.
int32_t CSimpleAnim_HookScript(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ---------------------------------------------------------------------------- AnimationGroup

// ref: FUN_004a6ea0
int32_t CSimpleAnimGroup_Play(lua_State* L) {
    GroupThis(L)->Play();

    return 0;
}

int32_t CSimpleAnimGroup_Pause(lua_State* L) {
    GroupThis(L)->Pause();

    return 0;
}

int32_t CSimpleAnimGroup_Stop(lua_State* L) {
    GroupThis(L)->Stop();

    return 0;
}

int32_t CSimpleAnimGroup_Finish(lua_State* L) {
    GroupThis(L)->Finish();

    return 0;
}

int32_t CSimpleAnimGroup_GetProgress(lua_State* L) {
    lua_pushnumber(L, GroupThis(L)->m_progress);

    return 1;
}

int32_t CSimpleAnimGroup_IsDone(lua_State* L) {
    lua_pushboolean(L, GroupThis(L)->IsDone());

    return 1;
}

int32_t CSimpleAnimGroup_IsPlaying(lua_State* L) {
    auto group = GroupThis(L);

    lua_pushboolean(L, group->m_playing && !group->m_paused);

    return 1;
}

int32_t CSimpleAnimGroup_IsPaused(lua_State* L) {
    lua_pushboolean(L, GroupThis(L)->m_paused);

    return 1;
}

int32_t CSimpleAnimGroup_IsPendingFinish(lua_State* L) {
    lua_pushboolean(L, GroupThis(L)->m_pendingFinish);

    return 1;
}

int32_t CSimpleAnimGroup_GetDuration(lua_State* L) {
    lua_pushnumber(L, GroupThis(L)->GetDuration());

    return 1;
}

// ref: FUN_004a7130
int32_t CSimpleAnimGroup_SetLooping(lua_State* L) {
    auto group = GroupThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:SetLooping(\"loopType\")", AnimName(group));

        return 0;
    }

    ANIM_LOOPTYPE loopType;

    // As with SetSmoothing: rejected, not ignored. Confirmed at 009eda60.
    if (!AnimLoopTypeFromName(lua_tostring(L, 2), loopType)) {
        luaL_error(L, "%s:SetLooping(): Unknown loop type specified", AnimName(group));

        return 0;
    }

    group->m_looping = loopType;

    return 0;
}

// ref: FUN_004a71f0
int32_t CSimpleAnimGroup_GetLooping(lua_State* L) {
    lua_pushstring(L, AnimLoopTypeName(GroupThis(L)->m_looping));

    return 1;
}

// ref: FUN_004a7240
int32_t CSimpleAnimGroup_GetLoopState(lua_State* L) {
    lua_pushstring(L, AnimLoopStateName(GroupThis(L)->m_loopState));

    return 1;
}

int32_t CSimpleAnimGroup_GetMaxOrder(lua_State* L) {
    lua_pushnumber(L, GroupThis(L)->GetMaxOrder());

    return 1;
}

int32_t CSimpleAnimGroup_SetInitialOffset(lua_State* L) {
    auto group = GroupThis(L);

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        luaL_error(L, "Usage: %s:SetInitialOffset(x, y)", AnimName(group));

        return 0;
    }

    group->m_initialOffsetX = static_cast<float>(lua_tonumber(L, 2));
    group->m_initialOffsetY = static_cast<float>(lua_tonumber(L, 3));

    return 0;
}

int32_t CSimpleAnimGroup_GetInitialOffset(lua_State* L) {
    auto group = GroupThis(L);

    lua_pushnumber(L, group->m_initialOffsetX);
    lua_pushnumber(L, group->m_initialOffsetY);

    return 2;
}

// Every animation as separate return values, not a table. The reference guards the stack first and
// raises its own message rather than letting lua_checkstack fail inside the loop.
int32_t CSimpleAnimGroup_GetAnimations(lua_State* L) {
    auto group = GroupThis(L);
    uint32_t count = group->m_animations.Count();

    if (!lua_checkstack(L, static_cast<int32_t>(count))) {
        luaL_error(L, "%s:GetAnimations(): Stack overflow", AnimName(group));

        return 0;
    }

    for (uint32_t i = 0; i < count; i++) {
        CSimpleAnim* anim = group->m_animations[i];

        anim->GetScriptMetaTable();
        lua_rawgeti(L, LUA_REGISTRYINDEX, anim->lua_objectRef);
    }

    return static_cast<int32_t>(count);
}

// ref: FUN_004a7e00
// The type defaults to "Animation" and the name is optional, in that argument order -- type first,
// name second, which is the opposite of what the name suggests.
//
// TODO the fourth argument, inheritsFrom. The reference resolves it to an XML template node and
// copies it onto the new animation, raising "Couldn't find inherited node" or "Recursively
// inherited node" on failure. Frozen has no XML template table yet (LoadXML_Animations is stage 3),
// so the argument is ignored rather than half-honoured.
int32_t CSimpleAnimGroup_CreateAnimation(lua_State* L) {
    auto group = GroupThis(L);

    const char* type = lua_isstring(L, 2) ? lua_tostring(L, 2) : "Animation";
    const char* name = lua_isstring(L, 3) ? lua_tostring(L, 3) : nullptr;

    CSimpleAnim* anim = group->CreateAnimation(type, name);

    if (!anim) {
        lua_pushnil(L);

        return 1;
    }

    anim->GetScriptMetaTable();
    lua_rawgeti(L, LUA_REGISTRYINDEX, anim->lua_objectRef);

    return 1;
}

int32_t CSimpleAnimGroup_HasScript(lua_State* L) {
    auto group = GroupThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:HasScript(\"type\")", AnimName(group));

        return 0;
    }

    FrameScript_Object::ScriptData data;
    lua_pushboolean(L, group->GetScriptByName(lua_tostring(L, 2), data) != nullptr);

    return 1;
}

int32_t CSimpleAnimGroup_GetScript(lua_State* L) {
    return GroupThis(L)->GetScript(L);
}

int32_t CSimpleAnimGroup_SetScript(lua_State* L) {
    return GroupThis(L)->SetScript(L);
}

// TODO see CSimpleAnim_HookScript.
int32_t CSimpleAnimGroup_HookScript(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleAnimGroup_GetObjectType(lua_State* L) {
    lua_pushstring(L, GroupThis(L)->GetObjectTypeName());

    return 1;
}

int32_t CSimpleAnimGroup_IsObjectType(lua_State* L) {
    auto group = GroupThis(L);

    if (!lua_isstring(L, 2)) {
        luaL_error(L, "Usage: %s:IsObjectType(\"type\")", AnimName(group));

        return 0;
    }

    lua_pushboolean(L, group->IsA(lua_tostring(L, 2)));

    return 1;
}

int32_t CSimpleAnimGroup_GetName(lua_State* L) {
    const char* name = GroupThis(L)->GetName();

    if (name) {
        lua_pushstring(L, name);
    } else {
        lua_pushnil(L);
    }

    return 1;
}

int32_t CSimpleAnimGroup_GetParent(lua_State* L) {
    auto region = GroupThis(L)->m_region;

    if (!region) {
        lua_pushnil(L);

        return 1;
    }

    region->GetScriptMetaTable();
    lua_rawgeti(L, LUA_REGISTRYINDEX, region->lua_objectRef);

    return 1;
}

// ---------------------------------------------------------------------------- tables

FrameScript_Method SimpleAnimMethods[NUM_SIMPLE_ANIM_SCRIPT_METHODS] = {
    { "Play",                   &CSimpleAnim_Play },
    { "Pause",                  &CSimpleAnim_Pause },
    { "Stop",                   &CSimpleAnim_Stop },
    { "IsDone",                 &CSimpleAnim_IsDone },
    { "IsPlaying",              &CSimpleAnim_IsPlaying },
    { "IsPaused",               &CSimpleAnim_IsPaused },
    { "IsStopped",              &CSimpleAnim_IsStopped },
    { "IsDelaying",             &CSimpleAnim_IsDelaying },
    { "GetElapsed",             &CSimpleAnim_GetElapsed },
    { "SetStartDelay",          &CSimpleAnim_SetStartDelay },
    { "GetStartDelay",          &CSimpleAnim_GetStartDelay },
    { "SetEndDelay",            &CSimpleAnim_SetEndDelay },
    { "GetEndDelay",            &CSimpleAnim_GetEndDelay },
    { "SetDuration",            &CSimpleAnim_SetDuration },
    { "GetDuration",            &CSimpleAnim_GetDuration },
    { "GetSmoothProgress",      &CSimpleAnim_GetSmoothProgress },
    { "SetSmoothProgress",      &CSimpleAnim_SetSmoothProgress },
    { "GetProgress",            &CSimpleAnim_GetProgress },
    { "GetProgressWithDelay",   &CSimpleAnim_GetProgressWithDelay },
    { "SetMaxFramerate",        &CSimpleAnim_SetMaxFramerate },
    { "GetMaxFramerate",        &CSimpleAnim_GetMaxFramerate },
    { "SetOrder",               &CSimpleAnim_SetOrder },
    { "GetOrder",               &CSimpleAnim_GetOrder },
    { "SetSmoothing",           &CSimpleAnim_SetSmoothing },
    { "GetSmoothing",           &CSimpleAnim_GetSmoothing },
    { "SetParent",              &CSimpleAnim_SetParent },
    { "GetRegionParent",        &CSimpleAnim_GetRegionParent },
    { "HasScript",              &CSimpleAnim_HasScript },
    { "GetScript",              &CSimpleAnim_GetScript },
    { "SetScript",              &CSimpleAnim_SetScript },
    { "HookScript",             &CSimpleAnim_HookScript }
};

FrameScript_Method SimpleAnimGroupMethods[NUM_SIMPLE_ANIM_GROUP_SCRIPT_METHODS] = {
    { "Play",                   &CSimpleAnimGroup_Play },
    { "Pause",                  &CSimpleAnimGroup_Pause },
    { "Stop",                   &CSimpleAnimGroup_Stop },
    { "Finish",                 &CSimpleAnimGroup_Finish },
    { "GetProgress",            &CSimpleAnimGroup_GetProgress },
    { "IsDone",                 &CSimpleAnimGroup_IsDone },
    { "IsPlaying",              &CSimpleAnimGroup_IsPlaying },
    { "IsPaused",               &CSimpleAnimGroup_IsPaused },
    { "IsPendingFinish",        &CSimpleAnimGroup_IsPendingFinish },
    { "GetDuration",            &CSimpleAnimGroup_GetDuration },
    { "SetLooping",             &CSimpleAnimGroup_SetLooping },
    { "GetLooping",             &CSimpleAnimGroup_GetLooping },
    { "GetLoopState",           &CSimpleAnimGroup_GetLoopState },
    { "GetMaxOrder",            &CSimpleAnimGroup_GetMaxOrder },
    { "SetInitialOffset",       &CSimpleAnimGroup_SetInitialOffset },
    { "GetInitialOffset",       &CSimpleAnimGroup_GetInitialOffset },
    { "GetAnimations",          &CSimpleAnimGroup_GetAnimations },
    { "CreateAnimation",        &CSimpleAnimGroup_CreateAnimation },
    { "HasScript",              &CSimpleAnimGroup_HasScript },
    { "GetScript",              &CSimpleAnimGroup_GetScript },
    { "SetScript",              &CSimpleAnimGroup_SetScript },
    { "HookScript",             &CSimpleAnimGroup_HookScript },
    { "GetObjectType",          &CSimpleAnimGroup_GetObjectType },
    { "IsObjectType",           &CSimpleAnimGroup_IsObjectType },
    { "GetName",                &CSimpleAnimGroup_GetName },
    { "GetParent",              &CSimpleAnimGroup_GetParent }
};
