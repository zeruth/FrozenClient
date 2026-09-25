#ifndef STORM_ARRAY_TS_FIXED_ARRAY_HPP
#define STORM_ARRAY_TS_FIXED_ARRAY_HPP

#include "storm/Memory.hpp"
#include "storm/array/TSBaseArray.hpp"
#include <cstdint>

template <class T>
class TSFixedArray : public TSBaseArray<T> {
    public:
    // Member functions
    TSFixedArray();
    TSFixedArray(const TSFixedArray& source);
    ~TSFixedArray();
    TSFixedArray& operator=(const TSFixedArray& source);
    void Clear();
    void ReallocAndClearData(uint32_t count);
    void ReallocData(uint32_t count);
    void Set(uint32_t count, const T* data);
    void SetCount(uint32_t count);
};

template <class T>
TSFixedArray<T>::TSFixedArray() {
    this->Constructor();
}

template <class T>
TSFixedArray<T>::TSFixedArray(const TSFixedArray<T>& source) {
    this->Constructor();
    this->Set(source.Count(), source.Ptr());
}

// ref: FUN_006c2b50 (the TEXTURECACHEROW instance)
template <class T>
TSFixedArray<T>::~TSFixedArray() {
    for (uint32_t i = 0; i < this->Count(); i++) {
        auto element = &this->operator[](i);
        element->~T();
    }

    if (this->Ptr()) {
        SMemFree(this->Ptr(), this->MemFileName(), this->MemLineNo(), 0x0);
    }
}

template <class T>
TSFixedArray<T>& TSFixedArray<T>::operator=(const TSFixedArray<T>& source) {
    if (this != &source) {
        this->Set(source.Count(), source.Ptr());
    }

    return *this;
}

// ref: FUN_00409770
template <class T>
void TSFixedArray<T>::Clear() {
    this->~TSFixedArray<T>();
    this->Constructor();
}

template <class T>
void TSFixedArray<T>::ReallocAndClearData(uint32_t count) {
    // Destruct existing array elements
    for (uint32_t i = 0; i < this->Count(); i++) {
        auto element = &this->operator[](i);
        element->~T();
    }

    // Reallocate if count changed
    if (count != this->m_count) {
        this->m_alloc = count;

        if (this->m_data || count) {
            void* m = SMemReAlloc(this->m_data, sizeof(T) * count, this->MemFileName(), this->MemLineNo(), 0x0);
            this->m_data = static_cast<T*>(m);
        }
    }
}

// ref: FUN_00408d80
// ref: FUN_00493a40 (the CFrameStrataNode* instance)
// ref: FUN_0058e030 (the CGQuestLog::QUESTPOI instance)
// ref: FUN_005fef70 (the NTempest::CFacet instance)
// ref: FUN_0060a1c0 (the UniqueSignal instance)
// ref: FUN_0060a260 (the const VehicleUIIndSeatRec* instance)
// ref: FUN_006c26e0 (the TSExplicitList<CHARCODEDESC> instance)
// ref: FUN_006c2840 (the TSExplicitList<KERNNODE> instance)
// ref: FUN_006c9df0 (the TEXTURECACHEROW instance)
// ref: FUN_00599030 (the NTempest::CImVector instance)
// ref: FUN_00514100 (the NearestUnitData instance)
// ref: FUN_0051a2e0 (the unsigned __int64 instance)
// ref: FUN_00525720 (the TSExplicitList<SAVEDVARIABLE> instance)
template <class T>
void TSFixedArray<T>::ReallocData(uint32_t count) {
    T* oldData = this->m_data;

    if (count < this->m_count) {
        for (uint32_t i = count; i < this->m_count; i++) {
            (&this->m_data[i])->~T();
        }
    }

    this->m_alloc = count;

    void* v6 = SMemReAlloc(oldData, sizeof(T) * count, this->MemFileName(), this->MemLineNo(), 0x10);
    this->m_data = (T*)v6;

    if (!v6) {
        this->m_data = (T*)SMemAlloc(sizeof(T) * count, this->MemFileName(), this->MemLineNo(), 0x0);

        if (oldData) {
            uint32_t smallestCount = count >= this->m_count ? this->m_count : count;

            for (uint32_t i = 0; i < smallestCount; i++) {
                new (&this->m_data[i]) T(oldData[i]);
                (&oldData[i])->~T();
            }

            SMemFree(oldData, this->MemFileName(), this->MemLineNo(), 0x0);
        }
    }
}

// ref: FUN_0058eb60 (the unsigned short instance)
// ref: FUN_0058ebd0 (the C2Vector instance)
template <class T>
void TSFixedArray<T>::Set(uint32_t count, const T* data) {
    this->ReallocAndClearData(count);

    for (uint32_t i = 0; i < count; i++) {
        new (&this->m_data[i]) T(data[i]);
    }

    this->m_count = count;
}

template <class T>
void TSFixedArray<T>::SetCount(uint32_t count) {
    if (count != this->m_count) {
        if (count) {
            this->ReallocData(count);

            for (uint32_t i = this->m_count; i < count; i++) {
                new (&this->m_data[i]) T();
            }

            this->m_count = count;
        } else {
            this->Clear();
        }
    }
}

#endif
