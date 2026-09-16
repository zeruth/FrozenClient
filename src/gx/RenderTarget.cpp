#include "gx/RenderTarget.hpp"
#include "gx/Device.hpp"

void GxRenderTargetGet(EGxBuffer buffer, CGxTex*& gxTex) {
    g_theGxDevicePtr->RenderTargetGet(buffer, gxTex);
}

void GxRenderTargetSet(EGxBuffer buffer, CGxTex* gxTex, uint32_t plane) {
    g_theGxDevicePtr->RenderTargetSet(buffer, gxTex, plane);
}

int32_t GxRenderTargetDump(CGxTex* gxTex, const char* path) {
    return g_theGxDevicePtr->RenderTargetDump(gxTex, path);
}
