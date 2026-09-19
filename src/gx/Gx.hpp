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

void GxFormatColor(CImVector&);

void GxMaxFpsSet(int32_t maxFps);

void GxMaxFpsBkSet(int32_t maxFps);

void GxMaxFpsSet(int32_t maxFps);

void GxMaxFpsBkSet(int32_t maxFps);

void GxMaxFpsSet(int32_t maxFps);

void GxMaxFpsBkSet(int32_t maxFps);

void GxMaxFpsSet(int32_t maxFps);

void GxMaxFpsBkSet(int32_t maxFps);

#endif
