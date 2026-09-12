#include "glue/CCharacterCreation.hpp"
#include "glue/CCharacterSelection.hpp"
#include "client/ClientServices.hpp"
#include "component/CCharacterComponent.hpp"
#include "component/Types.hpp"
#include "db/Db.hpp"
#include "glue/CGlueLoading.hpp"
#include "glue/CGlueMgr.hpp"
#include "model/CM2Shared.hpp"
#include "net/Connection.hpp"
#include "object/client/CGPlayer_C.hpp"
#include "ui/FrameScript.hpp"
#include "ui/simple/CSimpleModelFFX.hpp"
#include "util/Random.hpp"
#include <storm/String.hpp>
#include <tempest/Random.hpp>

CCharacterComponent* CCharacterCreation::s_character;
CSimpleModelFFX* CCharacterCreation::s_charCustomizeFrame;
float CCharacterCreation::s_charFacing;
CharacterPreferences* CCharacterCreation::s_charPreferences[22][2];
TSFixedArray<const ChrClassesRec*> CCharacterCreation::s_classes;
int32_t CCharacterCreation::s_existingCharacterIndex;
CNameGenerator CCharacterCreation::s_nameGenerator;
int32_t CCharacterCreation::s_prevFaceID;
int32_t CCharacterCreation::s_prevFacialHairStyleID;
int32_t CCharacterCreation::s_prevHairColorID;
int32_t CCharacterCreation::s_prevHairStyleID;
int32_t CCharacterCreation::s_prevSkinColorID;
int32_t CCharacterCreation::s_raceIndex;
TSGrowableArray<int32_t> CCharacterCreation::s_races;
int32_t CCharacterCreation::s_selectedClassID;

void CCharacterCreation::CalcClasses(int32_t raceID) {
    uint32_t classCount = 0;

    for (int32_t i = 0; i < g_charBaseInfoDB.GetNumRecords(); i++) {
        auto infoRec = g_charBaseInfoDB.GetRecordByIndex(i);

        if (infoRec->m_raceID == raceID) {
            classCount++;
        }
    }

    CCharacterCreation::s_classes.SetCount(classCount);

    uint32_t classIndex = 0;

    for (int32_t i = 0; i < g_charBaseInfoDB.GetNumRecords(); i++) {
        auto infoRec = g_charBaseInfoDB.GetRecordByIndex(i);

        if (infoRec->m_raceID != raceID) {
            continue;
        }

        auto classRec = g_chrClassesDB.GetRecord(infoRec->m_classID);
        CCharacterCreation::s_classes[classIndex++] = classRec;
    }
}

// Submits the character being customized (0x4E0380 in the original)
void CCharacterCreation::CreateCharacter(const char* name) {
    // TODO when s_existingCharacterIndex is set this is a customization, race change, or faction
    // change of an existing character rather than a creation

    if (!name) {
        auto prompt = FrameScript_GetText(ClientServices::GetErrorToken(89), -1, GENDER_NOT_APPLICABLE);
        FrameScript_SignalEvent(3, "%s%s", "OKAY", prompt);

        return;
    }

    // TODO the original runs the name through ClientServices name validation here

    auto& data = CCharacterCreation::s_character->m_data;

    CHARACTER_CREATE_INFO info;
    SStrCopy(info.name, name, sizeof(info.name));
    info.raceID = static_cast<uint8_t>(data.raceID);
    info.classID = static_cast<uint8_t>(CCharacterCreation::s_selectedClassID);
    info.sexID = static_cast<uint8_t>(data.sexID);
    info.skinColorID = static_cast<uint8_t>(data.skinColorID);
    info.faceID = static_cast<uint8_t>(data.faceID);
    info.hairStyleID = static_cast<uint8_t>(data.hairStyleID);
    info.hairColorID = static_cast<uint8_t>(data.hairColorID);
    info.facialHairStyleID = static_cast<uint8_t>(data.facialHairStyleID);
    info.outfitID = 0;

    CGlueMgr::CreateCharacter(&info);
}

void CCharacterCreation::CreateComponent(ComponentData* data, bool randomize) {
    auto modelData = Player_C_GetModelName(data->raceID, data->sexID);

    if (!modelData || !modelData->m_modelName) {
        return;
    }

    auto existingComponent = CCharacterCreation::s_character;

    if (existingComponent) {
        auto model = existingComponent->m_data.model;

        if (model->m_attachParent) {
            model->DetachFromParent();
        }

        CCharacterComponent::FreeComponent(existingComponent);
    }

    CCharacterCreation::s_character = CCharacterComponent::AllocComponent();

    auto scene = CCharacterCreation::s_charCustomizeFrame->GetScene();
    auto model = scene->CreateModel(modelData->m_modelName, 0);
    data->model = model;

    if (!model) {
        return;
    }

    // TODO lighting

    data->model->SetBoneSequence(-1, 0, 0, 0, 1.0f, 1, 1);

    data->flags |= 0x2;

    CCharacterCreation::s_character->Init(data, nullptr);

    if (randomize) {
        CCharacterCreation::s_character->RandomSkinColor(CONTEXT_CHAR_CREATE);
        CCharacterCreation::s_character->RandomHairColor(CONTEXT_CHAR_CREATE);
        CCharacterCreation::s_character->RandomHairStyle(CONTEXT_CHAR_CREATE);
        CCharacterCreation::s_character->RandomFace(CONTEXT_CHAR_CREATE);
        CCharacterCreation::s_character->RandomBeardStyle(CONTEXT_CHAR_CREATE);
    }

    CCharacterCreation::TrackVariations();

    CCharacterCreation::SetFacing(CCharacterCreation::s_charFacing);

    CCharacterCreation::Dress();

    CCharacterCreation::s_character->RenderPrep(0);

    if (CCharacterCreation::s_charCustomizeFrame->m_model) {
        model->AttachToParent(CCharacterCreation::s_charCustomizeFrame->m_model, 0, nullptr, 0);
    }
}

void CCharacterCreation::CycleCharCustomization(int32_t index, int32_t delta) {
    if (!delta) {
        return;
    }

    auto character = CCharacterCreation::s_character;

    switch (index) {
    case 0:
        if (delta > 0) {
            character->NextSkinColor(CONTEXT_CHAR_CREATE);
        } else {
            character->PrevSkinColor(CONTEXT_CHAR_CREATE);
        }

        CCharacterCreation::s_prevSkinColorID = character->m_data.skinColorID;

        break;

    case 1:
        if (delta > 0) {
            character->NextFace(CONTEXT_CHAR_CREATE, CCharacterCreation::s_prevSkinColorID);
        } else {
            character->PrevFace(CONTEXT_CHAR_CREATE, CCharacterCreation::s_prevSkinColorID);
        }

        break;

    case 2:
        if (delta > 0) {
            character->NextHairStyle(CONTEXT_CHAR_CREATE);
        } else {
            character->PrevHairStyle(CONTEXT_CHAR_CREATE);
        }

        break;

    case 3:
        if (delta > 0) {
            character->NextHairColor(CONTEXT_CHAR_CREATE);
        } else {
            character->PrevHairColor(CONTEXT_CHAR_CREATE);
        }

        break;

    case 4:
        if (delta > 0) {
            character->NextBeardStyle(CONTEXT_CHAR_CREATE);
        } else {
            character->PrevBeardStyle(CONTEXT_CHAR_CREATE);
        }

        break;

    default:
        break;
    }

    CGlueLoading::StartLoad(character, true);
}

void CCharacterCreation::Dress() {
    // TODO
}

int32_t CCharacterCreation::GetRandomClassID() {
    int32_t classIDs[10] = { 0 };
    int32_t c = 0;

    if (!CCharacterCreation::s_classes.Count()) {
        return 0;
    }

    for (int32_t i = 0; i < CCharacterCreation::s_classes.Count(); i++) {
        auto classRec = CCharacterCreation::s_classes[i];

        if (classRec->m_requiredExpansion <= ClientServices::Connection()->m_accountExpansion) {
            classIDs[c++] = classRec->m_ID;
        }
    }

    if (!c) {
        return 0;
    }

    auto classIndex = CRandom::dice(c, g_rndSeed);

    return classIDs[classIndex];
}

void CCharacterCreation::GetRandomRaceAndSex(ComponentData* data) {
    data->sexID = CRandom::dice(2, g_rndSeed);

    ChrRacesRec* raceRec;

    do {
        auto raceIndex = CRandom::dice(CCharacterCreation::s_races.Count(), g_rndSeed);
        auto raceID = CCharacterCreation::s_races[raceIndex];

        data->raceID = raceID;

        raceRec = g_chrRacesDB.GetRecord(raceID);
    } while (raceRec->m_requiredExpansion > ClientServices::Connection()->m_accountExpansion);
}

int32_t CCharacterCreation::GetSelectedRaceID() {
    return CCharacterCreation::s_races[CCharacterCreation::s_raceIndex];
}

void CCharacterCreation::Initialize() {
    CCharacterCreation::s_charFacing = 0.0f;
    CCharacterCreation::s_charCustomizeFrame = nullptr;
    CCharacterCreation::s_existingCharacterIndex = -1;

    memset(CCharacterCreation::s_charPreferences, 0, sizeof(CCharacterCreation::s_charPreferences));

    CCharacterCreation::s_races.SetCount(0);

    // TODO enum or define for faction sides
    for (int32_t side = 0; side < 2; side++) {
        for (int32_t race = 0; race < g_chrRacesDB.GetNumRecords(); race++) {
            auto raceRec = g_chrRacesDB.GetRecordByIndex(race);

            // TODO NPCOnly?
            if (raceRec->m_flags & 0x1) {
                continue;
            }

            auto factionTemplateRec = g_factionTemplateDB.GetRecord(raceRec->m_factionID);

            for (int32_t group = 0; group < g_factionGroupDB.GetNumRecords(); group++) {
                auto factionGroupRec = g_factionGroupDB.GetRecordByIndex(group);

                if (!factionGroupRec || !factionGroupRec->m_maskID) {
                    continue;
                }

                bool templateMatch = (1 << factionGroupRec->m_maskID) & factionTemplateRec->m_factionGroup;

                if (!templateMatch) {
                    continue;
                }

                bool sideMatch =
                       (side == 0 && !SStrCmpI(factionGroupRec->m_internalName, "Alliance"))
                    || (side == 1 && !SStrCmpI(factionGroupRec->m_internalName, "Horde"));

                if (!sideMatch) {
                    continue;
                }

                // Race is playable and part of a faction aligned to either Alliance or Horde
                *CCharacterCreation::s_races.New() = raceRec->m_ID;
            }
        }
    }
}

bool CCharacterCreation::IsClassValid(int32_t classID) {
    for (int32_t i = 0; i < CCharacterCreation::s_classes.Count(); i++) {
        auto classRec = CCharacterCreation::s_classes[i];

        if (classRec->m_ID == classID) {
            return true;
        }
    }

    return false;
}

bool CCharacterCreation::IsRaceClassValid(int32_t raceID, int32_t classID) {
    for (int32_t i = 0; i < g_charBaseInfoDB.GetNumRecords(); i++) {
        auto infoRec = g_charBaseInfoDB.GetRecordByIndex(i);

        if (infoRec->m_raceID == raceID && infoRec->m_classID == classID) {
            return true;
        }
    }

    return false;
}

void CCharacterCreation::RandomizeCharCustomization() {
    auto character = CCharacterCreation::s_character;

    character->RandomSkinColor(CONTEXT_CHAR_CREATE);
    character->RandomFace(CONTEXT_CHAR_CREATE);
    character->RandomHairColor(CONTEXT_CHAR_CREATE);
    character->RandomHairStyle(CONTEXT_CHAR_CREATE);
    character->RandomBeardStyle(CONTEXT_CHAR_CREATE);

    CCharacterCreation::TrackVariations();

    CCharacterCreation::Dress();

    CGlueLoading::StartLoad(character, true);
}

void CCharacterCreation::ResetCharCustomizeInfo() {
    if (!CCharacterCreation::s_charCustomizeFrame) {
        return;
    }

    CCharacterCreation::s_existingCharacterIndex = -1;

    auto model = CCharacterCreation::s_charCustomizeFrame->m_model;

    if (model) {
        model->DetachAllChildrenById(0);
    }

    ComponentData data = {};
    CCharacterCreation::GetRandomRaceAndSex(&data);
    CCharacterCreation::CalcClasses(data.raceID);

    CCharacterCreation::CreateComponent(&data, true);

    CCharacterCreation::SetSelectedClass(CCharacterCreation::GetRandomClassID());
    data.classID = CCharacterCreation::s_selectedClassID;

    CCharacterCreation::s_raceIndex = -1;

    for (int32_t i = 0; i < CCharacterCreation::s_races.Count(); i++) {
        auto raceID = CCharacterCreation::s_races[i];

        if (CCharacterCreation::s_character->m_data.raceID == raceID) {
            CCharacterCreation::s_raceIndex = i;
            break;
        }
    }

    CCharacterCreation::s_nameGenerator.Initialize(CCharacterCreation::s_character->m_data.raceID, CCharacterCreation::s_character->m_data.sexID);

    CGlueLoading::StartLoad(CCharacterCreation::s_character, true);
}

void CCharacterCreation::SetCharCustomizeModel(const char* filename) {
    if (!CCharacterCreation::s_charCustomizeFrame || !filename || !*filename) {
        return;
    }

    auto existingModel = CCharacterCreation::s_charCustomizeFrame->m_model;

    if (existingModel && !SStrCmpI(existingModel->m_shared->m_filePath, filename)) {
        return;
    }

    CCharacterCreation::s_charCustomizeFrame->SetModel(filename);
    auto customizeModel = CCharacterCreation::s_charCustomizeFrame->m_model;

    // TODO inlined?
    if (customizeModel) {
        // TODO lighting stuff
        customizeModel->IsDrawable(1, 1);
    }

    // TODO inlined?
    if (customizeModel) {
        auto characterModel = CCharacterCreation::s_character->m_data.model;

        if (characterModel) {
            characterModel->AttachToParent(customizeModel, 0, nullptr, 0);
        }
    }
}

void CCharacterCreation::SetFacing(float orientation) {
    CCharacterCreation::s_charFacing = orientation;

    auto characterModel = CCharacterCreation::s_character->m_data.model;

    if (characterModel) {
        C3Vector position = { 0.0f, 0.0f, 0.0f };
        characterModel->SetWorldTransform(position, orientation, 1.0f);
    }
}

void CCharacterCreation::SavePreferences() {
    auto raceID = CCharacterCreation::s_races.m_data[CCharacterCreation::s_raceIndex];
    auto sexID = CCharacterCreation::s_character->m_data.sexID;

    auto preferences = CCharacterCreation::s_charPreferences[raceID][sexID];

    if (!preferences) {
        preferences = STORM_NEW(CharacterPreferences);
        CCharacterCreation::s_charPreferences[raceID][sexID] = preferences;
    }

    CCharacterCreation::s_character->GetPreferences(preferences);
}

void CCharacterCreation::SetSelectedClass(int32_t classID) {
    if (!CCharacterCreation::IsClassValid(classID)) {
        return;
    }

    CCharacterCreation::s_selectedClassID = classID;

    ComponentData data = {};
    data.raceID = CCharacterCreation::s_character->m_data.raceID;
    data.sexID = CCharacterCreation::s_character->m_data.sexID;
    data.classID = classID;
    data.skinColorID = CCharacterCreation::s_character->m_data.skinColorID;
    data.hairStyleID = CCharacterCreation::s_character->m_data.hairStyleID;
    data.hairColorID = CCharacterCreation::s_character->m_data.hairColorID;
    data.facialHairStyleID = CCharacterCreation::s_character->m_data.facialHairStyleID;
    data.faceID = CCharacterCreation::s_character->m_data.faceID;

    CCharacterComponent::ValidateComponentData(&data, CONTEXT_CHAR_CREATE);

    CCharacterCreation::CreateComponent(&data, false);

    CCharacterCreation::Dress();

    CGlueLoading::StartLoad(CCharacterCreation::s_character, true);
}

void CCharacterCreation::SetSelectedRace(int32_t raceIndex) {
    if (raceIndex >= CCharacterCreation::s_races.Count() || raceIndex == CCharacterCreation::s_raceIndex) {
        return;
    }

    auto raceID = CCharacterCreation::s_races[raceIndex];
    auto currentSexID = CCharacterCreation::s_character->m_data.sexID;

    CCharacterCreation::SavePreferences();

    CCharacterCreation::s_raceIndex = raceIndex;

    ComponentData data = {};

    auto existingCharacter = CCharacterCreation::s_existingCharacterIndex >= 0
        ? CCharacterSelection::GetCharacterDisplay(CCharacterCreation::s_existingCharacterIndex)
        : nullptr;

    auto preferences = CCharacterCreation::s_charPreferences[raceID][currentSexID];

    bool useExistingCharacter = existingCharacter
        && existingCharacter->m_info.raceID == raceID
        && existingCharacter->m_info.customizeFlags & 0x110000;

    bool usePreferences = !useExistingCharacter && preferences;

    if (useExistingCharacter) {
        data.raceID = existingCharacter->m_info.raceID;
        data.sexID = existingCharacter->m_info.sexID;
        data.classID = existingCharacter->m_info.classID;
        data.skinColorID = existingCharacter->m_info.skinColorID;
        data.hairStyleID = existingCharacter->m_info.hairStyleID;
        data.hairColorID = existingCharacter->m_info.hairColorID;
        data.facialHairStyleID = existingCharacter->m_info.facialHairStyleID;
        data.faceID = existingCharacter->m_info.faceID;

        CCharacterCreation::CreateComponent(&data, false);

        CCharacterCreation::SetSelectedSex(existingCharacter->m_info.sexID);
    } else if (usePreferences) {
        data.SetPreferences(preferences);

        CCharacterCreation::CalcClasses(data.raceID);

        if (!CCharacterCreation::IsRaceClassValid(data.raceID, CCharacterCreation::s_selectedClassID)) {
            CCharacterCreation::s_selectedClassID = CCharacterCreation::GetRandomClassID();
        }

        data.classID = CCharacterCreation::s_selectedClassID;

        CCharacterComponent::ValidateComponentData(&data, CONTEXT_CHAR_CREATE);

        CCharacterCreation::CreateComponent(&data, false);
    } else {
        data.raceID = raceID;
        data.sexID = currentSexID;

        CCharacterCreation::CalcClasses(data.raceID);

        if (!CCharacterCreation::IsRaceClassValid(data.raceID, CCharacterCreation::s_selectedClassID)) {
            CCharacterCreation::SetSelectedClass(CCharacterCreation::GetRandomClassID());
        }

        data.classID = CCharacterCreation::s_selectedClassID;

        CCharacterCreation::CreateComponent(&data, true);
    }

    CCharacterCreation::s_nameGenerator.Initialize(raceID, CCharacterCreation::s_character->m_data.sexID);

    CCharacterCreation::Dress();

    CGlueLoading::StartLoad(CCharacterCreation::s_character, true);
}

void CCharacterCreation::SetSelectedSex(int32_t sexID) {
    if (sexID >= UNITSEX_LAST) {
        return;
    }

    auto currentRaceID = CCharacterCreation::s_character->m_data.raceID;
    auto currentSexID = CCharacterCreation::s_character->m_data.sexID;
    auto currentClassID = CCharacterCreation::s_selectedClassID;

    if (sexID == currentSexID) {
        return;
    }

    CCharacterCreation::SavePreferences();

    ComponentData data = {};
    data.raceID = currentRaceID;
    data.sexID = sexID;
    data.classID = currentClassID;

    auto existingCharacter = CCharacterCreation::s_existingCharacterIndex >= 0
        ? CCharacterSelection::GetCharacterDisplay(CCharacterCreation::s_existingCharacterIndex)
        : nullptr;

    auto preferences = CCharacterCreation::s_charPreferences[CCharacterCreation::GetSelectedRaceID()][sexID];

    bool useExistingCharacter = existingCharacter
        && existingCharacter->m_info.sexID == sexID
        && existingCharacter->m_info.customizeFlags & 0x1;

    bool usePreferences = !useExistingCharacter && preferences;

    if (useExistingCharacter) {
        data.raceID = existingCharacter->m_info.raceID;
        data.sexID = existingCharacter->m_info.sexID;
        data.classID = existingCharacter->m_info.classID;
        data.skinColorID = existingCharacter->m_info.skinColorID;
        data.hairStyleID = existingCharacter->m_info.hairStyleID;
        data.hairColorID = existingCharacter->m_info.hairColorID;
        data.facialHairStyleID = existingCharacter->m_info.facialHairStyleID;
        data.faceID = existingCharacter->m_info.faceID;

        CCharacterCreation::CreateComponent(&data, false);
    } else if (usePreferences) {
        data.SetPreferences(preferences);
        data.classID = currentClassID;

        CCharacterComponent::ValidateComponentData(&data, CONTEXT_CHAR_CREATE);

        CCharacterCreation::CreateComponent(&data, false);
    } else {
        data.raceID = currentRaceID;
        data.sexID = sexID;
        data.classID = currentClassID;

        CCharacterCreation::CreateComponent(&data, true);
    }

    CCharacterCreation::s_nameGenerator.Initialize(CCharacterCreation::s_character->m_data.raceID, CCharacterCreation::s_character->m_data.sexID);

    CGlueLoading::StartLoad(CCharacterCreation::s_character, true);
}

void CCharacterCreation::TrackVariations() {
    auto& data = CCharacterCreation::s_character->m_data;

    CCharacterCreation::s_prevSkinColorID = data.skinColorID;
    CCharacterCreation::s_prevFaceID = data.faceID;
    CCharacterCreation::s_prevHairColorID = data.hairColorID;
    CCharacterCreation::s_prevHairStyleID = data.hairStyleID;
    CCharacterCreation::s_prevFacialHairStyleID = data.facialHairStyleID;
}
