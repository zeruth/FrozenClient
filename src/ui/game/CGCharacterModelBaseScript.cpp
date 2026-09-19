#include "ui/game/CGCharacterModelBaseScript.hpp"
#include "util/Lua.hpp"
#include "ui/game/CGCharacterModelBase.hpp"
#include "ui/FrameScript.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t Script_SetUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_SetCreature(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t Script_RefreshUnit(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00597a10
// Rotation in radians, which is what the usage string says and what the dress-up and character
// panels pass when you drag the model.
//
// The reference hands it to a setter at 005970f0 that does three things beyond storing the angle,
// none of them ported:
//
//   - it plays a turn animation, choosing sequence 0xb or 0xc by whether the new angle is below or
//     above the current one, and only if the model has that sequence and is not already playing it;
//   - it raises a "turning" flag;
//   - it records a deadline of now + 100ms, which is presumably when that animation is dropped.
//
// Frozen keeps only the angle, in CSimpleModel::m_facing, which is the field the render pass feeds
// to SetWorldTransform -- so the model turns, without the little turn-in-place animation. Storing
// the angle without the flag and the deadline is deliberate: those two exist to end an animation
// that is not being started.
int32_t Script_SetRotation(lua_State* L) {
    auto type = CGCharacterModelBase::GetObjectType();
    auto model = static_cast<CGCharacterModelBase*>(FrameScript_GetObjectThis(L, type));

    if (!lua_isnumber(L, 2)) {
        return luaL_error(L, "Usage: SetRotation(rotation (in radians))");
    }

    model->m_facing = static_cast<float>(lua_tonumber(L, 2));

    return 0;
}

}

FrameScript_Method CGCharacterModelBaseMethods[] = {
    { "SetUnit",        &Script_SetUnit },
    { "SetCreature",    &Script_SetCreature },
    { "RefreshUnit",    &Script_RefreshUnit },
    { "SetRotation",    &Script_SetRotation },
};
