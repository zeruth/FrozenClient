#ifndef UTIL_BIT_ARRAY_HPP
#define UTIL_BIT_ARRAY_HPP

#include <cstdint>

class BitArray {
    public:
        // Member variables
        uint8_t* m_bits;
        uint32_t m_count;
        bool m_owned;

        // Member functions
        BitArray();
        ~BitArray();
        void Assign(uint8_t* bits, uint32_t count, bool owned);
        bool IsSet(uint32_t index) const;
        void Set(uint32_t index, bool value);
};

#endif
