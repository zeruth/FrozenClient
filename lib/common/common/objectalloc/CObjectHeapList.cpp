#include "common/objectalloc/CObjectHeapList.hpp"
#include <storm/Error.hpp>

void CObjectHeapList::Delete(uint32_t index) {
    auto heap = &this->m_heaps[index / this->m_objsPerBlock];

    if (heap->m_allocated == this->m_objsPerBlock) {
        this->m_numFullHeaps--;
    }

    heap->Delete(index % this->m_objsPerBlock, this->m_objSize, this->m_objsPerBlock);

    // TODO free empty heaps
}

// ref: FUN_004d3250
int32_t CObjectHeapList::New(uint32_t* index, void** a3, bool zero) {
    CObjectHeap* heap = nullptr;

    if (this->uint24 < this->m_heaps.Count()) {
        heap = &this->m_heaps[this->uint24];
    }

    if (!heap || heap->m_allocated == this->m_objsPerBlock || !heap->m_obj) {
        if (this->m_heaps.Count() == this->m_numFullHeaps) {
            this->uint24 = this->m_heaps.Count();

            heap = this->m_heaps.New();

            if (!heap->Allocate(this->m_objSize, this->m_objsPerBlock, this->m_heapName)) {
                return 0;
            }
        } else {
            // A block freed below the current one: the first that is not full, preferring one
            // whose memory is still allocated (FUN_004d3250).
            heap = nullptr;

            for (uint32_t i = 0; i < this->m_heaps.Count(); i++) {
                auto candidate = &this->m_heaps[i];

                if (candidate->m_allocated != this->m_objsPerBlock) {
                    this->uint24 = i;
                    heap = candidate;

                    if (candidate->m_obj) {
                        break;
                    }
                }
            }

            if (!heap) {
                return 0;
            }
        }
    }

    if (!heap->New(this->m_objSize, this->m_objsPerBlock, index, this->m_heapName, a3, zero)) {
        return 0;
    }

    *index += this->uint24 * this->m_objsPerBlock;

    if (heap->m_allocated == this->m_objsPerBlock) {
        this->m_numFullHeaps++;
    }

    return 1;
}
