#include "storm/Memory.hpp"

#include <cstring>

constexpr size_t ALIGNMENT = 8;

// The reference emits exactly two of these operators out of line, and this is one of them. Its
// allocation tag string is "new" at 0x009e0e14, and its body is this body: SMemAlloc(size, tag,
// -1, 0) and nothing else. The tag is pushed from twenty places in the binary, so the compiler
// inlined the operator nearly everywhere and this is the copy left over for the rest.
// ref: FUN_00401010
void* operator new(size_t bytes) {
    return SMemAlloc(bytes, "new", -1, 0x0);
}

void* operator new(size_t bytes, const std::nothrow_t&) noexcept {
    return SMemAlloc(bytes, "new(nothrow_t)", -1, 0x0);
}

void* operator new[](size_t bytes) {
    return SMemAlloc(bytes, "new[]", -1, 0x0);
}

void* operator new[](size_t bytes, const std::nothrow_t&) noexcept {
    return SMemAlloc(bytes, "new[](nothrow_t)", -1, 0x0);
}

void operator delete(void* ptr) noexcept {
    if (ptr) {
        SMemFree(ptr, "delete", -1, 0x0);
    }
}

void operator delete(void* ptr, const std::nothrow_t&) noexcept {
    if (ptr) {
        SMemFree(ptr, "delete(nothrow_t)", -1, 0x0);
    }
}

// The other operator the reference emits out of line, tag string "delete[]" at 0x009e0e18, with
// the same null guard around the same call. That tag is pushed from 87 places.
//
// Worth knowing for whoever looks next: those are the ONLY two allocator tag strings in that part
// of the reference's data. There is no "new[]" and no scalar "delete", so the reference's other
// operators are either folded into these two or never emitted, and frozen's four remaining
// overloads have nothing to link to.
// ref: FUN_00401030
void operator delete[](void* ptr) noexcept {
    if (ptr) {
        SMemFree(ptr, "delete[]", -1, 0x0);
    }
}

void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
    if (ptr) {
        SMemFree(ptr, "delete[](nothrow_t)", -1, 0x0);
    }
}

void* STORMAPI SMemAlloc(size_t bytes, const char* filename, int32_t linenumber, uint32_t flags) {
    size_t alignedBytes = (bytes + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    void* result;

    if (flags & SMEM_FLAG_ZEROMEMORY) {
        result = calloc(1, alignedBytes);
    } else {
        result = malloc(alignedBytes);
    }

    if (result) {
        return result;
    } else {
        // TODO handle errors
        return nullptr;
    }
}

int STORMAPI SMemCmp(void* ptrA, void* ptrB, size_t bytes) {
    return memcmp(ptrA, ptrB, bytes);
}

void STORMAPI SMemCopy(void* dst, void* src, size_t bytes) {
    memmove(dst, src, bytes);
}

void STORMAPI SMemFill(void* ptr, size_t bytes, uint8_t value) {
    memset(ptr, value, bytes);
}

void STORMAPI SMemFree(void* ptr, const char* filename, int32_t linenumber, uint32_t flags) {
    if (ptr) {
        free(ptr);
    }
}

void STORMAPI SMemMove(void* dst, void* src, size_t bytes) {
    memmove(dst, src, bytes);
}

void* STORMAPI SMemReAlloc(void* ptr, size_t bytes, const char* filename, int32_t linenumber, uint32_t flags) {
    if (flags == 0xB00BEEE5) {
        return nullptr;
    }

    if (!ptr) {
        return SMemAlloc(bytes, filename, linenumber, flags);
    }

    if (flags & 0x10) {
        return nullptr;
    }

    size_t alignedBytes = (bytes + (ALIGNMENT - 1)) & ~(ALIGNMENT - 1);

    void* result = realloc(ptr, alignedBytes);

    if (result) {
        if (flags & 0x8) {
            // TODO zero out expanded portion
        }

        return result;
    } else {
        if (alignedBytes) {
            // TODO handle errors
        }

        return nullptr;
    }
}

void STORMAPI SMemZero(void* ptr, size_t bytes) {
    uint8_t* ptrdata = static_cast<uint8_t*>(ptr);
    for (size_t i = 0; i < bytes; i++) {
        ptrdata[i] = 0;
    }
}
