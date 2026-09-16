#include "util/Lua.hpp"
#include "ui/game/CGTooltip.hpp"
#include "ui/game/CGTooltipScript.hpp"

int32_t CGTooltip::s_metatable;
int32_t CGTooltip::s_objectType;

CSimpleFrame* CGTooltip::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGTooltip)(parent);
}

void CGTooltip::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGTooltip::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGTooltip::RegisterScriptMethods);
}

int32_t CGTooltip::GetObjectType() {
    if (!CGTooltip::s_objectType) {
        CGTooltip::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGTooltip::s_objectType;
}

void CGTooltip::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGTooltipMethods, NUM_CG_TOOLTIP_SCRIPT_METHODS);
}

CGTooltip::CGTooltip(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

int32_t CGTooltip::GetScriptMetaTable() {
    return CGTooltip::s_metatable;
}

bool CGTooltip::IsA(int32_t type) {
    return type == CGTooltip::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

FrameScript_Object::ScriptIx* CGTooltip::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnTooltipSetDefaultAnchor")) {
        return &this->m_onTooltipSetDefaultAnchor;
    }

    if (!SStrCmpI(name, "OnTooltipAddMoney")) {
        data.wrapper = "return function(self, money) %s end";
        return &this->m_onTooltipAddMoney;
    }

    if (!SStrCmpI(name, "OnTooltipCleared")) {
        return &this->m_onTooltipCleared;
    }

    if (!SStrCmpI(name, "OnTooltipSetUnit")) {
        return &this->m_onTooltipSetUnit;
    }

    if (!SStrCmpI(name, "OnTooltipSetItem")) {
        return &this->m_onTooltipSetItem;
    }

    if (!SStrCmpI(name, "OnTooltipSetSpell")) {
        return &this->m_onTooltipSetSpell;
    }

    if (!SStrCmpI(name, "OnTooltipSetQuest")) {
        return &this->m_onTooltipSetQuest;
    }

    if (!SStrCmpI(name, "OnTooltipSetAchievement")) {
        return &this->m_onTooltipSetAchievement;
    }

    return CSimpleFrame::GetScriptByName(name, data);
}

void CGTooltip::RunOnTooltipSetDefaultAnchorScript() {
    if (this->m_onTooltipSetDefaultAnchor.luaRef) {
        this->RunScript(this->m_onTooltipSetDefaultAnchor, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipAddMoneyScript(int32_t money) {
    if (this->m_onTooltipAddMoney.luaRef) {
        auto L = FrameScript_GetContext();
        lua_pushnumber(L, money);

        this->RunScript(this->m_onTooltipAddMoney, 1, nullptr);
    }
}

void CGTooltip::RunOnTooltipClearedScript() {
    if (this->m_onTooltipCleared.luaRef) {
        this->RunScript(this->m_onTooltipCleared, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipSetUnitScript() {
    if (this->m_onTooltipSetUnit.luaRef) {
        this->RunScript(this->m_onTooltipSetUnit, 0, nullptr);
    }
}

void CGTooltip::RunOnTooltipSetItemScript() {
    if (this->m_onTooltipSetItem.luaRef) {
        this->RunScript(this->m_onTooltipSetItem, 0, nullptr);
    }
}
