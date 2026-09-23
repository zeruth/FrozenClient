#ifndef TEMPEST_COLOR_CONVERT_HPP
#define TEMPEST_COLOR_CONVERT_HPP

#include "tempest/Vector.hpp"
#include <cstdint>

// Index (0, 1, 2) of the component with the smallest magnitude.
uint32_t MinAxis(const C3Vector& v);

// rgb (0..1 floats, as a C3Vector x=r y=g z=b) to hue (degrees, -1 when grey), saturation, value.
void RgbToHsv(const C3Vector& rgb, C3Vector& hsv);

// The inverse of RgbToHsv.
void HsvToRgb(const C3Vector& hsv, C3Vector& rgb);

// rgb floats to a packed colour with alpha 255.
void PackColor(CImVector& out, const C3Vector& rgb);

// A packed colour's r, g, b back to 0..1 floats. The inverse of PackColor, minus its rounding.
void UnpackColor(C3Vector& out, const CImVector& color);

// Move a colour's r, g, b toward target's by alpha/256 (alpha 255 copies them); a is untouched.
void LerpColor(CImVector& color, uint32_t alpha, const CImVector& target);

#endif
