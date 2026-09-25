#ifndef COMMON_C_DATA_ALLOCATOR_HPP
#define COMMON_C_DATA_ALLOCATOR_HPP

#include <cstdint>

// A fixed-size allocator: blocks of m_dataPerBlock elements, each m_bytesPerData bytes, handed
// out from a free list threaded through the unused elements. Blocks are chained through a
// pointer-sized header in front of each block and are never returned to Storm.
//
// The reference's block header is 4 bytes; here it is a pointer, and an element must be at least
// pointer-sized to hold the free-list link.
class CDataAllocator {
    public:
    // Member variables
    uint32_t m_bytesPerData;
    uint32_t m_dataPerBlock;
    uint32_t m_dataCount;
    void* m_blockList;
    void* m_freeList;

    // Member functions
    void* GetData(int32_t zero, const char* fileName, int32_t lineNumber);
    void PutData(void* data);
};

#endif
