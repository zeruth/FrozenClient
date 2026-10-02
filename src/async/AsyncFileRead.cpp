#include <storm/Array.hpp>
#include "async/AsyncFileRead.hpp"
#include "async/CAsyncObject.hpp"
#include "gx/Device.hpp"
#include "util/SFile.hpp"
#include <common/Prop.hpp>
#include <common/Time.hpp>
#include <storm/Error.hpp>

uint32_t AsyncFileRead::s_threadSleep;
uint32_t AsyncFileRead::s_handlerTimeout = 100;
CAsyncObject* AsyncFileRead::s_asyncWaitObject;
void* AsyncFileRead::s_progressCallback;
void* AsyncFileRead::s_progressParam;
int32_t AsyncFileRead::s_progressCount;
void* AsyncFileRead::s_ingameProgressCallback;
void* AsyncFileRead::s_ingameStartCallback;
void* AsyncFileRead::s_propContext;
SEvent AsyncFileRead::s_shutdownEvent = SEvent(1, 0);
const char* AsyncFileRead::s_asyncQueueNames[NUM_ASYNC_QUEUES] = {
    "Disk Queue",
    "Net Geometry Queue",
    "Net Texture Queue"
};
CAsyncQueue* AsyncFileRead::s_asyncQueues[NUM_ASYNC_QUEUES];
SCritSect AsyncFileRead::s_queueLock;
SCritSect AsyncFileRead::s_userQueueLock;
TSList<CAsyncQueue, TSGetLink<CAsyncQueue>> AsyncFileRead::s_asyncQueueList;
TSList<CAsyncThread, TSGetLink<CAsyncThread>> AsyncFileRead::s_asyncThreadList;
STORM_EXPLICIT_LIST(CAsyncObject, link) AsyncFileRead::s_asyncFileReadPostList;
STORM_EXPLICIT_LIST(CAsyncObject, link) AsyncFileRead::s_asyncFileReadFreeList;
int32_t AsyncFileRead::s_waiting;
// ref: DAT_00b4a1ec
int32_t AsyncFileRead::s_queueLockHeld;

CAsyncQueue* AsyncFileReadCreateQueue() {
    CAsyncQueue* queue = AsyncFileRead::s_asyncQueueList.NewNode(0, 2, 0x8);
    return queue;
}

void AsyncFileReadCreateThread(CAsyncQueue* queue, const char* queueName) {
    CAsyncThread* thread = AsyncFileRead::s_asyncThreadList.NewNode(0, 2, 0x8);

    thread->queue = queue;
    thread->currentObject = nullptr;

    SThread::Create(AsyncFileReadThread, thread, thread->thread, const_cast<char*>(queueName), 0);
}

// ref: FUN_004ba3d0
// Put the object back in its queue's read list at the place its priority earns. `a2` decides what
// happens against an EQUAL priority: with it set the object goes in front of its equals, which is
// how a caller raises an already-queued read.
void AsyncFileReadLinkObject(CAsyncObject* object, int32_t a2) {
    if (!object->queue) {
        return;
    }

    object->link.Unlink();

    auto& readList = object->queue->readList;

    for (auto currentObject = readList.Head(); currentObject; currentObject = readList.Link(currentObject)->Next()) {
        uint8_t priority = object->priority;
        uint8_t currentPriority = currentObject->priority;

        if (priority <= currentPriority && (a2 || priority != currentPriority)) {
            readList.LinkNode(object, 2, currentObject);
            object->char25 = 0;

            return;
        }
    }

    readList.LinkToTail(object);
    object->char25 = 0;
}

// Called after every poll (0x00b4a224 / 0x00b4a228), and asked how many reads they still have
// outstanding when a wait for everything begins (0x00b4a234 / 0x00b4a238). The texture system
// registers one of each at startup.
static TSGrowableArray<void (*)()> s_asyncPollCallbacks;
static TSGrowableArray<int32_t (*)()> s_asyncPendingCounters;

// ref: FUN_004b9b20
// Hand finished reads to their callbacks, for as long as the handler's time budget allows, then
// run the registered poll callbacks.
int32_t AsyncFileReadPollHandler(const void* a1, void* a2) {
    uint32_t start = OsGetAsyncTimeMsPrecise();

    while (1) {
        AsyncFileRead::s_queueLock.Enter();

        CAsyncObject* object = AsyncFileRead::s_asyncFileReadPostList.Head();

        if (!object) {
            AsyncFileRead::s_queueLock.Leave();
            break;
        }

        AsyncFileRead::s_asyncFileReadPostList.UnlinkNode(object);

        if (AsyncFileRead::s_asyncWaitObject == object) {
            AsyncFileRead::s_asyncWaitObject = nullptr;
        }

        object->isProcessed = 1;

        AsyncFileRead::s_queueLock.Leave();

        object->userPostloadCallback(object->userArg);

        AsyncFileRead::s_progressCount--;

        // Check if we're exceeded the allowed running time
        if (OsGetAsyncTimeMsPrecise() - start > AsyncFileRead::s_handlerTimeout) {
            break;
        }
    }

    for (uint32_t i = 0; i < s_asyncPollCallbacks.Count(); i++) {
        s_asyncPollCallbacks[i]();
    }

    return 1;
}

uint32_t AsyncFileReadThread(void* param) {
    CAsyncThread* thread = static_cast<CAsyncThread*>(param);

    PropSelectContext(AsyncFileRead::s_propContext);

    while (AsyncFileRead::s_shutdownEvent.Wait(0)) {
        uint32_t sleep = 0;
        CAsyncObject* object;

        while (1) {
            AsyncFileRead::s_queueLock.Enter();

            object = thread->queue->readList.Head();

            if (object && thread->queue->int20 && /* TODO */ true) {
                // TODO
                // Sub4BA530(object, 1);

                AsyncFileRead::s_queueLock.Leave();
                continue;
            }

            if (!object) {
                object = thread->queue->list14.Head();
            }

            if (!object) {
                AsyncFileRead::s_queueLock.Leave();
                break;
            }

            object->link.Unlink();
            object->queue = nullptr;
            object->isCurrent = 1;
            thread->currentObject = object;

            AsyncFileRead::s_queueLock.Leave();

            int32_t tries = 10;
            while (1) {
                if (SFile::IsStreamingMode() && object->file) {
                    // TODO
                    // Sub421820(object->file, (object->priority > 127) + 1, 1);
                }

                if (SFile::Read(object->file, object->buffer, object->size, nullptr, nullptr, nullptr)) {
                    break;
                }

                tries--;

                // Handle failure
                if (tries == 0) {
                    // TODO
                    // Sub421850((object->file, v17, 512);
                    // v10 = Sub7717E0();
                    // Sub771A80(v10, v18, 512);
                    // nullsub_3(v17);

                    break;
                }
            }

            AsyncFileRead::s_queueLock.Enter();

            AsyncFileRead::s_asyncFileReadPostList.LinkToTail(object);

            thread->currentObject = nullptr;
            object->isCurrent = 0;
            object->isRead = 1;

            AsyncFileRead::s_queueLock.Leave();

            if (AsyncFileRead::s_threadSleep) {
                sleep++;

                if (sleep == AsyncFileRead::s_threadSleep) {
                    OsSleep(1);
                    sleep = 0;
                }
            }
        }

        OsSleep(1);
    }

    return 0;
}

// ref: FUN_004bac20
// Move a queued read to the front of its priority band and stamp it with the current frame.
// The stamp is what lets a texture wanted THIS frame outrank one asked for earlier and not
// drawn since. char25 gates the relink: an object the blocking wait path has already claimed
// (AsyncFileReadObject clears it there) is left exactly where it is.
void AsyncReadBumpPriority(CAsyncObject* object) {
    object->m_frameStamp = g_theGxDevicePtr->m_frameCount;

    if (object->char25) {
        AsyncFileReadLinkObject(object, 1);
    }
}

// ref: FUN_004ba060
void AsyncFileReadWait(CAsyncObject* object) {
    STORM_ASSERT(object);

    AsyncFileRead::s_waiting++;

    AsyncFileRead::s_queueLock.Enter();

    if (object->isProcessed) {
        AsyncFileRead::s_queueLock.Leave();
        return;
    }

    AsyncFileRead::s_asyncWaitObject = object;

    if (!object->isCurrent && !object->isRead) {
        object->link.Unlink();
        object->queue->readList.LinkToHead(object);
    }

    AsyncFileRead::s_queueLock.Leave();

    if (SFile::IsStreamingMode()) {
        // TODO
    }

    // TODO

    if (AsyncFileRead::s_ingameStartCallback) {
        // TODO AsyncFileRead::s_ingameStartCallback();
    }

    while (true) {
        if (AsyncFileRead::s_ingameProgressCallback) {
            // TODO AsyncFileRead::s_ingameProgressCallback(0.0, 0);
        }

        AsyncFileReadPollHandler(nullptr, nullptr);

        if (!AsyncFileRead::s_asyncWaitObject) {
            break;
        }

        OsSleep(1);
    }

    AsyncFileRead::s_waiting--;
}

// ref: FUN_004b9910
void AsyncFileReadSetProgressCallback(void* callback, void* param) {
    AsyncFileRead::s_progressCallback = callback;
    AsyncFileRead::s_progressParam = param;
}

// ref: FUN_004b9950
void AsyncFileReadLockQueue() {
    AsyncFileRead::s_queueLock.Enter();
    AsyncFileRead::s_queueLockHeld = 1;
}

// ref: FUN_004b9970
void AsyncFileReadUnlockQueue() {
    AsyncFileRead::s_queueLock.Leave();
    AsyncFileRead::s_queueLockHeld = 0;
}

// ref: FUN_004bad80
// Whether any read is still outstanding: a thread holding a request, a queue with requests
// waiting, or a finished read not yet handed to its callback.
bool AsyncFileReadIsBusy() {
    AsyncFileRead::s_queueLock.Enter();

    bool busy = false;

    for (auto thread = AsyncFileRead::s_asyncThreadList.Head(); thread; thread = AsyncFileRead::s_asyncThreadList.Next(thread)) {
        if (thread->currentObject) {
            busy = true;
        }
    }

    for (auto queue = AsyncFileRead::s_asyncQueueList.Head(); queue; queue = AsyncFileRead::s_asyncQueueList.Next(queue)) {
        if (queue->readList.Head()) {
            busy = true;
        }
    }

    if (AsyncFileRead::s_asyncFileReadPostList.Head()) {
        busy = true;
    }

    AsyncFileRead::s_queueLock.Leave();

    return busy;
}

// ref: FUN_004b9c60
// Adds a callback to run after every poll, once.
void AsyncFileReadRegisterPollCallback(void (*callback)()) {
    for (uint32_t i = 0; i < s_asyncPollCallbacks.Count(); i++) {
        if (s_asyncPollCallbacks[i] == callback) {
            return;
        }
    }

    *s_asyncPollCallbacks.New() = callback;
}

// ref: FUN_004b9d20
// Adds a counter of outstanding reads for AsyncFileReadWaitAll to sum, once.
void AsyncFileReadRegisterPendingCounter(int32_t (*counter)()) {
    for (uint32_t i = 0; i < s_asyncPendingCounters.Count(); i++) {
        if (s_asyncPendingCounters[i] == counter) {
            return;
        }
    }

    *s_asyncPendingCounters.New() = counter;
}

// ref: FUN_004bae10
// Blocks until every outstanding read has been handed to its callback, reporting progress as the
// share of the reads outstanding at the start that have since finished. The progress callback is
// one-shot: it is cleared on the way out.
void AsyncFileReadWaitAll() {
    int32_t total = 0;

    for (uint32_t i = 0; i < s_asyncPendingCounters.Count(); i++) {
        total += s_asyncPendingCounters[i]();
    }

    AsyncFileRead::s_progressCount = total;

    while (AsyncFileReadIsBusy()) {
        AsyncFileReadPollHandler(nullptr, nullptr);

        if (AsyncFileRead::s_progressCallback) {
            float progress = 1.0f;

            if (total) {
                progress = static_cast<float>(total - AsyncFileRead::s_progressCount) / static_cast<float>(total);

                if (progress < 0.0f) {
                    progress = 0.0f;
                } else if (progress > 1.0f) {
                    progress = 1.0f;
                }
            }

            reinterpret_cast<void (*)(float, void*)>(AsyncFileRead::s_progressCallback)(progress, AsyncFileRead::s_progressParam);
        }

        OsSleep(1);
    }

    AsyncFileRead::s_progressCallback = nullptr;
}
