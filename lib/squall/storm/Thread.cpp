#include "storm/Thread.hpp"

#if defined(WHOA_SYSTEM_WIN)
#include <windows.h>
#endif

#if defined(WHOA_SYSTEM_MAC) || defined(WHOA_SYSTEM_LINUX)
#include <pthread.h>
#endif

uintptr_t SGetCurrentThreadId() {
#if defined(WHOA_SYSTEM_WIN)
    return GetCurrentThreadId();
#endif

#if defined(WHOA_SYSTEM_MAC) || defined(WHOA_SYSTEM_LINUX)
    // pthread_t is a pointer on some platforms and an integer on others (e.g. Bionic)
    return (uintptr_t)pthread_self();
#endif
}
