#include "common/SHA1.hpp"
#include <cstdlib>
#include <cstring>

#if defined(WHOA_SYSTEM_WIN)
#include <malloc.h>
#endif

#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

void SHA1_Final(uint8_t* const digest, SHA1_CONTEXT* context) {
    uint8_t finalcount[8];
    for (uint32_t i = 0; i < 8; i++) {
        finalcount[i] = static_cast<uint8_t>((context->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }

    SHA1_Update(context, reinterpret_cast<const uint8_t*>("\200"), 1);

    while ((context->count[0] & 504) != 448) {
        SHA1_Update(context, reinterpret_cast<const uint8_t*>("\0"), 1);
    }

    SHA1_Update(context, finalcount, 8);

    for (uint32_t i = 0; i < SHA1_DIGEST_SIZE; i++) {
        digest[i] = static_cast<uint8_t>((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }

    memset(context->buffer, 0, sizeof(context->buffer));
    memset(context->state, 0, sizeof(context->state));
    memset(context->count, 0, sizeof(context->count));
    memset(finalcount, 0, sizeof(finalcount));
}

// ref: FUN_0077aaa0
void SHA1_Init(SHA1_CONTEXT* context) {
    context->count[0] = 0;
    context->count[1] = 0;
    context->state[0] = 0x67452301;
    context->state[1] = 0xEFCDAB89;
    context->state[2] = 0x98BADCFE;
    context->state[3] = 0x10325476;
    context->state[4] = 0xC3D2E1F0;
}

uint8_t* SHA1_InterleaveHash(uint8_t* const digest, const uint8_t* data, uint32_t len) {
    // Terminate data at first null
    uint32_t l;
    for (l = len; l; l--) {
        if (*data) {
            break;
        }

        data++;
    }

    if (l & 1) {
        data++;
        l--;
    }

    SHA1_CONTEXT ctx;
    uint8_t scratchDigest[SHA1_DIGEST_SIZE];

    auto scratchLen = l / 2;
    auto scratch = static_cast<uint8_t*>(alloca(scratchLen));
    if (!scratch) {
        return nullptr;
    }

    // Even half
    for (uint32_t i = 0; i < scratchLen; i++) {
        scratch[i] = data[i * 2];
    }

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, scratch, scratchLen);
    SHA1_Final(scratchDigest, &ctx);

    for (uint32_t i = 0; i < sizeof(scratchDigest); i++) {
        digest[i * 2] = scratchDigest[i];
    }

    // Odd half
    for (uint32_t i = 0; i < scratchLen; i++) {
        scratch[i] = data[i * 2 + 1];
    }

    SHA1_Init(&ctx);
    SHA1_Update(&ctx, scratch, scratchLen);
    SHA1_Final(scratchDigest, &ctx);

    for (uint32_t i = 0; i < sizeof(scratchDigest); i++) {
        digest[i * 2 + 1] = scratchDigest[i];
    }

    return digest;
}

// ref: FUN_0077a560
// The block is read big-endian into an 80-word schedule, and each word is cleared as its round
// consumes it. The input itself is never written.
void SHA1_Transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t w[80];

    for (uint32_t i = 0; i < 16; i++) {
        w[i] = (static_cast<uint32_t>(buffer[i * 4]) << 24)
            | (static_cast<uint32_t>(buffer[i * 4 + 1]) << 16)
            | (static_cast<uint32_t>(buffer[i * 4 + 2]) << 8)
            | static_cast<uint32_t>(buffer[i * 4 + 3]);
    }

    for (uint32_t i = 16; i < 80; i++) {
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];

    for (uint32_t i = 0; i < 80; i++) {
        uint32_t f;
        uint32_t k;

        if (i < 20) {
            f = (~b & d) | (c & b);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = ((c | b) & d) | (c & b);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        uint32_t temp = e + k + rol(a, 5) + f + w[i];
        w[i] = 0;

        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

void SHA1_Update(SHA1_CONTEXT* context, const uint8_t* data, uint32_t len) {
    uint32_t i, j;

    j = (context->count[0] >> 3) & 63;
    if ((context->count[0] += len << 3) < (len << 3)) {
        context->count[1]++;
    }

    context->count[1] += (len >> 29);

    if ((j + len) > 63) {
        memcpy(&context->buffer[j], data, (i = 64 - j));
        SHA1_Transform(context->state, reinterpret_cast<uint8_t*>(context->buffer));

        for (; i + 63 < len; i += 64) {
            SHA1_Transform(context->state, data + i);
        }

        j = 0;
    } else {
        i = 0;
    }

    memcpy(&context->buffer[j], &data[i], len - i);
}
