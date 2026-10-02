#include "gx/buffer/CGxPool.hpp"

// ref: FUN_00688260
void CGxPool::Discard() {
    this->Invalidate();
    this->unk1C = 0;
}

// ref: FUN_00688230
void CGxPool::Invalidate() {
    for (auto buf = this->m_bufList.Head(); buf; buf = this->m_bufList.Next(buf)) {
        buf->unk1C = 0;
    }
}

// ref: FUN_00687740
// The buffer list and the pool's own link unlink themselves.
CGxPool::~CGxPool() {
}

// ref: FUN_00685c00
// Detaches a buffer from this pool: off the list, and no longer pointing back at it.
void CGxPool::BufRemove(CGxBuf* buf) {
    buf->Unlink();
    buf->m_pool = nullptr;
}
