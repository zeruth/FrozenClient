#include "gx/Shader.hpp"
#include "gx/Device.hpp"

// `if (target) return &vertexConstants; else return &pixelConstants;` -- two globals and a
// test, at 0x00c5efe8 and 0x00c5dfe0. frozen forwards to the device, which owns the two
// arrays instead of keeping them at namespace scope; same function, one indirection more.
// ref: FUN_00683560
char* GxShaderConstantsLock(EGxShTarget target) {
    return g_theGxDevicePtr->ShaderConstantsLock(target);
}

void GxShaderConstantsSet(EGxShTarget target, uint32_t index, const float* constants, uint32_t count) {
    g_theGxDevicePtr->ShaderConstantsSet(target, index, constants, count);
}

// Widens the dirty range for one target rather than doing any unlocking: it lowers the range
// start toward `index` and raises the end toward `index + count - 1`, on the pair of globals
// belonging to whichever target was passed. Found beside its Lock in DrawBatch's sequence.
// ref: FUN_00683580
void GxShaderConstantsUnlock(EGxShTarget target, uint32_t index, uint32_t count) {
    g_theGxDevicePtr->ShaderConstantsUnlock(target, index, count);
}
