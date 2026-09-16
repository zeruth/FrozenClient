#ifndef WORLD_CLOUDS_HPP
#define WORLD_CLOUDS_HPP

#include <tempest/Vector.hpp>

class C44Matrix;
class CGxShader;

// The cloud sheet: a dome of animated cloud cover drawn over the sky gradient.
//
// There is no cloud texture file. The reference generates the sheet at runtime as 4-octave 3D value
// noise into two textures it double-buffers, regenerating a few rows per frame and flipping when a
// sheet completes (docs/ref/parity-sky-bodies.md section 2). The two names that look like texture
// paths, DNClouds0 and DNClouds1, are those two textures' debug names.

// Regenerate this frame's slice of the sheet. dt is in seconds.
void CloudsUpdate(float dt);

// Draw the sheet. Call inside the sky pass, after the gradient dome, with the sky's transform and
// shaders already set up; the caller owns the viewport and depth state.
void CloudsRender(const C44Matrix& viewProjT, const C3Vector& cameraPos, CGxShader* vs, CGxShader* ps);

// Release the generated textures (map change / shutdown).
void CloudsRelease();

#endif
