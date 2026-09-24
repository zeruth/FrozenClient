#include "gx/RenderState.hpp"
#include "gx/Shader.hpp"
#include "gx/Device.hpp"
#include "gx/Types.hpp"
#include "gx/texture/CGxTex.hpp"
#include <storm/Error.hpp>

void GxRsPop() {
    g_theGxDevicePtr->RsPop();
}

void GxRsPush() {
    g_theGxDevicePtr->RsPush();
}

// ref: FUN_00408bf0
void GxRsSet(EGxRenderState which, int32_t value) {
    STORM_ASSERT(which < GxRenderStates_Last);
    g_theGxDevicePtr->RsSet(which, value);
}

// The reference spells this one out rather than delegating -- it tests the device's m_context at
// +0xf58, compares against the stored float and calls IRsDirty itself -- but that is the body of
// RsSet inlined, the same way GxRsSet(int32_t) above is a thin wrapper here and inlined there.
//
// It is the generic float setter, not a fog-specific helper, even though it sits over in the world
// module at 0x763c70 rather than beside its siblings. Settled by looking at what its ten call sites
// push: 0x0 (GxRs_PolygonOffset), 0x4 (GxRs_MatSpecularExp), 0x8 and 0x9 (GxRs_FogStart and
// GxRs_FogEnd) and 0x52 (GxRs_PointScaleMax). Every one of those is a float-valued state and no
// non-float state appears, which is what a generic setter looks like and a fog helper does not.
// ref: FUN_00763c70
void GxRsSet(EGxRenderState which, float value) {
    STORM_ASSERT(which < GxRenderStates_Last);
    g_theGxDevicePtr->RsSet(which, value);
}

void GxRsSet(EGxRenderState which, uint32_t value) {
    STORM_ASSERT(which < GxRenderStates_Last);
    g_theGxDevicePtr->RsSet(which, value);
}

void GxRsSet(EGxRenderState which, CGxShader* value) {
    g_theGxDevicePtr->RsSet(which, value);
}

void GxRsSet(EGxRenderState which, CGxTex* value) {
    g_theGxDevicePtr->RsSet(which, value);
}

void GxRsSetAlphaRef() {
    g_theGxDevicePtr->RsSetAlphaRef();
}
