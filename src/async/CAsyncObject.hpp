#ifndef ASYNC_C_ASYNC_OBJECT_HPP
#define ASYNC_C_ASYNC_OBJECT_HPP

#include <storm/List.hpp>

class SFile;
class CAsyncQueue;

typedef void (*ASYNC_CALLBACK)(void*);

class CAsyncObject {
    public:
        // Member variables
        SFile* file;
        void* buffer;
        uint32_t size;
        void* userArg;
        ASYNC_CALLBACK userPostloadCallback;
        ASYNC_CALLBACK userFailedCallback;
        CAsyncQueue* queue;
        // +0x1c. The frame the read was last ASKED FOR, out of CGxDevice::m_frameCount. The
        // priority bump stamps it so that a texture being drawn now overtakes one requested
        // earlier and not wanted since; nothing compares two stamps yet, but both of the
        // reference's writers write this and neither writes a pointer.
        uint32_t m_frameStamp;
        uint8_t priority;
        uint8_t isProcessed;
        uint8_t isRead;
        uint8_t isCurrent;
        uint8_t char24;
        uint8_t char25;
        uint8_t padding[2];
        TSLink<CAsyncObject> link;
};

#endif
