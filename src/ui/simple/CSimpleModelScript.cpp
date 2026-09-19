#include "ui/simple/CSimpleModelScript.hpp"
#include "ui/simple/CSimpleModel.hpp"
#include "model/CM2Shared.hpp"
#include "util/Lua.hpp"
#include "util/Unimplemented.hpp"
#include <cstdint>

int32_t CSimpleModel_SetModel(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: %s:SetModel(\"file\")", model->GetDisplayName());
    }

    const char* file = lua_tostring(L, 2);

    model->SetModel(file);

    if (!model->m_model) {
        return luaL_error(L, "Invalid model file: %s", file);
    }

    return 0;
}

// ref: FUN_009605d0
int32_t CSimpleModel_GetModel(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (model->m_model) {
        lua_pushstring(L, model->m_model->m_shared->m_filePath);
    }

    // The reference returns 1 result even when no model is set, handing back whatever was
    // already on top of the stack. Reproduced as-is.
    return 1;
}

// ref: FUN_00960620
int32_t CSimpleModel_ClearModel(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    model->SetModel(static_cast<CM2Model*>(nullptr));

    return 0;
}

// ref: FUN_00960660
int32_t CSimpleModel_SetPosition(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    // The reference reads all three coordinates unconditionally, with no type check.
    auto x = static_cast<float>(lua_tonumber(L, 2));
    auto y = static_cast<float>(lua_tonumber(L, 3));
    auto z = static_cast<float>(lua_tonumber(L, 4));

    model->m_position.x = x;
    model->m_position.y = y;
    model->m_position.z = z;

    return 0;
}

// ref: FUN_009606e0
int32_t CSimpleModel_SetFacing(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetFacing(facing)", model->GetDisplayName());
    }

    model->m_facing = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

int32_t CSimpleModel_SetScale(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetScale(scale)", model->GetDisplayName());
    }

    float scale = lua_tonumber(L, 2);
    model->SetScale(scale);

    return 0;
}

int32_t CSimpleModel_SetSequence(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetSequence(sequence)", model->GetDisplayName());
    }

    uint32_t sequence = lua_tonumber(L, 2);

    if (sequence >= 506) {
        return luaL_error(L, "Error: %s:SetSequence(sequence) exceeds valid range of 0 - %d", model->GetDisplayName(), 506);
    }

    model->SetSequence(sequence);

    return 0;
}

int32_t CSimpleModel_SetSequenceTime(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        return luaL_error(L, "Usage: %s:SetSequenceTime(sequence, time)", model->GetDisplayName());
    }

    uint32_t sequence = lua_tonumber(L, 2);
    int32_t time = lua_tonumber(L, 3);
    model->SetSequenceTime(sequence, time);

    return 0;
}

int32_t CSimpleModel_SetCamera(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetCamera(index)", model->GetDisplayName());
    }

    int32_t index = lua_tonumber(L, 2);
    model->SetCameraByIndex(index);

    return 0;
}

// ref: FUN_00960d20
// Identified but not ported: the body is a call to the shared light-argument parser
// FUN_00960a10 (777 bytes, also used by CSimpleModelFFX's light methods) followed by
// FUN_0095f5c0, neither of which has been decompiled. GetLight below documents the field
// layout the parser has to fill; porting it needs those two decompilations.
int32_t CSimpleModel_SetLight(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00960dd0
int32_t CSimpleModel_GetLight(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    lua_checkstack(L, 13);

    auto& light = model->m_light;

    lua_pushnumber(L, light.m_visible);

    C3Vector dir;

    if (light.m_type == M2LIGHT_1) {
        lua_pushnumber(L, 1.0);
        dir = light.m_pos;
    } else {
        lua_pushnumber(L, 0.0);
        dir = light.m_dir;
    }

    lua_pushnumber(L, dir.x);
    lua_pushnumber(L, dir.y);
    lua_pushnumber(L, dir.z);

    int32_t results;

    auto& ambColor = light.m_ambColor;

    if (ambColor.x > 0.0f || ambColor.y > 0.0f || ambColor.z > 0.0f) {
        lua_pushnumber(L, 1.0);
        lua_pushnumber(L, ambColor.x);
        lua_pushnumber(L, ambColor.y);
        lua_pushnumber(L, ambColor.z);

        results = 9;
    } else {
        lua_pushnumber(L, 0.0);

        results = 6;
    }

    auto& dirColor = light.m_dirColor;

    if (dirColor.x <= 0.0f && dirColor.y <= 0.0f && dirColor.z <= 0.0f) {
        lua_pushnumber(L, 0.0);

        return results + 1;
    }

    lua_pushnumber(L, 1.0);
    lua_pushnumber(L, dirColor.x);
    lua_pushnumber(L, dirColor.y);
    lua_pushnumber(L, dirColor.z);

    return results + 4;
}

// ref: FUN_00960fc0
int32_t CSimpleModel_GetPosition(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, model->m_position.x);
    lua_pushnumber(L, model->m_position.y);
    lua_pushnumber(L, model->m_position.z);

    return 3;
}

// ref: FUN_00961040
int32_t CSimpleModel_GetFacing(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, model->m_facing);

    return 1;
}

// FUN_004a6700 is the model's GetScale -- it reads DAT_00b499ec, the model object type --
// and it returns TWO values through a helper at 004980d0, not one. The note that used to
// stand here guessed a single m_scale push, which would have been wrong in arity before it
// was wrong in value. Identify 004980d0 first.
int32_t CSimpleModel_GetScale(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleModel_AdvanceTime(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    // TODO GxDefaultApi();

    return 0;
}

// ref: FUN_00961120
// Identified but not ported: the binding checks lua_isstring(L, 2) and then hands the string
// to FUN_00960320(0xe, path), a file-scope helper (6 callees) that has not been decompiled.
// Texture type 0xe is the M2 item-icon slot, so this is a CM2Model::ReplaceTexture(14, ...)
// wrapper, but the texture creation it performs is unknown.
int32_t CSimpleModel_ReplaceIconTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CSimpleModel_SetFogColor(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    CImVector fogColor = { 0 };
    FrameScript_GetColor(L, 2, fogColor);

    model->m_flags |= 0x1;
    model->m_fogColor = fogColor;

    return 0;
}

// ref: FUN_00961200
int32_t CSimpleModel_GetFogColor(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    auto& fogColor = model->m_fogColor;

    lua_pushnumber(L, fogColor.r * (1.0f / 255.0f));
    lua_pushnumber(L, fogColor.g * (1.0f / 255.0f));
    lua_pushnumber(L, fogColor.b * (1.0f / 255.0f));
    lua_pushnumber(L, fogColor.a * (1.0f / 255.0f));

    return 4;
}

int32_t CSimpleModel_SetFogNear(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetFogNear(value)", model->GetDisplayName());
    }

    model->m_fogNear = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

// ref: FUN_00961350
int32_t CSimpleModel_GetFogNear(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, model->m_fogNear);

    return 1;
}

int32_t CSimpleModel_SetFogFar(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: %s:SetFogFar(value)", model->GetDisplayName());
    }

    model->m_fogFar = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

// ref: FUN_00961420
int32_t CSimpleModel_GetFogFar(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    lua_pushnumber(L, model->m_fogFar);

    return 1;
}

// ref: FUN_00961470
int32_t CSimpleModel_ClearFog(lua_State* L) {
    auto type = CSimpleModel::GetObjectType();
    auto model = static_cast<CSimpleModel*>(FrameScript_GetObjectThis(L, type));

    model->m_flags &= ~0x1u;

    return 0;
}

// ref: FUN_009614b0
// Identified but not ported: the binding checks lua_isnumber(L, 2) and then calls a virtual at
// CSimpleModel vtable slot +0xf0 with the value. Frozen has no counterpart for that virtual and
// its decompilation was not available, so the widget method it needs cannot be written yet.
int32_t CSimpleModel_SetGlow(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

FrameScript_Method SimpleModelMethods[NUM_SIMPLE_MODEL_SCRIPT_METHODS] = {
    { "SetModel",           &CSimpleModel_SetModel },
    { "GetModel",           &CSimpleModel_GetModel },
    { "ClearModel",         &CSimpleModel_ClearModel },
    { "SetPosition",        &CSimpleModel_SetPosition },
    { "SetFacing",          &CSimpleModel_SetFacing },
    { "SetModelScale",      &CSimpleModel_SetScale },
    { "SetSequence",        &CSimpleModel_SetSequence },
    { "SetSequenceTime",    &CSimpleModel_SetSequenceTime },
    { "SetCamera",          &CSimpleModel_SetCamera },
    { "SetLight",           &CSimpleModel_SetLight },
    { "GetLight",           &CSimpleModel_GetLight },
    { "GetPosition",        &CSimpleModel_GetPosition },
    { "GetFacing",          &CSimpleModel_GetFacing },
    { "GetModelScale",      &CSimpleModel_GetScale },
    { "AdvanceTime",        &CSimpleModel_AdvanceTime },
    { "ReplaceIconTexture", &CSimpleModel_ReplaceIconTexture },
    { "SetFogColor",        &CSimpleModel_SetFogColor },
    { "GetFogColor",        &CSimpleModel_GetFogColor },
    { "SetFogNear",         &CSimpleModel_SetFogNear },
    { "GetFogNear",         &CSimpleModel_GetFogNear },
    { "SetFogFar",          &CSimpleModel_SetFogFar },
    { "GetFogFar",          &CSimpleModel_GetFogFar },
    { "ClearFog",           &CSimpleModel_ClearFog },
    { "SetGlow",            &CSimpleModel_SetGlow }
};
