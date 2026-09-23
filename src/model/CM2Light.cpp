#include "model/CM2Light.hpp"
#include "model/CM2Scene.hpp"

// ref: FUN_00834a40
CM2Light::CM2Light() {
}

// ref: FUN_008348d0
void CM2Light::Initialize(CM2Scene* scene) {
    this->m_scene = scene;

    // TODO
}

// ref: FUN_00834c70
//
// The directional half below is ported. The point-light half is NOT, and it is not a small
// omission: point lights do not go on m_lightList at all, they go into a SPATIAL HASH GRID that
// the scene allocates lazily.
//
// From 0x00834c97: if scene + 0x24 is null it is filled with a 0x4000-byte SMemAlloc, zeroed. The
// cell index comes from the light's position scaled by the 0.05 at 0x00af59d4 -- one twentieth, so
// twenty world units to a cell -- truncated, masked to 6 bits per axis and combined as
// `(y & 0x3f) << 6 | (x & 0x3f)`. That is 64 x 64 = 4096 cells of one pointer each, which is
// exactly the 0x4000 bytes. Lights wrap every 1280 units.
//
// Porting it means the grid, its insert, the matching removal in Unlink, and the query in
// CM2Scene::SelectLights -- see the note there for where this sits in the chain.
void CM2Light::Link() {
    if (!this->m_visible || !this->m_scene) {
        return;
    }

    if (this->m_type == M2LIGHT_1) {
        // TODO -- the spatial hash grid described above
    } else {
        if (!(this->m_scene->m_flags & 0x1)) {
            this->m_lightPrev = &this->m_scene->m_lightList;
            this->m_lightNext = this->m_scene->m_lightList;
            this->m_scene->m_lightList = this;

            if (this->m_lightNext) {
                this->m_lightNext->m_lightPrev = &this->m_lightNext;
            }
        }
    }
}

void CM2Light::SetDirection(const C3Vector& dir) {
    this->m_dir = dir;

    if (this->m_dir.SquaredMag() > 0.00000023841858) {
        this->m_dir.Normalize();
    }
}

// ref: FUN_00835640
void CM2Light::SetLightType(M2LIGHTTYPE lightType) {
    if (this->m_type == lightType) {
        return;
    }

    this->m_type = lightType;

    if (this->m_visible && this->m_scene) {
        this->Unlink();
        this->Link();
    }
}

void CM2Light::SetVisible(int32_t visible) {
    if (this->m_visible == visible) {
        return;
    }

    this->m_visible = visible;

    if (this->m_scene) {
        if (this->m_visible) {
            this->Link();
        } else {
            this->Unlink();
        }
    }
}

void CM2Light::Unlink() {
    if (this->m_lightPrev) {
        *this->m_lightPrev = this->m_lightNext;
    }

    if (this->m_lightNext) {
        this->m_lightNext->m_lightPrev = this->m_lightPrev;
    }

    this->m_lightPrev = nullptr;
    this->m_lightNext = nullptr;
}
