#ifndef UTIL_UNIMPLEMENTED_HPP
#define UTIL_UNIMPLEMENTED_HPP

#include <cstdio>

// Reports a stub the first time it is hit, then stays quiet: the in-game interface calls some of
// these many times per frame
#define WHOA_UNIMPLEMENTED(...)                                                                                 \
    do {                                                                                                        \
        static bool s_reported = false;                                                                         \
        if (!s_reported) {                                                                                      \
            s_reported = true;                                                                                  \
            fprintf(stderr, "Function not yet implemented: %s in %s (line %i)\n", __FUNCTION__, __FILE__, __LINE__); \
        }                                                                                                       \
    } while (0);                                                                                                \
    return __VA_ARGS__;                                                                                         \
    (void)0

#endif
