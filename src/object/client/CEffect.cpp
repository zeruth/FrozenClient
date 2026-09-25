#include "object/client/CEffect.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2ParticleEmitter.hpp"
#include "model/CM2Shared.hpp"

// ref: FUN_006f75f0
// frozen leaves an emitter slot null for an emitter type it does not have (see
// CM2Model::InitializeLoaded), so a null slot is skipped; the reference has no such slots.
void CEffect::SetEmittersFlag400000(int32_t enable) {
    if (!this->m_model || !this->m_model->IsLoaded(0, 0)) {
        return;
    }

    for (uint32_t i = 0; ; i++) {
        auto model = this->m_model;

        if (!model->m_loaded) {
            model->WaitForLoad(nullptr);
        }

        if (model->m_shared->m_data->particles.Count() <= i) {
            break;
        }

        model = this->m_model;

        if (!model->m_loaded) {
            model->WaitForLoad(nullptr);
        }

        auto emitter = model->m_particleEmitters[i];

        if (!emitter) {
            continue;
        }

        if (enable == 0) {
            emitter->m_flags &= ~0x400000;
        } else {
            emitter->m_flags |= 0x400000;
        }
    }
}

// ref: FUN_006f76c0
void CEffect::LinkToHead(CEffect** head) {
    if (this->m_linkPrev) {
        *this->m_linkPrev = this->m_linkNext;
    }

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = this->m_linkPrev;
    }

    this->m_linkNext = nullptr;
    this->m_linkPrev = head;
    this->m_linkNext = *head;
    *head = this;

    if (this->m_linkNext) {
        this->m_linkNext->m_linkPrev = &this->m_linkNext;
    }
}

// ref: FUN_006f7850
void CEffect::DetachModel() {
    if (this->m_model && this->m_model->m_attachParent) {
        this->m_model->DetachFromParent();
    }
}
