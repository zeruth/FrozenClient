#ifndef UTIL_DYNAMIC_STRING_HPP
#define UTIL_DYNAMIC_STRING_HPP

#include <cstdint>

class DynamicString {
    public:
        // Member variables
        int32_t m_length;
        int32_t m_capacity;
        char* m_data;

        // Member functions
        ~DynamicString();
        void Grow();
        void Resize(int32_t size);
};

#endif
