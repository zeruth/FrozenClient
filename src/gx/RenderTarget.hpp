#ifndef GX_RENDER_TARGET_HPP
#define GX_RENDER_TARGET_HPP

#include "gx/Types.hpp"

class CGxTex;

void GxRenderTargetGet(EGxBuffer buffer, CGxTex*& gxTex);
void GxRenderTargetSet(EGxBuffer buffer, CGxTex* gxTex, uint32_t plane = 0);

// Debug only: read a render target back and write it out as a greyscale TGA. Returns 0 on failure.
int32_t GxRenderTargetDump(CGxTex* gxTex, const char* path);

#endif
