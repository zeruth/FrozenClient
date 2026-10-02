#ifndef GX_GX_HPP
#define GX_GX_HPP

#include "gx/CGxCaps.hpp"
#include "gx/CGxFormat.hpp"
#include "gx/Types.hpp"
#include <cstdint>

class CRect;

extern const char** g_gxShaderProfileNames[GxShTargets_Last];

const CGxCaps& GxCaps();

bool GxCapsWindowHasFocus(int32_t);

void GxCapsWindowSize(CRect&);

void GxCapsWindowSizeInScreenCoords(CRect& rect);

void GxFormatColor(CImVector&);

int32_t GxMaxFps();

int32_t GxMaxFpsBk();

void GxMaxFpsSet(int32_t maxFps);

void GxMaxFpsBkSet(int32_t maxFps);

#endif
