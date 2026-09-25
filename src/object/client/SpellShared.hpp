#ifndef OBJECT_CLIENT_SPELL_SHARED_HPP
#define OBJECT_CLIENT_SPELL_SHARED_HPP

#include <tempest/Vector.hpp>
#include <cstdint>

bool SpellEffectAppliesUnitAura(uint32_t effect);

bool SpellEffectIsAreaAura(uint32_t effect);

int32_t SpellTrajectorySolve(float pitch, const C3Vector* delta, float gravity, float* speed, float* time, C3Vector* velocity);

float SpellTrajectoryTime(float pitch, float speed, const C3Vector* delta, float gravity);

#endif
