#include "gx/CGxMatrixStack.hpp"

CGxMatrixStack::CGxMatrixStack() {
    this->m_flags[0] = F_Identity;
}

void CGxMatrixStack::Pop() {
    if (this->m_level > 0) {
        this->m_level--;
    }

    this->m_dirty = 1;
}

void CGxMatrixStack::Push() {
    if (this->m_level < 3) {
        this->m_level++;
    }

    this->m_mtx[this->m_level] = this->m_mtx[this->m_level - 1];
    this->m_flags[this->m_level] = this->m_flags[this->m_level - 1];

    this->m_dirty = 1;
}

C44Matrix& CGxMatrixStack::Top() {
    this->m_dirty = 1;
    this->m_flags[this->m_level] &= ~F_Identity;
    return this->m_mtx[this->m_level];
}

const C44Matrix& CGxMatrixStack::TopConst() {
    return this->m_mtx[this->m_level];
}

// ref: FUN_0057c340
// Loads the identity into the top of the stack, unless the top is already flagged as one --
// in which case nothing is written and the stack is not dirtied. The flag is assigned, not
// or-ed in. The reference reaches this on the world stack straight after an XformPush, and on
// the texgen stacks whenever a stage leaves a matrix-generating mode.
void CGxMatrixStack::SetIdentity() {
    if (this->m_flags[this->m_level] & F_Identity) {
        return;
    }

    this->m_mtx[this->m_level] = C44Matrix();
    this->m_dirty = 1;
    this->m_flags[this->m_level] = F_Identity;
}
