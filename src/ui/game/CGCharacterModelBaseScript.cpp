#include "ui/game/CGCharacterModelBaseScript.hpp"
#include "util/Lua.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "ui/game/CGCharacterModelBase.hpp"
#include "ui/game/ScriptUtil.hpp"
#include "ui/FrameScript.hpp"

namespace {

// ref: FUN_005978e0
int32_t Script_SetUnit(lua_State* L) {
    auto type = CGCharacterModelBase::GetObjectType();
    auto model = static_cast<CGCharacterModelBase*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isstring(L, 2)) {
        return luaL_error(L, "Usage: SetUnit(\"unit\")");
    }

    const char* token = lua_tolstring(L, 2, nullptr);
    WOWGUID guid = 0;
    Script_GetGUIDFromToken(token, guid, false);

    if (guid) {
        model->SetUnit(guid);
    }

    return 0;
}

// ref: FUN_00597960
// The creature's template comes out of the creature cache without asking the server: an entry
// that has not arrived yet sets nothing.
int32_t Script_SetCreature(lua_State* L) {
    auto type = CGCharacterModelBase::GetObjectType();
    auto model = static_cast<CGCharacterModelBase*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: SetCreature(creatureID)");
    }

    int32_t creatureID = static_cast<int32_t>(lua_tointeger(L, 2));
    auto creature = g_creatureCache.GetRecord(DBCACHEKEY32(static_cast<uint32_t>(creatureID)), nullptr, &DBCacheIgnoreCallback, nullptr, true);

    if (creature) {
        model->m_unitGUID = 0;
        model->m_creature = creature;

        if (model->m_visible) {
            model->SetCreatureModel(creature);
        }
    }

    return 0;
}

// ref: FUN_00597a10
int32_t Script_SetRotation(lua_State* L) {
    auto type = CGCharacterModelBase::GetObjectType();
    auto model = static_cast<CGCharacterModelBase*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: SetRotation(rotation (in radians))");
    }

    model->SetRotation(static_cast<float>(lua_tonumber(L, 2)));

    return 0;
}

// ref: FUN_00597b00
int32_t Script_RefreshUnit(lua_State* L) {
    auto type = CGCharacterModelBase::GetObjectType();
    auto model = static_cast<CGCharacterModelBase*>(FrameScript_GetObjectThis(L, type));

    model->SetUnit(model->m_unitGUID);

    return 0;
}

}

FrameScript_Method CGCharacterModelBaseMethods[] = {
    { "SetUnit",        &Script_SetUnit },
    { "SetCreature",    &Script_SetCreature },
    { "RefreshUnit",    &Script_RefreshUnit },
    { "SetRotation",    &Script_SetRotation },
};
