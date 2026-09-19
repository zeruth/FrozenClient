#include "ui/game/CGQuestPOIFrameScript.hpp"
#include "util/Lua.hpp"
#include "ui/game/CGQuestPOIFrame.hpp"
#include "ui/FrameScript.hpp"
#include "util/Unimplemented.hpp"

namespace {

int32_t CGQuestPOIFrame_SetFillTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// Every one of these clamps before storing, and the bounds are the reference's own. A caller
// handing in nonsense gets the nearest legal value rather than an error.
static float ClampPOI(float value, float low, float high) {
    return value < low ? low : (value > high ? high : value);
}

// ref: FUN_0058e660
int32_t CGQuestPOIFrame_SetFillAlpha(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    // 0 to 255, stored as a byte: the interface passes these in byte range, not 0..1.
    frame->m_fillAlpha = static_cast<uint8_t>(
        ClampPOI(static_cast<float>(lua_tonumber(L, 2)), 0.0f, 255.0f) + 0.5f);

    return 0;
}

int32_t CGQuestPOIFrame_SetBorderTexture(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0058e6f0
int32_t CGQuestPOIFrame_SetBorderAlpha(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    frame->m_borderAlpha = static_cast<uint8_t>(
        ClampPOI(static_cast<float>(lua_tonumber(L, 2)), 0.0f, 255.0f) + 0.5f);

    return 0;
}

// ref: FUN_0058e780
int32_t CGQuestPOIFrame_SetBorderScalar(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    frame->m_borderScalar = ClampPOI(static_cast<float>(lua_tonumber(L, 2)), 0.0f, 10.0f);

    return 0;
}

int32_t CGQuestPOIFrame_DrawQuestBlob(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0058e800
int32_t CGQuestPOIFrame_EnableSmoothing(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    frame->m_smoothing = lua_toboolean(L, 2) != 0;

    return 0;
}

// ref: FUN_0058e850
int32_t CGQuestPOIFrame_EnableMerging(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    frame->m_merging = lua_toboolean(L, 2) != 0;

    return 0;
}

// ref: FUN_0058e8a0
int32_t CGQuestPOIFrame_SetMergeThreshold(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    // Neither bound is zero: below a tenth or above a half the reference refuses to go.
    frame->m_mergeThreshold = ClampPOI(static_cast<float>(lua_tonumber(L, 2)), 0.1f, 0.5f);

    return 0;
}

// ref: FUN_0058e920
int32_t CGQuestPOIFrame_SetNumSplinePoints(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    // Eight to thirty, and thirty is also what a non-number falls back to.
    auto points = static_cast<int32_t>(lua_tointeger(L, 2));

    frame->m_numSplinePoints = points < 8 ? 8 : (points > 30 ? 30 : points);

    return 0;
}

int32_t CGQuestPOIFrame_UpdateQuestPOI(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGQuestPOIFrame_UpdateMouseOverTooltip(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

int32_t CGQuestPOIFrame_GetTooltipIndex(lua_State* L) {
    WHOA_UNIMPLEMENTED(0);
}

// ref: FUN_0058eac0
int32_t CGQuestPOIFrame_GetNumTooltips(lua_State* L) {
    auto type = CGQuestPOIFrame::GetObjectType();
    auto frame = static_cast<CGQuestPOIFrame*>(FrameScript_GetObjectThis(L, type));

    lua_pushinteger(L, frame->m_numTooltips);

    return 1;
}

}

FrameScript_Method CGQuestPOIFrameMethods[] = {
    { "SetFillTexture",         &CGQuestPOIFrame_SetFillTexture },
    { "SetFillAlpha",           &CGQuestPOIFrame_SetFillAlpha },
    { "SetBorderTexture",       &CGQuestPOIFrame_SetBorderTexture },
    { "SetBorderAlpha",         &CGQuestPOIFrame_SetBorderAlpha },
    { "SetBorderScalar",        &CGQuestPOIFrame_SetBorderScalar },
    { "DrawQuestBlob",          &CGQuestPOIFrame_DrawQuestBlob },
    { "EnableSmoothing",        &CGQuestPOIFrame_EnableSmoothing },
    { "EnableMerging",          &CGQuestPOIFrame_EnableMerging },
    { "SetMergeThreshold",      &CGQuestPOIFrame_SetMergeThreshold },
    { "SetNumSplinePoints",     &CGQuestPOIFrame_SetNumSplinePoints },
    { "UpdateQuestPOI",         &CGQuestPOIFrame_UpdateQuestPOI },
    { "UpdateMouseOverTooltip", &CGQuestPOIFrame_UpdateMouseOverTooltip },
    { "GetTooltipIndex",        &CGQuestPOIFrame_GetTooltipIndex },
    { "GetNumTooltips",         &CGQuestPOIFrame_GetNumTooltips },
};
