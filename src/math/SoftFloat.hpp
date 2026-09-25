#ifndef MATH_SOFT_FLOAT_HPP
#define MATH_SOFT_FLOAT_HPP

#include <cstdint>

// Integer helpers over IEEE single-precision bit patterns. The reference keeps a small set of
// these beside each other; the ones not listed here are not ported yet.

// The number of leading zero bits in a 32-bit value, 32 for zero.
int32_t CountLeadingZeros(uint32_t value);

// The product of two floats given and returned as bit patterns, computed on the integer unit:
// the mantissa product is truncated, and a result whose exponent underflows is flushed to zero.
void SoftFloatMultiply(uint32_t* out, const uint32_t* a, const uint32_t* b);

#endif
