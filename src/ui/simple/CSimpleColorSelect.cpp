#include "ui/simple/CSimpleColorSelect.hpp"

#include "ui/LoadXML.hpp"
#include "ui/simple/CSimpleColorSelectScript.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/CStatus.hpp"
#include "util/Lua.hpp"
#include <cmath>
#include <common/XML.hpp>
#include <storm/String.hpp>

int32_t CSimpleColorSelect::s_metatable;
int32_t CSimpleColorSelect::s_objectType;

void CSimpleColorSelect::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleColorSelect::RegisterScriptMethods);
    CSimpleColorSelect::s_metatable = ref;
}

int32_t CSimpleColorSelect::GetObjectType() {
    if (!CSimpleColorSelect::s_objectType) {
        CSimpleColorSelect::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleColorSelect::s_objectType;
}

void CSimpleColorSelect::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleColorSelectMethods,
                                              NUM_SIMPLE_COLOR_SELECT_SCRIPT_METHODS);
}

CSimpleColorSelect::CSimpleColorSelect(CSimpleFrame* parent) : CSimpleFrame(parent) {
}

FrameScript_Object::ScriptIx* CSimpleColorSelect::GetScriptByName(const char* name, ScriptData& data) {
    if (!SStrCmpI(name, "OnColorSelect")) {
        data.wrapper = "return function(self, r, g, b) %s end";
        return &this->m_onColorSelect;
    }

    return CSimpleFrame::GetScriptByName(name, data);
}

void CSimpleColorSelect::GetColorRGB(float& r, float& g, float& b) {
    // Standard HSV to RGB. The wheel works in HSV because that is what its geometry maps to --
    // angle is hue, radius is saturation -- and the interface asks for RGB.
    float h = this->m_hue;

    while (h < 0.0f) {
        h += 360.0f;
    }

    while (h >= 360.0f) {
        h -= 360.0f;
    }

    float c = this->m_value * this->m_saturation;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = this->m_value - c;

    float rp = 0.0f;
    float gp = 0.0f;
    float bp = 0.0f;

    if (h < 60.0f) {
        rp = c; gp = x;
    } else if (h < 120.0f) {
        rp = x; gp = c;
    } else if (h < 180.0f) {
        gp = c; bp = x;
    } else if (h < 240.0f) {
        gp = x; bp = c;
    } else if (h < 300.0f) {
        rp = x; bp = c;
    } else {
        rp = c; bp = x;
    }

    r = rp + m;
    g = gp + m;
    b = bp + m;
}

int32_t CSimpleColorSelect::GetScriptMetaTable() {
    return CSimpleColorSelect::s_metatable;
}

bool CSimpleColorSelect::IsA(int32_t type) {
    return type == CSimpleColorSelect::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

void CSimpleColorSelect::LoadXML(const XMLNode* node, CStatus* status) {
    CSimpleFrame::LoadXML(node, status);

    // The four textures are declared as child elements rather than as regions in a layer, so they
    // are created here and kept by name.
    struct {
        const char* element;
        CSimpleTexture** slot;
    } wanted[] = {
        { "ColorWheelTexture", &this->m_wheelTexture },
        { "ColorWheelThumbTexture", &this->m_wheelThumbTexture },
        { "ColorValueTexture", &this->m_valueTexture },
        { "ColorValueThumbTexture", &this->m_valueThumbTexture },
    };

    for (XMLNode* child = node->m_child; child; child = child->m_next) {
        for (auto& entry : wanted) {
            if (SStrCmpI(child->GetName(), entry.element, STORM_MAX_STR)) {
                continue;
            }

            // Go through LoadXML_Texture rather than constructing directly: it runs PreLoadXML and
            // PostLoadXML too, and those are what register the texture's `name` so siblings can
            // anchor to it. Skipping them created the textures but left ColorPickerWheel nameless,
            // and the sibling anchoring to it reported "Couldn't find relative frame".
            CSimpleTexture* texture = LoadXML_Texture(child, this, status);
            texture->SetFrame(this, DRAWLAYER_ARTWORK, texture->m_shown);
            *entry.slot = texture;
        }
    }
}

void CSimpleColorSelect::RunOnColorSelectScript() {
    if (!this->m_onColorSelect.luaRef) {
        return;
    }

    float r;
    float g;
    float b;
    this->GetColorRGB(r, g, b);

    auto L = FrameScript_GetContext();
    lua_pushnumber(L, r);
    lua_pushnumber(L, g);
    lua_pushnumber(L, b);

    this->RunScript(this->m_onColorSelect, 3, nullptr);
}

void CSimpleColorSelect::SetColorHSV(float hue, float saturation, float value) {
    this->m_hue = hue;
    this->m_saturation = saturation < 0.0f ? 0.0f : (saturation > 1.0f ? 1.0f : saturation);
    this->m_value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

    this->RunOnColorSelectScript();
}

void CSimpleColorSelect::SetColorRGB(float r, float g, float b) {
    // RGB to HSV, so the wheel and value strip can show the colour the interface handed us.
    float maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = maxC - minC;

    float hue = 0.0f;

    if (delta > 0.0f) {
        if (maxC == r) {
            hue = 60.0f * fmodf((g - b) / delta, 6.0f);
        } else if (maxC == g) {
            hue = 60.0f * (((b - r) / delta) + 2.0f);
        } else {
            hue = 60.0f * (((r - g) / delta) + 4.0f);
        }
    }

    if (hue < 0.0f) {
        hue += 360.0f;
    }

    this->m_hue = hue;
    this->m_saturation = maxC > 0.0f ? delta / maxC : 0.0f;
    this->m_value = maxC;

    this->RunOnColorSelectScript();
}
