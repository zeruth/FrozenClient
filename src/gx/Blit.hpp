#ifndef GX_BLIT_HPP
#define GX_BLIT_HPP

#include "gx/Types.hpp"
#include <cstdint>

class C2iVector;

typedef void (*BLIT_FUNCTION)(const C2iVector&, const void*, uint32_t, void*, uint32_t);

// 1 when a blitter for this format pair exists and ran, 0 when there is none. The table is very
// sparse -- InitBlit fills 26 of its 676 slots -- so the answer is genuinely useful.
int32_t Blit(const C2iVector&, BlitAlpha, const void*, uint32_t, BlitFormat, void*, uint32_t, BlitFormat);

BlitFormat GxGetBlitFormat(EGxTexFormat);

#endif
