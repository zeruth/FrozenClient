#include "ui/game/CGCooldown.hpp"
#include "ui/game/CGCooldownScript.hpp"

int32_t CGCooldown::s_metatable;
int32_t CGCooldown::s_objectType;

CSimpleFrame* CGCooldown::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGCooldown)(parent);
}

void CGCooldown::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGCooldown::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGCooldown::RegisterScriptMethods);
}

int32_t CGCooldown::GetObjectType() {
    if (!CGCooldown::s_objectType) {
        CGCooldown::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGCooldown::s_objectType;
}

bool CGCooldown::IsA(int32_t type) {
    return type == CGCooldown::GetObjectType()
        || CSimpleFrame::IsA(type);
}

void CGCooldown::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGCooldownMethods, NUM_CG_COOLDOWN_SCRIPT_METHODS);
}

CGCooldown::CGCooldown(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

// ref: FUN_005ec790
void CGCooldown::UpdateColors() {
    uint32_t alpha = (static_cast<uint32_t>(this->alphaBD) * this->m_alpha) / 0xff;

    if (this->m_unk2a8 == 0) {
        this->m_color37c.value = ((alpha & 0xff) * 0xa0) / 0xff << 24;
        this->m_color42c.value = static_cast<uint32_t>(static_cast<uint8_t>(alpha)) << 24 | 0xffffff;
        this->m_color3d0.value = 0;

        return;
    }

    this->m_color37c.value = 0;
    this->m_color42c.value = 0;
    this->m_color3d0.value = static_cast<uint32_t>(static_cast<uint8_t>(alpha)) << 24 | 0x50a0ff;
}

int32_t CGCooldown::GetScriptMetaTable() {
    return CGCooldown::s_metatable;
}
