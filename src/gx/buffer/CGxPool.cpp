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
