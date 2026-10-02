#include "ui/game/CGDressUpModelFrameScript.hpp"
#include "ui/game/CGDressUpModelFrame.hpp"
#include "ui/FrameScript.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t Script_Undress(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_00597ed0
// Back to what the unit is wearing: the model is rebuilt from the unit.
int32_t Script_Dress(lua_State* L) {
    auto type = CGDressUpModelFrame::GetObjectType();
    auto model = static_cast<CGDressUpModelFrame*>(FrameScript_GetObjectThis(L, type));

    model->SetUnit(model->m_unitGUID);

    return 0;
}

int32_t Script_TryOn(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

}

FrameScript_Method CGDressUpModelFrameMethods[] = {
    { "Undress",    &Script_Undress },
    { "Dress",      &Script_Dress },
    { "TryOn",      &Script_TryOn },
};
