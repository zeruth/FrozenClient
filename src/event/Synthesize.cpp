#include "event/Synthesize.hpp"
#include "event/EvtContext.hpp"
#include "event/Queue.hpp"
#include <common/Time.hpp>
#include <cstdio>
#include <cstdlib>

void SynthesizeDestroy(EvtContext* context) {
    // TODO

    IEvtQueueDispatch(context, EVENT_ID_CLOSE, nullptr);

    context->m_critsect.Enter();
    context->m_schedState = EvtContext::SCHEDSTATE_DESTROYED;
    context->m_critsect.Leave();

    IEvtQueueDispatchAll(context);

    IEvtQueueDispatch(context, EVENT_ID_DESTROY, nullptr);

    // TODO

    // Leave without running static destructors. exit() runs them, and the client keeps its caches
    // and lists in namespace-scope Storm containers whose nodes live in Storm/ObjectAlloc heaps;
    // static destruction order across translation units is unspecified, so a container can walk
    // nodes whose heap is already gone. That faulted on every clean exit (the component texture
    // cache, then the connection list), each fix only exposing the next container. Everything that
    // must persist has been written by the shutdown above, so nothing is left to release.
    fflush(nullptr);
    quick_exit(0);
}

void SynthesizeIdle(EvtContext* context, uint32_t currTime, float elapsedSec) {
    bool closed;

    context->m_critsect.Enter();
    closed = context->m_schedState == EvtContext::SCHEDSTATE_CLOSED;
    context->m_critsect.Leave();

    if (closed) {
        return;
    }

    uint32_t schedFlags = context->m_schedFlags;

    if (schedFlags & 0x2) {
        context->m_schedFlags = schedFlags | 0x4;
    }

    EVENT_DATA_IDLE data;
    data.elapsedSec = elapsedSec;
    data.time = currTime;

    IEvtQueueDispatch(context, EVENT_ID_IDLE, &data);
}

int32_t SynthesizeInitialize(EvtContext* context) {
    uint32_t schedFlags = context->m_schedFlags;

    if (schedFlags & 0x1) {
        return 0;
    }

    context->m_schedFlags = schedFlags | 0x1;
    context->m_schedLastIdle = OsGetAsyncTimeMs();

    IEvtQueueDispatch(context, EVENT_ID_INITIALIZE, nullptr);

    return 1;
}

void SynthesizePaint(EvtContext* context) {
    bool closed;

    context->m_critsect.Enter();
    closed = context->m_schedState == EvtContext::SCHEDSTATE_CLOSED;
    context->m_critsect.Leave();

    if (closed) {
        return;
    }

    uint32_t schedFlags = context->m_schedFlags;

    if (schedFlags & 0x4) {
        context->m_schedFlags = schedFlags & ~0x4;
        IEvtQueueDispatch(context, EVENT_ID_PAINT, nullptr);
    }
}

void SynthesizePoll(EvtContext* context) {
    bool closed;

    context->m_critsect.Enter();
    closed = context->m_schedState == EvtContext::SCHEDSTATE_CLOSED;
    context->m_critsect.Leave();

    if (closed) {
        return;
    }

    IEvtQueueDispatch(context, EVENT_ID_POLL, nullptr);
}

void SynthesizeTick(EvtContext* context, uint32_t currTime, float elapsedSec) {
    context->m_critsect.Enter();
    bool closed = context->m_schedState == EvtContext::SCHEDSTATE_CLOSED;
    context->m_critsect.Leave();

    if (closed) {
        return;
    }

    EVENT_DATA_TICK data = { elapsedSec, currTime };

    IEvtQueueDispatch(context, EVENT_ID_TICK, &data);
}
