#include "ui/simple/CSimpleCheckbox.hpp"
#include "ui/LoadXML.hpp"
#include "ui/Util.hpp"
#include "ui/simple/CSimpleCheckboxScript.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include <common/XML.hpp>

int32_t CSimpleCheckbox::s_metatable;
int32_t CSimpleCheckbox::s_objectType;

void CSimpleCheckbox::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleCheckbox::RegisterScriptMethods);
    CSimpleCheckbox::s_metatable = ref;
}

int32_t CSimpleCheckbox::GetObjectType() {
    if (!CSimpleCheckbox::s_objectType) {
        CSimpleCheckbox::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleCheckbox::s_objectType;
}

void CSimpleCheckbox::RegisterScriptMethods(lua_State* L) {
    CSimpleButton::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleCheckboxMethods, NUM_SIMPLE_CHECKBOX_SCRIPT_METHODS);
}

int32_t CSimpleCheckbox::GetScriptMetaTable() {
    return CSimpleCheckbox::s_metatable;
}

bool CSimpleCheckbox::IsA(int32_t type) {
    return type == CSimpleCheckbox::s_objectType
        || type == CSimpleButton::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

void CSimpleCheckbox::Enable(int32_t enabled) {
    this->CSimpleButton::Enable(enabled);
    this->SetChecked(this->m_checked, 1);
}

int32_t CSimpleCheckbox::GetChecked() {
    return this->m_checked;
}

void CSimpleCheckbox::LoadXML(const XMLNode* node, CStatus* status) {
    CSimpleButton::LoadXML(node, status);

    auto checkedAttr = node->GetAttributeByName("checked");
    if (checkedAttr && *checkedAttr) {
        auto checked = StringToBOOL(checkedAttr);
        this->SetChecked(checked, 0);
    }

    for (auto child = node->m_child; child; child = child->m_next) {
        if (!SStrCmpI(child->GetName(), "CheckedTexture")) {
            auto checkedTexture = LoadXML_Texture(child, this, status);
            this->SetCheckedTexture(checkedTexture);

        } else if (!SStrCmpI(child->GetName(), "DisabledCheckedTexture")) {
            auto disabledTexture = LoadXML_Texture(child, this, status);
            this->SetDisabledCheckedTexture(disabledTexture);

        }
    }
}

void CSimpleCheckbox::OnClick(const char* btn, int32_t a3) {
    this->SetChecked(this->m_checked == 0, 0);
    this->CSimpleButton::OnClick(btn, a3);
}

void CSimpleCheckbox::SetChecked(int32_t checked, int32_t force) {
    if (checked == this->m_checked && !force) {
        return;
    }

    this->m_checked = checked;

    if (this->m_checkedTexture) {
        this->m_checkedTexture->Hide();
    }

    if (this->m_disabledTexture) {
        this->m_disabledTexture->Hide();
    }

    if (this->m_checked) {
        if (this->m_disabledTexture && this->m_state == BUTTONSTATE_DISABLED) {
            this->m_disabledTexture->Show();
        } else if (this->m_checkedTexture) {
            this->m_checkedTexture->Show();
        }
    }
}

// ref: FUN_009623f0
// ref: FUN_00962660
int32_t CSimpleCheckbox::SetCheckedTexture(const char* texFile) {
    if (this->m_checkedTexture) {
        this->m_checkedTexture->SetTexture(texFile, false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI);

        return 1;
    }

    // TODO auto m = CDataAllocator::GetData(CSimpleTexture::s_allocator, 0, __FILE__, __LINE__);
    auto m = SMemAlloc(sizeof(CSimpleTexture), __FILE__, __LINE__, 0x0);
    auto texture = new (m) CSimpleTexture(nullptr, DRAWLAYER_ARTWORK, 1);

    if (!texture->SetTexture(texFile, false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI)) {
        delete texture;

        return 0;
    }

    texture->SetAllPoints(this, 1);
    texture->SetBlendMode(GxBlend_Add);
    this->SetCheckedTexture(texture);

    return 1;
}

// ref: FUN_00962720
int32_t CSimpleCheckbox::SetDisabledCheckedTexture(const char* texFile) {
    if (this->m_disabledTexture) {
        this->m_disabledTexture->SetTexture(texFile, false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI);

        return 1;
    }

    // TODO auto m = CDataAllocator::GetData(CSimpleTexture::s_allocator, 0, __FILE__, __LINE__);
    auto m = SMemAlloc(sizeof(CSimpleTexture), __FILE__, __LINE__, 0x0);
    auto texture = new (m) CSimpleTexture(nullptr, DRAWLAYER_ARTWORK, 1);

    if (!texture->SetTexture(texFile, false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI)) {
        delete texture;

        return 0;
    }

    texture->SetAllPoints(this, 1);
    texture->SetBlendMode(GxBlend_Add);
    this->SetDisabledCheckedTexture(texture);

    return 1;
}

void CSimpleCheckbox::SetCheckedTexture(CSimpleTexture* texture) {
    if (texture == this->m_checkedTexture) {
        return;
    }

    if (this->m_checkedTexture) {
        delete this->m_checkedTexture;
    }

    if (texture) {
        texture->SetFrame(this, DRAWLAYER_ARTWORK_OVERLAY, 0);
    }

    this->m_checkedTexture = texture;
    this->SetChecked(this->m_checked, 1);
}

// ref: FUN_00962440
void CSimpleCheckbox::SetDisabledCheckedTexture(CSimpleTexture* texture) {
    if (texture == this->m_disabledTexture) {
        return;
    }

    if (this->m_disabledTexture) {
        delete this->m_disabledTexture;
    }

    if (texture) {
        texture->SetFrame(this, DRAWLAYER_ARTWORK_OVERLAY, 0);
    }

    this->m_disabledTexture = texture;
    this->SetChecked(this->m_checked, 1);
}
