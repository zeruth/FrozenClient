#ifndef WORLD_PARTICLE_FX_HPP
#define WORLD_PARTICLE_FX_HPP

#include <cstdint>
#include <tempest/Vector.hpp>

class CM2Model;

// Stand-in M2 particle emitters (the reference simulates these inside CM2Model and draws them as
// elements of passes 1/2 via CM2SceneRender::DrawParticle, which is still a stub). Each model with
// emitters gets a simulation keyed by the model pointer; ParticleFxUpdateModel steps it, and
// ParticleFxRender draws every live particle as a camera-facing quad in the transparent block.

// Step the emitters of one visible model (no-op for models without emitters). worldOffset is the
// camera position the scene subtracts from model transforms, so particles live in world space.
void ParticleFxUpdateModel(CM2Model* model, float dt);

// Drop the simulation of a model that is going away
void ParticleFxForgetModel(CM2Model* model);

// Draw all live particles (call once per frame after the models have been updated)
void ParticleFxRender();

// Simulations of models not updated for a while are released; call once per frame
void ParticleFxEndFrame();

// How far, in world yards, this model's emitters can reach beyond its origin; 0 for a model with no
// emitters. Add it to the model's cull radius.
//
// A model's own bounding sphere covers its mesh, and for a brazier or a torch that is the base, not
// the flames. Emitters were only stepped and drawn for models that passed the frustum test, so the
// moment a fire's base left the screen its flames vanished -- and came back with a jolt when the
// base returned. The reference merges each emitter's own bounding box into the model's animated
// bounds; this is the same idea: the reach is the larger of a static estimate from the emitter
// definitions (speed x life, plus the quad size, from the model's origin) and the live extent of
// the particles from the last step.
float ParticleFxCullExtent(CM2Model* model, float scale);


#endif
