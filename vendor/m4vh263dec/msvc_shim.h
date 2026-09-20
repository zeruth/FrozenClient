#ifndef M4VH263DEC_MSVC_SHIM_H
#define M4VH263DEC_MSVC_SHIM_H

// Force-included on MSVC only, so the vendored sources stay byte-identical to upstream AOSP.
//
// The decoder marks its IDCT rows with __attribute__((no_sanitize("signed-integer-overflow"))),
// a UBSan suppression for the deliberate wraparound in those kernels. It is a Clang/GCC spelling
// and has no bearing on what the code computes, so defining it away is safe.
#ifdef _MSC_VER
#define __attribute__(x)
#endif

#endif
