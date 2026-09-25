#ifndef COMMON_SHA1_HPP
#define COMMON_SHA1_HPP

#include <cstdint>

#define SHA1_DIGEST_SIZE 20

// The reference's order (FUN_0077aaa0): the bit count first, then the chaining state.
typedef struct {
    uint32_t count[2];
    uint32_t state[5];
    char buffer[64];
} SHA1_CONTEXT;

void SHA1_Final(uint8_t* const digest, SHA1_CONTEXT* context);

void SHA1_Init(SHA1_CONTEXT* context);

uint8_t* SHA1_InterleaveHash(uint8_t* digest, const uint8_t* data, uint32_t len);

void SHA1_Update(SHA1_CONTEXT* context, const uint8_t* data, uint32_t len);

#endif
