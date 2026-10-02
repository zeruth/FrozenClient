#include "gx/RenderTarget.hpp"
#include "gx/Device.hpp"

void GxRenderTargetGet(EGxBuffer buffer, CGxTex*& gxTex) {
    g_theGxDevicePtr->RenderTargetGet(buffer, gxTex);
}

// ref: FUN_0057e4f0
// Bind a texture as a render target.
//
// IDENTIFIED 2026-09-28 by correcting a matcher link. The call-graph matcher had bound this
// reference function to CGxDevice::TexCreate, which takes ELEVEN parameters against this one's
// three; what the reference actually does is dirty one render state and call the device through
// vtable +0x5c, and its call sites read (buffer, tex, plane) -- FUN_007bb830 binds a depth
// surface with (1, tex, 0) and a colour one with (0, tex, 0).
//
// NOT FULLY PORTED: the reference clears a device flag and calls IRsDirty(0x14) first, so that a
// texture still bound as a sampler is dropped before it becomes a target. frozen goes straight to
// the device method. That is a real gap -- binding a target that is also a live sampler is
// undefined on D3D9 -- and it belongs with the device's render-state bookkeeping rather than here.
void GxRenderTargetSet(EGxBuffer buffer, CGxTex* gxTex, uint32_t plane) {
    g_theGxDevicePtr->RenderTargetSet(buffer, gxTex, plane);
}


