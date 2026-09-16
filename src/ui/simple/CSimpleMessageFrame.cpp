#include "ui/simple/CSimpleMessageFrame.hpp"

#include "ui/Util.hpp"
#include "ui/simple/CSimpleFontString.hpp"
#include "ui/simple/CSimpleFontedFrameFont.hpp"
#include "ui/simple/CSimpleMessageFrameScript.hpp"
#include "ui/simple/CSimpleTop.hpp"
#include "util/CStatus.hpp"
#include <common/XML.hpp>
#include <storm/String.hpp>

int32_t CSimpleMessageFrame::s_metatable;
int32_t CSimpleMessageFrame::s_objectType;

namespace {

uint8_t ToChannel(float value) {
    float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);

    return static_cast<uint8_t>(clamped * 255.0f);
}

} // namespace

MESSAGENODE::~MESSAGENODE() {
    if (this->string) {
        delete this->string;
    }
}

void CSimpleMessageFrame::CreateScriptMetaTable() {
    lua_State* L = FrameScript_GetContext();
    int32_t ref = FrameScript_Object::CreateScriptMetaTable(L, &CSimpleMessageFrame::RegisterScriptMethods);
    CSimpleMessageFrame::s_metatable = ref;
}

int32_t CSimpleMessageFrame::GetObjectType() {
    if (!CSimpleMessageFrame::s_objectType) {
        CSimpleMessageFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CSimpleMessageFrame::s_objectType;
}

void CSimpleMessageFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, SimpleMessageFrameMethods, NUM_SIMPLE_MESSAGE_FRAME_SCRIPT_METHODS);
}

CSimpleMessageFrame::CSimpleMessageFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
    auto m = SMemAlloc(sizeof(CSimpleFontedFrameFont), __FILE__, __LINE__, 0x0);
    this->m_font = new (m) CSimpleFontedFrameFont(this);
}

void CSimpleMessageFrame::AddMessage(const char* text, float r, float g, float b, int32_t id) {
    if (!text || !*text) {
        return;
    }

    // TODO auto stringMem = CDataAllocator::GetData(CSimpleFontString::s_allocator, 0x0, __FILE__, __LINE__);
    auto stringMem = SMemAlloc(sizeof(CSimpleFontString), __FILE__, __LINE__, 0x0);
    auto string = new (stringMem) CSimpleFontString(this, DRAWLAYER_ARTWORK, 1);

    string->SetFontObject(this->m_font);
    string->SetWidth(this->GetWidth());
    string->SetText(text, 1);

    CImVector color;
    color.r = ToChannel(r);
    color.g = ToChannel(g);
    color.b = ToChannel(b);
    color.a = 255;
    string->SetVertexColor(color);

    // Newest at the head; LayoutMessages walks from the head and decides which end of the frame
    // that corresponds to.
    auto node = this->m_messages.NewNode(1, 0, 0x0);
    node->string = string;
    node->age = 0.0f;
    node->id = id;
    node->color = color;

    this->LayoutMessages();
}

int32_t CSimpleMessageFrame::MessageCount() {
    int32_t count = 0;

    for (auto node = this->m_messages.Head(); node; node = this->m_messages.Link(node)->Next()) {
        count++;
    }

    return count;
}

void CSimpleMessageFrame::Clear() {
    this->m_messages.Clear();
}

void CSimpleMessageFrame::FontUpdated(CSimpleFontedFrameFont* font, int32_t a3) {
    for (auto node = this->m_messages.Head(); node; node = this->m_messages.Link(node)->Next()) {
        if (node->string) {
            node->string->SetFontObject(this->m_font);
        }
    }

    this->LayoutMessages();
}

int32_t CSimpleMessageFrame::GetScriptMetaTable() {
    return CSimpleMessageFrame::s_metatable;
}

bool CSimpleMessageFrame::IsA(int32_t type) {
    return type == CSimpleMessageFrame::s_objectType
        || type == CSimpleFrame::s_objectType
        || type == CScriptRegion::s_objectType
        || type == CScriptObject::s_objectType;
}

void CSimpleMessageFrame::LayoutMessages() {
    // The head of the list is the newest message. With INSERT_MODE_BOTTOM the newest sits at the
    // bottom and older ones march upward, which is what the error and raid-warning frames want;
    // with INSERT_MODE_TOP the stack grows the other way.
    bool upward = this->m_insertMode == INSERT_MODE_BOTTOM;

    FRAMEPOINT anchor = upward ? FRAMEPOINT_BOTTOM : FRAMEPOINT_TOP;
    float direction = upward ? 1.0f : -1.0f;
    float offset = 0.0f;

    for (auto node = this->m_messages.Head(); node; node = this->m_messages.Link(node)->Next()) {
        if (!node->string) {
            continue;
        }

        node->string->ClearAllPoints();
        node->string->SetPoint(anchor, this, anchor, 0.0f, offset * direction, 1);

        offset += CSimpleTop::RoundToPixelHeight(node->string->GetHeight());
    }
}

void CSimpleMessageFrame::LoadXML(const XMLNode* node, CStatus* status) {
    CSimpleFrame::LoadXML(node, status);

    const char* fadeAttr = node->GetAttributeByName("fade");
    if (fadeAttr && *fadeAttr) {
        this->m_fade = !SStrCmpI(fadeAttr, "true", STORM_MAX_STR);
    }

    const char* fadeDurationAttr = node->GetAttributeByName("fadeDuration");
    if (fadeDurationAttr && *fadeDurationAttr) {
        this->m_fadeDuration = SStrToFloat(fadeDurationAttr);
    }

    const char* displayDurationAttr = node->GetAttributeByName("displayDuration");
    if (displayDurationAttr && *displayDurationAttr) {
        this->m_displayDuration = SStrToFloat(displayDurationAttr);
    }

    const char* insertModeAttr = node->GetAttributeByName("insertMode");
    if (insertModeAttr && *insertModeAttr) {
        this->m_insertMode = !SStrCmpI(insertModeAttr, "TOP", STORM_MAX_STR)
            ? INSERT_MODE_TOP
            : INSERT_MODE_BOTTOM;
    }

    const char* fontAttr = node->GetAttributeByName("font");
    if (fontAttr && *fontAttr) {
        auto font = CSimpleFont::GetFont(fontAttr, 0);

        if (font) {
            this->m_font->SetFontObject(font);
        } else {
            status->Add(
                STATUS_WARNING,
                "%s %s: Couldn't find font object named %s",
                this->GetObjectTypeName(),
                this->GetDisplayName(),
                fontAttr
            );
        }
    }

    for (XMLNode* child = node->m_child; child; child = child->m_next) {
        if (!SStrCmpI(child->GetName(), "FontString", STORM_MAX_STR)) {
            this->m_font->LoadXML(child, status);
        }
    }
}

void CSimpleMessageFrame::OnLayerUpdate(float elapsedSec) {
    CSimpleFrame::OnLayerUpdate(elapsedSec);

    if (!this->m_messages.Head()) {
        return;
    }

    bool expired = false;

    auto node = this->m_messages.Head();

    while (node) {
        auto next = this->m_messages.Link(node)->Next();

        node->age += elapsedSec;

        if (this->m_fade) {
            float over = node->age - this->m_displayDuration;

            if (over >= this->m_fadeDuration) {
                // Unlinking destroys the node, and with it the font string it owns.
                this->m_messages.UnlinkNode(node);
                delete node;
                expired = true;

                node = next;
                continue;
            }

            if (over > 0.0f && node->string) {
                CImVector faded = node->color;
                faded.a = ToChannel(1.0f - (over / this->m_fadeDuration));
                node->string->SetVertexColor(faded);
            }
        }

        node = next;
    }

    if (expired) {
        this->LayoutMessages();
    }
}
