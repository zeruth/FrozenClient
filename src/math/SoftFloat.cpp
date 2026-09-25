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
