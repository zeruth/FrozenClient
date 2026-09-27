#ifndef MODEL_M2_INTERNAL_HPP
#define MODEL_M2_INTERNAL_HPP

#include <cstdint>

extern uint32_t* g_modelPool;

class CM2Model;

// Destruct a model and give its block back to `pool`. The reference factors this out because two
// creation paths unwind through it; CM2Model::Release does the same two steps inline.
void DestroyModel(uint32_t* pool, CM2Model* model);

#endif
