#include "object/client/CGPlayer_C.hpp"
#include "object/client/CGObject_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "model/Model2.hpp"
#include "model/CM2Model.hpp"
#include "object/client/ObjMgr.hpp"
#include "object/Types.hpp"
#include "world/World.hpp"
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"

CGObject_C::CGObject_C(uint32_t time, CClientObjCreate& objCreate) {
    // TODO

    this->m_model = nullptr;
    this->m_worldObject = 0;

    // TODO

    this->m_lockCount = 0;
    this->m_disabled = false;
    this->m_inReenable = false;
    this->m_postInited = false;
    this->m_flag19 = false;
    this->m_disablePending = false;

    // TODO

    ClntObjMgrLinkInNewObject(this);

    // TODO
}

CGObject_C::~CGObject_C() {
    // TODO
}

void CGObject_C::AddWorldObject() {
    if (!this->m_model) {
        const char* fileName;
        if (this->GetModelFileName(fileName)) {
            auto model = CWorld::GetM2Scene()->CreateModel(fileName, 0);
            model->SetLightingCallback(&CWorld::LightingCallback, nullptr);
            this->SetModel(model);

            // Players wear a composited character body; other units keep their creature skin
            if (this->IsA(TYPE_PLAYER)) {
                static_cast<CGPlayer_C*>(this)->BuildCharacterComponent();
            }

            // Set the unit's looping idle pose from its current state (dead / stand state / emote).
            // The per-frame update in CGWorldFrame re-runs this so the pose tracks state changes; the
            // sequence is queued inside the model if it is still loading.
            if (this->IsA(TYPE_UNIT)) {
                static_cast<CGUnit_C*>(this)->UpdateIdleAnimation();
            }

            // Non-player units carry their weapons in UNIT_VIRTUAL_ITEM_SLOT_ID; the reference
            // attaches those to the creature's hands, the same way players equip weapons.
            if (this->IsA(TYPE_UNIT) && !this->IsA(TYPE_PLAYER)) {
                auto unitC = static_cast<CGUnit_C*>(this);
                auto unit = unitC->Unit();

                // A humanoid NPC (its display carries extended character data) is composited like a
                // player from its CreatureDisplayInfoExtra; only ordinary creature models fall back
                // to the monster skin/geoset path, exactly as the reference chooses between them.
                if (!unitC->BuildNpcCharacterComponent()) {
                    auto modelDataRec = unitC->GetModelData();
                    auto displayInfoRec = g_creatureDisplayInfoDB.GetRecord(unitC->GetDisplayID());

                    if (displayInfoRec) {
                        CCharacterComponent::ApplyMonsterGeosets(model, displayInfoRec);
                        CCharacterComponent::ReplaceMonsterSkin(model, displayInfoRec, modelDataRec);
                    }
                }

                if (unit) {
                    static const INVENTORY_SLOTS handSlots[3] = { INVSLOT_MAINHAND, INVSLOT_OFFHAND, INVSLOT_RANGED };
                    bool sheathed = (unit->bytes2 & 0xFF) == 0;

                    for (int32_t i = 0; i < 3; i++) {
                        int32_t entryID = unit->virtualItemSlotID[i];

                        if (!entryID) {
                            continue;
                        }

                        auto itemRec = g_itemDB.GetRecord(entryID);

                        if (!itemRec || itemRec->m_displayInfoID <= 0) {
                            continue;
                        }

                        auto displayRec = g_itemDisplayInfoDB.GetRecord(itemRec->m_displayInfoID);

                        if (!displayRec) {
                            continue;
                        }

                        bool shield = itemRec->m_inventoryType == INVTYPE_SHIELD;
                        bool heldRight = itemRec->m_inventoryType == INVTYPE_RANGEDRIGHT || itemRec->m_inventoryType == INVTYPE_THROWN;

                        CCharacterComponent::AddHandItem(
                            model,
                            displayRec,
                            handSlots[i],
                            static_cast<SHEATHE_TYPE>(itemRec->m_sheatheType),
                            sheathed,
                            shield,
                            heldRight,
                            0
                        );
                    }
                }
            }

            model->Release();
        }
    }

    if (!this->m_model) {
        return;
    }

    if (ClntObjMgrGetPlayerType() != PLAYER_NORMAL) {
        return;
    }

    if (this->m_worldObject) {
        // TODO SysMsgPrintf(1, 2, "OBJECTALREADYACTIVE|0x%016I64X", this->GetGUID());

        return;
    }

    uint32_t objFlags = 0x0;

    if (this->IsA(TYPE_GAMEOBJECT)) {
        objFlags |= 0x8 | 0x2 | 0x1;
    } else if (this->IsA(TYPE_DYNAMICOBJECT)) {
        objFlags |= 0x8 | 0x2;
    } else if (this->IsA(TYPE_CORPSE)) {
        // TODO
    } else if (this->IsA(TYPE_UNIT)) {
        // TODO

        objFlags |= 0x10;

        if (this->IsA(TYPE_PLAYER)) {
            objFlags |= 0x20;
        }
    }

    this->m_worldObject = CWorld::AddObject(this->GetObjectModel(), nullptr, nullptr, this->GetGUID(), 0, objFlags);

    if (!this->m_inReenable && this->m_postInited) {
        this->UpdateWorldObject(false);
    }
}

int32_t CGObject_C::CanBeTargetted() {
    return false;
}

int32_t CGObject_C::CanHighlight() {
    return false;
}

void CGObject_C::Disable() {
    // TODO

    this->m_disabled = true;
    // TODO other flag manipulation

    this->m_disableTimeMs = CWorld::GetCurTimeMs();
}

float CGObject_C::GetFacing() const {
    return 0.0f;
}

int32_t CGObject_C::GetModelFileName(const char*& name) const {
    return false;
}

CM2Model* CGObject_C::GetObjectModel() {
    return this->m_model;
}

C3Vector CGObject_C::GetPosition() const {
    return { 0.0f, 0.0f, 0.0f };
}

float CGObject_C::GetRawFacing() const {
    return this->GetFacing();
}

WOWGUID CGObject_C::GetTransportGUID() const {
    return 0;
}

int32_t CGObject_C::IsInReenable() {
    return this->m_inReenable;
}

int32_t CGObject_C::IsObjectLocked() {
    return this->m_lockCount != 0;
}

void CGObject_C::PostReenable() {
    // TODO

    this->m_inReenable = false;

    // TODO
}

void CGObject_C::Reenable() {
    this->m_disabled = false;
    this->m_inReenable = true;

    // TODO
}

void CGObject_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_postInited = true;

    // TODO
}

uint32_t CGObject_C::GetBlock(uint32_t block) const {
    auto storage = reinterpret_cast<const uint32_t*>(this->m_obj);

    return storage[block];
}

uint32_t CGObject_C::BlockIndexOf(const void* field) const {
    auto storage = reinterpret_cast<const uint32_t*>(this->m_obj);

    return static_cast<uint32_t>(static_cast<const uint32_t*>(field) - storage);
}

void CGObject_C::SetBlock(uint32_t block, uint32_t value) {
    auto storage = reinterpret_cast<uint32_t*>(this->m_obj);
    storage[block] = value;
}

void CGObject_C::SetDisablePending(int32_t pending) {
    if (pending) {
        this->m_disablePending = true;
    } else {
        this->m_disablePending = false;
    }
}

void CGObject_C::SetModel(CM2Model* model) {
    // No change
    if (this->m_model == model) {
        return;
    }

    if (model) {
        model->AddRef();
    }

    this->m_model = model;

    this->SetModelFinish(model);
}

void CGObject_C::SetModelFinish(CM2Model* model) {
    // TODO
}

void CGObject_C::SetObjectLocked(int32_t locked) {
    if (locked) {
        if (this->m_lockCount != 0xFFFF) {
            this->m_lockCount++;
        }
    } else {
        if (this->m_lockCount != 0) {
            this->m_lockCount--;
        }
    }
}

void CGObject_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->m_obj = reinterpret_cast<CGObjectData*>(&storage[CGObject::GetBaseOffset()]);
    this->m_objSaved = &saved[CGObject::GetBaseOffsetSaved()];
}

void CGObject_C::SetTypeID(OBJECT_TYPE_ID typeID) {
    this->m_typeID = typeID;

    switch (typeID) {
        case ID_OBJECT:
            this->m_obj->m_type = HIER_TYPE_OBJECT;
            break;

        case ID_ITEM:
            this->m_obj->m_type = HIER_TYPE_ITEM;
            break;

        case ID_CONTAINER:
            this->m_obj->m_type = HIER_TYPE_CONTAINER;
            break;

        case ID_UNIT:
            this->m_obj->m_type = HIER_TYPE_UNIT;
            break;

        case ID_PLAYER:
            this->m_obj->m_type = HIER_TYPE_PLAYER;
            break;

        case ID_GAMEOBJECT:
            this->m_obj->m_type = HIER_TYPE_GAMEOBJECT;
            break;

        case ID_DYNAMICOBJECT:
            this->m_obj->m_type = HIER_TYPE_DYNAMICOBJECT;
            break;

        case ID_CORPSE:
            this->m_obj->m_type = HIER_TYPE_CORPSE;
            break;

        default:
            break;
    }
}

void CGObject_C::UpdateWorldObject(int32_t a2) {
    // TODO
}
