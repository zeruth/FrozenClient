#include "component/CCharacterComponent.hpp"
#include "component/Texture.hpp"
#include "component/Util.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include "gx/Blp.hpp"
#include "gx/Device.hpp"
#include "gx/Texture.hpp"
#include "model/CM2Model.hpp"
#include "model/CM2Scene.hpp"
#include "object/Types.hpp"
#include "util/CStatus.hpp"
#include "util/Random.hpp"
#include "util/Unimplemented.hpp"
#include <common/ObjectAlloc.hpp>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Random.hpp>

uint32_t* CCharacterComponent::s_characterFacialHairStylesList;
st_race* CCharacterComponent::s_chrVarArray;
uint32_t CCharacterComponent::s_chrVarArrayLength;
EGxTexFormat CCharacterComponent::s_gxFormat;
ITEM_FUNC CCharacterComponent::s_itemFunc[];
uint32_t CCharacterComponent::s_mipLevels;
PREP_FUNC CCharacterComponent::s_prepFunc[];
CompSectionInfo CCharacterComponent::s_sectionInfo[];
MipBits* CCharacterComponent::s_textureBuffer;
MipBits* CCharacterComponent::s_textureBufferCompressed;
uint32_t CCharacterComponent::s_textureSize;
int32_t CCharacterComponent::s_thread;
int32_t CCharacterComponent::s_compress;

CompSectionInfo CCharacterComponent::s_sectionInfoRaw[] = {
    { { 0,   0   }, { 256, 128 } }, // SECTION_ARM_UPPER
    { { 0,   128 }, { 256, 128 } }, // SECTION_ARM_LOWER
    { { 0,   256 }, { 256, 64  } }, // SECTION_HAND
    { { 256, 0,  }, { 256, 128 } }, // SECTION_TORSO_UPPER
    { { 256, 128 }, { 256, 64  } }, // SECTION_TORSO_LOWER
    { { 256, 192 }, { 256, 128 } }, // SECTION_LEG_UPPER
    { { 256, 320 }, { 256, 128 } }, // SECTION_LEG_LOWER
    { { 256, 448 }, { 256, 64  } }, // SECTION_FOOT
    { { 0,   320 }, { 256, 64  } }, // SECTION_HEAD_UPPER
    { { 0,   384 }, { 256, 128 } }, // SECTION_HEAD_LOWER
};

const char* s_componentSections[] = {
    "ArmUpperTexture",
    "ArmLowerTexture",
    "HandTexture",
    "TorsoUpperTexture",
    "TorsoLowerTexture",
    "LegUpperTexture",
    "LegLowerTexture",
    "FootTexture"
};

const char* s_fileDecorations[] = {
    "M",
    "F",
    "U"
};

/**
 * Texture priorities for each item slot and component section. Determines order for pasting
 * textures in RenderPrep functions. Priority start and end for leg component sections is
 * adjusted in the corresponding RenderPrep functions.
 */
int32_t s_itemPriority[NUM_ITEM_SLOT][NUM_COMPONENT_SECTIONS] = {
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, // ITEMSLOT_0
    { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }, // ITEMSLOT_1
    {  0,  0, -1,  0,  0, -1, -1, -1, -1, -1 }, // ITEMSLOT_2
    {  1,  1, -1,  1,  1,  1,  1, -1, -1, -1 }, // ITEMSLOT_3
    { -1, -1, -1, -1,  5,  2, -1, -1, -1, -1 }, // ITEMSLOT_4
    { -1, -1, -1, -1, -1,  0,  0, -1, -1, -1 }, // ITEMSLOT_5
    { -1, -1, -1, -1, -1, -1,  2,  0, -1, -1 }, // ITEMSLOT_6
    { -1,  2, -1, -1, -1, -1, -1, -1, -1, -1 }, // ITEMSLOT_7
    { -1,  3,  0, -1, -1, -1, -1, -1, -1, -1 }, // ITEMSLOT_8
    { -1, -1, -1,  4,  4, -1, -1, -1, -1, -1 }, // ITEMSLOT_9
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // ITEMSLOT_10
    {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 }, // ITEMSLOT_11
};

/**
 * Total item texture priorities for each component section.
 */
#define SECTION_AU_ITEM_PRIORITIES 2
#define SECTION_AL_ITEM_PRIORITIES 7
#define SECTION_HA_ITEM_PRIORITIES 1
#define SECTION_TU_ITEM_PRIORITIES 5
#define SECTION_TL_ITEM_PRIORITIES 7
#define SECTION_LU_ITEM_PRIORITIES 3
#define SECTION_LL_ITEM_PRIORITIES 6
#define SECTION_FO_ITEM_PRIORITIES 1
#define SECTION_HU_ITEM_PRIORITIES 0
#define SECTION_HL_ITEM_PRIORITIES 0

int32_t s_bInRenderPrep = 0;
char s_buffer[STORM_MAX_PATH];
uint32_t* s_componentHeap;
char* s_pathEnd;
char s_path[STORM_MAX_PATH];
char* s_pathEnd2;
char s_path2[STORM_MAX_PATH];
CStatus s_status;

static CVar* s_componentTextureLevelCvar;
static CVar* s_componentThreadCvar;
static CVar* s_componentCompressCvar;

#define TEXTURE_INDEX(section, texture) (3 * section + texture)

bool ComponentTextureLevelCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // 3.3.5a release client has no logic, only returns true
    return true;
}

bool ComponentThreadCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // 3.3.5a release client has no logic, only returns true
    return true;
}

bool ComponentCompressCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // 3.3.5a release client has no logic, only returns true
    return true;
}

void ReplaceParticleColor(CM2Model* model, int32_t particleColorID) {
    // TODO
}

int32_t CCharacterComponent::AddHandItem(CM2Model* model, const ItemDisplayInfoRec* displayRec, INVENTORY_SLOTS invSlot, SHEATHE_TYPE sheatheType, bool sheathed, bool shield, bool a7, int32_t visualID) {
    if (!model || !displayRec || invSlot > INVSLOT_TABARD) {
        return -1;
    }

    auto itemModelPath = "Item\\ObjectComponents\\Weapon\\";
    auto itemTexturePath = "Item\\ObjectComponents\\Weapon\\";

    GEOCOMPONENTLINKS itemLink;
    GEOCOMPONENTLINKS sheatheLink;

    if (invSlot == INVSLOT_MAINHAND) {
        itemLink = ATTACH_HANDR;
        sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, true);
    } else if (invSlot == INVSLOT_OFFHAND) {
        itemLink = ATTACH_HANDL;
        sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, false);
    } else if (invSlot == INVSLOT_RANGED) {
        if (a7) {
            itemLink = ATTACH_HANDR;
            sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, true);
        } else {
            itemLink = ATTACH_HANDL;
            sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, false);
        }
    } else {
        return -1;
    }

    if (shield) {
        itemModelPath = "Item\\ObjectComponents\\Shield\\";
        itemTexturePath = "Item\\ObjectComponents\\Shield\\";

        itemLink = ATTACH_SHIELD;
    }

    CCharacterComponent::RemoveLinkpt(model, itemLink);
    CCharacterComponent::RemoveLinkpt(model, sheatheLink);

    auto link = sheathed ? sheatheLink : itemLink;

    if (model->IsLoaded(0, 0) && !model->HasAttachment(link)) {
        return -1;
    }

    SStrPrintf(s_buffer, sizeof(s_buffer), "%s%s", itemModelPath, displayRec->m_modelName[0]);
    SStrCopy(s_pathEnd, s_buffer);

    SStrPrintf(s_buffer, sizeof(s_buffer), "%s%s.blp", itemTexturePath, displayRec->m_modelTexture[0]);
    SStrCopy(s_pathEnd2, s_buffer);

    if (visualID == 0) {
        visualID = displayRec->m_itemVisual;
    }

    CCharacterComponent::AddLink(model, link, s_path, s_path2, visualID, displayRec);

    return link;
}

void CCharacterComponent::AddLink(CM2Model* parent, GEOCOMPONENTLINKS link, char const* modelPath, char const* texturePath, int32_t visualID, const ItemDisplayInfoRec* displayRec) {
    if (!parent) {
        return;
    }

    // Create item model

    auto model = parent->m_scene->CreateModel(modelPath, 0);

    if (!model) {
        return;
    }

    // Create item texture

    auto textureFlags = CGxTexFlags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
    auto texture = TextureCreate(texturePath, textureFlags, &s_status, 0);

    if (!texture) {
        model->Release();
        return;
    }

    // Replace item texture

    model->ReplaceTexture(2, texture);
    HandleClose(texture);

    // Add item visual

    if (visualID > 0) {
        // TODO CCharacterComponent::ComponentUtilAddItemVisual(model, visualID);
    }

    // Attach item to parent

    parent->DetachAllChildrenById(link);
    model->AttachToParent(parent, link, nullptr, 0);

    if (link == ATTACH_HANDR) {
        CCharacterComponent::ComponentCloseFingers(parent, HAND_RIGHT);
    } else if (link == ATTACH_HANDL) {
        CCharacterComponent::ComponentCloseFingers(parent, HAND_LEFT);
    }

    // Replace item particle color

    // TODO ReplaceParticleColor(displayRec->m_particleColorID, model);

    model->Release();
}

CCharacterComponent* CCharacterComponent::AllocComponent() {
    uint32_t memHandle;
    void* mem;

    if (!ObjectAlloc(*s_componentHeap, &memHandle, &mem, false)) {
        return nullptr;
    }

    auto component = new (mem) CCharacterComponent();
    component->m_memHandle = memHandle;

    return component;
}

void CCharacterComponent::ApplyMonsterGeosets(CM2Model* model, const CreatureDisplayInfoRec* displayInfoRec) {
    if (!model || !displayInfoRec || !displayInfoRec->m_creatureGeosetData) {
        return;
    }

    for (int32_t group = 100, dataOfs = 0; group < 900; group += 100, dataOfs += 4) {
        auto section = (displayInfoRec->m_creatureGeosetData >> dataOfs) & 0xF;

        if (section) {
            // Hide all sections in group
            model->SetGeometryVisible(group, group + 99, false);

            // Show matching section
            model->SetGeometryVisible(group + section, group + section, true);
        }
    }

    model->OptimizeVisibleGeometry();
}

void CCharacterComponent::ComponentCloseFingers(CM2Model* model, COMP_HAND_SLOT handSlot) {
    uint32_t firstBone;
    uint32_t lastBone;

    if (handSlot == HAND_LEFT) {
        firstBone = 13;
        lastBone = 17;
    } else {
        firstBone = 8;
        lastBone = 12;
    }

    for (uint32_t boneId = firstBone; boneId <= lastBone; boneId++) {
        model->SetBoneSequence(boneId, 15, 0xFFFFFFFF, 0, 1.0f, 0, 1);
    }
}

void CCharacterComponent::ComponentOpenFingers(CM2Model* model, COMP_HAND_SLOT handSlot) {
    uint32_t firstBone;
    uint32_t lastBone;

    if (handSlot == HAND_LEFT) {
        firstBone = 13;
        lastBone = 17;
    } else {
        firstBone = 8;
        lastBone = 12;
    }

    for (uint32_t boneId = firstBone; boneId <= lastBone; boneId++) {
        model->UnsetBoneSequence(boneId, 0, 1);
    }
}

HTEXTURE CCharacterComponent::CreateTexture(const char* fileName, CStatus* status) {
    auto texFlags = CGxTexFlags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
    return TextureCreate(fileName, texFlags, status, 0);
}

void CCharacterComponent::FreeComponent(CCharacterComponent* component) {
    component->~CCharacterComponent();
    ObjectFree(*s_componentHeap, component->m_memHandle);
}

GEOCOMPONENTLINKS CCharacterComponent::GetSheatheLink(SHEATHE_TYPE sheatheType, bool a2) {
    switch (sheatheType) {
    case SHEATHE_1:
        return a2 ? ATTACH_SHEATH_MAINHAND : ATTACH_SHEATH_OFFHAND;
    case SHEATHE_2:
        return a2 ? ATTACH_LARGEWEAPONLEFT : ATTACH_LARGEWEAPONRIGHT;
    case SHEATHE_3:
        return a2 ? ATTACH_HIPWEAPONLEFT : ATTACH_HIPWEAPONRIGHT;
    case SHEATHE_4:
        return ATTACH_SHEATH_SHIELD;
    default:
        return ATTACH_NONE;
    }
}

void CCharacterComponent::Initialize() {
    s_componentTextureLevelCvar = CVar::Register(
        "componentTextureLevel",
        "Number of mip levels used for character component textures",
        0x1,
        "8",
        &ComponentTextureLevelCallback,
        DEBUG
    );

    s_componentThreadCvar = CVar::Register(
        "componentThread",
        "Multi thread character component processing",
        0x1,
        "1",
        &ComponentThreadCallback,
        DEBUG
    );

    s_componentCompressCvar = CVar::Register(
        "componentCompress",
        "Character component texture compression",
        0x1,
        "1",
        &ComponentCompressCallback,
        DEBUG
    );

    auto textureLevel = s_componentTextureLevelCvar->GetInt();
    auto compress = s_componentCompressCvar->GetInt();

    // The original composes character textures on a worker thread when componentThread is set
    // and the machine has more than one CPU, which is also what allows compression and texture
    // level 9. Composition here always happens on the main thread, so take the single threaded
    // path: no compression and at most texture level 8.
    int32_t thread = 0;

    if (textureLevel > 8) {
        textureLevel = 8;
    }

    compress = 0;

    if (textureLevel < 7) {
        textureLevel = 7;
    }

    CCharacterComponent::Initialize(GxTex_Rgb565, textureLevel, thread, compress);
}

// ref: FUN_004f1a20
void CCharacterComponent::Initialize(EGxTexFormat textureFormat, uint32_t textureLevel, int32_t thread, int32_t compress) {
    auto heapId = static_cast<uint32_t*>(STORM_ALLOC(sizeof(uint32_t)));

    if (heapId) {
        *heapId = ObjectAllocAddHeap(sizeof(CCharacterComponent), 32, "CCharacterComponent", true);
        s_componentHeap = heapId;
    } else {
        s_componentHeap = nullptr;
    }

    s_pathEnd = s_path;
    s_pathEnd2 = s_path2;

    CCharacterComponent::s_prepFunc[SECTION_ARM_UPPER]      = &CCharacterComponent::RenderPrepAU;
    CCharacterComponent::s_prepFunc[SECTION_ARM_LOWER]      = &CCharacterComponent::RenderPrepAL;
    CCharacterComponent::s_prepFunc[SECTION_HAND]           = &CCharacterComponent::RenderPrepHA;
    CCharacterComponent::s_prepFunc[SECTION_TORSO_UPPER]    = &CCharacterComponent::RenderPrepTU;
    CCharacterComponent::s_prepFunc[SECTION_TORSO_LOWER]    = &CCharacterComponent::RenderPrepTL;
    CCharacterComponent::s_prepFunc[SECTION_LEG_UPPER]      = &CCharacterComponent::RenderPrepLU;
    CCharacterComponent::s_prepFunc[SECTION_LEG_LOWER]      = &CCharacterComponent::RenderPrepLL;
    CCharacterComponent::s_prepFunc[SECTION_FOOT]           = &CCharacterComponent::RenderPrepFO;
    CCharacterComponent::s_prepFunc[SECTION_HEAD_UPPER]     = &CCharacterComponent::RenderPrepHU;
    CCharacterComponent::s_prepFunc[SECTION_HEAD_LOWER]     = &CCharacterComponent::RenderPrepHL;

    CCharacterComponent::s_itemFunc[SECTION_ARM_UPPER]      = &CCharacterComponent::UpdateItemAU;
    CCharacterComponent::s_itemFunc[SECTION_ARM_LOWER]      = &CCharacterComponent::UpdateItemAL;
    CCharacterComponent::s_itemFunc[SECTION_HAND]           = &CCharacterComponent::UpdateItemHA;
    CCharacterComponent::s_itemFunc[SECTION_HEAD_UPPER]     = &CCharacterComponent::UpdateItemHU;
    CCharacterComponent::s_itemFunc[SECTION_HEAD_LOWER]     = &CCharacterComponent::UpdateItemHL;
    CCharacterComponent::s_itemFunc[SECTION_TORSO_UPPER]    = &CCharacterComponent::UpdateItemTU;
    CCharacterComponent::s_itemFunc[SECTION_TORSO_LOWER]    = &CCharacterComponent::UpdateItemTL;
    CCharacterComponent::s_itemFunc[SECTION_LEG_UPPER]      = &CCharacterComponent::UpdateItemLU;
    CCharacterComponent::s_itemFunc[SECTION_LEG_LOWER]      = &CCharacterComponent::UpdateItemLL;
    CCharacterComponent::s_itemFunc[SECTION_FOOT]           = &CCharacterComponent::UpdateItemFO;

    // Mip levels between 6 and 9; 9 needs compression
    if (textureLevel < 10) {
        if (textureLevel < 6) {
            textureLevel = 6;
        }
    } else {
        textureLevel = 9;
    }

    if (!compress && textureLevel > 8) {
        textureLevel = 8;
    }

    CCharacterComponent::s_textureSize = 1 << textureLevel;

    // Scale section info to match mip levels
    for (int32_t i = 0; i < NUM_COMPONENT_SECTIONS; i++) {
        auto& info = CCharacterComponent::s_sectionInfo[i];
        auto& infoRaw = CCharacterComponent::s_sectionInfoRaw[i];

        info.pos.x = infoRaw.pos.x >> (9 - textureLevel);
        info.pos.y = infoRaw.pos.y >> (9 - textureLevel);
        info.size.x = infoRaw.size.x >> (9 - textureLevel);
        info.size.y = infoRaw.size.y >> (9 - textureLevel);
    }

    CCharacterComponent::s_mipLevels = textureLevel;

    // TODO FUN_004f2db0: clears one component counter (DAT_00b6ba50), not identified yet

    uint32_t varArrayLength = (g_chrRacesDB.m_maxID + 1) * UNITSEX_NUM_SEXES;
    CCharacterComponent::s_chrVarArrayLength = varArrayLength;

    BuildComponentArray(varArrayLength, &CCharacterComponent::s_chrVarArray);
    CountFacialFeatures(varArrayLength, &CCharacterComponent::s_characterFacialHairStylesList);

    CCharacterComponent::s_thread = thread;
    CCharacterComponent::s_compress = 0;
    CCharacterComponent::s_gxFormat = textureFormat;

    if (thread) {
        if (compress) {
            CCharacterComponent::s_gxFormat = static_cast<EGxTexFormat>(6);
        }

        CCharacterComponent::s_compress = compress != 0;

        // TODO FUN_004f16f0: start the component worker thread
    }
    // else: the reference clears the ten words of worker-thread state here; frozen keeps none

    // frozen-only: the reference allocates the composition buffer lazily on its worker; this port
    // composes on the main thread and needs the buffer up front
    CCharacterComponent::s_textureBuffer = TextureAllocMippedImg(
        PIXEL_ARGB8888,
        CCharacterComponent::s_textureSize,
        CCharacterComponent::s_textureSize
    );
}

int32_t CCharacterComponent::NextBeardStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);

    bool found;
    this->GetSectionsRecord(VARIATION_FACIAL_HAIR, data.facialHairStyleID, data.hairColorID, &found);

    // Facial features without sections (e.g. tauren horns) are simply numbered
    if (!found) {
        auto numStyles = static_cast<int32_t>(CCharacterComponent::s_characterFacialHairStylesList[data.raceID * UNITSEX_NUM_SEXES + data.sexID]);
        auto style = data.facialHairStyleID + 1;

        this->SetBeardStyle(style < numStyles ? style : 0, true, nullptr);

        return 1;
    }

    auto numStyles = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACIAL_HAIR);

    if (numStyles <= 0) {
        return 0;
    }

    auto style = data.facialHairStyleID + 1;

    if (style >= numStyles) {
        style = 0;
    }

    while (style != data.facialHairStyleID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACIAL_HAIR, style);

        for (int32_t color = 0; color < numColors; color++) {
            auto rec = this->GetSectionsRecord(VARIATION_FACIAL_HAIR, style, color, nullptr);

            if (!rec || !ComponentCheckSectionFlags(rec->m_flags, selection)) {
                continue;
            }

            // Keep the current hair color if the new style supports it
            auto currentColorRec = this->GetSectionsRecord(VARIATION_FACIAL_HAIR, style, data.hairColorID, nullptr);

            if (currentColorRec && ComponentCheckSectionFlags(currentColorRec->m_flags, selection)) {
                this->SetBeardStyle(currentColorRec->m_variationIndex, true, nullptr);
                return 1;
            }

            this->SetHairColor(rec->m_colorIndex, false, nullptr);
            this->SetBeardStyle(rec->m_variationIndex, true, nullptr);

            return 1;
        }

        style++;

        if (style >= numStyles) {
            style = 0;
        }
    }

    return 0;
}

int32_t CCharacterComponent::NextFace(COMPONENT_CONTEXT context, int32_t colorOffset) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numFaces = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACE);

    if (numFaces <= 0) {
        return 0;
    }

    auto face = data.faceID + 1;

    if (face >= numFaces) {
        face = 0;
    }

    while (face != data.faceID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACE, face);

        // Start the color search at the preferred skin color
        for (int32_t color = 0; color < numColors; color++) {
            color = (color + colorOffset) % numColors;

            auto faceRec = this->GetSectionsRecord(VARIATION_FACE, face, color, nullptr);
            auto skinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, color, nullptr);
            auto underwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, color, nullptr);

            bool valid = faceRec && ComponentCheckSectionFlags(faceRec->m_flags, selection)
                && skinRec && ComponentCheckSectionFlags(skinRec->m_flags, selection)
                && (context == CONTEXT_2 || (underwearRec && ComponentCheckSectionFlags(underwearRec->m_flags, selection)));

            if (!valid) {
                continue;
            }

            // Keep the current skin color if the new face supports it
            auto currentFaceRec = this->GetSectionsRecord(VARIATION_FACE, face, data.skinColorID, nullptr);
            auto currentSkinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, data.skinColorID, nullptr);
            auto currentUnderwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, data.skinColorID, nullptr);

            bool currentValid = currentFaceRec && ComponentCheckSectionFlags(currentFaceRec->m_flags, selection)
                && currentSkinRec && ComponentCheckSectionFlags(currentSkinRec->m_flags, selection)
                && (context == CONTEXT_2 || (currentUnderwearRec && ComponentCheckSectionFlags(currentUnderwearRec->m_flags, selection)));

            if (currentValid) {
                this->SetFace(face, true, nullptr);
                return 1;
            }

            this->SetSkinColor(color, false, true, nullptr);
            this->SetFace(face, true, nullptr);

            return 1;
        }

        face++;

        if (face >= numFaces) {
            face = 0;
        }
    }

    return 0;
}

int32_t CCharacterComponent::NextHairColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR, data.hairStyleID);

    if (numColors <= 0) {
        return 0;
    }

    auto color = data.hairColorID + 1;

    if (color >= numColors) {
        color = 0;
    }

    while (color != data.hairColorID) {
        auto rec = this->GetSectionsRecord(VARIATION_HAIR, data.hairStyleID, color, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            this->SetHairColor(rec->m_colorIndex, true, nullptr);
            return 1;
        }

        color++;

        if (color >= numColors) {
            color = 0;
        }
    }

    return 0;
}

int32_t CCharacterComponent::NextHairStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numStyles = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR);

    if (numStyles <= 0) {
        return 0;
    }

    auto style = data.hairStyleID + 1;

    if (style >= numStyles) {
        style = 0;
    }

    while (style != data.hairStyleID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR, style);

        for (int32_t color = 0; color < numColors; color++) {
            auto rec = this->GetSectionsRecord(VARIATION_HAIR, style, color, nullptr);

            if (!rec || !ComponentCheckSectionFlags(rec->m_flags, selection)) {
                continue;
            }

            // Keep the current hair color if the new style supports it
            auto currentColorRec = this->GetSectionsRecord(VARIATION_HAIR, style, data.hairColorID, nullptr);

            if (currentColorRec && ComponentCheckSectionFlags(currentColorRec->m_flags, selection)) {
                this->SetHairStyle(currentColorRec->m_variationIndex, nullptr);
                return 1;
            }

            this->SetHairColor(rec->m_colorIndex, true, nullptr);
            this->SetHairStyle(rec->m_variationIndex, nullptr);

            // The new hair color may invalidate the facial hair style
            selection = GetSelectionFromContext(context, data.classID);

            auto facialHairStyle = ComponentGetFacialHairStyleByIndex(data.raceID, data.sexID, data.classID, data.hairColorID, data.facialHairStyleID, 0, selection);

            if (facialHairStyle >= 0) {
                this->SetBeardStyle(facialHairStyle, true, nullptr);
            }

            return 1;
        }

        style++;

        if (style >= numStyles) {
            style = 0;
        }
    }

    return 0;
}

int32_t CCharacterComponent::NextSkinColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_SKIN, 0);

    if (numColors <= 0) {
        return 0;
    }

    auto color = data.skinColorID + 1;

    if (color >= numColors) {
        color = 0;
    }

    while (color != data.skinColorID) {
        auto skinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, color, nullptr);
        auto faceRec = this->GetSectionsRecord(VARIATION_FACE, data.faceID, color, nullptr);
        auto underwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, color, nullptr);

        bool valid = skinRec && ComponentCheckSectionFlags(skinRec->m_flags, selection)
            && faceRec && ComponentCheckSectionFlags(faceRec->m_flags, selection)
            && (context == CONTEXT_2 || (underwearRec && ComponentCheckSectionFlags(underwearRec->m_flags, selection)));

        if (valid) {
            this->SetSkinColor(skinRec->m_colorIndex, true, true, nullptr);
            return 1;
        }

        color++;

        if (color >= numColors) {
            color = 0;
        }
    }

    return 0;
}

void CCharacterComponent::Paste(void* srcTexture, MipBits* dstMips, const C2iVector& dstPos, const C2iVector& srcPos, const C2iVector& srcSize, TCTEXTUREINFO& srcInfo, int32_t srcMipLevel) {
    uint32_t dstWidth = CCharacterComponent::s_textureSize * 4;

    auto srcPal = TextureCacheGetPal(srcTexture);

    if (!srcPal) {
        // TODO fill in with crappy green (0x00FF00FF)

        return;
    }

    switch (srcInfo.alphaSize) {
    case 0:
        CCharacterComponent::PasteOpaque(
            srcTexture,
            srcPal,
            dstMips,
            dstPos,
            dstWidth,
            srcPos,
            srcSize,
            srcInfo,
            srcMipLevel,
            -srcMipLevel
        );

        break;

    case 1:
        CCharacterComponent::PasteTransparent1Bit(
            srcTexture,
            srcPal,
            dstMips,
            dstPos,
            dstWidth,
            srcPos,
            srcSize,
            srcInfo,
            srcMipLevel,
            -srcMipLevel
        );

        break;

    case 4:
        CCharacterComponent::PasteTransparent4Bit(
            srcTexture,
            srcPal,
            dstMips,
            dstPos,
            dstWidth,
            srcPos,
            srcSize,
            srcInfo,
            srcMipLevel,
            -srcMipLevel
        );

        break;

    case 8:
        CCharacterComponent::PasteTransparent8Bit(
            srcTexture,
            srcPal,
            dstMips,
            dstPos,
            dstWidth,
            srcPos,
            srcSize,
            srcInfo,
            srcMipLevel,
            -srcMipLevel
        );

        break;

    default:
        // Do nothing
        break;
    }
}

void CCharacterComponent::PasteFromSkin(COMPONENT_SECTIONS section, void* srcTexture, MipBits* dstMips) {
    if (!TextureCacheHasMips(srcTexture)) {
        return;
    }

    auto& sectionInfo = CCharacterComponent::s_sectionInfo[section];

    TCTEXTUREINFO srcInfo;
    TextureCacheGetInfo(srcTexture, srcInfo, 1);

    // Skin is always opaque
    srcInfo.alphaSize = 0;

    if (srcInfo.width >= CCharacterComponent::s_textureSize || srcInfo.height >= CCharacterComponent::s_textureSize ) {
        // Calculate source mip level appropriate for CCharacterComponent::s_textureSize
        int32_t srcMipLevel = 0;
        int32_t srcWidth = srcInfo.width;
        while (srcWidth > CCharacterComponent::s_textureSize) {
            srcWidth /= 2;
            srcMipLevel++;
        }

        CCharacterComponent::Paste(srcTexture, dstMips, sectionInfo.pos, sectionInfo.pos, sectionInfo.size, srcInfo, srcMipLevel);
    } else {
        CCharacterComponent::PasteScale(srcTexture, dstMips, sectionInfo.pos, sectionInfo.pos, sectionInfo.size, srcInfo);
    }
}

void CCharacterComponent::PasteOpaque(void* srcTexture, const BlpPalPixel* srcPal, MipBits* dstMips, const C2iVector& dstPos, uint32_t dstWidth, const C2iVector& srcPos, const C2iVector& srcSize, TCTEXTUREINFO& srcInfo, int32_t srcMipLevel, int32_t dstMipLevelOfs) {
    // Prepare first mip level

    C2iVector curSrcSize = srcSize;

    C2iVector curSrcPos = srcPos;
    C2iVector curDstPos = dstPos;

    uint32_t curSrcWidth = srcInfo.width >> srcMipLevel;
    uint32_t curDstWidth = dstWidth;

    // Paste texture for each mip level

    for (int32_t curMipLevel = srcMipLevel; curMipLevel < srcInfo.mipCount; curMipLevel++) {
        auto srcMip = TextureCacheGetMip(srcTexture, curMipLevel);
        auto srcRow = &srcMip[(curSrcPos.y * curSrcWidth) + curSrcPos.x];

        auto dstMip = reinterpret_cast<uint8_t*>(dstMips->mip[curMipLevel + dstMipLevelOfs]);
        auto dstRow = &dstMip[(curDstPos.y * curDstWidth) + (curDstPos.x * 4)];

        // Calculate the end of the source region
        auto srcEnd = srcRow + curSrcWidth * curSrcSize.y;

        // Copy each row
        while (srcRow < srcEnd) {
            auto dst = reinterpret_cast<C4Pixel*>(dstRow);

            for (uint32_t x = 0; x < curSrcSize.x; x++) {
                // Get palette entry
                auto& src = srcPal[srcRow[x]];

                // Copy color from palette entry
                dst->b = src.b;
                dst->g = src.g;
                dst->r = src.r;
                dst->a = 0xFF;

                dst++;
            }

            // Move to next row
            srcRow += curSrcWidth;
            dstRow += curDstWidth;
        }

        // Prepare next mip level

        curSrcSize.x = std::max(1, curSrcSize.x / 2);
        curSrcSize.y = std::max(1, curSrcSize.y / 2);

        curSrcPos.x /= 2;
        curSrcPos.y /= 2;
        curDstPos.x /= 2;
        curDstPos.y /= 2;

        curDstWidth /= 2;
        curSrcWidth /= 2;
    }
}

void CCharacterComponent::PasteScale(void* srcTexture, MipBits* dstMips, const C2iVector& a3, const C2iVector& a4, const C2iVector& a5, TCTEXTUREINFO& srcInfo) {
    WHOA_UNIMPLEMENTED();
}

void CCharacterComponent::PasteToSection(COMPONENT_SECTIONS section, void* srcTexture, MipBits* dstMips) {
    if (!TextureCacheHasMips(srcTexture)) {
        return;
    }

    auto& sectionInfo = CCharacterComponent::s_sectionInfo[section];

    TCTEXTUREINFO srcInfo;
    TextureCacheGetInfo(srcTexture, srcInfo, 1);

    if (srcInfo.width >= sectionInfo.size.x || srcInfo.height >= sectionInfo.size.y) {
        // Calculate source mip level appropriate for section size
        int32_t srcMipLevel = 0;
        int32_t srcWidth = srcInfo.width;
        while (srcWidth > sectionInfo.size.x) {
            srcWidth /= 2;
            srcMipLevel++;
        }

        C2iVector srcPos = { 0, 0 };

        CCharacterComponent::Paste(srcTexture, dstMips, sectionInfo.pos, srcPos, sectionInfo.size, srcInfo, srcMipLevel);
    } else {
        C2iVector srcPos = { 0, 0 };

        CCharacterComponent::PasteScale(srcTexture, dstMips, sectionInfo.pos, srcPos, sectionInfo.size, srcInfo);
    }
}

void CCharacterComponent::PasteTransparent1Bit(void* srcTexture, const BlpPalPixel* srcPal, MipBits* dstMips, const C2iVector& dstPos, uint32_t dstWidth, const C2iVector& srcPos, const C2iVector& srcSize, TCTEXTUREINFO& srcInfo, int32_t srcMipLevel, int32_t dstMipLevelOfs) {
    // Prepare first mip level

    C2iVector curSrcSize = srcSize;

    C2iVector curSrcPos = srcPos;
    C2iVector curDstPos = dstPos;

    uint32_t curSrcWidth = srcInfo.width >> srcMipLevel;
    uint32_t curDstWidth = dstWidth;

    uint32_t curSrcHeight = srcInfo.height >> srcMipLevel;

    // Paste texture for each mip level

    for (int32_t curMipLevel = srcMipLevel; curMipLevel < srcInfo.mipCount; curMipLevel++) {
        auto srcMip = TextureCacheGetMip(srcTexture, curMipLevel);
        auto srcRow = &srcMip[(curSrcPos.y * curSrcWidth) + curSrcPos.x];
        auto srcAlphaRow = &srcMip[(curSrcWidth * curSrcHeight) + ((curSrcPos.y * curSrcWidth) + curSrcPos.x) / 8];

        auto dstMip = reinterpret_cast<uint8_t*>(dstMips->mip[curMipLevel + dstMipLevelOfs]);
        auto dstRow = &dstMip[(curDstPos.y * curDstWidth) + (curDstPos.x * 4)];

        // Calculate the end of the source region
        auto srcEnd = srcRow + curSrcWidth * curSrcSize.y;

        // Copy each row
        while (srcRow < srcEnd) {
            auto dst = reinterpret_cast<C4Pixel*>(dstRow);

            for (uint32_t x = 0; x < curSrcSize.x; x++) {
                // Get palette entry
                auto& src = srcPal[srcRow[x]];

                // Get alpha
                uint8_t packedAlpha = srcAlphaRow[x / 8];
                uint8_t transparent = (packedAlpha & 1 << x % 8) == 0;

                // Blend src color with dst
                dst->b = transparent ? dst->b : src.b;
                dst->g = transparent ? dst->g : src.g;
                dst->r = transparent ? dst->r : src.r;
                dst->a = 0xFF;

                dst++;
            }

            // Move to next row
            srcRow += curSrcWidth;
            srcAlphaRow += curSrcWidth / 8;
            dstRow += curDstWidth;
        }

        // Prepare next mip level

        curSrcSize.x = std::max(1, curSrcSize.x / 2);
        curSrcSize.y = std::max(1, curSrcSize.y / 2);

        curSrcPos.x /= 2;
        curSrcPos.y /= 2;
        curDstPos.x /= 2;
        curDstPos.y /= 2;

        curDstWidth /= 2;
        curSrcWidth /= 2;

        curSrcHeight /= 2;
    }
}

void CCharacterComponent::PasteTransparent4Bit(void* srcTexture, const BlpPalPixel* srcPal, MipBits* dstMips, const C2iVector& dstPos, uint32_t dstWidth, const C2iVector& srcPos, const C2iVector& srcSize, TCTEXTUREINFO& srcInfo, int32_t srcMipLevel, int32_t dstMipLevelOfs) {
    // Prepare first mip level

    C2iVector curSrcSize = srcSize;

    C2iVector curSrcPos = srcPos;
    C2iVector curDstPos = dstPos;

    uint32_t curSrcWidth = srcInfo.width >> srcMipLevel;
    uint32_t curDstWidth = dstWidth;

    uint32_t curSrcHeight = srcInfo.height >> srcMipLevel;

    // Paste texture for each mip level

    for (int32_t curMipLevel = srcMipLevel; curMipLevel < srcInfo.mipCount; curMipLevel++) {
        auto srcMip = TextureCacheGetMip(srcTexture, curMipLevel);
        auto srcRow = &srcMip[(curSrcPos.y * curSrcWidth) + curSrcPos.x];

        // Two texels of alpha per byte, the first texel in the low nibble
        uint32_t srcTexelIndex = (curSrcPos.y * curSrcWidth) + curSrcPos.x;
        auto srcAlphaPlane = &srcMip[curSrcWidth * curSrcHeight];

        auto dstMip = reinterpret_cast<uint8_t*>(dstMips->mip[curMipLevel + dstMipLevelOfs]);
        auto dstRow = &dstMip[(curDstPos.y * curDstWidth) + (curDstPos.x * 4)];

        // Calculate the end of the source region
        auto srcEnd = srcRow + curSrcWidth * curSrcSize.y;

        // Copy each row
        while (srcRow < srcEnd) {
            auto dst = reinterpret_cast<C4Pixel*>(dstRow);

            for (uint32_t x = 0; x < curSrcSize.x; x++) {
                // Get palette entry
                auto& src = srcPal[srcRow[x]];

                // Get alpha
                uint32_t texelIndex = srcTexelIndex + x;
                uint8_t packedAlpha = srcAlphaPlane[texelIndex / 2];
                uint8_t alpha = ((texelIndex & 1) ? (packedAlpha >> 4) : (packedAlpha & 0xF)) * 0x11;
                uint8_t invAlpha = 0xFF - alpha;

                // Blend src color with dst
                dst->b = (src.b * alpha + dst->b * invAlpha) >> 8;
                dst->g = (src.g * alpha + dst->g * invAlpha) >> 8;
                dst->r = (src.r * alpha + dst->r * invAlpha) >> 8;
                dst->a = 0xFF;

                dst++;
            }

            // Move to next row
            srcRow += curSrcWidth;
            srcTexelIndex += curSrcWidth;
            dstRow += curDstWidth;
        }

        // Prepare next mip level

        curSrcSize.x = std::max(1, curSrcSize.x / 2);
        curSrcSize.y = std::max(1, curSrcSize.y / 2);

        curSrcPos.x /= 2;
        curSrcPos.y /= 2;
        curDstPos.x /= 2;
        curDstPos.y /= 2;

        curDstWidth /= 2;
        curSrcWidth /= 2;

        curSrcHeight /= 2;
    }
}

void CCharacterComponent::PasteTransparent8Bit(void* srcTexture, const BlpPalPixel* srcPal, MipBits* dstMips, const C2iVector& dstPos, uint32_t dstWidth, const C2iVector& srcPos, const C2iVector& srcSize, TCTEXTUREINFO& srcInfo, int32_t srcMipLevel, int32_t dstMipLevelOfs) {
    // Prepare first mip level

    C2iVector curSrcSize = srcSize;

    C2iVector curSrcPos = srcPos;
    C2iVector curDstPos = dstPos;

    uint32_t curSrcWidth = srcInfo.width >> srcMipLevel;
    uint32_t curDstWidth = dstWidth;

    uint32_t curSrcHeight = srcInfo.height >> srcMipLevel;

    // Paste texture for each mip level

    for (int32_t curMipLevel = srcMipLevel; curMipLevel < srcInfo.mipCount; curMipLevel++) {
        auto srcMip = TextureCacheGetMip(srcTexture, curMipLevel);
        auto srcRow = &srcMip[(curSrcPos.y * curSrcWidth) + curSrcPos.x];
        auto srcAlphaRow = &srcMip[(curSrcWidth * curSrcHeight) + (curSrcPos.y * curSrcWidth) + curSrcPos.x];

        auto dstMip = reinterpret_cast<uint8_t*>(dstMips->mip[curMipLevel + dstMipLevelOfs]);
        auto dstRow = &dstMip[(curDstPos.y * curDstWidth) + (curDstPos.x * 4)];

        // Calculate the end of the source region
        auto srcEnd = srcRow + curSrcWidth * curSrcSize.y;

        // Copy each row
        while (srcRow < srcEnd) {
            auto dst = reinterpret_cast<C4Pixel*>(dstRow);

            for (uint32_t x = 0; x < curSrcSize.x; x++) {
                // Get palette entry
                auto& src = srcPal[srcRow[x]];

                // Get alpha
                uint8_t alpha = srcAlphaRow[x];
                uint8_t invAlpha = 0xFF - alpha;

                // Blend src color with dst
                dst->b = (src.b * alpha + dst->b * invAlpha) >> 8;
                dst->g = (src.g * alpha + dst->g * invAlpha) >> 8;
                dst->r = (src.r * alpha + dst->r * invAlpha) >> 8;
                dst->a = 0xFF;

                dst++;
            }

            // Move to next row
            srcRow += curSrcWidth;
            srcAlphaRow += curSrcWidth;
            dstRow += curDstWidth;
        }

        // Prepare next mip level

        curSrcSize.x = std::max(1, curSrcSize.x / 2);
        curSrcSize.y = std::max(1, curSrcSize.y / 2);

        curSrcPos.x /= 2;
        curSrcPos.y /= 2;
        curDstPos.x /= 2;
        curDstPos.y /= 2;

        curDstWidth /= 2;
        curSrcWidth /= 2;

        curSrcHeight /= 2;
    }
}

int32_t CCharacterComponent::PrevBeardStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);

    bool found;
    this->GetSectionsRecord(VARIATION_FACIAL_HAIR, data.facialHairStyleID, data.hairColorID, &found);

    // Facial features without sections (e.g. tauren horns) are simply numbered
    if (!found) {
        auto numStyles = static_cast<int32_t>(CCharacterComponent::s_characterFacialHairStylesList[data.raceID * UNITSEX_NUM_SEXES + data.sexID]);
        auto style = data.facialHairStyleID - 1;

        this->SetBeardStyle(style >= 0 ? style : numStyles - 1, true, nullptr);

        return 1;
    }

    auto numStyles = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACIAL_HAIR);

    if (numStyles <= 0) {
        return 0;
    }

    auto style = data.facialHairStyleID - 1;

    if (style < 0) {
        style = numStyles - 1;
    }

    while (style != data.facialHairStyleID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACIAL_HAIR, style);

        for (int32_t color = 0; color < numColors; color++) {
            auto rec = this->GetSectionsRecord(VARIATION_FACIAL_HAIR, style, color, nullptr);

            if (!rec || !ComponentCheckSectionFlags(rec->m_flags, selection)) {
                continue;
            }

            // Keep the current hair color if the new style supports it
            auto currentColorRec = this->GetSectionsRecord(VARIATION_FACIAL_HAIR, style, data.hairColorID, nullptr);

            if (currentColorRec && ComponentCheckSectionFlags(currentColorRec->m_flags, selection)) {
                this->SetBeardStyle(currentColorRec->m_variationIndex, true, nullptr);
                return 1;
            }

            this->SetHairColor(rec->m_colorIndex, false, nullptr);
            this->SetBeardStyle(rec->m_variationIndex, true, nullptr);

            return 1;
        }

        style--;

        if (style < 0) {
            style = numStyles - 1;
        }
    }

    return 0;
}

int32_t CCharacterComponent::PrevFace(COMPONENT_CONTEXT context, int32_t colorOffset) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numFaces = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACE);

    if (numFaces <= 0) {
        return 0;
    }

    auto face = data.faceID - 1;

    if (face < 0) {
        face = numFaces - 1;
    }

    while (face != data.faceID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_FACE, face);

        // Start the color search at the preferred skin color
        for (int32_t color = 0; color < numColors; color++) {
            color = (color + colorOffset) % numColors;

            auto faceRec = this->GetSectionsRecord(VARIATION_FACE, face, color, nullptr);
            auto skinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, color, nullptr);
            auto underwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, color, nullptr);

            bool valid = faceRec && ComponentCheckSectionFlags(faceRec->m_flags, selection)
                && skinRec && ComponentCheckSectionFlags(skinRec->m_flags, selection)
                && (context == CONTEXT_2 || (underwearRec && ComponentCheckSectionFlags(underwearRec->m_flags, selection)));

            if (!valid) {
                continue;
            }

            // Keep the current skin color if the new face supports it
            auto currentFaceRec = this->GetSectionsRecord(VARIATION_FACE, face, data.skinColorID, nullptr);
            auto currentSkinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, data.skinColorID, nullptr);
            auto currentUnderwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, data.skinColorID, nullptr);

            bool currentValid = currentFaceRec && ComponentCheckSectionFlags(currentFaceRec->m_flags, selection)
                && currentSkinRec && ComponentCheckSectionFlags(currentSkinRec->m_flags, selection)
                && (context == CONTEXT_2 || (currentUnderwearRec && ComponentCheckSectionFlags(currentUnderwearRec->m_flags, selection)));

            if (currentValid) {
                this->SetFace(face, true, nullptr);
                return 1;
            }

            this->SetSkinColor(color, false, true, nullptr);
            this->SetFace(face, true, nullptr);

            return 1;
        }

        face--;

        if (face < 0) {
            face = numFaces - 1;
        }
    }

    return 0;
}

int32_t CCharacterComponent::PrevHairColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR, data.hairStyleID);

    if (numColors <= 0) {
        return 0;
    }

    auto color = data.hairColorID - 1;

    if (color < 0) {
        color = numColors - 1;
    }

    while (color != data.hairColorID) {
        auto rec = this->GetSectionsRecord(VARIATION_HAIR, data.hairStyleID, color, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            this->SetHairColor(rec->m_colorIndex, true, nullptr);
            return 1;
        }

        color--;

        if (color < 0) {
            color = numColors - 1;
        }
    }

    return 0;
}

int32_t CCharacterComponent::PrevHairStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numStyles = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR);

    if (numStyles <= 0) {
        return 0;
    }

    auto style = data.hairStyleID - 1;

    if (style < 0) {
        style = numStyles - 1;
    }

    while (style != data.hairStyleID) {
        auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_HAIR, style);

        for (int32_t color = 0; color < numColors; color++) {
            auto rec = this->GetSectionsRecord(VARIATION_HAIR, style, color, nullptr);

            if (!rec || !ComponentCheckSectionFlags(rec->m_flags, selection)) {
                continue;
            }

            // Keep the current hair color if the new style supports it
            auto currentColorRec = this->GetSectionsRecord(VARIATION_HAIR, style, data.hairColorID, nullptr);

            if (currentColorRec && ComponentCheckSectionFlags(currentColorRec->m_flags, selection)) {
                this->SetHairStyle(currentColorRec->m_variationIndex, nullptr);
                return 1;
            }

            this->SetHairColor(rec->m_colorIndex, true, nullptr);
            this->SetHairStyle(rec->m_variationIndex, nullptr);

            // The new hair color may invalidate the facial hair style
            selection = GetSelectionFromContext(context, data.classID);

            auto facialHairStyle = ComponentGetFacialHairStyleByIndex(data.raceID, data.sexID, data.classID, data.hairColorID, data.facialHairStyleID, 0, selection);

            if (facialHairStyle >= 0) {
                this->SetBeardStyle(facialHairStyle, true, nullptr);
            }

            return 1;
        }

        style--;

        if (style < 0) {
            style = numStyles - 1;
        }
    }

    return 0;
}

int32_t CCharacterComponent::PrevSkinColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumColors(CCharacterComponent::s_chrVarArray, data.raceID, data.sexID, VARIATION_SKIN, 0);

    if (numColors <= 0) {
        return 0;
    }

    auto color = data.skinColorID - 1;

    if (color < 0) {
        color = numColors - 1;
    }

    while (color != data.skinColorID) {
        auto skinRec = this->GetSectionsRecord(VARIATION_SKIN, 0, color, nullptr);
        auto faceRec = this->GetSectionsRecord(VARIATION_FACE, data.faceID, color, nullptr);
        auto underwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, color, nullptr);

        bool valid = skinRec && ComponentCheckSectionFlags(skinRec->m_flags, selection)
            && faceRec && ComponentCheckSectionFlags(faceRec->m_flags, selection)
            && (context == CONTEXT_2 || (underwearRec && ComponentCheckSectionFlags(underwearRec->m_flags, selection)));

        if (valid) {
            this->SetSkinColor(skinRec->m_colorIndex, true, true, nullptr);
            return 1;
        }

        color--;

        if (color < 0) {
            color = numColors - 1;
        }
    }

    return 0;
}

int32_t CCharacterComponent::RandomBeardStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numStyles = ComponentGetNumFacialHairStyles(data.raceID, data.sexID, data.classID, data.hairColorID, context);
    auto index = numStyles > 0 ? CRandom::dice(numStyles, g_rndSeed) : 0;
    auto style = ComponentGetFacialHairStyleByIndex(data.raceID, data.sexID, data.classID, data.hairColorID, data.facialHairStyleID, index, selection);

    if (style < 0) {
        return 0;
    }

    this->SetBeardStyle(style, true, nullptr);

    return 1;
}

int32_t CCharacterComponent::RandomFace(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numFaces = ComponentGetNumFaces(data.raceID, data.sexID, data.classID, data.skinColorID, context);
    auto index = numFaces > 0 ? CRandom::dice(numFaces, g_rndSeed) : 0;
    auto face = ComponentGetFaceByIndex(data.raceID, data.sexID, data.skinColorID, index, selection);

    if (face < 0) {
        return 0;
    }

    this->SetFace(face, true, nullptr);

    return 1;
}

int32_t CCharacterComponent::RandomHairColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumHairColors(data.raceID, data.sexID, data.classID, data.hairStyleID, context);
    auto index = numColors > 0 ? CRandom::dice(numColors, g_rndSeed) : 0;
    auto color = ComponentGetHairColorByIndex(data.raceID, data.sexID, data.hairStyleID, index, selection);

    if (color < 0) {
        return 0;
    }

    this->SetHairColor(color, true, nullptr);

    return 1;
}

int32_t CCharacterComponent::RandomHairStyle(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numStyles = ComponentGetNumHairStyles(data.raceID, data.sexID, data.classID, data.hairColorID, context);
    auto index = numStyles > 0 ? CRandom::dice(numStyles, g_rndSeed) : 0;
    auto style = ComponentGetHairStyleByIndex(data.raceID, data.sexID, data.hairColorID, index, selection);

    if (style < 0) {
        return 0;
    }

    this->SetHairStyle(style, nullptr);

    return 1;
}

int32_t CCharacterComponent::RandomSkinColor(COMPONENT_CONTEXT context) {
    auto& data = this->m_data;
    auto selection = GetSelectionFromContext(context, data.classID);
    auto numColors = ComponentGetNumSkinColors(data.raceID, data.sexID, data.classID, context);
    auto index = numColors > 0 ? CRandom::dice(numColors, g_rndSeed) : 0;
    auto color = ComponentGetSkinColorByIndex(data.raceID, data.sexID, index, selection);

    if (color < 0) {
        return 0;
    }

    this->SetSkinColor(color, true, true, nullptr);

    return 1;
}

void CCharacterComponent::RemoveHandItem(CM2Model* model, INVENTORY_SLOTS invSlot, SHEATHE_TYPE sheatheType, bool shield) {
    if (!model || invSlot < INVSLOT_BACK || invSlot > INVSLOT_TABARD) {
        return;
    }

    auto itemLink = ATTACH_NONE;
    auto sheatheLink = ATTACH_NONE;

    if (invSlot == INVSLOT_MAINHAND) {
        itemLink = ATTACH_HANDR;
        sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, true);
    } else if (invSlot == INVSLOT_OFFHAND || invSlot == INVSLOT_RANGED) {
        itemLink = shield ? ATTACH_SHIELD : ATTACH_HANDL;
        sheatheLink = CCharacterComponent::GetSheatheLink(sheatheType, false);
    }

    CCharacterComponent::RemoveLinkpt(model, itemLink);
    CCharacterComponent::RemoveLinkpt(model, sheatheLink);
}

void CCharacterComponent::RemoveItem(ITEM_SLOT itemSlot) {
    if (!this->m_items[itemSlot]) {
        return;
    }

    this->m_flags |= 0x4;

    switch (itemSlot) {
    case ITEMSLOT_0: {
        // Helm: restore the hair and facial geosets it replaced
        this->m_data.geosets[0] = ComponentGetHairGeoset(&this->m_data);

        // TODO detach the helm model

        auto facialHairStyleRec = ComponentGetFacialHairStyleRecord(&this->m_data);

        if (facialHairStyleRec) {
            this->m_data.geosets[1] = 100 + facialHairStyleRec->m_geoset[0];
            this->m_data.geosets[3] = 300 + facialHairStyleRec->m_geoset[1];
            this->m_data.geosets[2] = 200 + facialHairStyleRec->m_geoset[2];
            this->m_data.geosets[7] = 702;
            this->m_data.geosets[16] = 1600 + facialHairStyleRec->m_geoset[3];
            this->m_data.geosets[17] = 1700 + facialHairStyleRec->m_geoset[4];
        }

        this->m_items[itemSlot] = 0;

        return;
    }

    case ITEMSLOT_1:
        // Detach both shoulder pads.
        if (this->m_data.model) {
            this->m_data.model->DetachAllChildrenById(ATTACH_SHOULDERR);
            this->m_data.model->DetachAllChildrenById(ATTACH_SHOULDERL);
        }

        this->m_items[itemSlot] = 0;

        return;

    case ITEMSLOT_3:
        this->m_flags &= ~0x20;
        break;

    case ITEMSLOT_9:
        if (this->m_flags & 0x40) {
            this->m_items[itemSlot] = 0;

            return;
        }

        this->m_data.geosets[15] = 1501;
        this->ClearTorsoItemDisplays(4);

        break;

    case ITEMSLOT_10:
        this->m_data.geosets[15] = 1501;
        this->m_items[itemSlot] = 0;

        return;

    case ITEMSLOT_11:
        // TODO detach the quiver model

        this->m_items[itemSlot] = 0;

        return;

    default:
        break;
    }

    auto displayRec = g_itemDisplayInfoDB.GetRecord(this->m_items[itemSlot]);

    this->m_items[itemSlot] = 0;

    for (int32_t section = 0; section < NUM_COMPONENT_SECTIONS; section++) {
        (this->*CCharacterComponent::s_itemFunc[section])(itemSlot, displayRec, false);
    }
}

void CCharacterComponent::RemoveItemByInventoryType(int32_t inventoryType) {
    switch (inventoryType) {
    case INVTYPE_HEAD:
        this->RemoveItem(ITEMSLOT_0);
        break;

    case INVTYPE_SHOULDER:
        this->RemoveItem(ITEMSLOT_1);
        break;

    case INVTYPE_BODY:
        this->RemoveItem(ITEMSLOT_2);
        break;

    case INVTYPE_CHEST:
    case INVTYPE_ROBE:
        this->RemoveItem(ITEMSLOT_3);
        break;

    case INVTYPE_WAIST:
        this->RemoveItem(ITEMSLOT_4);
        break;

    case INVTYPE_LEGS:
        this->RemoveItem(ITEMSLOT_5);
        break;

    case INVTYPE_FEET:
        this->RemoveItem(ITEMSLOT_6);
        break;

    case INVTYPE_WRISTS:
        this->RemoveItem(ITEMSLOT_7);
        break;

    case INVTYPE_HANDS:
        this->RemoveItem(ITEMSLOT_8);
        break;

    case INVTYPE_CLOAK:
        this->RemoveItem(ITEMSLOT_10);
        break;

    case INVTYPE_TABARD:
        this->RemoveItem(ITEMSLOT_9);
        break;

    default:
        break;
    }
}

void CCharacterComponent::RemoveLinkpt(CM2Model* model, GEOCOMPONENTLINKS link) {
    if (link == ATTACH_NONE) {
        return;
    }

    if (link == ATTACH_SHIELD || link == ATTACH_HANDL) {
        CCharacterComponent::ComponentOpenFingers(model, HAND_LEFT);
    }

    if (link == ATTACH_HANDR) {
        CCharacterComponent::ComponentOpenFingers(model, HAND_RIGHT);
    }

    if (model && model->IsLoaded(0, 0)) {
        if (model->HasAttachment(link)) {
            model->DetachAllChildrenById(link);
        }
    }
}

void CCharacterComponent::ReplaceMonsterSkin(CM2Model* model, const CreatureDisplayInfoRec* displayInfoRec, const CreatureModelDataRec* modelDataRec) {
    if (!model || !displayInfoRec || !modelDataRec) {
        return;
    }

    CStatus status;
    char texturePath[STORM_MAX_PATH];

    // Copy model path to use as base path for texture
    auto src = modelDataRec->m_modelName;
    auto dst = texturePath;
    while (*src) {
        *dst++ = *src++;
    }
    *dst = '\0';

    // Locate start of model file name
    auto lastSlash = strrchr(texturePath, '\\');
    auto modelFileName = lastSlash ? lastSlash + 1 : texturePath;

    auto textureFlags = CGxTexFlags(GxTex_LinearMipLinear, 1, 1, 0, 0, 0, 1);

    for (uint32_t i = 0; i < 3; i++) {
        auto textureName = displayInfoRec->m_textureVariation[i];

        if (textureName[0] == '\0') {
            continue;
        }

        // Replace model file name with texture name
        src = textureName;
        dst = modelFileName;
        while (*src) {
            *dst++ = *src++;
        }
        *dst = '\0';

        auto texture = TextureCreate(texturePath, textureFlags, &status, 0);

        if (texture) {
            model->ReplaceTexture(11 + i, texture);
            HandleClose(texture);
        }
    }

    ReplaceParticleColor(model, displayInfoRec->m_particleColorID);
}

void CCharacterComponent::UpdateBaseTexture(EGxTexCommand cmd, uint32_t width, uint32_t height, uint32_t depth, uint32_t mipLevel, void* userArg, uint32_t& texelStrideInBytes, const void*& texels) {
    auto component = static_cast<CCharacterComponent*>(userArg);

    switch(cmd) {
    case GxTex_Lock: {
        if (!s_bInRenderPrep) {
            component->RenderPrepAll();
        }

        break;
    }

    case GxTex_Latch: {
        if (component->m_textureFormat == GxTex_Dxt1) {
            // TODO
            STORM_ASSERT(false);
        } else {
            texelStrideInBytes = 4 * width;
        }

        // TODO conditional check on some member of component

        auto buffer = component->m_textureFormat == GxTex_Dxt1
            ? CCharacterComponent::s_textureBufferCompressed
            : CCharacterComponent::s_textureBuffer;

        texels = buffer->mip[mipLevel];

        break;
    }

    // TODO unknown command
    case 3: {
        // TODO
        break;
    }
    }
}

CCharacterComponent::~CCharacterComponent() {
    // TODO

    if (this->m_baseTexture) {
        // TODO GxTexSetCannotUpdate

        HandleClose(this->m_baseTexture);
        this->m_baseTexture = nullptr;
    }

    if (this->m_data.model) {
        this->m_data.model->Release();
        this->m_data.model = nullptr;
    }

    for (auto& texture : this->m_texture) {
        TextureCacheDestroyTexture(texture);
        texture = nullptr;
    }

    for (auto& itemDisplay : this->m_itemDisplays) {
        for (auto& texture : itemDisplay.texture) {
            TextureCacheDestroyTexture(texture);
            texture = nullptr;
        }
    }

    // TODO
}

void CCharacterComponent::AddItem(ITEM_SLOT itemSlot, int32_t displayID, int32_t a4) {
    if (displayID <= 0) {
        return;
    }

    auto displayRec = g_itemDisplayInfoDB.GetRecord(displayID);

    if (!displayRec) {
        return;
    }

    this->AddItem(itemSlot, displayRec, a4);
}

void CCharacterComponent::AddItem(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, int32_t a4) {
    this->m_flags |= 0x4;

    this->m_items[itemSlot] = displayRec->m_ID;

    // Helm

    if (itemSlot == ITEMSLOT_0) {
        // TODO handle helm

        return;
    }

    // Shoulders: two separate pad models attached to the shoulder-right (5) and shoulder-left (6)
    // attachment points, the same way the reference dresses a character. modelName/modelTexture[0]
    // is the right pad, [1] the left.
    if (itemSlot == ITEMSLOT_1) {
        CM2Model* body = this->m_data.model;

        if (body) {
            static const GEOCOMPONENTLINKS shoulderLinks[2] = { ATTACH_SHOULDERR, ATTACH_SHOULDERL };

            for (int32_t side = 0; side < 2; side++) {
                if (!displayRec->m_modelName[side] || !*displayRec->m_modelName[side]) {
                    continue;
                }

                SStrPrintf(s_buffer, sizeof(s_buffer), "Item\\ObjectComponents\\Shoulder\\%s", displayRec->m_modelName[side]);
                SStrCopy(s_pathEnd, s_buffer);

                SStrPrintf(s_buffer, sizeof(s_buffer), "Item\\ObjectComponents\\Shoulder\\%s.blp", displayRec->m_modelTexture[side]);
                SStrCopy(s_pathEnd2, s_buffer);

                CCharacterComponent::AddLink(body, shoulderLinks[side], s_path, s_path2, displayRec->m_itemVisual, displayRec);
            }
        }

        return;
    }

    // Cape: not an attached model but a body geoset (group 15) plus a dedicated cape texture painted
    // onto it. The length variant is the cloak's geosetGroup[0] -> geoset 1500+variant (the skin has
    // 1501-1506), and the texture (m_modelTexture[0], e.g. "Cape_Cloth_A_02Green") is the object-skin
    // texture, type 2 on the body model -- both verified against the client data.
    if (itemSlot == ITEMSLOT_10) {
        int32_t variant = displayRec->m_geosetGroup[0];
        this->m_data.geosets[15] = variant ? (1500 + variant) : 1501;

        CM2Model* body = this->m_data.model;

        if (body && displayRec->m_modelTexture[0] && *displayRec->m_modelTexture[0]) {
            SStrPrintf(s_buffer, sizeof(s_buffer), "Item\\ObjectComponents\\Cape\\%s.blp", displayRec->m_modelTexture[0]);

            auto textureFlags = CGxTexFlags(GxTex_LinearMipNearest, 0, 0, 0, 0, 0, 1);
            auto texture = TextureCreate(s_buffer, textureFlags, &s_status, 0);

            if (texture) {
                body->ReplaceTexture(2, texture);
                HandleClose(texture);
            }
        }

        return;
    }

    // Unk

    if (itemSlot == ITEMSLOT_11) {
        // TODO handle unknown item

        return;
    }

    if (itemSlot == ITEMSLOT_3) {
        // TODO flag manipulation
    }

    bool isNPC = this->m_data.flags & 0x1;

    if (isNPC) {
        return;
    }

    // Items don't manipulate head component sections

    for (int32_t section = SECTION_ARM_UPPER; section <= SECTION_FOOT; section++) {
        if (*displayRec->m_texture[section] && s_itemPriority[itemSlot][section] != -1) {
            (this->*CCharacterComponent::s_itemFunc[section])(itemSlot, displayRec, true);
        }
    }
}

void CCharacterComponent::AddItemBySlot(INVENTORY_SLOTS invSlot, int32_t displayID, int32_t a4) {
    if (invSlot > EQUIPPED_LAST) {
        return;
    }

    STORM_ASSERT(displayID > 0);

    switch (invSlot) {
    case INVSLOT_HEAD:
        this->AddItem(ITEMSLOT_0, displayID, a4);
        break;

    case INVSLOT_SHOULDER:
        this->AddItem(ITEMSLOT_1, displayID, a4);
        break;

    case INVSLOT_BODY:
        this->AddItem(ITEMSLOT_2, displayID, a4);
        break;

    case INVSLOT_CHEST:
        this->AddItem(ITEMSLOT_3, displayID, a4);
        break;

    case INVSLOT_WAIST:
        this->AddItem(ITEMSLOT_4, displayID, a4);
        break;

    case INVSLOT_LEGS:
        this->AddItem(ITEMSLOT_5, displayID, a4);
        break;

    case INVSLOT_FEET:
        this->AddItem(ITEMSLOT_6, displayID, a4);
        break;

    case INVSLOT_WRIST:
        this->AddItem(ITEMSLOT_7, displayID, a4);
        break;

    case INVSLOT_HAND:
        this->AddItem(ITEMSLOT_8, displayID, a4);
        break;

    case INVSLOT_BACK:
        this->AddItem(ITEMSLOT_10, displayID, a4);
        break;

    case INVSLOT_TABARD:
        this->AddItem(ITEMSLOT_9, displayID, a4);
        break;

    default:
        break;
    }
}

void CCharacterComponent::AddItemByInventoryType(int32_t inventoryType, int32_t displayID) {
    this->RemoveItemByInventoryType(inventoryType);

    if (displayID <= 0) {
        return;
    }

    switch (inventoryType) {
    case INVTYPE_HEAD:
        this->AddItem(ITEMSLOT_0, displayID, 0);
        break;

    case INVTYPE_SHOULDER:
        this->AddItem(ITEMSLOT_1, displayID, 0);
        break;

    case INVTYPE_BODY:
        this->AddItem(ITEMSLOT_2, displayID, 0);
        break;

    case INVTYPE_CHEST:
    case INVTYPE_ROBE:
        this->AddItem(ITEMSLOT_3, displayID, 0);
        break;

    case INVTYPE_WAIST:
        this->AddItem(ITEMSLOT_4, displayID, 0);
        break;

    case INVTYPE_LEGS:
        this->AddItem(ITEMSLOT_5, displayID, 0);
        break;

    case INVTYPE_FEET:
        this->AddItem(ITEMSLOT_6, displayID, 0);
        break;

    case INVTYPE_WRISTS:
        this->AddItem(ITEMSLOT_7, displayID, 0);
        break;

    case INVTYPE_HANDS:
        this->AddItem(ITEMSLOT_8, displayID, 0);
        break;

    case INVTYPE_WEAPON:
    case INVTYPE_2HWEAPON:
    case INVTYPE_WEAPONMAINHAND:
        this->AddHandItemDisplay(displayID, 0, INVSLOT_MAINHAND, false);
        break;

    case INVTYPE_SHIELD:
        this->AddHandItemDisplay(displayID, 1, INVSLOT_OFFHAND, true);
        break;

    case INVTYPE_CLOAK:
        this->AddItem(ITEMSLOT_10, displayID, 0);
        break;

    case INVTYPE_TABARD:
        this->AddItem(ITEMSLOT_9, displayID, 0);
        break;

    case INVTYPE_WEAPONOFFHAND:
        this->AddHandItemDisplay(displayID, 1, INVSLOT_OFFHAND, false);
        break;

    default:
        break;
    }
}

void CCharacterComponent::AddHandItemDisplay(int32_t displayID, int32_t handIndex, INVENTORY_SLOTS invSlot, bool shield) {
    auto displayRec = g_itemDisplayInfoDB.GetRecord(displayID);

    if (!displayRec) {
        return;
    }

    this->m_handItems[handIndex] = displayID;

    CCharacterComponent::AddHandItem(this->m_data.model, displayRec, invSlot, SHEATHE_0, false, shield, false, 0);
}

void CCharacterComponent::ClearItemDisplay(COMPONENT_SECTIONS section, int32_t priority) {
    if (priority == -1) {
        return;
    }

    if (this->m_itemDisplays[section].texture[priority]) {
        TextureCacheDestroyTexture(this->m_itemDisplays[section].texture[priority]);
        this->m_itemDisplays[section].texture[priority] = nullptr;
    }

    this->m_itemDisplays[section].displayID[priority] = 0;

    this->m_itemDisplays[section].priorityDirty &= ~(1 << priority);
}

void CCharacterComponent::ClearTorsoItemDisplays(int32_t fromPriority) {
    // Drops the shirt, chest, and tabard layers from the torso sections
    for (int32_t priority = fromPriority; priority > 1; priority--) {
        this->ClearItemDisplay(SECTION_TORSO_UPPER, priority);
        this->ClearItemDisplay(SECTION_TORSO_LOWER, priority);
    }

    this->m_sectionDirty |= (1 << SECTION_TORSO_UPPER) | (1 << SECTION_TORSO_LOWER);

    // TODO component request logic

    this->m_flags &= ~0x8;
}

void CCharacterComponent::CreateBaseTexture() {
    auto dataFormat = this->m_textureFormat == GxTex_Dxt1
        ? GxTex_Dxt1
        : GxTex_Argb8888;

    CGxTexFlags flags = CGxTexFlags(GxTex_LinearMipLinear, 0, 0, 0, 0, 0, 1);
    if (GxDevApi() == GxApi_GLL) {
        flags.m_bit15 = 1;
    }

    auto baseTexture = TextureCreate(
       CCharacterComponent::s_textureSize,
       CCharacterComponent::s_textureSize,
       this->m_textureFormat,
       dataFormat,
       flags,
       this,
       &CCharacterComponent::UpdateBaseTexture,
       "CharacterBaseSkin",
       1
    );

    this->m_baseTexture = baseTexture;

    this->m_data.model->ReplaceTexture(1, this->m_baseTexture);
}

void* CCharacterComponent::CreateTexture(const ItemDisplayInfoRec* displayRec, int32_t section) {
    SStrPrintf(
        s_path,
        STORM_MAX_PATH,
        "Item\\TextureComponents\\%s\\%s_%s.blp",
        s_componentSections[section],
        displayRec->m_texture[section],
        s_fileDecorations[2]
    );

    // Substitute gender suffix
    if (!SFile::FileExists(s_path)) {
        s_path[SStrLen(s_path) - 5] = *s_fileDecorations[this->m_data.sexID];
    }

    return TextureCacheCreateTexture(s_path);
}

void CCharacterComponent::GeosRenderPrep() {
    // Check for eye glow

    bool eyeGlow = false;

    if (this->m_data.classID == 6) {
        eyeGlow = true;
    } else {
        auto sectionsRec = this->GetSectionsRecord(VARIATION_FACE, this->m_data.faceID, this->m_data.skinColorID, nullptr);

        if (sectionsRec && sectionsRec->m_flags & 0x4) {
            eyeGlow = true;
        }
    }

    // Hide all geosets (0 - 2000)

    this->m_data.model->SetGeometryVisible(0, 2000, 0);

    // Show base skin geoset (0)

    this->m_data.model->SetGeometryVisible(0, 0, 1);

    // Show all enabled geosets

    for (int32_t geoset = 0; geoset < NUM_GEOSET; geoset++) {
        if (geoset == GEOSET_EYE_EFFECTS && eyeGlow) {
            this->m_data.model->SetGeometryVisible(1703, 1703, 1);
        } else {
            this->m_data.model->SetGeometryVisible(this->m_data.geosets[geoset], this->m_data.geosets[geoset], 1);
        }
    }

    // TODO

    this->m_flags &= ~0x4;
}

void CCharacterComponent::GetPreferences(CharacterPreferences* preferences) {
    if (!preferences) {
        return;
    }

    preferences->raceID = this->m_data.raceID;
    preferences->sexID = this->m_data.sexID;
    preferences->classID = this->m_data.classID;
    preferences->hairColorID = this->m_data.hairColorID;
    preferences->skinColorID = this->m_data.skinColorID;
    preferences->faceID = this->m_data.faceID;
    preferences->facialHairStyleID = this->m_data.facialHairStyleID;
    preferences->hairStyleID = this->m_data.hairStyleID;
}

CharSectionsRec* CCharacterComponent::GetSectionsRecord(COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex, int32_t colorIndex, bool* found) {
    return ComponentGetSectionsRecord(
        CCharacterComponent::s_chrVarArray,
        this->m_data.raceID,
        this->m_data.sexID,
        sectionIndex,
        variationIndex,
        colorIndex,
        found
    );
}

int32_t CCharacterComponent::Init(ComponentData* data, const char* a3) {
    // If existing model is present, release it before copying in new data
    if (this->m_data.model) {
        this->m_data.model->Release();
    }

    this->m_data = *data;

    // TODO

    this->SetSkinColor(this->m_data.skinColorID, false, true, a3);
    this->SetHairStyle(this->m_data.hairStyleID, a3);
    this->SetBeardStyle(this->m_data.facialHairStyleID, false, a3);

    return 1;
}

int32_t CCharacterComponent::ItemsLoaded(int32_t a2) {
    if (a2) {
        TCTEXTUREINFO info;

        for (int32_t section = 0; section < NUM_COMPONENT_SECTIONS; section++) {
            if (!(this->m_sectionDirty & (1 << section))) {
                continue;
            }

            auto& itemDisplay = this->m_itemDisplays[section];

            for (int32_t priority = 0; priority < 7; priority++) {
                if (!itemDisplay.displayID[priority] && !itemDisplay.texture[priority]) {
                    continue;
                }

                if (itemDisplay.displayID[priority] && !itemDisplay.texture[priority]) {
                    auto displayRec = g_itemDisplayInfoDB.GetRecord(itemDisplay.displayID[priority]);
                    itemDisplay.texture[priority] = this->CreateTexture(displayRec, section);
                }

                // Trigger texture load
                TextureCacheGetInfo(itemDisplay.texture[priority], info, 1);

                if (TextureCacheHasMips(itemDisplay.texture[priority])) {
                    itemDisplay.priorityDirty |= (1 << priority);
                } else {
                    itemDisplay.priorityDirty &= ~(1 << priority);
                }
            }
        }

        return 1;
    }

    // Without forcing, report whether every item texture has finished loading

    int32_t loaded = 1;

    TCTEXTUREINFO info;

    for (int32_t section = 0; section < NUM_COMPONENT_SECTIONS; section++) {
        if (!(this->m_sectionDirty & (1 << section))) {
            continue;
        }

        auto& itemDisplay = this->m_itemDisplays[section];

        for (int32_t priority = 0; priority < 7; priority++) {
            if (!itemDisplay.displayID[priority] && !itemDisplay.texture[priority]) {
                continue;
            }

            if (itemDisplay.displayID[priority] && !itemDisplay.texture[priority]) {
                auto displayRec = g_itemDisplayInfoDB.GetRecord(itemDisplay.displayID[priority]);

                if (displayRec) {
                    itemDisplay.texture[priority] = this->CreateTexture(displayRec, section);
                }
            }

            if (!TextureCacheGetInfo(itemDisplay.texture[priority], info, 0)) {
                loaded = 0;
                continue;
            }

            if (TextureCacheHasMips(itemDisplay.texture[priority])) {
                itemDisplay.priorityDirty |= (1 << priority);
            } else {
                itemDisplay.priorityDirty &= ~(1 << priority);
            }
        }
    }

    return loaded;
}

void CCharacterComponent::LoadBaseVariation(COMPONENT_VARIATIONS sectionIndex, int32_t textureIndex, int32_t variationIndex, int32_t colorIndex, COMPONENT_SECTIONS section, const char* a7) {
    int32_t index = TEXTURE_INDEX(sectionIndex, textureIndex);

    if (this->m_texture[index]) {
        TextureCacheDestroyTexture(this->m_texture[index]);
        this->m_texture[index] = nullptr;
    }

    auto valid = ComponentValidateBase(
        CCharacterComponent::s_chrVarArray,
        this->m_data.raceID,
        this->m_data.sexID,
        sectionIndex,
        variationIndex,
        colorIndex
    );
    if (!valid) {
        return;
    }

    auto sectionsRec = this->GetSectionsRecord(sectionIndex, variationIndex, colorIndex, nullptr);

    auto textureName = sectionsRec->m_textureName[textureIndex];

    if (*textureName) {
        SStrCopy(s_pathEnd, textureName);
        this->m_texture[index] = TextureCacheCreateTexture(s_path);
    }

    this->m_sectionDirty |= 1 << section;

    // TODO

    this->m_flags &= ~0x8;
}

static void UpdateBaseTextureRect(HTEXTURE baseTexture, const C2iVector& pos, const C2iVector& size) {
    auto gxTex = TextureGetGxTex(baseTexture, 1, nullptr);

    if (gxTex) {
        GxTexUpdate(gxTex, pos.x, pos.y, pos.x + size.x, pos.y + size.y, 1);
    }
}

void CCharacterComponent::PrepSections() {
    // Composite each dirty section into the shared texture buffer and push it to the base texture

    for (int32_t section = 0; section < NUM_COMPONENT_SECTIONS; section++) {
        if (!(this->m_sectionDirty & (1 << section))) {
            continue;
        }

        (this->*CCharacterComponent::s_prepFunc[section])();

        if (!(this->m_flags & 0x1) && this->m_textureFormat != GxTex_Dxt1) {
            auto& sectionInfo = CCharacterComponent::s_sectionInfo[section];
            UpdateBaseTextureRect(this->m_baseTexture, sectionInfo.pos, sectionInfo.size);
        }
    }

    if (!(this->m_flags & 0x1) && this->m_textureFormat != GxTex_Dxt1) {
        return;
    }

    if (this->m_textureFormat == GxTex_Dxt1) {
        // TODO compress s_textureBuffer into s_textureBufferCompressed
    }

    // Flag 0x1 means the whole texture changed (e.g. a new skin color)
    if (this->m_flags & 0x1) {
        C2iVector pos = { 0, 0 };
        C2iVector size = { static_cast<int32_t>(CCharacterComponent::s_textureSize), static_cast<int32_t>(CCharacterComponent::s_textureSize) };
        UpdateBaseTextureRect(this->m_baseTexture, pos, size);

        return;
    }

    for (int32_t section = 0; section < NUM_COMPONENT_SECTIONS; section++) {
        if (this->m_sectionDirty & (1 << section)) {
            auto& sectionInfo = CCharacterComponent::s_sectionInfo[section];
            UpdateBaseTextureRect(this->m_baseTexture, sectionInfo.pos, sectionInfo.size);
        }
    }
}

int32_t CCharacterComponent::RenderPrep(int32_t a2) {
    if (this->m_data.flags & 0x1) {
        if (this->m_flags & 0x4) {
            this->GeosRenderPrep();
        }

        return 1;
    }

    // TODO

    if (a2) {
        // TODO

        this->VariationsLoaded(1);
        this->ItemsLoaded(1);

        this->m_flags |= 8u;

        this->RenderPrepSections();
        // TODO this->Sub79F820();

        return 1;
    }

    // TODO
    return 1;
}

void CCharacterComponent::RenderPrepAL() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_ARM_LOWER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_ARM_LOWER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    for (int32_t priority = 0; priority < SECTION_AL_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_ARM_LOWER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepAU() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_ARM_UPPER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_ARM_UPPER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    for (int32_t priority = 0; priority < SECTION_AU_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_ARM_UPPER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepFO() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_FOOT];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_FOOT, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    for (int32_t priority = 0; priority < SECTION_FO_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_FOOT, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepHA() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_HAND];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_HAND, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    for (int32_t priority = 0; priority < SECTION_HA_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_HAND, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepHL() {
    auto sectionsRec = this->GetSectionsRecord(VARIATION_SKIN, 0, this->m_data.skinColorID, nullptr);

    // Skin texture

    if (sectionsRec && sectionsRec->m_flags & 0x8) {
        auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
        CCharacterComponent::PasteFromSkin(SECTION_HEAD_LOWER, skinTexture, CCharacterComponent::s_textureBuffer);
    }

    // Face texture

    auto faceLowerTexture = this->m_texture[TEXTURE_INDEX(VARIATION_FACE, 0)];
    if (faceLowerTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_LOWER, faceLowerTexture, CCharacterComponent::s_textureBuffer);
    }

    // Hair textures

    auto facialHairLowerTexture = this->m_texture[TEXTURE_INDEX(VARIATION_FACIAL_HAIR, 0)];
    if (facialHairLowerTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_LOWER, facialHairLowerTexture, CCharacterComponent::s_textureBuffer);
    }

    auto hairLowerTexture = this->m_texture[TEXTURE_INDEX(VARIATION_HAIR, 1)];
    if (hairLowerTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_LOWER, hairLowerTexture, CCharacterComponent::s_textureBuffer);
    }
}

void CCharacterComponent::RenderPrepHU() {
    auto sectionsRec = this->GetSectionsRecord(VARIATION_SKIN, 0, this->m_data.skinColorID, nullptr);

    // Skin texture

    if (sectionsRec && sectionsRec->m_flags & 0x8) {
        auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
        CCharacterComponent::PasteFromSkin(SECTION_HEAD_UPPER, skinTexture, CCharacterComponent::s_textureBuffer);
    }

    // Face texture

    auto faceUpperTexture = this->m_texture[TEXTURE_INDEX(VARIATION_FACE, 1)];
    if (faceUpperTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_UPPER, faceUpperTexture, CCharacterComponent::s_textureBuffer);
    }

    // Hair textures

    auto facialHairUpperTexture = this->m_texture[TEXTURE_INDEX(VARIATION_FACIAL_HAIR, 1)];
    if (facialHairUpperTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_UPPER, facialHairUpperTexture, CCharacterComponent::s_textureBuffer);
    }

    auto hairUpperTexture = this->m_texture[TEXTURE_INDEX(VARIATION_HAIR, 2)];
    if (hairUpperTexture) {
        CCharacterComponent::PasteToSection(SECTION_HEAD_UPPER, hairUpperTexture, CCharacterComponent::s_textureBuffer);
    }
}

void CCharacterComponent::RenderPrepLL() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_LEG_LOWER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_LEG_LOWER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    auto firstPriority = 0;
    auto itemPriorities = SECTION_LL_ITEM_PRIORITIES;

    if (this->m_flags & 0x20) {
        firstPriority = 1;
        itemPriorities = SECTION_LL_ITEM_PRIORITIES - 2;
    }

    for (int32_t priority = firstPriority; priority < itemPriorities; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_LEG_LOWER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepLU() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_LEG_UPPER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_LEG_UPPER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Underwear texture

    if ((this->m_flags & 0x20) || !(itemDisplay.priorityDirty & ((1 << 0) | (1 << 1)))) {
        auto bottomUnderwearTexture = this->m_texture[TEXTURE_INDEX(VARIATION_UNDERWEAR, 0)];
        if (bottomUnderwearTexture) {
            CCharacterComponent::PasteToSection(SECTION_LEG_UPPER, bottomUnderwearTexture, CCharacterComponent::s_textureBuffer);
        }
    }

    // Item textures

    auto firstPriority = (this->m_flags & 0x20) ? 1 : 0;

    for (int32_t priority = firstPriority; priority < SECTION_LU_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_LEG_UPPER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepTL() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_TORSO_LOWER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_TORSO_LOWER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Item textures

    for (int32_t priority = 0; priority < SECTION_TL_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_TORSO_LOWER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepTU() {
    auto& itemDisplay = this->m_itemDisplays[SECTION_TORSO_UPPER];

    // Skin texture

    auto skinTexture = this->m_texture[TEXTURE_INDEX(VARIATION_SKIN, 0)];
    CCharacterComponent::PasteFromSkin(SECTION_TORSO_UPPER, skinTexture, CCharacterComponent::s_textureBuffer);

    // Underwear texture

    if (!(itemDisplay.priorityDirty & ((1 << 0) | (1 << 1) | (1 << 2)))) {
        auto topUnderwearTexture = this->m_texture[TEXTURE_INDEX(VARIATION_UNDERWEAR, 1)];
        if (topUnderwearTexture) {
            CCharacterComponent::PasteToSection(SECTION_TORSO_UPPER, topUnderwearTexture, CCharacterComponent::s_textureBuffer);
        }
    }

    // Item textures

    for (int32_t priority = 0; priority < SECTION_TU_ITEM_PRIORITIES; priority++) {
        if (itemDisplay.priorityDirty & (1 << priority)) {
            CCharacterComponent::PasteToSection(SECTION_TORSO_UPPER, itemDisplay.texture[priority], CCharacterComponent::s_textureBuffer);
        }
    }
}

void CCharacterComponent::RenderPrepAll() {
    // TODO

    this->m_flags &= ~0x8;

    this->VariationsLoaded(1);
    this->ItemsLoaded(1);

    for (uint32_t i = 0; i < NUM_COMPONENT_SECTIONS; i++) {
        (this->*CCharacterComponent::s_prepFunc[i])();
    }

    // TODO

    this->m_flags &= ~0x1;

    this->m_sectionDirty = 0;

    // TODO
}

void CCharacterComponent::RenderPrepSections() {
    s_bInRenderPrep = 1;

    if (this->m_flags & 0x4) {
        this->GeosRenderPrep();
    }

    if (!this->m_baseTexture) {
        this->CreateBaseTexture();
    }

    this->PrepSections();

    this->m_flags &= ~0x1;

    this->m_sectionDirty = 0;

    // Item textures have been composited, so release them until the section changes again

    for (auto& itemDisplay : this->m_itemDisplays) {
        for (int32_t priority = 0; priority < 7; priority++) {
            if (itemDisplay.displayID[priority] && itemDisplay.texture[priority]) {
                TextureCacheDestroyTexture(itemDisplay.texture[priority]);
                itemDisplay.texture[priority] = nullptr;
            }
        }
    }

    // TODO component request logic

    s_bInRenderPrep = 0;
}

void CCharacterComponent::ReplaceExtraSkinTexture(const char* a2) {
    if (!ComponentValidateBase(
       CCharacterComponent::s_chrVarArray,
       this->m_data.raceID,
       this->m_data.sexID,
       VARIATION_SKIN,
       0,
       this->m_data.hairColorID
    )) {
        return;
    }

    auto sectionsRec = this->GetSectionsRecord(VARIATION_SKIN, 0, this->m_data.skinColorID, nullptr);

    if (!*sectionsRec->m_textureName[1]) {
        return;
    }

    SStrCopy(s_pathEnd, sectionsRec->m_textureName[1]);

    auto extraSkinTexture = CCharacterComponent::CreateTexture(s_path, &s_status);

    if (extraSkinTexture) {
        this->m_data.model->ReplaceTexture(8, extraSkinTexture);
        HandleClose(extraSkinTexture);
    }
}

void CCharacterComponent::ReplaceHairTexture(int32_t hairStyleID, const char* a3) {
    if (!ComponentValidateBase(
       CCharacterComponent::s_chrVarArray,
       this->m_data.raceID,
       this->m_data.sexID,
       VARIATION_HAIR,
       hairStyleID,
       this->m_data.hairColorID
    )) {
        return;
    }

    auto sectionsRec = this->GetSectionsRecord(VARIATION_HAIR, hairStyleID, this->m_data.hairColorID, nullptr);
    if (!*sectionsRec->m_textureName[0]) {
        return;
    }

    SStrCopy(s_pathEnd, sectionsRec->m_textureName[0]);

    auto hairTexture = CCharacterComponent::CreateTexture(s_path, &s_status);
    if (hairTexture) {
        this->m_data.model->ReplaceTexture(6, hairTexture);
        HandleClose(hairTexture);
    }
}

void CCharacterComponent::SetBeardStyle(int32_t facialHairStyleID, bool a3, const char* a4) {
    auto listIndex = this->m_data.raceID * 2 + this->m_data.sexID;
    if (facialHairStyleID < 0 || facialHairStyleID > CCharacterComponent::s_characterFacialHairStylesList[listIndex]) {
        return;
    }

    this->m_data.facialHairStyleID = facialHairStyleID;

    auto facialHairStyleRec = ComponentGetFacialHairStyleRecord(&this->m_data);

    if (facialHairStyleRec) {
        this->m_data.geosets[1] = 100 + facialHairStyleRec->m_geoset[0];
        this->m_data.geosets[2] = 200 + facialHairStyleRec->m_geoset[2];
        this->m_data.geosets[3] = 300 + facialHairStyleRec->m_geoset[1];
        this->m_data.geosets[16] = 1600 + facialHairStyleRec->m_geoset[3];
        this->m_data.geosets[17] = 1700 + facialHairStyleRec->m_geoset[4];

        this->m_flags |= 0x4;

        if (
            (facialHairStyleRec->m_geoset[0] || facialHairStyleRec->m_geoset[1] || facialHairStyleRec->m_geoset[2])
            && (!this->m_texture[TEXTURE_INDEX(VARIATION_HAIR, 1)] || !this->m_texture[TEXTURE_INDEX(VARIATION_HAIR, 2)])
        ) {
            this->ReplaceHairTexture(1, a4);
        }
    }

    bool isNPC = this->m_data.flags & 0x1;

    if (!isNPC) {
        if (a3) {
            this->LoadBaseVariation(VARIATION_FACIAL_HAIR, 0, this->m_data.facialHairStyleID, this->m_data.hairColorID, SECTION_HEAD_LOWER, a4);
            this->LoadBaseVariation(VARIATION_FACIAL_HAIR, 1, this->m_data.facialHairStyleID, this->m_data.hairColorID, SECTION_HEAD_UPPER, a4);
        }

        this->m_sectionDirty |= (1 << SECTION_HEAD_LOWER) | (1 << SECTION_HEAD_UPPER);

        // TODO component request logic

        this->m_flags &= ~0x8;
    }
}

void CCharacterComponent::SetFace(int32_t faceID, bool a3, const char* a4) {
    bool isNPC = this->m_data.flags & 0x1;

    if (isNPC) {
        return;
    }

    auto skinSectionsRec = this->GetSectionsRecord(VARIATION_SKIN, 0, this->m_data.skinColorID, nullptr);

    if (skinSectionsRec && skinSectionsRec->m_flags & 0x8) {
        return;
    }

    if (!ComponentValidateBase(CCharacterComponent::s_chrVarArray, this->m_data.raceID, this->m_data.sexID, VARIATION_FACE, faceID, this->m_data.skinColorID)) {
        return;
    }

    this->m_data.faceID = faceID;

    this->LoadBaseVariation(VARIATION_FACE, 0, this->m_data.faceID, this->m_data.skinColorID, SECTION_HEAD_LOWER, a4);
    this->LoadBaseVariation(VARIATION_FACE, 1, this->m_data.faceID, this->m_data.skinColorID, SECTION_HEAD_UPPER, a4);

    if (a3) {
        this->LoadBaseVariation(VARIATION_HAIR, 1, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_LOWER, a4);
        this->LoadBaseVariation(VARIATION_HAIR, 2, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_UPPER, a4);
    }

    this->m_flags |= 0x4;
    this->m_sectionDirty |= (1 << SECTION_HEAD_LOWER) | (1 << SECTION_HEAD_UPPER);

    // TODO

    this->m_flags &= ~0x8;
}

void CCharacterComponent::SetHairColor(int32_t hairColorID, bool a3, const char* a4) {
    if (!ComponentValidateBase(
        CCharacterComponent::s_chrVarArray,
        this->m_data.raceID,
        this->m_data.sexID,
        VARIATION_HAIR,
        this->m_data.hairStyleID,
        hairColorID
    )) {
        return;
    }

    this->m_data.hairColorID = hairColorID;

    auto raceRec = g_chrRacesDB.GetRecord(this->m_data.raceID);

    if (this->m_data.sexID == UNITSEX_MALE && this->m_data.hairStyleID == 0 && raceRec->m_flags & 0x8) {
        this->ReplaceHairTexture(1, a4);
    }

    this->ReplaceHairTexture(this->m_data.hairStyleID, a4);

    bool isNPC = this->m_data.flags & 0x1;

    if (!isNPC) {
        if (a3) {
            this->LoadBaseVariation(VARIATION_HAIR, 1, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_LOWER, a4);
            this->LoadBaseVariation(VARIATION_HAIR, 2, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_UPPER, a4);
        }

        this->LoadBaseVariation(VARIATION_FACIAL_HAIR, 0, this->m_data.facialHairStyleID, this->m_data.hairColorID, SECTION_HEAD_LOWER, a4);
        this->LoadBaseVariation(VARIATION_FACIAL_HAIR, 1, this->m_data.facialHairStyleID, this->m_data.hairColorID, SECTION_HEAD_UPPER, a4);
    }
}

void CCharacterComponent::SetHairStyle(int32_t hairStyleID, const char* a3) {
    if (!ComponentValidateBase(
        CCharacterComponent::s_chrVarArray,
        this->m_data.raceID,
        this->m_data.sexID,
        VARIATION_HAIR,
        hairStyleID,
        this->m_data.hairColorID
    )) {
        return;
    }

    this->m_data.hairStyleID = hairStyleID;

    auto hairGeoset = ComponentGetHairGeoset(&this->m_data);
    this->m_data.geosets[0] = hairGeoset;

    bool isNPC = this->m_data.flags & 0x1;

    if (!isNPC) {
        this->LoadBaseVariation(VARIATION_HAIR, 1, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_LOWER, a3);
        this->LoadBaseVariation(VARIATION_HAIR, 2, this->m_data.hairStyleID, this->m_data.hairColorID, SECTION_HEAD_UPPER, a3);
    }

    this->SetHairColor(this->m_data.hairColorID, false, a3);

    this->m_flags |= 0x4;
    this->m_sectionDirty |= (1 << SECTION_HEAD_LOWER) | (1 << SECTION_HEAD_UPPER);

    // TODO component request logic

    this->m_flags &= ~0x8;
}

void CCharacterComponent::SetSkinColor(int32_t skinColorID, bool a3, bool a4, const char* a5) {
    bool isNPC = this->m_data.flags & 0x1;

    this->m_data.skinColorID = skinColorID;

    if (isNPC) {
        return;
    }

    if (!ComponentValidateBase(CCharacterComponent::s_chrVarArray, this->m_data.raceID, this->m_data.sexID, VARIATION_SKIN, 0, skinColorID)) {
        return;
    }

    auto numColors = ComponentGetNumColors(
        CCharacterComponent::s_chrVarArray,
        this->m_data.raceID,
        this->m_data.sexID,
        VARIATION_SKIN,
        0
    );

    auto sectionsRec = this->GetSectionsRecord(VARIATION_SKIN, 0, skinColorID, nullptr);

    if (skinColorID < numColors && sectionsRec && !(sectionsRec->m_flags & 0x8)) {
        auto underwearRec = this->GetSectionsRecord(VARIATION_UNDERWEAR, 0, skinColorID, nullptr);

        auto t0 = TEXTURE_INDEX(VARIATION_UNDERWEAR, 0);
        auto t1 = TEXTURE_INDEX(VARIATION_UNDERWEAR, 1);

        if (this->m_texture[t0]) {
            TextureCacheDestroyTexture(this->m_texture[t0]);
            this->m_texture[t0] = nullptr;
        }

        if (this->m_texture[t1]) {
            TextureCacheDestroyTexture(this->m_texture[t1]);
            this->m_texture[t1] = nullptr;
        }

        if (*underwearRec->m_textureName[0]) {
            SStrCopy(s_pathEnd, underwearRec->m_textureName[0]);
            this->m_texture[t0] = TextureCacheCreateTexture(s_path);
            STORM_ASSERT(this->m_texture[t0]);
        }

        if (*underwearRec->m_textureName[1]) {
            SStrCopy(s_pathEnd, underwearRec->m_textureName[1]);
            this->m_texture[t1] = TextureCacheCreateTexture(s_path);
            STORM_ASSERT(this->m_texture[t1]);
        }
    }

    this->ReplaceExtraSkinTexture(a5);

    this->LoadBaseVariation(
        VARIATION_SKIN,
        0,
        0,
        this->m_data.skinColorID,
        SECTION_TORSO_UPPER,
        a5
    );

    this->SetFace(this->m_data.faceID, a3, a5);

    if (a4) {
        this->m_flags |= 0x1;

        // TODO component request logic

        this->m_flags &= ~0x8;
    }

    this->m_sectionDirty = -1;

    // TODO component request logic

    this->m_flags &= ~0x8;
}

void CCharacterComponent::UpdateItem(ITEM_SLOT itemSlot, COMPONENT_SECTIONS section, const ItemDisplayInfoRec* displayRec, bool update) {
    auto priority = s_itemPriority[itemSlot][section];

    if (section == SECTION_ARM_LOWER) {
        if (displayRec && displayRec->m_geosetGroup[0]) {
            if (itemSlot == ITEMSLOT_3) {
                priority = 5;
            } else if (itemSlot == ITEMSLOT_8) {
                priority = 6;
            }
        }
    }

    if (section == SECTION_LEG_LOWER) {
        if (itemSlot == ITEMSLOT_3 && displayRec && displayRec->m_geosetGroup[2]) {
            priority = 4;

            this->m_flags |= 0x4;
        }

        if (itemSlot == ITEMSLOT_5 && displayRec && displayRec->m_geosetGroup[2]) {
            // TODO

            this->m_flags |= 0x4;
        }

        if (itemSlot == ITEMSLOT_6 && displayRec && displayRec->m_geosetGroup[0]) {
            priority = 3;

            this->m_flags |= 0x4;
        }
    }

    if (section == SECTION_FOOT) {
        if (this->m_flags & 0x10) {
            update = false;
        }
    }

    if (update) {
        if (!this->UpdateItemDisplay(section, displayRec, priority)) {
            return;
        }
    } else {
        this->ClearItemDisplay(section, priority);

        if (section == SECTION_LEG_LOWER) {
            // TODO
        }
    }

    if (priority != -1) {
        this->m_sectionDirty |= (1 << section);

        // TODO component request logic

        this->m_flags &= ~0x8;
    }
}

void CCharacterComponent::UpdateItemAL(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_ARM_LOWER, displayRec, update);
}

void CCharacterComponent::UpdateItemAU(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_ARM_UPPER, displayRec, update);
}

void CCharacterComponent::UpdateItemFO(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_FOOT, displayRec, update);
}

void CCharacterComponent::UpdateItemHA(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_HAND, displayRec, update);
}

void CCharacterComponent::UpdateItemHL(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    // No item displays for head sections
}

void CCharacterComponent::UpdateItemHU(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    // No item displays for head sections
}

void CCharacterComponent::UpdateItemLL(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_LEG_LOWER, displayRec, update);
}

void CCharacterComponent::UpdateItemLU(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_LEG_UPPER, displayRec, update);
}

void CCharacterComponent::UpdateItemTL(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_TORSO_LOWER, displayRec, update);
}

void CCharacterComponent::UpdateItemTU(ITEM_SLOT itemSlot, const ItemDisplayInfoRec* displayRec, bool update) {
    this->UpdateItem(itemSlot, SECTION_TORSO_UPPER, displayRec, update);
}

int32_t CCharacterComponent::UpdateItemDisplay(COMPONENT_SECTIONS section, const ItemDisplayInfoRec* newDisplayRec, int32_t priority) {
    bool isNPC = this->m_data.flags & 0x1;

    if (isNPC) {
        return 0;
    }

    if (this->m_itemDisplays[section].displayID[priority]) {
        auto curDisplayID = this->m_itemDisplays[section].displayID[priority];
        auto curDisplayRec = g_itemDisplayInfoDB.GetRecord(curDisplayID);

        // New display is same as old display
        if (curDisplayRec && curDisplayRec->m_texture[section] == newDisplayRec->m_texture[section]) {
            return 0;
        }
    }

    this->m_itemDisplays[section].priorityDirty &= ~(1 << priority);

    if (this->m_itemDisplays[section].texture[priority]) {
        TextureCacheDestroyTexture(this->m_itemDisplays[section].texture[priority]);
        this->m_itemDisplays[section].texture[priority] = nullptr;
    }

    this->m_itemDisplays[section].displayID[priority] = newDisplayRec->m_ID;

    return 1;
}

void CCharacterComponent::ValidateComponentData(ComponentData* data, COMPONENT_CONTEXT context) {
    auto selection = GetSelectionFromContext(context, data->classID);

    // Skin color

    if (!ComponentValidateSkin(data->raceID, data->sexID, data->classID, data->skinColorID, context)) {
        auto numColors = ComponentGetNumSkinColors(data->raceID, data->sexID, data->classID, context);

        data->skinColorID = numColors > 0
            ? ComponentGetSkinColorByIndex(data->raceID, data->sexID, data->skinColorID % numColors, selection)
            : 0;
    }

    // Face

    if (!ComponentValidateFace(data->raceID, data->sexID, data->classID, data->skinColorID, data->faceID, context)) {
        auto numFaces = ComponentGetNumFaces(data->raceID, data->sexID, data->classID, data->skinColorID, context);

        data->faceID = numFaces > 0
            ? ComponentGetFaceByIndex(data->raceID, data->sexID, data->skinColorID, data->faceID % numFaces, selection)
            : 0;
    }

    // Hair style and color

    if (!ComponentValidateHair(data->raceID, data->sexID, data->classID, data->hairColorID, data->hairStyleID, context)) {
        auto numStyles = ComponentGetNumHairStyles(data->raceID, data->sexID, data->classID, data->hairColorID, context);

        if (numStyles > 0) {
            data->hairStyleID = ComponentGetHairStyleByIndex(data->raceID, data->sexID, data->hairColorID, data->hairStyleID % numStyles, selection);
        } else {
            // No style supports the current hair color, so pick a style that has valid colors
            // and a color that style supports
            auto numVariations = ComponentGetNumVariations(CCharacterComponent::s_chrVarArray, data->raceID, data->sexID, VARIATION_HAIR);

            TSFixedArray<int32_t> numColors;
            numColors.SetCount(numVariations);

            int32_t numValidStyles = 0;

            for (int32_t style = 0; style < numVariations; style++) {
                numColors[style] = ComponentGetNumHairColors(data->raceID, data->sexID, data->classID, style, context);

                if (numColors[style] > 0) {
                    numValidStyles++;
                }
            }

            if (numValidStyles > 0) {
                int32_t chosenStyle = -1;

                for (int32_t style = 0; style < numVariations; style++) {
                    if (numColors[style] > 0 && data->hairStyleID % numValidStyles == style) {
                        chosenStyle = style;
                        break;
                    }
                }

                if (chosenStyle < 0 || numColors[chosenStyle] <= 0) {
                    for (int32_t style = 0; style < numVariations; style++) {
                        if (numColors[style] > 0) {
                            chosenStyle = style;
                            break;
                        }
                    }
                }

                data->hairStyleID = chosenStyle;
                data->hairColorID = ComponentGetHairColorByIndex(data->raceID, data->sexID, chosenStyle, data->hairColorID % numColors[chosenStyle], selection);
            } else {
                data->hairStyleID = 0;
            }
        }
    }

    // Facial hair

    if (!ComponentValidateFacialHair(data->raceID, data->sexID, data->classID, data->hairColorID, data->facialHairStyleID, context)) {
        auto numStyles = ComponentGetNumFacialHairStyles(data->raceID, data->sexID, data->classID, data->hairColorID, context);

        data->facialHairStyleID = numStyles > 0
            ? ComponentGetFacialHairStyleByIndex(data->raceID, data->sexID, data->classID, data->hairColorID, data->facialHairStyleID, data->facialHairStyleID % numStyles, selection)
            : 0;
    }
}

int32_t CCharacterComponent::VariationsLoaded(int32_t a2) {
    TCTEXTUREINFO info;

    for (int32_t v = 0; v < NUM_COMPONENT_VARIATIONS; v++) {
        for (int32_t t = 0; t < 3; t++) {
            auto texture = this->m_texture[TEXTURE_INDEX(v, t)];

            if (texture && !TextureCacheGetInfo(texture, info, a2)) {
                return 0;
            }
        }
    }

    this->m_flags &= ~0x2;

    return 1;
}
