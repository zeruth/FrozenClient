#include "ui/game/CGTooltip.hpp"
#include "ui/game/CGTooltipScript.hpp"
#include "gx/Coordinate.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/FrameScript.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleStatusBar.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/Lua.hpp"
#include <storm/String.hpp>
#include <tempest/Rect.hpp>
#include <tempest/Vector.hpp>
#include <cmath>
#include <cstring>

int32_t CGTooltip::s_metatable;
int32_t CGTooltip::s_objectType;

// The colour a line takes when the caller names none, NORMAL_FONT_COLOR (DAT_00ad2d2c).
static const uint32_t TOOLTIP_DEFAULT_COLOR = 0xFFFFD200;

// A length in UI units as a layout length, the conversion every tooltip measurement goes through:
// the reference divides by the aspect compensation and 1024 (DAT_009e300c), then takes the
// result through NDCToDDCWidth.
static float TooltipUIToDDC(float ui) {
    return NDCToDDCWidth(ui / (CoordinateGetAspectCompensation() * 1024.0f));
}

CSimpleFrame* CGTooltip::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGTooltip)(parent);
}

// ref: FUN_0061dba0
void CGTooltip::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGTooltip::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGTooltip::RegisterScriptMethods);
}

// ref: FUN_00514410
int32_t CGTooltip::GetObjectType() {
    if (!CGTooltip::s_objectType) {
        CGTooltip::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGTooltip::s_objectType;
}

// ref: FUN_0061b4a0
void CGTooltip::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGTooltipMethods, NUM_CG_TOOLTIP_SCRIPT_METHODS);
}

// ref: FUN_0061dee0
CGTooltip::CGTooltip(CSimpleFrame* parent) : CSimpleFrame(parent) {
}

// ref: FUN_0061e160
CGTooltip::~CGTooltip() {
    if (this->m_statusUnitGUID && ClntObjMgrGetCurrent()) {
        // TODO the status bar's health callback comes off the unit here (FUN_004d5b40 with
        // FUN_0061bf10, or FUN_0061bf80 for a game object) once the mirror handlers are ported.
    }
}

// ref: FUN_0061a280
FrameScript_Object::ScriptIx* CGTooltip::GetScriptByName(const char* name, ScriptData& data) {
    auto script = CSimpleFrame::GetScriptByName(name, data);

    if (script) {
        return script;
    }

    if (!SStrCmpI(name, "OnTooltipSetDefaultAnchor", STORM_MAX_STR)) {
        return &this->m_onTooltipSetDefaultAnchor;
    }

    if (!SStrCmpI(name, "OnTooltipCleared", STORM_MAX_STR)) {
        return &this->m_onTooltipCleared;
    }

    if (!SStrCmpI(name, "OnTooltipAddMoney", STORM_MAX_STR)) {
        data.wrapper = "return function(self,cost,maxcost) %s end";
        return &this->m_onTooltipAddMoney;
    }

    if (!SStrCmpI(name, "OnTooltipSetUnit", STORM_MAX_STR)) {
        return &this->m_onTooltipSetUnit;
    }

    if (!SStrCmpI(name, "OnTooltipSetItem", STORM_MAX_STR)) {
        return &this->m_onTooltipSetItem;
    }

    if (!SStrCmpI(name, "OnTooltipSetSpell", STORM_MAX_STR)) {
        return &this->m_onTooltipSetSpell;
    }

    if (!SStrCmpI(name, "OnTooltipSetQuest", STORM_MAX_STR)) {
        return &this->m_onTooltipSetQuest;
    }

    if (!SStrCmpI(name, "OnTooltipSetAchievement", STORM_MAX_STR)) {
        return &this->m_onTooltipSetAchievement;
    }

    if (!SStrCmpI(name, "OnTooltipSetEquipmentSet", STORM_MAX_STR)) {
        return &this->m_onTooltipSetEquipmentSet;
    }

    if (!SStrCmpI(name, "OnTooltipSetFrameStack", STORM_MAX_STR)) {
        return &this->m_onTooltipSetFrameStack;
    }

    return nullptr;
}

// ref: FUN_0061e0c0
int32_t CGTooltip::GetScriptMetaTable() {
    return CGTooltip::s_metatable;
}

// ref: FUN_0061e0e0
bool CGTooltip::IsA(int32_t type) {
    return type == CGTooltip::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

// ref: FUN_0061e120
bool CGTooltip::IsA(const char* typeName) {
    return !SStrCmpI(typeName, "GameTooltip", STORM_MAX_STR) || CScriptRegion::IsA(typeName);
}

// ref: FUN_0061e0b0
const char* CGTooltip::GetObjectTypeName() {
    return "GameTooltip";
}

// ref: FUN_0061fb30
// Finds the lines the template declares -- <name>TextLeft<n> and <name>TextRight<n>, counted until
// a pair is missing, then stored on the second pass -- the ten <name>Texture<n> slots and the
// status bar.
void CGTooltip::PostLoadXML(const XMLNode* node, CStatus* status) {
    CSimpleFrame::PostLoadXML(node, status);

    auto name = this->GetName();

    if (!name) {
        name = "<unnamed>";
    }

    char leftName[256];
    char rightName[256];

    for (int32_t pass = 0; pass < 2; pass++) {
        if (pass == 1) {
            this->m_leftStrings.SetCount(this->m_maxLines);
            this->m_rightStrings.SetCount(this->m_maxLines);
            this->m_wrapLine.SetCount(this->m_maxLines);
        }

        this->m_maxLines = 0;

        while (true) {
            SStrPrintf(leftName, sizeof(leftName), "%sTextLeft%d", name, this->m_maxLines + 1);
            SStrPrintf(rightName, sizeof(rightName), "%sTextRight%d", name, this->m_maxLines + 1);

            auto left = static_cast<CSimpleFontString*>(CScriptObject::GetScriptObjectByName(leftName, CSimpleFontString::GetObjectType()));
            auto right = static_cast<CSimpleFontString*>(CScriptObject::GetScriptObjectByName(rightName, CSimpleFontString::GetObjectType()));

            if (!left || !right) {
                break;
            }

            if (pass == 1) {
                this->m_leftStrings[this->m_maxLines] = left;
                this->m_rightStrings[this->m_maxLines] = right;
                this->m_wrapLine[this->m_maxLines] = 0;
            }

            this->m_maxLines++;
        }
    }

    char textureName[256];

    for (uint32_t i = 0; i < 10; i++) {
        SStrPrintf(textureName, sizeof(textureName), "%sTexture%d", name, i + 1);
        this->m_textures[i] = static_cast<CSimpleTexture*>(CScriptObject::GetScriptObjectByName(textureName, CSimpleTexture::GetObjectType()));
    }

    SStrPrintf(textureName, sizeof(textureName), "%sStatusBar", name);
    this->m_statusBar = static_cast<CSimpleStatusBar*>(CScriptObject::GetScriptObjectByName(textureName, CSimpleStatusBar::GetObjectType()));
}

// ref: FUN_0061fe30
// One more line, from a pair of font strings made elsewhere.
void CGTooltip::AddFontStrings(CSimpleFontString* left, CSimpleFontString* right) {
    this->m_maxLines++;

    this->m_leftStrings.SetCount(this->m_maxLines);
    this->m_rightStrings.SetCount(this->m_maxLines);
    this->m_wrapLine.SetCount(this->m_maxLines);

    this->m_leftStrings[this->m_maxLines - 1] = left;
    this->m_rightStrings[this->m_maxLines - 1] = right;
    this->m_wrapLine[this->m_maxLines - 1] = 0;
}

// ref: FUN_0061fec0
// The next line takes the text. The tooltip always keeps a spare line: when the one being filled
// is the last there is, the next pair is made at once -- left under the last left, right with its
// right edge 40 along from the new left's left -- in the last line's fonts.
void CGTooltip::AddLine(const char* left, const char* right, const CImVector& leftColor, const CImVector& rightColor, int32_t wrap) {
    bool hasLeft = left && *left;
    bool hasRight = right && *right;

    if ((!hasLeft && !hasRight) || !this->m_maxLines) {
        return;
    }

    if (this->m_numLines == this->m_maxLines - 1) {
        auto name = this->GetName();

        if (!name) {
            name = "<unnamed>";
        }

        char fontStringName[256];

        SStrPrintf(fontStringName, sizeof(fontStringName), "%sTextLeft%d", name, this->m_maxLines + 1);
        auto newLeft = STORM_NEW(CSimpleFontString)(this, DRAWLAYER_ARTWORK, 1);
        newLeft->SetName(fontStringName);

        auto lastLeft = this->m_leftStrings[this->m_maxLines - 1];
        newLeft->SetFontObject(lastLeft->GetFontObject());
        newLeft->SetPoint(FRAMEPOINT_TOPLEFT, lastLeft, FRAMEPOINT_BOTTOMLEFT, 0.0f, TooltipUIToDDC(-2.0f), 0);
        newLeft->Hide();

        SStrPrintf(fontStringName, sizeof(fontStringName), "%sTextRight%d", name, this->m_maxLines + 1);
        auto newRight = STORM_NEW(CSimpleFontString)(this, DRAWLAYER_ARTWORK, 1);
        newRight->SetName(fontStringName);

        newRight->SetFontObject(this->m_rightStrings[this->m_maxLines - 1]->GetFontObject());
        newRight->SetPoint(FRAMEPOINT_RIGHT, newLeft, FRAMEPOINT_LEFT, TooltipUIToDDC(40.0f), 0.0f, 0);
        newRight->Hide();

        this->AddFontStrings(newLeft, newRight);
    }

    if (hasLeft) {
        auto line = this->m_leftStrings[this->m_numLines];
        line->SetVertexColor(leftColor);
        line->SetText(left, 1);
        line->Show();
    }

    if (hasRight) {
        auto line = this->m_rightStrings[this->m_numLines];
        line->SetVertexColor(rightColor);
        line->SetText(right, 1);
        line->Show();

        wrap = 0;
    }

    this->m_wrapLine[this->m_numLines] = wrap;
    this->m_numLines++;
}

// ref: FUN_006201c0
// A line in the normal font colour on both sides.
void CGTooltip::AddLine(const char* left, const char* right, int32_t wrap) {
    CImVector color;
    color.value = TOOLTIP_DEFAULT_COLOR;

    this->AddLine(left, right, color, color, wrap);
}

// ref: FUN_0061c8b0
// Puts the next of the template's textures at the head of the line last added: the line moves
// right by the texture's width, the texture hangs off its top-left, and the line after it (if
// there is one) moves back. A texture on the line right under another one's does not indent.
void CGTooltip::AddTexture(const char* fileName, const CRect& texCoords, const CImVector& color) {
    auto index = this->m_numTextures;

    if (index >= 10 || !this->m_textures[index] || this->m_numLines < 2) {
        return;
    }

    auto texture = this->m_textures[index];
    auto line = this->m_numLines - 1;

    float indent;

    if (index == 0 || this->m_textureLine[index - 1] != this->m_numLines - 2) {
        indent = texture->GetWidth() + 0.005f;
    } else {
        indent = 0.0f;
    }

    this->m_leftStrings[line]->SetPoint(FRAMEPOINT_TOPLEFT, this->m_leftStrings[line - 1], FRAMEPOINT_BOTTOMLEFT, indent, TooltipUIToDDC(-2.0f), 1);

    texture->ClearAllPoints();
    texture->SetPoint(FRAMEPOINT_TOPRIGHT, this->m_leftStrings[line], FRAMEPOINT_TOPLEFT, -0.005f, 0.0f, 1);

    if (line < this->m_maxLines - 1 && this->m_leftStrings[line + 1]) {
        float y = TooltipUIToDDC(-2.0f);
        this->m_leftStrings[line + 1]->SetPoint(FRAMEPOINT_TOPLEFT, this->m_leftStrings[line], FRAMEPOINT_BOTTOMLEFT, -(texture->GetWidth() + 0.005f), y, 1);
    }

    texture->SetTexture(fileName, false, false, CSimpleTexture::s_textureFilterMode, ImageMode_UI);
    texture->SetTexCoord(texCoords);
    texture->SetVertexColor(color);
    texture->Show();

    this->m_textureLine[index] = line;
    this->m_numTextures++;
}

// ref: FUN_0061eab0
// The first line gets `text` after what it already says.
void CGTooltip::AppendText(const char* text) {
    if (!this->m_numLines) {
        return;
    }

    auto existing = this->m_leftStrings[0]->GetText();

    if (existing && !*existing) {
        existing = nullptr;
    }

    char line[256];
    SStrPrintf(line, sizeof(line), "%s%s", existing, text);

    this->m_leftStrings[0]->SetText(line, 1);

    this->CalculateSize();
}

// ref: FUN_0061caf0
// Sizes the tooltip to its lines. The width is the widest line that does not wrap (or the minimum
// width, which a forced minimum makes the only vote); a wrapping line may widen it up to 230.4,
// measured segment by segment where it breaks. The right-hand texts are pinned to that width and
// the lines stacked two apart, inside a 10.24 border on every side and the padding on the right.
void CGTooltip::CalculateSize() {
    if (!this->m_owner) {
        return;
    }

    float width = TooltipUIToDDC(this->m_minimumWidth);

    if (!this->m_minimumWidthForced) {
        for (uint32_t i = 0; i < this->m_numLines; i++) {
            if (this->m_wrapLine[i]) {
                continue;
            }

            auto left = this->m_leftStrings[i];
            auto right = this->m_rightStrings[i];

            float lineWidth = 0.0f;

            if (left->IsShown() && right->IsShown()) {
                lineWidth = TooltipUIToDDC(38.4f);
            }

            if (left->IsShown()) {
                lineWidth += left->GetWidth();
            }

            if (right->IsShown()) {
                lineWidth += right->GetWidth();
            }

            for (uint32_t t = 0; t < this->m_numTextures; t++) {
                if (this->m_textureLine[t] == i) {
                    lineWidth = this->m_textures[t]->GetWidth() + lineWidth + 0.005f;
                    break;
                }
            }

            if (width < lineWidth) {
                width = lineWidth;
            }
        }
    }

    for (uint32_t i = 0; i < this->m_numLines; i++) {
        if (!this->m_minimumWidthForced && !this->m_wrapLine[i]) {
            continue;
        }

        auto left = this->m_leftStrings[i];

        float lineWidth = left->GetStringWidth();
        float cap = TooltipUIToDDC(230.4f);

        if (cap <= lineWidth) {
            lineWidth = cap;
        }

        float measured = lineWidth;

        if (width < measured) {
            auto text = left->GetText();

            if (text && !*text) {
                text = nullptr;
            }

            uint32_t points[30];
            uint32_t count = left->GetWrapPoints(text, measured, points, 30);

            if (count < 30) {
                count = left->GetWrapPoints(text, lineWidth, points, 30);
            } else {
                count = 30;
            }

            measured = 0.0f;

            for (uint32_t k = 0; k < count; k++) {
                auto start = points[k];
                auto end = k < count - 1 ? points[k + 1] : SStrLen(text);

                float segment = left->GetTextWidth(text + start, end - start);

                if (measured < segment) {
                    measured = segment;
                }
            }
        }

        if (width <= measured) {
            width = measured;
        }

        left->SetWidth(width);
    }

    for (uint32_t i = 0; i < this->m_numLines; i++) {
        auto right = this->m_rightStrings[i];

        if (!right->IsShown()) {
            continue;
        }

        float x = width;

        for (uint32_t t = 0; t < this->m_numTextures; t++) {
            if (this->m_textureLine[t] == i) {
                x = width - this->m_textures[t]->GetWidth() - 0.005f;
                break;
            }
        }

        right->SetPoint(FRAMEPOINT_RIGHT, this->m_leftStrings[i], FRAMEPOINT_LEFT, x, 0.0f, 1);
    }

    float height = 0.0f;

    for (uint32_t i = 0; i < this->m_numLines; i++) {
        auto left = this->m_leftStrings[i];

        if (!left->IsShown()) {
            continue;
        }

        if (height != 0.0f) {
            height = TooltipUIToDDC(2.0f) + height;
        }

        height = left->GetHeight() + height;
    }

    float border = TooltipUIToDDC(10.24f);

    this->SetWidth(border + border + this->m_padding + width);
    this->SetHeight(border + border + height);

    if (this->m_anchorType != TOOLTIP_ANCHOR_CURSOR && this->m_anchorType != TOOLTIP_ANCHOR_CURSOR_RIGHT) {
        this->SetAnchor(0);
    }

    this->Resize(1);
}

// ref: FUN_0061c620
// Empties the tooltip: the status bar lets go of its unit, what the tooltip was filled from is
// forgotten (all but the item GUID), the textures come off their lines and the lines below them go
// back -- with no gap, which is the reference's own -- every line in use is emptied and hidden,
// an unforced minimum width goes, and OnTooltipCleared runs when there was anything to clear.
void CGTooltip::ClearTooltip() {
    if (this->m_statusUnitGUID) {
        // TODO the status bar's health callback comes off the unit (FUN_004d5b40 with FUN_0061bf10
        // for a unit, FUN_0061bf80 for a game object) once the mirror handlers are ported.
        this->m_statusBar->Hide();
    }

    this->m_unitGUID = 0;
    this->m_guid330 = 0;
    this->m_guid338 = 0;
    this->m_guid348 = 0;
    this->m_statusUnitGUID = 0;
    this->m_itemID = 0;
    this->m_spellID = 0;
    this->m_questID = 0;
    this->m_achievementID = 0;
    this->m_spellID2 = 0;

    for (uint32_t i = 0; i < this->m_numTextures; i++) {
        auto line = this->m_textureLine[i];

        this->m_leftStrings[line]->SetPoint(FRAMEPOINT_TOPLEFT, this->m_leftStrings[line - 1], FRAMEPOINT_BOTTOMLEFT, 0.0f, TooltipUIToDDC(-2.0f), 1);

        if (line < this->m_maxLines - 1 && this->m_leftStrings[line + 1]) {
            this->m_leftStrings[line + 1]->SetPoint(FRAMEPOINT_TOPLEFT, this->m_leftStrings[line], FRAMEPOINT_BOTTOMLEFT, 0.0f, 0.0f, 1);
        }

        this->m_textures[i]->Hide();
    }

    auto hadLines = this->m_numLines != 0;

    this->m_numTextures = 0;

    if (hadLines) {
        for (uint32_t i = 0; i < this->m_numLines; i++) {
            this->m_leftStrings[i]->SetWidth(0.0f);

            this->m_leftStrings[i]->SetText("", 1);
            this->m_leftStrings[i]->Hide();

            this->m_rightStrings[i]->SetText("", 1);
            this->m_rightStrings[i]->Hide();

            this->m_wrapLine[i] = 0;
        }

        this->m_numLines = 0;
    }

    if (!this->m_minimumWidthForced) {
        this->m_minimumWidth = 0.0f;
    }

    if (hadLines && this->m_onTooltipCleared.luaRef) {
        this->RunScript(this->m_onTooltipCleared, 0, nullptr);
    }
}

// ref: FUN_0061b290
// A tooltip at the cursor goes at once; any other fades out over two seconds.
void CGTooltip::FadeOut() {
    if (this->m_anchorType != TOOLTIP_ANCHOR_CURSOR && this->m_anchorType != TOOLTIP_ANCHOR_CURSOR_RIGHT) {
        this->m_fading = 1;
        this->m_fadeTime = 2.0f;

        return;
    }

    this->Hide();
    this->m_fading = 0;
}

// ref: FUN_0061a0d0
void CGTooltip::RunOnTooltipAddMoneyScript(int32_t cost, int32_t maxCost) {
    if (!this->m_onTooltipAddMoney.luaRef) {
        return;
    }

    auto L = FrameScript_GetContext();
    lua_pushnumber(L, cost);

    if (maxCost > 0) {
        lua_pushnumber(L, maxCost);
    } else {
        lua_pushnil(L);
    }

    this->RunScript(this->m_onTooltipAddMoney, 2, nullptr);
}

// ref: FUN_0061b040
// Puts the tooltip against its owner by the anchor type. ANCHOR_PRESERVE leaves it where it is;
// ANCHOR_NONE keeps its points unless forced, and the cursor anchors are placed every frame by
// OnLayerUpdate instead.
void CGTooltip::SetAnchor(int32_t force) {
    if (!this->m_owner || this->m_anchorType == TOOLTIP_ANCHOR_PRESERVE) {
        return;
    }

    if (force || this->m_anchorType != TOOLTIP_ANCHOR_NONE) {
        this->ClearAllPoints();
    }

    auto x = this->m_anchorOffset.x;
    auto y = this->m_anchorOffset.y;

    switch (this->m_anchorType) {
        case TOOLTIP_ANCHOR_LEFT:
            this->SetPoint(FRAMEPOINT_BOTTOMRIGHT, this->m_owner, FRAMEPOINT_TOPLEFT, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_RIGHT:
            this->SetPoint(FRAMEPOINT_BOTTOMLEFT, this->m_owner, FRAMEPOINT_TOPRIGHT, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_BOTTOMLEFT:
            this->SetPoint(FRAMEPOINT_TOPRIGHT, this->m_owner, FRAMEPOINT_BOTTOMLEFT, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_BOTTOM:
            this->SetPoint(FRAMEPOINT_TOP, this->m_owner, FRAMEPOINT_BOTTOM, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_BOTTOMRIGHT:
            this->SetPoint(FRAMEPOINT_TOPLEFT, this->m_owner, FRAMEPOINT_BOTTOMRIGHT, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_TOPLEFT:
            this->SetPoint(FRAMEPOINT_BOTTOMLEFT, this->m_owner, FRAMEPOINT_TOPLEFT, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_TOP:
            this->SetPoint(FRAMEPOINT_BOTTOM, this->m_owner, FRAMEPOINT_TOP, x, y, 1);
            break;

        case TOOLTIP_ANCHOR_TOPRIGHT:
            this->SetPoint(FRAMEPOINT_BOTTOMRIGHT, this->m_owner, FRAMEPOINT_TOPRIGHT, x, y, 1);
            break;

        default:
            break;
    }
}

// ref: FUN_0061ea00
// Empties the tooltip and takes the new owner and anchor. The same owner, anchor and offsets
// change nothing more; anything else is placed again, a tooltip with no owner anchored nowhere.
void CGTooltip::SetOwner(CSimpleFrame* owner, TOOLTIP_ANCHORPOINT anchorType, float offsetX, float offsetY) {
    this->ClearTooltip();
    this->SetFrameAlpha(255);
    this->m_fading = 0;

    if (owner == this->m_owner
        && anchorType == this->m_anchorType
        && std::fabs(offsetX - this->m_anchorOffset.x) < 2.384185791015625e-07f
        && std::fabs(offsetY - this->m_anchorOffset.y) < 2.384185791015625e-07f) {
        return;
    }

    this->m_anchorOffset.x = offsetX;
    this->m_owner = owner;
    this->m_anchorOffset.y = offsetY;
    this->m_anchorType = TOOLTIP_ANCHOR_NONE;

    if (owner) {
        this->m_anchorType = anchorType;
    }

    this->SetAnchor(1);
}

// ref: FUN_0061cff0
// A tooltip shows only with an owner and something in it, laid out first; otherwise it hides.
void CGTooltip::OnLayerShow() {
    if (this->m_owner && this->m_numLines) {
        this->SetFrameAlpha(255);
        this->m_fading = 0;
        this->CalculateSize();

        CSimpleFrame::OnLayerShow();

        return;
    }

    this->Hide();
}

// ref: FUN_0061b2e0
// A cursor tooltip follows the mouse -- its bottom at the cursor, or its bottom-left beside it by
// the anchor offsets -- and a fading one loses alpha until it hides.
void CGTooltip::OnLayerUpdate(float elapsedSec) {
    CSimpleFrame::OnLayerUpdate(elapsedSec);

    if (this->m_anchorType == TOOLTIP_ANCHOR_CURSOR || this->m_anchorType == TOOLTIP_ANCHOR_CURSOR_RIGHT) {
        float x = 0.0f;
        float y = 0.0f;
        NDCToDDC(this->m_top->m_mousePosition.x, this->m_top->m_mousePosition.y, &x, &y);

        float scale = 1.0f / this->m_layoutScale;
        x *= scale;
        y *= scale;

        FRAMEPOINT point;

        if (this->m_anchorType == TOOLTIP_ANCHOR_CURSOR) {
            point = FRAMEPOINT_BOTTOM;
        } else {
            y += this->m_anchorOffset.y;
            x += this->m_anchorOffset.x;
            point = FRAMEPOINT_BOTTOMLEFT;
        }

        this->SetPoint(point, CSimpleTop::s_instance, FRAMEPOINT_BOTTOMLEFT, x, y, 1);
    }

    if (!this->m_fading) {
        return;
    }

    auto remaining = this->m_fadeTime - elapsedSec;
    this->m_fadeTime = remaining;

    if (remaining > 0.0f) {
        if (remaining > 1.0f) {
            remaining = 1.0f;
        }

        this->SetFrameAlpha(static_cast<uint8_t>(static_cast<int32_t>(std::lround(remaining * 255.0f)) & 0xFF));

        return;
    }

    this->SetFrameAlpha(255);
    this->Hide();
}

// ref: FUN_0061eb20
// Hiding lets go of the owner.
int32_t CGTooltip::HideThis() {
    this->SetOwner(nullptr, TOOLTIP_ANCHOR_LEFT, 0.0f, 0.0f);

    return CSimpleFrame::HideThis();
}

// ref: FUN_0061b5b0
void ItemStatTotals::Clear() {
    memset(this->stats, 0, sizeof(this->stats));
    this->dps = 0.0f;
    this->flags = 0;
}

// ref: FUN_0061b8e0
void ItemStatTotals::AddArmorAndResistances(const ItemStats_C* info) {
    this->flags |= 0x8;

    this->stats[0] += info->armor;

    for (int32_t i = 0; i < 6; i++) {
        this->stats[1 + i] += info->resistance[i];
    }
}

// ref: FUN_0061b930
void ItemStatTotals::AddSockets(const ItemStats_C* info) {
    this->flags |= 0x10;

    for (int32_t i = 0; i < ItemStats_C::MAX_SOCKETS; i++) {
        auto color = info->socketColor[i];

        if (color & 0x1) {
            this->stats[69]++;
        }

        if (color & 0x2) {
            this->stats[70]++;
        }

        if (color & 0x4) {
            this->stats[71]++;
        }

        if (color & 0x8) {
            this->stats[72]++;
        }
    }
}

// Stat type 38 (attack power) also counts toward both attack power totals, and 39 (ranged attack
// power) toward the ranged one.
// ref: FUN_0061b990
void ItemStatTotals::AddItemStats(const ItemStats_C* info) {
    this->flags |= 0x20;

    for (int32_t i = 0; i < info->statsCount; i++) {
        auto type = info->statType[i];
        auto value = info->statValue[i];

        this->stats[11 + type] += value;

        if (type == 38) {
            this->stats[8] += value;
            this->stats[9] += value;
        } else if (type == 39) {
            this->stats[9] += value;
        }
    }
}

// Adds stats[index] into each of the count stats named by into. When collapse is set and they all
// come out equal, that common value moves back to stats[index] and the others are cleared;
// otherwise stats[index] is cleared.
// ref: FUN_0061b9e0
void ItemStatTotals::FoldStats(int32_t index, const int32_t* into, uint32_t count, int32_t collapse) {
    auto value = this->stats[index];
    int32_t previous = 0;

    for (uint32_t i = 0; i < count; i++) {
        this->stats[into[i]] += value;

        collapse = collapse && (i == 0 || this->stats[into[i]] == previous) ? 1 : 0;

        previous = this->stats[into[i]];
    }

    if (!collapse) {
        this->stats[index] = 0;
        return;
    }

    this->stats[index] = this->stats[into[0]];

    for (uint32_t i = 0; i < count; i++) {
        this->stats[into[i]] = 0;
    }
}

// Formats a time as "<n> days/hours/min/sec" through the <prefix>_DAYS, _HOURS, _MIN or _SEC
// global string, choosing the largest unit the time reaches. time is milliseconds, or seconds when
// inSeconds is set; roundUp rounds the count up instead of down, and a count that rounds up to a
// whole next unit is shown as 1 of that unit. A non-zero displayValue is printed in place of the
// count.
// ref: FUN_0061a9e0
void FormatTimeInterval(char* dest, uint32_t destSize, uint64_t time, const char* prefix, int32_t displayValue, int32_t roundUp, bool inSeconds) {
    if (!dest || !prefix) {
        return;
    }

    uint32_t minute = inSeconds ? 60 : 60000;
    uint32_t hour = minute * 60;
    *dest = '\0';
    uint32_t day = minute * 1440;

    auto value = static_cast<uint32_t>(time);
    const char* format;

    if ((time >> 32) == 0 && value < day) {
        if (value < hour) {
            if (value < minute) {
                if (!inSeconds) {
                    value /= 1000;
                }

                format = "%s_SEC";
            } else {
                auto count = roundUp
                    ? (static_cast<uint64_t>(value) - 1) / minute + 1
                    : static_cast<uint64_t>(value) / minute;

                value = static_cast<uint32_t>(count);

                if (count == 60) {
                    value = 1;
                    format = "%s_HOURS";
                } else {
                    format = "%s_MIN";
                }
            }
        } else {
            auto count = roundUp
                ? (static_cast<uint64_t>(value) - 1) / hour + 1
                : static_cast<uint64_t>(value) / hour;

            value = static_cast<uint32_t>(count);

            if (count == 24) {
                format = "%s_DAYS";
                value = 1;
            } else {
                format = "%s_HOURS";
            }
        }
    } else {
        if (!roundUp) {
            value = static_cast<uint32_t>(time / day);
        } else {
            value = static_cast<uint32_t>((time - 1) / day) + 1;
        }

        format = "%s_DAYS";
    }

    char key[256];
    SStrPrintf(key, sizeof(key), format, prefix);

    if (displayValue) {
        SStrPrintf(dest, destSize, FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE), displayValue);
        return;
    }

    SStrPrintf(dest, destSize, FrameScript_GetText(key, -1, GENDER_NOT_APPLICABLE), value);
}
