#ifndef WORLD_PARTICLE_FX_HPP
#define WORLD_PARTICLE_FX_HPP

#include <cstdint>

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

#endif
