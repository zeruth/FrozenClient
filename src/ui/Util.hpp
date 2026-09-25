#ifndef UI_UTIL_HPP
#define UI_UTIL_HPP

#include "gx/Types.hpp"
#include "ui/Types.hpp"
#include <cstdint>

struct lua_State;
class C2Vector;
class CRect;

const char* LanguageProcess(const char* string);

uint32_t StringToMouseButton(const char* name);
int32_t StringToBlendMode(const char* string, EGxBlend& blend);
const char* BlendModeToString(EGxBlend blend);

int32_t StringToBOOL(const char* string);

bool StringToBOOL(const char* string, int32_t def);

bool StringToBOOL(lua_State* L, int32_t idx, int32_t def);

uint64_t StringToClickAction(const char* string);

const char* DrawLayerToString(int32_t layer);
int32_t StringToDrawLayer(const char* string, int32_t& layer);

// The nine anchor points of a rect, in FRAMEPOINT order.
void RectFramePoints(C2Vector* points, const CRect& rect);

const char* FramePointToString(FRAMEPOINT point);

int32_t StringToFramePoint(const char* string, FRAMEPOINT& point);

int32_t StringToTooltipAnchor(const char* string, TOOLTIP_ANCHORPOINT& anchor);
const char* TooltipAnchorToString(TOOLTIP_ANCHORPOINT anchor);

const char* FrameStrataToString(FRAME_STRATA strata);

int32_t StringToFrameStrata(const char* string, FRAME_STRATA& strata);

uint32_t StringToFontFlags(const char* string);

const char* FontFlagsToString(uint32_t fontFlags);

int32_t StringToJustify(const char* string, uint32_t& justify);

const char* JustifyToString(uint32_t justify);

const char* OrientationToString(ORIENTATION orientation);
int32_t StringToOrientation(const char* string, ORIENTATION& orientation);

#endif
