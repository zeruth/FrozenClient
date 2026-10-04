#include "math/SoftFloat.hpp"

// ref: FUN_0095d210
int32_t CountLeadingZeros(uint32_t value) {
    int32_t bit = 31;

    if (value) {
        while ((value >> bit) == 0) {
            bit--;
        }
    }

    return value ? 31 - bit : 32;
}

// ref: FUN_0095d230
void SoftFloatMultiply(uint32_t* out, const uint32_t* a, const uint32_t* b) {
    uint32_t bitsA = *a;
    uint32_t bitsB = *b;

    uint32_t mantissaA = bitsA & 0x7FFFFF;
    uint32_t sign = (bitsB ^ bitsA) & 0x80000000;
    uint32_t exponentA = bitsA & 0x7F800000;
    uint32_t exponentB = bitsB & 0x7F800000;
    uint32_t mantissaB = bitsB & 0x7FFFFF;

    if (mantissaA && mantissaB) {
        uint32_t exponent = exponentB + 0xC0800000 + exponentA;

        uint64_t product = static_cast<uint64_t>((mantissaB | 0xFF800000) << 8)
            * static_cast<uint64_t>((mantissaA | 0xFF800000) << 8);
        uint32_t high = static_cast<uint32_t>(product >> 32);
        uint32_t carry = static_cast<uint32_t>(product >> 63);

        // A mantissa product in [2, 4) carries into the exponent
        uint32_t exponentCarry = (high & 0x80000000) ? 0x800000 : 0;

        uint32_t result = ((high >> carry) >> 7) & 0x7FFFFF;
        result |= exponentCarry + exponent;
        result |= sign;

        *out = result & ~static_cast<uint32_t>(static_cast<int32_t>(exponent - 0x800000) >> 31);

        return;
    }

    if (exponentA && exponentB) {
        uint32_t exponent = exponentB + 0xC0800000 + exponentA;

        *out = (exponent | mantissaB | mantissaA | sign) & ~static_cast<uint32_t>(static_cast<int32_t>(exponent - 0x800000) >> 31);

        return;
    }

    *out = 0;
}

// ref: FUN_0095d2f0
void SoftFloatAdd(uint32_t* out, const uint32_t* a, const uint32_t* b) {
    uint32_t bitsA = *a;
    uint32_t exponentA = bitsA & 0x7F800000;

    if (!exponentA) {
        *out = *b;

        return;
    }

    uint32_t bitsB = *b;
    uint32_t exponentB = bitsB & 0x7F800000;

    if (!exponentB) {
        *out = bitsA;

        return;
    }

    // Each mantissa with its hidden bit, doubled, and signed
    int32_t signA = static_cast<int32_t>(bitsA) >> 31;
    int32_t mantissaA = static_cast<int32_t>(((bitsA & 0x7FFFFF) | 0x800000) * 2);
    mantissaA = (mantissaA ^ signA) - signA;

    int32_t signB = static_cast<int32_t>(bitsB) >> 31;
    int32_t mantissaB = static_cast<int32_t>(((bitsB & 0x7FFFFF) | 0x800000) * 2);
    mantissaB = (mantissaB ^ signB) - signB;

    int32_t delta = static_cast<int32_t>(exponentB - exponentA);
    uint32_t exponent;

    if (delta > 0) {
        // b is so much larger that a vanishes
        if (delta >= 0xB800000) {
            *out = *b;

            return;
        }

        mantissaA >>= static_cast<uint32_t>(delta) >> 23;
        exponent = exponentB;
    } else {
        if (delta <= static_cast<int32_t>(0xF4800000)) {
            *out = bitsA;

            return;
        }

        mantissaB >>= (exponentA - exponentB) >> 23;
        exponent = exponentA;
    }

    int32_t sum = mantissaB + mantissaA;

    if (sum == 0) {
        *out = 0;

        return;
    }

    uint32_t sign = static_cast<uint32_t>(sum) & 0x80000000;

    if (sum < 0) {
        sum = -sum;
    }

    int32_t shift = 8 - CountLeadingZeros(static_cast<uint32_t>(sum));

    if (shift >= 0) {
        sum >>= shift;
    } else {
        sum <<= -shift;
    }

    *out = ((static_cast<uint32_t>(shift - 1) << 23) + exponent) | (static_cast<uint32_t>(sum) & 0x7FFFFF) | sign;
}

// ref: FUN_0095d460
// The largest whole value not above a.
void SoftFloatFloor(uint32_t* out, const uint32_t* a) {
    uint32_t bits = *a;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 0x7F;

    if (exponent < 0) {
        uint32_t signMask = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);
        *out = ((bits * 2) & signMask) ? g_softFloatMinusOne : g_softFloatZero;

        return;
    }

    if (exponent >= 23) {
        *out = bits;

        return;
    }

    uint32_t mask = static_cast<uint32_t>(static_cast<int32_t>(0x80000000) >> (exponent + 8));
    uint32_t positiveMask = ~static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);

    // Positive: drop the fraction
    if ((bits * 2) & positiveMask) {
        *out = bits & mask;

        return;
    }

    // Negative: round the magnitude up, carrying into the exponent
    uint32_t mantissa = ((bits & 0x7FFFFF) + ~mask) & mask;
    exponent += static_cast<int32_t>(mantissa >> 23);

    *out = (static_cast<uint32_t>(exponent + 0x7F) << 23) | (mantissa & 0x7FFFFF) | 0x80000000;
}

// ref: FUN_0095d500
// The smallest whole value not below a. Below one in magnitude a negative value gives 0 and
// anything else -- zero included -- gives 1.
void SoftFloatCeil(uint32_t* out, const uint32_t* a) {
    uint32_t bits = *a;
    int32_t exponent = static_cast<int32_t>((bits >> 23) & 0xFF) - 0x7F;

    if (exponent < 0) {
        uint32_t signMask = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);
        *out = ((bits * 2) & signMask) ? g_softFloatZero : g_softFloatOne;

        return;
    }

    if (exponent >= 23) {
        *out = bits;

        return;
    }

    uint32_t mask = static_cast<uint32_t>(static_cast<int32_t>(0x80000000) >> (exponent + 8));
    uint32_t signMask = static_cast<uint32_t>(static_cast<int32_t>(bits) >> 31);

    // Negative: drop the fraction
    if ((bits * 2) & signMask) {
        *out = bits & mask;

        return;
    }

    // Positive: round up, carrying into the exponent
    uint32_t mantissa = ((bits & 0x7FFFFF) + ~mask) & mask;
    exponent += static_cast<int32_t>(mantissa >> 23);

    *out = (static_cast<uint32_t>(exponent + 0x7F) << 23) | (mantissa & 0x7FFFFF);
}

// ref: FUN_0095d590
// Half up: the floor of a + 0.5.
void SoftFloatRound(uint32_t* out, const uint32_t* a) {
    uint32_t sum;
    SoftFloatAdd(&sum, a, &g_softFloatHalf);

    SoftFloatFloor(out, &sum);
}

// ref: FUN_0095d5c0
void SoftFloatFromInt(uint32_t* out, int32_t value) {
    if (value == 0) {
        *out = 0;

        return;
    }

    uint32_t sign = static_cast<uint32_t>(value) & 0x80000000;
    uint32_t magnitude = static_cast<uint32_t>(value < 0 ? -value : value);

    int32_t zeros = CountLeadingZeros(magnitude);
    int32_t exponent = 31 - zeros;
    int32_t shift = zeros - 8;

    if (shift >= 0) {
        magnitude <<= shift;
    } else {
        magnitude = static_cast<uint32_t>(static_cast<int32_t>(magnitude) >> -shift);
    }

    *out = (static_cast<uint32_t>(exponent + 0x7F) << 23) | (magnitude & 0x7FFFFF) | sign;
}

static uint32_t SoftFloatConstant(int32_t value) {
    uint32_t bits;
    SoftFloatFromInt(&bits, value);

    return bits;
}

// Built by the reference's static initializers: three through FUN_0095d5c0, two as bit patterns.
uint32_t g_softFloatMinusOne = SoftFloatConstant(-1);  // ref: DAT_00dce218
uint32_t g_softFloatZero = SoftFloatConstant(0);       // ref: DAT_00dce224
uint32_t g_softFloatOne = SoftFloatConstant(1);        // ref: DAT_00dce2a8
uint32_t g_softFloatHalf = 0x3F000000;                 // ref: DAT_00dce284
uint32_t g_softFloatHundredth = 0x3C23D70A;            // ref: DAT_00dce234
