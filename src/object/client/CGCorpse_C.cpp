#include "object/client/CGCorpse_C.hpp"
#include "component/CCharacterComponent.hpp"
#include "component/ComponentData.hpp"
#include "db/Db.hpp"
#include "gx/RenderState.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "object/client/CEffect.hpp"
#include "object/client/CGItem_C.hpp"
#include "object/client/CGUnit_C.hpp"
#include "object/client/Mirror.hpp"
#include "object/client/ObjMgr.hpp"
#include "ui/game/CGGameUI.hpp"
#include "world/CWorld.hpp"
#include "world/Shadow.hpp"
#include "world/WorldFacets.hpp"
#include <storm/String.hpp>
#include <tempest/Box.hpp>
#include <tempest/Sphere.hpp>

namespace {

// The bones' file name, built where the reference keeps it (0x00ca1000).
char s_skeletonName[260];

// 0x00ad6628: the sex part of a skeleton's name.
const char* const s_sexNames[] = { "Male", "Female" };

// The facets under a corpse, kept between calls as the reference keeps them (0x00ca1104).
CFacetList s_groundFacets;

// ref: FUN_00706240
// CORPSE_FIELD_FLAGS changed: bones or a body, the model is made again.
int32_t OnCorpseFlagsChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    if (auto corpse = static_cast<CGCorpse_C*>(ClntObjMgrObjectPtr(guid, TYPE_CORPSE, ".\\Corpse_C.cpp", 0x22))) {
        corpse->RebuildModel();
    }

    return 1;
}

// ref: FUN_00706280
// CORPSE_FIELD_DYNAMIC_FLAGS changed: the loot sparkle follows.
int32_t OnCorpseDynamicFlagsChanged(WOWGUID guid, uint32_t offset, uint32_t size, const void* old, void* param) {
    if (auto corpse = static_cast<CGCorpse_C*>(ClntObjMgrObjectPtr(guid, TYPE_CORPSE, ".\\Corpse_C.cpp", 0x33))) {
        corpse->OnDynamicFlagsChanged(*static_cast<const uint32_t*>(old));
    }

    return 1;
}

} // namespace

// ref: FUN_00706430
CGCorpse_C::CGCorpse_C(uint32_t time, CClientObjCreate& objCreate)
    : CGObject_C(time, objCreate)
    , m_passenger(objCreate.move.status.transport, objCreate.move.status.position18, this->m_obj->m_guid) {
    this->m_passenger.m_facing = objCreate.move.status.facing24;
}

// ref: FUN_007065b0
CGCorpse_C::~CGCorpse_C() {
    if (this->m_component) {
        CCharacterComponent::FreeComponent(this->m_component);
        this->m_component = nullptr;
    }

    this->ReleaseLootSparkle();
}

// ref: FUN_00705b20
// The corpse appears where it lies: a body is dressed -- a character's composited from its
// appearance, a creature's reskinned -- and the items it wore put on (helm and cloak as it showed
// them); it lies dead or floats; a lootable one sparkles; and the player's own body is the one the
// game UI points the way back to.
//
// PARTIAL: the hand items look the slot's value up as an item object (FUN_00707330 for the sheath),
// which a corpse never has; and the tabard's guild emblem waits on the guild cache.
void CGCorpse_C::PostInit(uint32_t time, const CClientObjCreate& init, bool a4) {
    this->m_passenger.JoinTransport(init.move.status.position28);

    if (!this->GetTransportGUID()) {
        this->m_passenger.m_facing = init.move.status.facing34;
    }

    this->CGObject_C::PostInit(time, init, a4);

    auto data = this->Corpse();
    auto model = this->m_model;

    auto display = g_creatureDisplayInfoDB.GetRecord(data->displayID);
    auto modelData = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;
    bool character = !modelData || (modelData->m_flags & 0x4);

    if (!(data->flags & 0x1) && model) {
        if (!character) {
            CCharacterComponent::ReplaceMonsterSkin(model, display, modelData);
        } else {
            this->m_component = CCharacterComponent::AllocComponent();

            uint32_t bytes1 = data->bytes1;
            uint32_t bytes2 = data->bytes2;

            ComponentData component;
            component.raceID = (bytes1 >> 8) & 0xff;
            component.sexID = (bytes1 >> 16) & 0xff;
            component.skinColorID = (bytes1 >> 24) & 0xff;
            component.faceID = bytes2 & 0xff;
            component.hairStyleID = (bytes2 >> 8) & 0xff;
            component.hairColorID = (bytes2 >> 16) & 0xff;
            component.facialHairStyleID = (bytes2 >> 24) & 0xff;
            component.model = model;

            if (data->owner == ClntObjMgrGetActivePlayer()) {
                component.flags |= 0x2;
            } else {
                component.flags &= ~0x2u;
            }

            model->m_refCount++;

            this->m_component->Init(&component, nullptr);
        }
    }

    // In water it floats; on land it lies dead.
    uint32_t floorFlags;
    float floor;
    uint32_t floorArea;
    C3Vector position = this->GetPosition();

    if (!CWorld::GetObjectFloor(this->m_worldObject, &floorFlags, &floor, &floorArea)
        || floor - position.z <= 0.6666667f) {
        this->m_corpseFlags &= ~0x2u;

        if (model) {
            model->SetBoneSequence(0xffffffff, 6, 0xffffffff, 0, 1.0f, 1, 1);
        }
    } else {
        this->m_corpseFlags |= 0x2;

        if (model) {
            model->SetBoneSequence(0xffffffff, 0x84, 0xffffffff, 0, 1.0f, 1, 1);
        }
    }

    if (this->m_component) {
        for (uint32_t slot = 0; slot < 19; slot++) {
            uint32_t displayID = data->items[slot] & 0xffffff;
            auto item = g_itemDisplayInfoDB.GetRecord(static_cast<int32_t>(displayID));

            if (!item || slot == 17) {
                continue;
            }

            if (slot == 15 || slot == 16) {
                // The weapons: only an item object the slot names (never there for a corpse).
                continue;
            }

            if (slot == 0 && (data->flags & 0x8)) {
                continue;
            }

            if (slot == 14 && (data->flags & 0x10)) {
                continue;
            }

            this->m_component->AddItemBySlot(static_cast<INVENTORY_SLOTS>(slot), item->m_ID, 0);

            if (slot == 18 && (item->m_flags & 0x1)) {
                this->ApplyGuildTabard(true);
            }
        }
    }

    if (this->IsLootable()) {
        this->AttachLootSparkle();
    }

    if (!(data->flags & 0x1) && data->owner == ClntObjMgrGetActivePlayer()) {
        GameUISetCorpseGUID(this->GetGUID());
    }
}

void CGCorpse_C::SetStorage(uint32_t* storage, uint32_t* saved) {
    this->CGObject_C::SetStorage(storage, saved);

    this->m_corpse = reinterpret_cast<CGCorpseData*>(&storage[CGCorpse::GetBaseOffset()]);
    this->m_corpseSaved = &saved[CGCorpse::GetBaseOffsetSaved()];
}

// ref: FUN_007057a0
const char* CGCorpse_C::GetPortraitTextureName() const {
    auto displayID = this->Corpse()->displayID;

    if (displayID) {
        auto rec = g_creatureDisplayInfoDB.GetRecord(displayID);

        if (rec) {
            return rec->m_portraitTextureName;
        }
    }

    return nullptr;
}

// ref: FUN_007059a0
// TODO(GuildCache): the guild's tabard emblem (FUN_007eada0, asked for again through
// FUN_00705950 when `query` and the record has not arrived; then FUN_004ec1c0). Frozen has no
// guild cache.
void CGCorpse_C::ApplyGuildTabard(bool query) {
    (void)query;
}

// ref: FUN_00705ac0
// The display's own scale: the display's times its model's.
float CGCorpse_C::GetDisplayScale() const {
    auto display = g_creatureDisplayInfoDB.GetRecord(this->Corpse()->displayID);
    auto modelData = display ? g_creatureModelDataDB.GetRecord(display->m_modelID) : nullptr;

    if (!modelData) {
        return 1.0f;
    }

    return modelData->m_modelScale * display->m_creatureModelScale;
}

// ref: FUN_007058f0
int32_t CGCorpse_C::IsLootable() const {
    return this->Corpse()->dynamicFlags & 0x1;
}

// ref: FUN_00705900
// The loot sparkle (hard-coded effect 4) on the body's attachment 0x13.
void CGCorpse_C::AttachLootSparkle() {
    if (this->m_lootSparkle) {
        return;
    }

    auto effect = CEffect::GetHardcodedEffect(4);

    if (!effect) {
        return;
    }

    this->m_lootSparkle = CWorld::GetM2Scene()->CreateModel(effect->m_fileName, 0);

    if (this->m_lootSparkle && this->m_model) {
        this->m_lootSparkle->AttachToParent(this->m_model, 0x13, nullptr, 0);
    }
}

void CGCorpse_C::ReleaseLootSparkle() {
    if (!this->m_lootSparkle) {
        return;
    }

    if (this->m_lootSparkle->m_attachParent) {
        this->m_lootSparkle->DetachFromParent();
    }

    this->m_lootSparkle->Release();
    this->m_lootSparkle = nullptr;
}

// ref: FUN_00706120
// Bones or a body: the sparkle, the world entry and the component go, and the model the flags name
// now is made and entered again.
void CGCorpse_C::RebuildModel() {
    this->ReleaseLootSparkle();
    this->RemoveWorldObject();

    if (this->m_component) {
        CCharacterComponent::FreeComponent(this->m_component);
        this->m_component = nullptr;
    }

    this->m_corpseFlags &= ~0x1u;

    const char* fileName;

    if (!this->GetModelFileName(fileName)) {
        return;
    }

    auto model = CWorld::GetM2Scene()->CreateModel(fileName, 0);

    this->SetModel(model);

    if (this->IsLootable()) {
        this->AttachLootSparkle();
    }

    if (model) {
        model->Release();
    }

    this->AddWorldObject();
}

// ref: FUN_007061e0
void CGCorpse_C::OnDynamicFlagsChanged(uint32_t old) {
    uint32_t now = this->Corpse()->dynamicFlags;

    if (!((now ^ old) & 0x1)) {
        return;
    }

    if (now & 0x1) {
        this->AttachLootSparkle();
        return;
    }

    this->ReleaseLootSparkle();
}

// ref: FUN_00705f30
void CGCorpse_C::Disable() {
    this->CGObject_C::Disable();

    this->m_passenger.m_transportLink.Unlink();

    auto data = this->Corpse();

    if (!(data->flags & 0x1) && data->owner == ClntObjMgrGetActivePlayer()) {
        GameUISetCorpseGUID(0);
    }

    this->ReleaseLootSparkle();
}

// ref: FUN_00705fa0
void CGCorpse_C::Reenable() {
    this->CGObject_C::Reenable();

    this->m_corpseFlags &= ~0x1u;

    if (this->IsLootable()) {
        this->AttachLootSparkle();
    }

    if (this->m_model) {
        this->AddWorldObject();
    }

    auto data = this->Corpse();

    if (!(data->flags & 0x1) && data->owner == ClntObjMgrGetActivePlayer()) {
        GameUISetCorpseGUID(this->GetGUID());
    }
}

// ref: FUN_00705610
void CGCorpse_C::PostReenable() {
    this->m_passenger.LeaveTransport();
    this->CGObject_C::PostReenable();
}

// ref: FUN_007058b0
// A body fades in once its composited skin is uploaded.
int32_t CGCorpse_C::ShouldFadeOut() {
    if (!this->m_component) {
        return 1;
    }

    return TextureIsGxTexUploaded(this->m_component->m_baseTexture) && this->m_component->RenderPrep(0) ? 1 : 0;
}

// ref: FUN_007064e0
C3Vector CGCorpse_C::GetPosition() const {
    return this->m_passenger.GetPosition(this->m_passenger.m_position);
}

// ref: FUN_00706500
C3Vector CGCorpse_C::GetRawPosition() const {
    return this->m_passenger.m_position;
}

// ref: FUN_00706530
float CGCorpse_C::GetFacing() const {
    return this->m_passenger.GetFacing(this->m_passenger.m_facing);
}

// ref: FUN_00706550
float CGCorpse_C::GetRawFacing() const {
    return this->m_passenger.m_facing;
}

// ref: FUN_00706560
WOWGUID CGCorpse_C::GetTransportGUID() const {
    return this->m_passenger.m_transportGUID;
}

// ref: FUN_00705670
// Bones are the race's death skeleton; a body is its display's model.
int32_t CGCorpse_C::GetModelFileName(const char*& name) const {
    auto data = this->Corpse();
    uint32_t race = (data->bytes1 >> 8) & 0xff;
    uint32_t sex = (data->bytes1 >> 16) & 0xff;

    if (data->flags & 0x1) {
        auto rec = g_chrRacesDB.GetRecord(static_cast<int32_t>(race));

        if (!rec) {
            return 0;
        }

        SStrPrintf(s_skeletonName, sizeof(s_skeletonName), "World\\Generic\\PassiveDoodads\\DeathSkeletons\\%s%sDeathSkeleton.mdx",
                   rec->m_clientFileString, s_sexNames[sex & 1]);
        name = s_skeletonName;

        return 1;
    }

    auto display = g_creatureDisplayInfoDB.GetRecord(data->displayID);

    if (!display) {
        // "INVALIDPLAYERDISPLAYID|%d|%d|%d" (SysMsgPrintf) in the reference.
        return 0;
    }

    auto modelData = g_creatureModelDataDB.GetRecord(display->m_modelID);

    if (!modelData) {
        // "INVALIDPLAYERMODELRECORD|%d|%d|%d" (SysMsgPrintf) in the reference.
        return 0;
    }

    name = modelData->m_modelName;

    return name != nullptr;
}

// ref: FUN_007062f0
// The selection circle under a targeted corpse, projected on the ground at its radius (up to 10).
void CGCorpse_C::Virtual06C() {
    C3Vector position = this->GetPosition();
    float radius = this->GetScale() * this->m_selectionRadius;

    if (10.0f < radius) {
        radius = 10.0f;
    }

    CAaBox bounds;
    bounds.b = { position.x - radius, position.y - radius, position.z - (radius + radius) };
    bounds.t = { position.x + radius, position.y + radius, radius + radius + position.z };

    // FUN_00744150: the circle turned to face the camera.
    C44Matrix texMatrix;
    texMatrix.RotateAroundZ(-FacingBetween(CWorld::GetCameraPos(), this->GetPosition()));

    GxRsPush();

    if (SelectionCircleSetStates()) {
        CImVector color;
        color.value = 0xff7f7f7f;
        DecalDrawProjected(bounds, color, texMatrix, 0.5f, 0x200122, 0, 0.4f);
    }

    GxRsPop();
}

// ref: FUN_00706570
float CGCorpse_C::GetScale() const {
    return this->GetDisplayScale() * this->m_scaleMultiplier * this->m_scale;
}

// ref: FUN_00705850
void CGCorpse_C::OnModelLoaded(CM2Model* model) {
    this->CGObject_C::OnModelLoaded(model);

    model->SetBoneSequence(0xffffffff, (this->m_corpseFlags & 0x2) ? 0x84 : 6, 0xffffffff, 0, 1.0f, 1, 1);

    CAaSphere sphere;
    model->GetBoundingSphere(sphere);
    this->m_selectionRadius = sphere.r * 0.5f;
}

// ref: FUN_007066b0
// The ground's normal under the corpse, taken once its model is in: the average normal of the
// upward facets in a box of its size dropped half its height.
C3Vector CGCorpse_C::GetGroundNormal() {
    if (!(this->m_corpseFlags & 0x1)) {
        auto model = this->GetObjectModel();

        if (model && model->IsLoaded(0, 0)) {
            CAaBox box;
            model->GetBoundingBox(box);

            float scale = this->GetBaseScale();
            float height = (box.t.z - box.b.z) * scale;

            box.Scale(scale);

            // FUN_00705630
            C3Vector position = this->GetPosition();
            box.b = { box.b.x + position.x, box.b.y + position.y, box.b.z + position.z };
            box.t = { box.t.x + position.x, box.t.y + position.y, box.t.z + position.z };
            box.b.z -= height * 0.5f;

            if (CWorld::QueryFacets(box, s_groundFacets, 0x100111, nullptr) && s_groundFacets.facets.Count()) {
                C3Vector sum = { 0.0f, 0.0f, 0.0f };
                uint32_t count = 0;

                for (uint32_t i = 0; i < s_groundFacets.facets.Count(); i++) {
                    const auto& normal = s_groundFacets.facets[i].plane.n;

                    if (0.0f < normal.z) {
                        count++;
                        sum.x += normal.x;
                        sum.y += normal.y;
                        sum.z += normal.z;
                    }
                }

                if (count) {
                    float inv = 1.0f / static_cast<float>(count);
                    this->m_groundNormal = { inv * sum.x, sum.y * inv, inv * sum.z };
                }
            }

            this->m_corpseFlags |= 0x1;
        }
    }

    return this->m_groundNormal;
}

// ref: FUN_007068d0
// The corpse's effects step, and its model is placed lying along the ground.
int32_t CGCorpse_C::PlaceModel(float elapsed) {
    for (auto effect = this->m_effects; effect;) {
        auto next = effect->m_linkNext;
        effect->Update();
        effect = next;
    }

    auto model = this->GetObjectModel();

    if (model) {
        C3Vector normal = this->GetGroundNormal();
        float scale = this->GetScale();
        float facing = this->GetRenderFacing();

        model->SetWorldTransform(this->GetPosition(), facing, scale, &normal, 0xffffffff);
    }

    return 1;
}

// ref: FUN_007057e0
// Floating bones are not drawn, nor a body whose skin is not composited yet.
void CGCorpse_C::GetHidden(uint32_t flags, int32_t* hidden, int32_t* hiddenOther) {
    this->CGObject_C::GetHidden(flags, hidden, hiddenOther);

    if (*hidden == 0 && *hiddenOther == 0) {
        if ((this->m_corpseFlags & 0x2) && (this->Corpse()->flags & 0x1)) {
            *hiddenOther = 1;
            return;
        }

        if (this->m_component && !this->m_component->RenderPrep(0)) {
            *hiddenOther = 1;
        }
    } else if (this->m_component) {
        // FUN_004efed0, as a hidden unit's.
        if ((this->m_component->m_data.flags & 0x1) || (this->m_component->m_flags & 0x1) == 0) {
            if (this->m_component->m_flags & 0x4) {
                this->m_component->GeosRenderPrep();
            }
        }
    }
}

// ref: FUN_00706590
// Bones are picked only while they can be looted.
int32_t CGCorpse_C::CanHighlight() {
    auto data = this->Corpse();

    if ((data->flags & 0x1) && !(data->dynamicFlags & 0x1)) {
        return 0;
    }

    return 1;
}

// ref: FUN_00706010
// PHASE4(Player_C): clicking a corpse -- the player's own body reclaims it when dead (dismounting
// first, FUN_006dcb40, or "you can't do that while mounted", 0x8e), anything lootable (0x20) is
// looted (FUN_0053bce0, FUN_0071f890, FUN_006d6f40) -- goes through Player_C's interaction
// requests, which are not ported.
void CGCorpse_C::Virtual0B0() {
}

void CorpseInitialize() {
    static bool registered = false;

    if (registered) {
        return;
    }

    registered = true;

    MirrorRegisterHandler(ID_CORPSE, 0x6c, 4, &OnCorpseFlagsChanged, nullptr, 0, 0);
    MirrorRegisterHandler(ID_CORPSE, 0x70, 4, &OnCorpseDynamicFlagsChanged, nullptr, 0, 0);
}
