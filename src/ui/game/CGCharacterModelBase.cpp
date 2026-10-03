#include "ui/game/CGCharacterModelBase.hpp"
#include "ui/game/CGCharacterModelBaseScript.hpp"
#include "component/CCharacterComponent.hpp"
#include "db/Db.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/DBCacheInstances.hpp"
#include "object/client/ObjMgr.hpp"
#include <common/Time.hpp>

int32_t CGCharacterModelBase::s_metatable;
int32_t CGCharacterModelBase::s_objectType;

CSimpleFrame* CGCharacterModelBase::Create(CSimpleFrame* parent) {
    // TODO use CDataAllocator
    return STORM_NEW(CGCharacterModelBase)(parent);
}

// ref: FUN_00597a80
void CGCharacterModelBase::CreateScriptMetaTable() {
    auto L = FrameScript_GetContext();
    CGCharacterModelBase::s_metatable = FrameScript_Object::CreateScriptMetaTable(L, &CGCharacterModelBase::RegisterScriptMethods);
}

int32_t CGCharacterModelBase::GetObjectType() {
    if (!CGCharacterModelBase::s_objectType) {
        CGCharacterModelBase::s_objectType = ++FrameScript_Object::s_objectTypes;
    }

    return CGCharacterModelBase::s_objectType;
}

// ref: FUN_005972d0
bool CGCharacterModelBase::IsA(int32_t type) {
    return type == CGCharacterModelBase::GetObjectType()
        || CSimpleModel::IsA(type);
}

// ref: FUN_00597230
void CGCharacterModelBase::RegisterScriptMethods(lua_State* L) {
    CSimpleModel::RegisterScriptMethods(L);
    FrameScript_Object::FillScriptMethodTable(L, CGCharacterModelBaseMethods, NUM_CG_CHARACTER_MODEL_BASE_SCRIPT_METHODS);
}

// ref: FUN_00597370
// The model frame's own light: directional, a 0.7 grey ambient and a warm 0.8/0.8/0.64 key
// from straight ahead.
CGCharacterModelBase::CGCharacterModelBase(CSimpleFrame* parent) : CSimpleModel(parent) {
    this->m_light.SetLightType(M2LIGHT_0);
    this->m_light.SetDirection({ 0.0f, -0.70710599f, -0.70710599f });

    this->m_light.m_dirColor = { 0.8f, 0.8f, 0.64f };
    this->m_light.m_ambColor = { 0.7f, 0.7f, 0.7f };

    this->m_light.SetDirection({ 0.0f, 1.0f, 0.0f });
    this->m_light.SetVisible(1);
}

// ref: FUN_005970f0
// Turning the model plays the turn-in-place animation toward the new facing, unless the model
// lacks it or is already playing it.
void CGCharacterModelBase::SetRotation(float rotation) {
    if (this->m_model && this->m_model->IsLoaded(0, 0)) {
        uint32_t sequence = 0;

        if (rotation > this->m_facing) {
            sequence = 0xc;
        } else if (rotation < this->m_facing) {
            sequence = 0xb;
        }

        if (this->m_model->HasSequence(sequence) && this->m_model->GetBoneUint90(-1) != sequence) {
            this->m_model->SetBoneSequence(-1, sequence, -1, 0, 1.0f, 1, 1);
        }
    }

    this->m_turning = 1;
    uint32_t now = static_cast<uint32_t>(OsGetAsyncTimeMs());
    this->m_facing = rotation;
    this->m_turnEnd = now + 100;
}

// ref: FUN_005971b0
// A frame that stopped turning goes back to its stand animation once the deadline passes.
void CGCharacterModelBase::OnLayerUpdate(float elapsedSec) {
    CSimpleModel::OnLayerUpdate(elapsedSec);

    // The model test is frozen's: the reference calls through the model pointer unchecked, and a
    // frame shown before anything was set on it has none.
    if (!this->m_turning && this->m_turnEnd < static_cast<uint32_t>(OsGetAsyncTimeMs())) {
        if (this->m_model && this->m_model->GetBoneUint90(-1)) {
            this->m_model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1);
            this->m_turnEnd = 0;
        }
    }

    this->m_turning = 0;
}

// ref: FUN_005975b0
// A copy of the unit's own model, in this frame's scene.
CM2Model* CGCharacterModelBase::CreateModelFromUnit(CGUnit_C* unit) {
    if (!unit) {
        return nullptr;
    }

    return this->GetScene()->CreateModelFrom(unit->m_model, 0);
}

// ref: FUN_005975e0
// Shows a unit: its model duplicated with everything attached to it stripped, standing, with its
// hands closed around whatever the unit carries.
void CGCharacterModelBase::SetUnitModel(CGUnit_C* unit) {
    if (!unit) {
        return;
    }

    auto model = this->CreateModelFromUnit(unit);

    if (!model) {
        return;
    }

    if (!model->m_loaded) {
        model->WaitForLoad(nullptr);
    }

    ComponentRemoveAttachments(model);
    this->SetModel(model);

    model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1);

    for (auto attached = model->m_attachList; attached; attached = attached->m_attachNext) {
        if (attached->m_attachId == 2) {
            CCharacterComponent::ComponentCloseFingers(model, static_cast<COMP_HAND_SLOT>(1));
            break;
        }
    }

    for (auto attached = model->m_attachList; attached; attached = attached->m_attachNext) {
        if (attached->m_attachId == 1) {
            CCharacterComponent::ComponentCloseFingers(model, static_cast<COMP_HAND_SLOT>(0));
            break;
        }
    }

    model->Release();
}

// ref: FUN_00597700
// Shows a creature template: its first display's model with that display's skin.
void CGCharacterModelBase::SetCreatureModel(const CreatureStats_C* creature) {
    if (!creature) {
        return;
    }

    auto displayInfoRec = g_creatureDisplayInfoDB.GetRecord(creature->m_displayID[0]);

    if (!displayInfoRec) {
        return;
    }

    auto modelDataRec = g_creatureModelDataDB.GetRecord(displayInfoRec->m_modelID);

    if (!modelDataRec) {
        return;
    }

    auto model = this->GetScene()->CreateModel(modelDataRec->m_modelName, 0);

    if (!model) {
        return;
    }

    CCharacterComponent::ReplaceMonsterSkin(model, displayInfoRec, modelDataRec);
    this->SetModel(model);
    model->SetBoneSequence(-1, 0, -1, 0, 1.0f, 1, 1);
    model->Release();
}

// ref: FUN_005977e0
void CGCharacterModelBase::SetUnit(WOWGUID guid) {
    this->m_unitGUID = guid;
    this->m_creature = nullptr;

    if (this->m_visible) {
        auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(guid, TYPE_UNIT, __FILE__, __LINE__));

        if (unit) {
            this->SetUnitModel(unit);
        }
    }
}

// ref: FUN_00597840
void CGCharacterModelBase::SetCreature(const CreatureStats_C* creature) {
    this->m_unitGUID = 0;
    this->m_creature = creature;

    if (this->m_visible && creature) {
        this->SetCreatureModel(creature);
    }
}

// ref: FUN_00597870
// The model is built when the frame is about to show, from whichever of the unit and the
// creature was set last.
void CGCharacterModelBase::OnLayerShow() {
    if (!this->m_visible) {
        if (this->m_unitGUID) {
            auto unit = static_cast<CGUnit_C*>(ClntObjMgrObjectPtr(this->m_unitGUID, TYPE_UNIT, __FILE__, __LINE__));

            if (unit) {
                this->SetUnitModel(unit);
            }
        } else if (this->m_creature) {
            this->SetCreatureModel(this->m_creature);
        }
    }

    CSimpleModel::OnLayerShow();
}

int32_t CGCharacterModelBase::GetScriptMetaTable() {
    return CGCharacterModelBase::s_metatable;
}
