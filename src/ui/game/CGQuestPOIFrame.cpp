#include "ui/game/CGQuestPOIFrame.hpp"
#include "ui/game/CGQuestPOIFrameScript.hpp"
#include <cmath>

int32_t CGQuestPOIFrame::s_metatable;
int32_t CGQuestPOIFrame::s_objectType;

CSimpleFrame* CGQuestPOIFrame::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator

    return STORM_NEW(CGQuestPOIFrame)(parent);
}

void CGQuestPOIFrame::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGQuestPOIFrame::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGQuestPOIFrame::RegisterScriptMethods);
}

int32_t CGQuestPOIFrame::GetObjectType() {
    if (!CGQuestPOIFrame::s_objectType) {
        CGQuestPOIFrame::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGQuestPOIFrame::s_objectType;
}

bool CGQuestPOIFrame::IsA(int32_t type) {
    return type == CGQuestPOIFrame::GetObjectType()
        || CSimpleFrame::IsA(type);
}

void CGQuestPOIFrame::RegisterScriptMethods(lua_State* L) {
    CSimpleFrame::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGQuestPOIFrameMethods, NUM_CG_QUEST_POI_FRAME_SCRIPT_METHODS);
}

CGQuestPOIFrame::CGQuestPOIFrame(CSimpleFrame* parent) : CSimpleFrame(parent) {
    // TODO
}

int32_t CGQuestPOIFrame::GetScriptMetaTable() {
    return CGQuestPOIFrame::s_metatable;
}

// ref: FUN_0058e5c0
bool QuestPOILineIntersect(const C2Vector& a1, const C2Vector& a2, const C2Vector& b1, const C2Vector& b2, C3Vector& out) {
    float denom = (b1.y - b2.y) * (a1.x - a2.x) - (a1.y - a2.y) * (b1.x - b2.x);

    // 2^-22, the constant at 0x009ea27c.
    if (fabsf(denom) < 2.384185791015625e-07f) {
        return false;
    }

    float crossA = a1.x * a2.y - a2.x * a1.y;
    float crossB = b1.x * b2.y - b2.x * b1.y;
    float inv = 1.0f / denom;

    out.x = (crossA * (b1.x - b2.x) - crossB * (a1.x - a2.x)) * inv;
    out.y = ((b1.y - b2.y) * crossA - (a1.y - a2.y) * crossB) * inv;
    out.z = 0.0f;

    return true;
}
