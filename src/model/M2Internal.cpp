#include "model/M2Internal.hpp"
#include "model/M2Model.hpp"

uint32_t* g_modelPool;

// The per-bone and per-light runtime records are constructed by their member initialisers; the
// reference builds them field by field in these two constructors.

// ref: FUN_0082bd60
M2ModelBone::M2ModelBone() {
}

// ref: FUN_00824980
M2ModelLight::M2ModelLight() {
}
