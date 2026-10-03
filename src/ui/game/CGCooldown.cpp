#include "ui/game/CGCooldown.hpp"
#include "ui/CRenderBatch.hpp"
#include "ui/Util.hpp"
#include "ui/game/CGCooldownScript.hpp"
#include "ui/simple/CSimpleTexture.hpp"
#include "util/CStatus.hpp"
#include <common/Time.hpp>
#include <common/XML.hpp>
#include <cmath>

namespace {

// ref: 0x00ad13e8 -- the sweep fan: eight triangles, each closing on the centre vertex 9.
const uint16_t s_swirlIndices[24] = {
    0, 9, 1,  1, 9, 2,  2, 9, 3,  3, 9, 4,  4, 9, 5,  5, 9, 6,  6, 9, 7,  7, 9, 8,
};

// ref: 0x00ad13dc (flash) and 0x00ad13d0 (edge) -- two identical quads.
const uint16_t s_flashIndices[6] = { 0, 2, 1, 1, 2, 3 };
const uint16_t s_edgeIndices[6] = { 0, 2, 1, 1, 2, 3 };

// ref: 0x00ad1418 -- the flash's texture scale at each sixth of its second.
const float s_flashScale[7] = {
    1.0f, 0.699300706f, 0.540540516f, 0.628930807f, 0.751879692f, 0.625f, 0.869565248f,
};

// ref: 0x00ad1434 -- the flash's alpha at each sixth of its second.
const float s_flashAlpha[7] = { 0.0f, 0.5f, 1.0f, 0.75f, 0.5f, 0.25f, 0.0f };

// ref: 0x00c24254 -- the flash's turn at each sixth. The reference fills entries 1..6 from the
// constants at 0x00a1d214..0x00a1d200 behind a once-guard (0x00c24270); entry 0 is never written
// and stays zero in .bss.
const float s_flashAngle[7] = {
    0.0f, -0.130376101f, -0.259181410f, -0.392699093f, -0.518362820f, -0.651880503f, -0.785398185f,
};

}

int32_t CGCooldown::s_metatable;
int32_t CGCooldown::s_objectType;

CSimpleFrame* CGCooldown::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGCooldown)(parent);
}

// ref: FUN_005ecac0
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

// ref: FUN_005ebe70
bool CGCooldown::IsA(int32_t type) {
    return type == CGCooldown::GetObjectType()
        || CSimpleFrame::IsA(type);
}

// ref: FUN_005ebe10
void CGCooldown::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGCooldownMethods, NUM_CG_COOLDOWN_SCRIPT_METHODS);
}

// ref: FUN_005ecae0
CGCooldown::CGCooldown(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // Every field is zeroed by its initializer, as the reference zeroes them here.
}

// ref: FUN_005eb040
CGCooldown::~CGCooldown() {
    if (this->m_starTexture) {
        HandleClose(this->m_starTexture);
    }

    if (this->m_edgeTexture) {
        HandleClose(this->m_edgeTexture);
    }

    this->m_starTexture = nullptr;
    this->m_edgeTexture = nullptr;
}

// ref: FUN_005eb0b0
int32_t CGCooldown::GetScriptMetaTable() {
    return CGCooldown::s_metatable;
}

// ref: FUN_005eb0c0
void CGCooldown::LoadXML(const XMLNode* node, CStatus* status) {
    CSimpleFrame::LoadXML(node, status);

    auto reverse = node->GetAttributeByName("reverse");
    if (reverse && *reverse) {
        this->m_reverse = StringToBOOL(reverse);
    }

    auto drawEdge = node->GetAttributeByName("drawEdge");
    if (drawEdge && *drawEdge) {
        this->m_drawEdge = StringToBOOL(drawEdge);
    }
}

// ref: FUN_005eb130
// The sweep's first quarter turn: from twelve o'clock to three.
void CGCooldown::UpdateSegment01(int32_t segment, float fraction) {
    float f1, f2;
    f1 = (this->m_rect.minX + this->m_rect.maxX) * 0.5f;
    f2 = (this->m_rect.maxY + this->m_rect.minY) * 0.5f;
    if (this->m_reverse == 0) {
        if (segment == 0) {
            this->m_swirlPos[0].x = (this->m_rect.maxX - f1) * fraction + f1;
            this->m_swirlPos[0].y = this->m_rect.maxY;
            this->m_swirlPos[1].x = this->m_rect.maxX;
            this->m_swirlPos[1].y = this->m_rect.maxY;
        }
        else {
            this->m_swirlPos[1].y = this->m_rect.maxY - (this->m_rect.maxY - f2) * fraction;
            this->m_swirlPos[1].x = this->m_rect.maxX;
            this->m_swirlPos[0].x = this->m_swirlPos[1].x;
            this->m_swirlPos[0].y = this->m_swirlPos[1].y;
            this->m_swirlPos[0].z = this->m_swirlPos[1].z;
        }
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[2].y = f2;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[3].y = this->m_rect.minY;
        this->m_swirlPos[4].x = f1;
        this->m_swirlPos[4].y = this->m_rect.minY;
        this->m_swirlPos[5].x = this->m_rect.minX;
        this->m_swirlPos[5].y = this->m_rect.minY;
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[6].y = f2;
        this->m_swirlPos[7].x = this->m_rect.minX;
        this->m_swirlPos[7].y = this->m_rect.maxY;
        return;
    }
    if (segment != 0) {
        this->m_swirlPos[2].y = this->m_rect.maxY - (this->m_rect.maxY - f2) * fraction;
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[1].x = this->m_rect.maxX;
        this->m_swirlPos[1].y = this->m_rect.maxY;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[3].y = this->m_swirlPos[1].y;
        this->m_swirlPos[4].x = this->m_rect.maxX;
        this->m_swirlPos[4].y = this->m_swirlPos[1].y;
        this->m_swirlPos[5].x = this->m_rect.maxX;
        this->m_swirlPos[5].y = this->m_swirlPos[1].y;
        this->m_swirlPos[6].x = this->m_rect.maxX;
        this->m_swirlPos[6].y = this->m_swirlPos[1].y;
        this->m_swirlPos[7].x = this->m_rect.maxX;
        this->m_swirlPos[7].y = this->m_swirlPos[1].y;
        this->m_swirlPos[8].x = this->m_rect.maxX;
        this->m_swirlPos[8].y = this->m_swirlPos[1].y;
        return;
    }
    f1 = (this->m_rect.maxX - f1) * fraction + f1;
    this->m_swirlPos[1].x = f1;
    this->m_swirlPos[1].y = this->m_rect.maxY;
    this->m_swirlPos[2].x = f1;
    this->m_swirlPos[2].y = this->m_rect.maxY;
    this->m_swirlPos[3].x = this->m_swirlPos[1].x;
    this->m_swirlPos[3].y = this->m_rect.maxY;
    this->m_swirlPos[4].x = this->m_swirlPos[1].x;
    this->m_swirlPos[4].y = this->m_rect.maxY;
    this->m_swirlPos[5].x = this->m_swirlPos[1].x;
    this->m_swirlPos[5].y = this->m_rect.maxY;
    this->m_swirlPos[6].x = this->m_swirlPos[1].x;
    this->m_swirlPos[6].y = this->m_rect.maxY;
    this->m_swirlPos[7].x = this->m_swirlPos[1].x;
    this->m_swirlPos[7].y = this->m_rect.maxY;
    this->m_swirlPos[8].x = this->m_swirlPos[1].x;
    this->m_swirlPos[8].y = this->m_rect.maxY;
    return;
}

// ref: FUN_005eb3a0
// Three o'clock to six.
void CGCooldown::UpdateSegment23(int32_t segment, float fraction) {
    float f1, f2;
    f1 = (this->m_rect.minX + this->m_rect.maxX) * 0.5f;
    f2 = (this->m_rect.maxY + this->m_rect.minY) * 0.5f;
    if (this->m_reverse == 0) {
        if (segment < 3) {
            this->m_swirlPos[2].y = f2 - (f2 - this->m_rect.minY) * fraction;
            this->m_swirlPos[2].x = this->m_rect.maxX;
            this->m_swirlPos[0].x = this->m_swirlPos[2].x;
            this->m_swirlPos[0].y = this->m_swirlPos[2].y;
            this->m_swirlPos[0].z = this->m_swirlPos[2].z;
            this->m_swirlPos[1].x = this->m_swirlPos[2].x;
            this->m_swirlPos[1].y = this->m_swirlPos[2].y;
            this->m_swirlPos[1].z = this->m_swirlPos[2].z;
            this->m_swirlPos[3].x = this->m_rect.maxX;
            this->m_swirlPos[3].y = this->m_rect.minY;
        }
        else {
            this->m_swirlPos[3].x = this->m_rect.maxX - (this->m_rect.maxX - f1) * fraction;
            this->m_swirlPos[3].y = this->m_rect.minY;
            this->m_swirlPos[0].x = this->m_swirlPos[3].x;
            this->m_swirlPos[0].y = this->m_swirlPos[3].y;
            this->m_swirlPos[0].z = this->m_swirlPos[3].z;
            this->m_swirlPos[1].x = this->m_swirlPos[3].x;
            this->m_swirlPos[1].y = this->m_swirlPos[3].y;
            this->m_swirlPos[1].z = this->m_swirlPos[3].z;
            this->m_swirlPos[2].x = this->m_swirlPos[3].x;
            this->m_swirlPos[2].y = this->m_swirlPos[3].y;
            this->m_swirlPos[2].z = this->m_swirlPos[3].z;
        }
        this->m_swirlPos[4].x = f1;
        this->m_swirlPos[4].y = this->m_rect.minY;
        this->m_swirlPos[5].x = this->m_rect.minX;
        this->m_swirlPos[5].y = this->m_rect.minY;
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[6].y = f2;
        this->m_swirlPos[7].x = this->m_rect.minX;
        this->m_swirlPos[7].y = this->m_rect.maxY;
        return;
    }
    if (segment < 3) {
        this->m_swirlPos[3].y = f2 - (f2 - this->m_rect.minY) * fraction;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[1].x = this->m_rect.maxX;
        this->m_swirlPos[1].y = this->m_rect.maxY;
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[2].y = f2;
        this->m_swirlPos[4].x = this->m_rect.maxX;
        this->m_swirlPos[4].y = this->m_swirlPos[3].y;
        this->m_swirlPos[5].x = this->m_rect.maxX;
        this->m_swirlPos[5].y = this->m_swirlPos[3].y;
        this->m_swirlPos[6].x = this->m_rect.maxX;
        this->m_swirlPos[6].y = this->m_swirlPos[3].y;
        this->m_swirlPos[7].x = this->m_rect.maxX;
        this->m_swirlPos[7].y = this->m_swirlPos[3].y;
        this->m_swirlPos[8].x = this->m_rect.maxX;
        this->m_swirlPos[8].y = this->m_swirlPos[3].y;
        return;
    }
    f1 = this->m_rect.maxX - (this->m_rect.maxX - f1) * fraction;
    this->m_swirlPos[4].x = f1;
    this->m_swirlPos[4].y = this->m_rect.minY;
    this->m_swirlPos[1].x = this->m_rect.maxX;
    this->m_swirlPos[1].y = this->m_rect.maxY;
    this->m_swirlPos[2].x = this->m_rect.maxX;
    this->m_swirlPos[2].y = f2;
    this->m_swirlPos[3].x = this->m_rect.maxX;
    this->m_swirlPos[3].y = this->m_rect.minY;
    this->m_swirlPos[5].x = f1;
    this->m_swirlPos[5].y = this->m_rect.minY;
    this->m_swirlPos[6].x = this->m_swirlPos[4].x;
    this->m_swirlPos[6].y = this->m_rect.minY;
    this->m_swirlPos[7].x = this->m_swirlPos[4].x;
    this->m_swirlPos[7].y = this->m_rect.minY;
    this->m_swirlPos[8].x = this->m_swirlPos[4].x;
    this->m_swirlPos[8].y = this->m_rect.minY;
    return;
}

// ref: FUN_005eb670
// Six o'clock to nine.
void CGCooldown::UpdateSegment45(int32_t segment, float fraction) {
    float f1, f2, f3;
    f1 = (this->m_rect.minX + this->m_rect.maxX) * 0.5f;
    f3 = (this->m_rect.maxY + this->m_rect.minY) * 0.5f;
    if (this->m_reverse == 0) {
        if (segment < 5) {
            this->m_swirlPos[4].x = f1 - (f1 - this->m_rect.minX) * fraction;
            this->m_swirlPos[4].y = this->m_rect.minY;
            this->m_swirlPos[0].x = this->m_swirlPos[4].x;
            this->m_swirlPos[0].y = this->m_swirlPos[4].y;
            this->m_swirlPos[0].z = this->m_swirlPos[4].z;
            this->m_swirlPos[1].x = this->m_swirlPos[4].x;
            this->m_swirlPos[1].y = this->m_swirlPos[4].y;
            this->m_swirlPos[1].z = this->m_swirlPos[4].z;
            this->m_swirlPos[2].x = this->m_swirlPos[4].x;
            this->m_swirlPos[2].y = this->m_swirlPos[4].y;
            this->m_swirlPos[2].z = this->m_swirlPos[4].z;
            this->m_swirlPos[3].x = this->m_swirlPos[4].x;
            this->m_swirlPos[3].y = this->m_swirlPos[4].y;
            this->m_swirlPos[3].z = this->m_swirlPos[4].z;
            this->m_swirlPos[5].x = this->m_rect.minX;
            this->m_swirlPos[5].y = this->m_rect.minY;
        }
        else {
            this->m_swirlPos[5].y = (f3 - this->m_rect.minY) * fraction + this->m_rect.minY;
            this->m_swirlPos[5].x = this->m_rect.minX;
            this->m_swirlPos[0].x = this->m_swirlPos[5].x;
            this->m_swirlPos[0].y = this->m_swirlPos[5].y;
            this->m_swirlPos[0].z = this->m_swirlPos[5].z;
            this->m_swirlPos[1].x = this->m_swirlPos[5].x;
            this->m_swirlPos[1].y = this->m_swirlPos[5].y;
            this->m_swirlPos[1].z = this->m_swirlPos[5].z;
            this->m_swirlPos[2].x = this->m_swirlPos[5].x;
            this->m_swirlPos[2].y = this->m_swirlPos[5].y;
            this->m_swirlPos[2].z = this->m_swirlPos[5].z;
            this->m_swirlPos[3].x = this->m_swirlPos[5].x;
            this->m_swirlPos[3].y = this->m_swirlPos[5].y;
            this->m_swirlPos[3].z = this->m_swirlPos[5].z;
            this->m_swirlPos[4].x = this->m_swirlPos[5].x;
            this->m_swirlPos[4].y = this->m_swirlPos[5].y;
            this->m_swirlPos[4].z = this->m_swirlPos[5].z;
        }
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[6].y = f3;
        this->m_swirlPos[7].x = this->m_rect.minX;
        this->m_swirlPos[7].y = this->m_rect.maxY;
        return;
    }
    if (segment < 5) {
        f2 = f1 - (f1 - this->m_rect.minX) * fraction;
        this->m_swirlPos[5].x = f2;
        this->m_swirlPos[5].y = this->m_rect.minY;
        this->m_swirlPos[1].x = this->m_rect.maxX;
        this->m_swirlPos[1].y = this->m_rect.maxY;
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[2].y = f3;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[3].y = this->m_rect.minY;
        this->m_swirlPos[4].x = f1;
        this->m_swirlPos[4].y = this->m_rect.minY;
        this->m_swirlPos[6].x = f2;
        this->m_swirlPos[6].y = this->m_rect.minY;
        this->m_swirlPos[7].x = this->m_swirlPos[5].x;
        this->m_swirlPos[7].y = this->m_rect.minY;
        this->m_swirlPos[8].x = this->m_swirlPos[5].x;
        this->m_swirlPos[8].y = this->m_rect.minY;
        return;
    }
    this->m_swirlPos[6].y = (f3 - this->m_rect.minY) * fraction + this->m_rect.minY;
    this->m_swirlPos[6].x = this->m_rect.minX;
    this->m_swirlPos[1].x = this->m_rect.maxX;
    this->m_swirlPos[1].y = this->m_rect.maxY;
    this->m_swirlPos[2].x = this->m_rect.maxX;
    this->m_swirlPos[2].y = f3;
    this->m_swirlPos[3].x = this->m_rect.maxX;
    this->m_swirlPos[3].y = this->m_rect.minY;
    this->m_swirlPos[4].x = f1;
    this->m_swirlPos[4].y = this->m_rect.minY;
    this->m_swirlPos[5].x = this->m_rect.minX;
    this->m_swirlPos[5].y = this->m_rect.minY;
    this->m_swirlPos[7].x = this->m_rect.minX;
    this->m_swirlPos[7].y = this->m_swirlPos[6].y;
    this->m_swirlPos[8].x = this->m_rect.minX;
    this->m_swirlPos[8].y = this->m_swirlPos[6].y;
    return;
}

// ref: FUN_005eb9a0
// Nine o'clock back to twelve.
void CGCooldown::UpdateSegment67(int32_t segment, float fraction) {
    float f1, f2;
    f1 = (this->m_rect.minX + this->m_rect.maxX) * 0.5f;
    f2 = (this->m_rect.maxY + this->m_rect.minY) * 0.5f;
    if (this->m_reverse == 0) {
        if (6 < segment) {
            this->m_swirlPos[7].x = (f1 - this->m_rect.minX) * fraction + this->m_rect.minX;
            this->m_swirlPos[7].y = this->m_rect.maxY;
            this->m_swirlPos[0].x = this->m_swirlPos[7].x;
            this->m_swirlPos[0].y = this->m_swirlPos[7].y;
            this->m_swirlPos[0].z = this->m_swirlPos[7].z;
            this->m_swirlPos[1].x = this->m_swirlPos[7].x;
            this->m_swirlPos[1].y = this->m_swirlPos[7].y;
            this->m_swirlPos[1].z = this->m_swirlPos[7].z;
            this->m_swirlPos[2].x = this->m_swirlPos[7].x;
            this->m_swirlPos[2].y = this->m_swirlPos[7].y;
            this->m_swirlPos[2].z = this->m_swirlPos[7].z;
            this->m_swirlPos[3].x = this->m_swirlPos[7].x;
            this->m_swirlPos[3].y = this->m_swirlPos[7].y;
            this->m_swirlPos[3].z = this->m_swirlPos[7].z;
            this->m_swirlPos[4].x = this->m_swirlPos[7].x;
            this->m_swirlPos[4].y = this->m_swirlPos[7].y;
            this->m_swirlPos[4].z = this->m_swirlPos[7].z;
            this->m_swirlPos[5].x = this->m_swirlPos[7].x;
            this->m_swirlPos[5].y = this->m_swirlPos[7].y;
            this->m_swirlPos[5].z = this->m_swirlPos[7].z;
            this->m_swirlPos[6].x = this->m_swirlPos[7].x;
            this->m_swirlPos[6].y = this->m_swirlPos[7].y;
            this->m_swirlPos[6].z = this->m_swirlPos[7].z;
            return;
        }
        this->m_swirlPos[6].y = (this->m_rect.maxY - f2) * fraction + f2;
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[0].x = this->m_swirlPos[6].x;
        this->m_swirlPos[0].y = this->m_swirlPos[6].y;
        this->m_swirlPos[0].z = this->m_swirlPos[6].z;
        this->m_swirlPos[1].x = this->m_swirlPos[6].x;
        this->m_swirlPos[1].y = this->m_swirlPos[6].y;
        this->m_swirlPos[1].z = this->m_swirlPos[6].z;
        this->m_swirlPos[2].x = this->m_swirlPos[6].x;
        this->m_swirlPos[2].y = this->m_swirlPos[6].y;
        this->m_swirlPos[2].z = this->m_swirlPos[6].z;
        this->m_swirlPos[3].x = this->m_swirlPos[6].x;
        this->m_swirlPos[3].y = this->m_swirlPos[6].y;
        this->m_swirlPos[3].z = this->m_swirlPos[6].z;
        this->m_swirlPos[4].x = this->m_swirlPos[6].x;
        this->m_swirlPos[4].y = this->m_swirlPos[6].y;
        this->m_swirlPos[4].z = this->m_swirlPos[6].z;
        this->m_swirlPos[5].x = this->m_swirlPos[6].x;
        this->m_swirlPos[5].y = this->m_swirlPos[6].y;
        this->m_swirlPos[5].z = this->m_swirlPos[6].z;
    }
    else {
        if (segment < 7) {
            this->m_swirlPos[7].y = (this->m_rect.maxY - f2) * fraction + f2;
            this->m_swirlPos[7].x = this->m_rect.minX;
            this->m_swirlPos[1].x = this->m_rect.maxX;
            this->m_swirlPos[1].y = this->m_rect.maxY;
            this->m_swirlPos[2].x = this->m_rect.maxX;
            this->m_swirlPos[2].y = f2;
            this->m_swirlPos[3].x = this->m_rect.maxX;
            this->m_swirlPos[3].y = this->m_rect.minY;
            this->m_swirlPos[4].x = f1;
            this->m_swirlPos[4].y = this->m_rect.minY;
            this->m_swirlPos[5].x = this->m_rect.minX;
            this->m_swirlPos[5].y = this->m_rect.minY;
            this->m_swirlPos[6].x = this->m_rect.minX;
            this->m_swirlPos[6].y = f2;
            this->m_swirlPos[8].x = this->m_rect.minX;
            this->m_swirlPos[8].y = this->m_swirlPos[7].y;
            return;
        }
        this->m_swirlPos[8].y = this->m_rect.maxY;
        this->m_swirlPos[8].x = (f1 - this->m_rect.minX) * fraction + this->m_rect.minX;
        this->m_swirlPos[1].x = this->m_rect.maxX;
        this->m_swirlPos[1].y = this->m_rect.maxY;
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[2].y = f2;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[3].y = this->m_rect.minY;
        this->m_swirlPos[4].x = f1;
        this->m_swirlPos[4].y = this->m_rect.minY;
        this->m_swirlPos[5].x = this->m_rect.minX;
        this->m_swirlPos[5].y = this->m_rect.minY;
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[6].y = f2;
    }
    this->m_swirlPos[7].x = this->m_rect.minX;
    this->m_swirlPos[7].y = this->m_rect.maxY;
    return;
}

// ref: FUN_005ebd20
void CGCooldown::OnFrameRender(CRenderBatch* batch, uint32_t layer) {
    CSimpleFrame::OnFrameRender(batch, layer);

    auto shader = CSimpleTexture::GetImageModePixelShader(ImageMode_UI);

    if (layer == DRAWLAYER_ARTWORK) {
        if (this->m_starTexture) {
            batch->Queue(
                TextureGetTexturePtr(this->m_starTexture),
                GxBlend_Alpha,
                10,
                this->m_swirlPos,
                this->m_swirlTexCoord,
                0,
                &this->m_color37c,
                24,
                s_swirlIndices,
                shader
            );

            batch->Queue(
                TextureGetTexturePtr(this->m_starTexture),
                GxBlend_Add,
                4,
                this->m_flashPos,
                this->m_flashTexCoord,
                0,
                &this->m_color3d0,
                6,
                s_flashIndices,
                shader
            );
        }
    } else if (layer == DRAWLAYER_ARTWORK_OVERLAY && this->m_edgeTexture && this->m_drawEdge) {
        batch->Queue(
            TextureGetTexturePtr(this->m_edgeTexture),
            GxBlend_Opaque,
            4,
            this->m_edgePos,
            this->m_edgeTexCoord,
            0,
            &this->m_color42c,
            6,
            s_edgeIndices,
            shader
        );
    }
}

// ref: FUN_005ebf20
// Back to a full, unturned square: every fan vertex on the origin, the flash and edge quads on
// the frame's corners.
void CGCooldown::ResetGeometry() {
    this->m_swirlPos[0].x = 0.0f;
    this->m_swirlPos[0].y = 0.0f;
    this->m_swirlPos[0].z = 1.0f;
    this->m_swirlPos[1].x = 0.0f;
    this->m_swirlPos[1].y = 0.0f;
    this->m_swirlPos[1].z = 1.0f;
    this->m_swirlPos[2].x = 0.0f;
    this->m_swirlPos[2].y = 0.0f;
    this->m_swirlPos[2].z = 1.0f;
    this->m_swirlPos[3].x = 0.0f;
    this->m_swirlPos[3].y = 0.0f;
    this->m_swirlPos[3].z = 1.0f;
    this->m_swirlPos[4].x = 0.0f;
    this->m_swirlPos[4].y = 0.0f;
    this->m_swirlPos[4].z = 1.0f;
    this->m_swirlPos[5].x = 0.0f;
    this->m_swirlPos[5].y = 0.0f;
    this->m_swirlPos[6].x = 0.0f;
    this->m_swirlPos[5].z = 1.0f;
    this->m_swirlPos[6].y = 0.0f;
    this->m_swirlPos[6].z = 1.0f;
    this->m_swirlPos[7].x = 0.0f;
    this->m_swirlPos[7].y = 0.0f;
    this->m_swirlPos[8].x = 0.0f;
    this->m_swirlPos[7].z = 1.0f;
    this->m_swirlPos[8].y = 0.0f;
    this->m_swirlPos[8].z = 1.0f;
    this->m_flashPos[0].x = this->m_rect.minX;
    this->m_flashPos[0].y = this->m_rect.maxY;
    this->m_flashPos[0].z = 1.0f;
    this->m_flashTexCoord[0].x = 0.0f;
    this->m_flashTexCoord[0].y = 0.0f;
    this->m_flashPos[1].x = this->m_rect.maxX;
    this->m_flashPos[1].y = this->m_rect.maxY;
    this->m_flashPos[1].z = 1.0f;
    this->m_flashTexCoord[1].x = 1.0f;
    this->m_flashTexCoord[1].y = 0.0f;
    this->m_flashPos[2].x = this->m_rect.minX;
    this->m_flashPos[2].y = this->m_rect.minY;
    this->m_flashPos[2].z = 1.0f;
    this->m_flashTexCoord[2].y = 1.0f;
    this->m_flashTexCoord[2].x = 0.0f;
    this->m_flashPos[3].x = this->m_rect.maxX;
    this->m_flashPos[3].y = this->m_rect.minY;
    this->m_flashPos[3].z = 1.0f;
    this->m_flashTexCoord[3].x = 1.0f;
    this->m_flashTexCoord[3].y = 1.0f;
    if (this->m_drawEdge != 0) {
        this->m_edgePos[0].x = this->m_rect.minX;
        this->m_edgePos[0].y = this->m_rect.maxY;
        this->m_edgePos[0].z = 1.0f;
        this->m_edgeTexCoord[0].x = 0.0f;
        this->m_edgeTexCoord[0].y = 0.0f;
        this->m_edgePos[1].x = this->m_rect.maxX;
        this->m_edgePos[1].y = this->m_rect.maxY;
        this->m_edgePos[1].z = 1.0f;
        this->m_edgeTexCoord[1].x = 1.0f;
        this->m_edgeTexCoord[1].y = 0.0f;
        this->m_edgePos[2].x = this->m_rect.minX;
        this->m_edgePos[2].y = this->m_rect.minY;
        this->m_edgePos[2].z = 1.0f;
        this->m_edgeTexCoord[2].y = 1.0f;
        this->m_edgeTexCoord[2].x = 0.0f;
        this->m_edgePos[3].x = this->m_rect.maxX;
        this->m_edgePos[3].y = this->m_rect.minY;
        this->m_edgePos[3].z = 1.0f;
        this->m_edgeTexCoord[3].x = 1.0f;
        this->m_edgeTexCoord[3].y = 1.0f;
        return;
    }
    return;
}

// ref: FUN_005ec1e0
// Lay the sweep out for m_elapsed: which eighth of the turn it is in and how far through it,
// the edge's texture turned to match -- or, while the flash plays, the flash's turn, size and
// alpha.
void CGCooldown::UpdateGeometry() {
    if (this->m_flashing == 0) {
        float elapsed = static_cast<float>(this->m_elapsed);
        float duration = static_cast<float>(this->m_duration);
        float segmentTime = duration * 0.125f;
        float invSegmentTime = 1.0f / segmentTime;
        int32_t segment = static_cast<int32_t>(invSegmentTime * elapsed);
        float fraction = (elapsed - static_cast<float>(segment) * segmentTime) * invSegmentTime;

        float centerX = (this->m_rect.maxX + this->m_rect.minX) * 0.5f;
        float centerY = (this->m_rect.minY + this->m_rect.maxY) * 0.5f;

        // The fan's twelve o'clock vertex: the first one when running backwards, the last
        // otherwise.
        if (this->m_reverse) {
            this->m_swirlPos[0] = { centerX, this->m_rect.maxY, 1.0f };
            this->m_swirlTexCoord[0] = { 0.5f, 0.0f };
        } else {
            this->m_swirlPos[8] = { centerX, this->m_rect.maxY, 1.0f };
            this->m_swirlTexCoord[8] = { 0.5f, 0.0f };
        }

        this->m_swirlPos[9] = { centerX, centerY, 1.0f };
        this->m_swirlTexCoord[9] = { 0.5f, 0.5f };

        if (segment < 4) {
            if (segment < 2) {
                this->UpdateSegment01(segment, fraction);
            } else {
                this->UpdateSegment23(segment, fraction);
            }
        } else if (segment < 6) {
            this->UpdateSegment45(segment, fraction);
        } else {
            this->UpdateSegment67(segment, fraction);
        }

        if (this->m_drawEdge) {
            float angle = (elapsed / duration) * -6.28318548f;
            float c = std::cos(angle);
            float s = std::sin(angle);
            float scale = this->m_layoutScale;

            float cn = c * -0.5f;
            float sn = s * -0.5f;
            float ch = c * 0.5f;
            float sh = s * 0.5f;

            this->m_edgeTexCoord[0].x = (cn - sn) * scale + 0.5f;
            this->m_edgeTexCoord[0].y = (sn + cn) * scale + 0.5f;
            this->m_edgeTexCoord[1].x = (ch - sn) * scale + 0.5f;
            this->m_edgeTexCoord[1].y = (sh + cn) * scale + 0.5f;
            this->m_edgeTexCoord[2].x = (cn - sh) * scale + 0.5f;
            this->m_edgeTexCoord[2].y = (ch + sn) * scale + 0.5f;
            this->m_edgeTexCoord[3].x = (ch - sh) * scale + 0.5f;
            this->m_edgeTexCoord[3].y = 0.5f + (sh + ch) * scale;
        }

        return;
    }

    if (this->m_reverse) {
        // Running backwards, the finish shows the whole square dark instead of a flash.
        float centerX = (this->m_rect.minX + this->m_rect.maxX) * 0.5f;
        float centerY = (this->m_rect.maxY + this->m_rect.minY) * 0.5f;

        this->m_swirlPos[0] = { centerX, this->m_rect.maxY, 1.0f };
        this->m_swirlTexCoord[0] = { 0.5f, 0.0f };
        this->m_swirlPos[1].x = this->m_rect.maxX;
        this->m_swirlPos[1].y = this->m_rect.maxY;
        this->m_swirlPos[2].x = this->m_rect.maxX;
        this->m_swirlPos[2].y = centerY;
        this->m_swirlPos[3].x = this->m_rect.maxX;
        this->m_swirlPos[3].y = this->m_rect.minY;
        this->m_swirlPos[4].x = centerX;
        this->m_swirlPos[4].y = this->m_rect.minY;
        this->m_swirlPos[5].x = this->m_rect.minX;
        this->m_swirlPos[5].y = this->m_rect.minY;
        this->m_swirlPos[6].x = this->m_rect.minX;
        this->m_swirlPos[6].y = centerY;
        this->m_swirlPos[7].x = this->m_rect.minX;
        this->m_swirlPos[7].y = this->m_rect.maxY;
        this->m_swirlPos[8].x = centerX;
        this->m_swirlPos[8].y = this->m_rect.maxY;
        this->m_swirlPos[9] = { centerX, centerY, 1.0f };
        this->m_swirlTexCoord[9] = { 0.5f, 0.5f };

        return;
    }

    float elapsed = static_cast<float>(this->m_elapsed);
    int32_t step = static_cast<int32_t>(elapsed * 0.00599999959f);
    float t = (elapsed - static_cast<float>(step) * 166.666672f) * 0.00599999959f;

    uint32_t alpha = ((static_cast<uint32_t>(this->alphaBD) * this->m_alpha) / 0xff) & 0xff;
    float flashAlpha = ((s_flashAlpha[step + 1] - s_flashAlpha[step]) * t + s_flashAlpha[step]) * static_cast<float>(alpha);
    this->m_color3d0.a = static_cast<uint8_t>(static_cast<int32_t>(flashAlpha));

    float angle = (s_flashAngle[step + 1] - s_flashAngle[step]) * t + s_flashAngle[step];
    float c = std::cos(angle);
    float s = std::sin(angle);

    float cn = c * -0.5f;
    float sn = s * -0.5f;
    float ch = c * 0.5f;
    float sh = s * 0.5f;

    this->m_flashTexCoord[0].x = cn - sn;
    this->m_flashTexCoord[0].y = sn + cn;
    this->m_flashTexCoord[1].x = ch - sn;
    this->m_flashTexCoord[1].y = sh + cn;
    this->m_flashTexCoord[2].x = cn - sh;
    this->m_flashTexCoord[2].y = ch + sn;
    this->m_flashTexCoord[3].x = ch - sh;
    this->m_flashTexCoord[3].y = sh + ch;

    float scale = (s_flashScale[step + 1] - s_flashScale[step]) * t + s_flashScale[step];

    for (auto& texCoord : this->m_flashTexCoord) {
        texCoord.x = texCoord.x * scale + 0.5f;
        texCoord.y = texCoord.y * scale + 0.5f;
    }
}

// ref: FUN_005ec8c0
void CGCooldown::UpdateAlpha() {
    CSimpleFrame::UpdateAlpha();
    this->UpdateColors();
}

// ref: FUN_005ec8d0
void CGCooldown::OnFrameSizeChanged(const CRect& rect) {
    CSimpleFrame::OnFrameSizeChanged(rect);
    this->ResetGeometry();
}

// ref: FUN_005ec8f0
void CGCooldown::OnLayerUpdate(float elapsedSec) {
    CSimpleFrame::OnLayerUpdate(elapsedSec);

    uint32_t elapsed = static_cast<uint32_t>(OsGetAsyncTimeMs()) - this->m_start;
    this->m_elapsed = elapsed;

    if (this->m_duration <= elapsed) {
        if (this->m_flashing == 0) {
            if (this->m_reverse == 0) {
                // The sweep is done: play the one-second finish flash.
                this->m_start = static_cast<uint32_t>(OsGetAsyncTimeMs());
                this->m_flashing = 1;
                this->m_duration = 1000;
                this->m_elapsed = 0;
                this->UpdateColors();
                this->UpdateGeometry();

                return;
            }

            this->m_elapsed = this->m_duration - 1;
            this->UpdateGeometry();

            return;
        }

        this->m_flashing = 0;
        this->Hide();
    }

    this->UpdateGeometry();
}

// ref: FUN_005ec790
void CGCooldown::UpdateColors() {
    uint32_t alpha = (static_cast<uint32_t>(this->alphaBD) * this->m_alpha) / 0xff;

    if (this->m_flashing == 0) {
        this->m_color37c.value = ((alpha & 0xff) * 0xa0) / 0xff << 24;
        this->m_color42c.value = static_cast<uint32_t>(static_cast<uint8_t>(alpha)) << 24 | 0xffffff;
        this->m_color3d0.value = 0;

        return;
    }

    this->m_color37c.value = 0;
    this->m_color42c.value = 0;
    this->m_color3d0.value = static_cast<uint32_t>(static_cast<uint8_t>(alpha)) << 24 | 0x50a0ff;
}

// ref: FUN_005ecd70
void CGCooldown::SetCooldown(uint32_t start, uint32_t duration) {
    this->m_start = start;
    this->m_duration = duration;
    this->m_elapsed = 0;
    this->m_flashing = 0;

    if (!this->m_starTexture) {
        CStatus status;
        this->m_starTexture = TextureCreate("interface\\cooldown\\star4.blp", CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 3);
    }

    if (!this->m_edgeTexture) {
        CStatus status;
        this->m_edgeTexture = TextureCreate("interface\\cooldown\\edge.blp", CGxTexFlags(GxTex_Linear, 0, 0, 0, 0, 0, 1), &status, 3);
    }

    this->UpdateColors();
    this->ResetGeometry();
    this->Show();
}
