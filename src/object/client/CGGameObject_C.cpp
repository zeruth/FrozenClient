#include "object/client/CGGameObject_C.hpp"
#include "db/Db.hpp"
#include <storm/String.hpp>

CGGameObject_C::CGGameObject_C(uint32_t time, CClientObjCreate& objCreate) : CGObject_C(time, objCreate) {
    // TODO
}

CGGameObject_C::~CGGameObject_C() {
    // TODO
}

void CGGameObject_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    // TODO

    this->CGObject_C::PostInit(time, init, a4);

    // TODO
}

void CGGameObject_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_gameObj = reinterpret_cast<CGGameObjectData*>(&storage[CGGameObject::GetBaseOffset()]);
    this->m_gameObjSaved = &saved[CGGameObject::GetBaseOffsetSaved()];
}

int32_t CGGameObject_C::GetModelFileName(const char*& name) const {
    // Without this every game object in the world was invisible: the base CGObject_C returns false,
    // so AddWorldObject never created a model. 136 of the 138 objects visible at the Ebon Hold spawn
    // are game objects, which is most of what the zone is built from.
    auto display = g_gameObjectDisplayInfoDB.GetRecord(this->GetDisplayID());

    if (!display || !display->m_modelName || !*display->m_modelName) {
        return false;
    }

    name = display->m_modelName;

    // 4% of GameObjectDisplayInfo rows name a .wmo rather than a model. Those need the WMO loader,
    // not CM2Scene::CreateModel, and handing one to CreateModel would fail to load while looking
    // like it had been handled. Refuse them here so they stay visibly unimplemented rather than
    // silently broken -- see the WMO game object note in docs/world-render-inventory.md.
    size_t len = SStrLen(name);

    if (len > 4 && !SStrCmpI(name + len - 4, ".wmo", STORM_MAX_STR)) {
        return false;
    }

    return true;
}
