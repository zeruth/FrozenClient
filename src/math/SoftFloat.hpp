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

// The sum of two floats as bit patterns. An operand with a zero exponent is taken as zero, and one
// more than 23 binary places smaller than the other is dropped.
void SoftFloatAdd(uint32_t* out, const uint32_t* a, const uint32_t* b);

void SoftFloatFloor(uint32_t* out, const uint32_t* a);

void SoftFloatCeil(uint32_t* out, const uint32_t* a);

void SoftFloatRound(uint32_t* out, const uint32_t* a);

// The float nearest an integer, truncating the bits past the mantissa.
void SoftFloatFromInt(uint32_t* out, int32_t value);

extern uint32_t g_softFloatMinusOne;
extern uint32_t g_softFloatZero;
extern uint32_t g_softFloatOne;
extern uint32_t g_softFloatHalf;
extern uint32_t g_softFloatHundredth;

#endif
