#include "component/Util.hpp"
#include "component/CCharacterComponent.hpp"
#include "component/ComponentData.hpp"
#include "component/Types.hpp"
#include "db/Db.hpp"
#include "object/Types.hpp"
#include <storm/Memory.hpp>

int32_t BuildComponentArray(uint32_t varArrayLength, st_race** varArrayPtr) {
    if (!varArrayLength) {
        return 0;
    }

    auto varArray = new (STORM_ALLOC(sizeof(st_race) * varArrayLength)) st_race[varArrayLength];

    int32_t prevRaceID = g_charSectionsDB.GetNumRecords() > 0 ? g_charSectionsDB.GetRecordByIndex(0)->m_raceID : 4;
    int32_t prevSexID = g_charSectionsDB.GetNumRecords() > 0 ? g_charSectionsDB.GetRecordByIndex(0)->m_sexID : 8;
    int32_t prevBaseSection = g_charSectionsDB.GetNumRecords() > 0 ? g_charSectionsDB.GetRecordByIndex(0)->m_baseSection : 12;
    int32_t prevVariationIndex = 0;

    // Build sections

    int32_t variationIndex = 0;

    for (int32_t i = 0; i < g_charSectionsDB.GetNumRecords(); i++) {
        auto sectionsRec = g_charSectionsDB.GetRecordByIndex(i);

        if (sectionsRec->m_baseSection >= NUM_COMPONENT_VARIATIONS) {
            continue;
        }

        auto sectionChange = prevRaceID != sectionsRec->m_raceID
            || prevSexID != sectionsRec->m_sexID
            || prevBaseSection != sectionsRec->m_baseSection;

        auto lastRecord = i == g_charSectionsDB.GetNumRecords() - 1;

        if (sectionChange || lastRecord) {
            auto& section = varArray[(prevRaceID * UNITSEX_NUM_SEXES + prevSexID)].sections[prevBaseSection];

            section.variationCount = variationIndex + 1;

            if (section.variationCount > 0) {
                section.variationArray = new (STORM_ALLOC(sizeof(st_variation) * section.variationCount)) st_variation[section.variationCount];
            }

            variationIndex = 0;
        }

        if (variationIndex <= sectionsRec->m_variationIndex) {
            variationIndex = sectionsRec->m_variationIndex;
        }

        prevRaceID = sectionsRec->m_raceID;
        prevSexID = sectionsRec->m_sexID;
        prevBaseSection = sectionsRec->m_baseSection;
    }

    // Build variations

    prevRaceID = 1;
    prevSexID = 0;
    prevBaseSection = 0;
    prevVariationIndex = 0;

    int32_t colorIndex = 0;

    for (int32_t i = 0; i < g_charSectionsDB.GetNumRecords(); i++) {
        auto sectionsRec = g_charSectionsDB.GetRecordByIndex(i);

        if (sectionsRec->m_baseSection >= NUM_COMPONENT_VARIATIONS) {
            continue;
        }

        auto sectionChange = prevRaceID != sectionsRec->m_raceID
            || prevSexID != sectionsRec->m_sexID
            || prevBaseSection != sectionsRec->m_baseSection
            || prevVariationIndex != sectionsRec->m_variationIndex;

        auto lastRecord = i == g_charSectionsDB.GetNumRecords() - 1;

        if (sectionChange || lastRecord) {
            auto& section = varArray[(prevRaceID * UNITSEX_NUM_SEXES + prevSexID)].sections[prevBaseSection];

            if (section.variationCount > 0) {
                auto& variation = section.variationArray[prevVariationIndex];

                variation.colorCount = colorIndex + 1;

                if (variation.colorCount > 0) {
                    variation.colorArray = new (STORM_ALLOC(sizeof(st_color) * variation.colorCount)) st_color[variation.colorCount];
                }
            }

            colorIndex = 0;
        }

        if (colorIndex <= sectionsRec->m_colorIndex) {
            colorIndex = sectionsRec->m_colorIndex;
        }

        prevRaceID = sectionsRec->m_raceID;
        prevSexID = sectionsRec->m_sexID;
        prevBaseSection = sectionsRec->m_baseSection;
        prevVariationIndex = sectionsRec->m_variationIndex;
    }

    // Build colors

    for (int32_t i = 0; i < g_charSectionsDB.GetNumRecords(); i++) {
        auto sectionsRec = g_charSectionsDB.GetRecordByIndex(i);

        if (sectionsRec->m_baseSection >= NUM_COMPONENT_VARIATIONS) {
            continue;
        }

        auto& section = varArray[(sectionsRec->m_raceID * UNITSEX_NUM_SEXES + sectionsRec->m_sexID)].sections[sectionsRec->m_baseSection];

        if (section.variationCount > 0 && sectionsRec->m_variationIndex < section.variationCount) {
            auto& variation = section.variationArray[sectionsRec->m_variationIndex];

            if (variation.colorCount > 0 && sectionsRec->m_colorIndex < variation.colorCount) {
                auto& color = variation.colorArray[sectionsRec->m_colorIndex];
                color.rec = sectionsRec;
            }
        }
    }

    *varArrayPtr = varArray;

    return 1;
}

bool ComponentCheckSectionFlags(int32_t flags, COMPONENT_SELECTION selection) {
    switch (selection) {
        case SELECTION_0:
            return (flags & 0x1) && !(flags & 0xC);

        case SELECTION_1:
            return (flags & 0x1) && (flags & 0x14) && !(flags & 0x8);

        case SELECTION_2:
            return (flags & 0x3) && !(flags & 0xC);

        case SELECTION_3:
            return (flags & 0x3) && (flags & 0x14) && !(flags & 0x8);

        case SELECTION_4:
            return true;

        case SELECTION_5:
            return !(flags & 0xC);

        case SELECTION_6:
            return (flags & 0x14) && !(flags & 0x8);

        default:
            return false;
    }
}

int32_t ComponentGetFaceByIndex(int32_t raceId, int32_t sexId, int32_t skinColorId, int32_t index, COMPONENT_SELECTION selection) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto numFaces = ComponentGetNumVariations(varArray, raceId, sexId, VARIATION_FACE);

    int32_t validIndex = 0;

    for (int32_t i = 0; i < numFaces; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACE, i, skinColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            if (index == validIndex) {
                return rec->m_variationIndex;
            }

            validIndex++;
        }
    }

    return -1;
}

int32_t ComponentGetFacialHairStyleByIndex(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t facialHairStyleId, int32_t index, COMPONENT_SELECTION selection) {
    auto varArray = CCharacterComponent::s_chrVarArray;

    bool found;
    ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACIAL_HAIR, facialHairStyleId, hairColorId, &found);

    // Races whose facial features are pure geometry (e.g. tauren horns) have no facial hair
    // sections, so their styles are simply numbered
    if (!found) {
        if (index < static_cast<int32_t>(CCharacterComponent::s_characterFacialHairStylesList[raceId * UNITSEX_NUM_SEXES + sexId])) {
            return index;
        }

        return -1;
    }

    auto context = GetContextFromSelection(selection);
    auto numStyles = ComponentGetNumFacialHairStyles(raceId, sexId, classId, hairColorId, context);

    int32_t validIndex = 0;

    for (int32_t i = 0; i < numStyles; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACIAL_HAIR, i, hairColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            if (index == validIndex) {
                return rec->m_variationIndex;
            }

            validIndex++;
        }
    }

    return -1;
}

int32_t ComponentGetHairColorByIndex(int32_t raceId, int32_t sexId, int32_t hairStyleId, int32_t index, COMPONENT_SELECTION selection) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto numColors = ComponentGetNumColors(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId);

    int32_t validIndex = 0;

    for (int32_t i = 0; i < numColors; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId, i, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            if (index == validIndex) {
                return rec->m_colorIndex;
            }

            validIndex++;
        }
    }

    return -1;
}

CharacterFacialHairStylesRec* ComponentGetFacialHairStyleRecord(ComponentData* data) {
    for (int32_t i = 0; i < g_characterFacialHairStylesDB.GetNumRecords(); i++) {
        auto facialHairStyleRec = g_characterFacialHairStylesDB.GetRecordByIndex(i);

        if (facialHairStyleRec->m_raceID == data->raceID && facialHairStyleRec->m_sexID == data->sexID && facialHairStyleRec->m_variationID == data->facialHairStyleID) {
            return facialHairStyleRec;
        }
    }

    return nullptr;
}

int32_t ComponentGetHairGeoset(ComponentData* data) {
    for (int32_t i = 0; i < g_charHairGeosetsDB.GetNumRecords(); i++) {
        auto hairGeosetRec = g_charHairGeosetsDB.GetRecordByIndex(i);

        if (hairGeosetRec->m_raceID == data->raceID && hairGeosetRec->m_sexID == data->sexID && hairGeosetRec->m_variationID == data->hairStyleID) {
            auto geosetId = hairGeosetRec->m_geosetID;
            return geosetId > 0 ? geosetId : 1;
        }
    }

    return 1;
}

int32_t ComponentGetHairStyleByIndex(int32_t raceId, int32_t sexId, int32_t hairColorId, int32_t index, COMPONENT_SELECTION selection) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto numStyles = ComponentGetNumVariations(varArray, raceId, sexId, VARIATION_HAIR);

    int32_t validIndex = 0;

    for (int32_t i = 0; i < numStyles; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_HAIR, i, hairColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            if (index == validIndex) {
                return rec->m_variationIndex;
            }

            validIndex++;
        }
    }

    return -1;
}

int32_t ComponentGetNumColors(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex) {
    auto& section = varArray[(raceId * UNITSEX_NUM_SEXES + sexId)].sections[sectionIndex];

    if (variationIndex >= section.variationCount || section.variationCount == 0) {
        return 0;
    }

    if (!section.variationArray || !section.variationArray[variationIndex].colorArray) {
        return 0;
    }

    return section.variationArray[variationIndex].colorCount;
}

int32_t ComponentGetNumFaces(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto selection = GetSelectionFromContext(context, classId);
    auto numFaces = ComponentGetNumVariations(varArray, raceId, sexId, VARIATION_FACE);

    int32_t count = 0;

    for (int32_t i = 0; i < numFaces; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACE, i, skinColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            count++;
        }
    }

    return count;
}

int32_t ComponentGetNumFacialHairStyles(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto selection = GetSelectionFromContext(context, classId);
    auto numStyles = ComponentGetNumVariations(varArray, raceId, sexId, VARIATION_FACIAL_HAIR);

    if (numStyles == 0) {
        return CCharacterComponent::s_characterFacialHairStylesList[raceId * UNITSEX_NUM_SEXES + sexId];
    }

    int32_t count = 0;

    for (int32_t i = 0; i < numStyles; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACIAL_HAIR, i, hairColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            count++;
        }
    }

    return count;
}

int32_t ComponentGetNumHairColors(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairStyleId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto selection = GetSelectionFromContext(context, classId);
    auto numColors = ComponentGetNumColors(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId);

    int32_t count = 0;

    for (int32_t i = 0; i < numColors; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId, i, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            count++;
        }
    }

    return count;
}

int32_t ComponentGetNumHairStyles(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto selection = GetSelectionFromContext(context, classId);
    auto numStyles = ComponentGetNumVariations(varArray, raceId, sexId, VARIATION_HAIR);

    int32_t count = 0;

    for (int32_t i = 0; i < numStyles; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_HAIR, i, hairColorId, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            count++;
        }
    }

    return count;
}

int32_t ComponentGetNumSkinColors(int32_t raceId, int32_t sexId, int32_t classId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto selection = GetSelectionFromContext(context, classId);
    auto numColors = ComponentGetNumColors(varArray, raceId, sexId, VARIATION_SKIN, 0);

    int32_t count = 0;

    for (int32_t i = 0; i < numColors; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_SKIN, 0, i, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            count++;
        }
    }

    return count;
}

int32_t ComponentGetNumVariations(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex) {
    auto& section = varArray[(raceId * UNITSEX_NUM_SEXES + sexId)].sections[sectionIndex];

    if (!section.variationArray) {
        return 0;
    }

    return section.variationCount;
}

int32_t ComponentGetSkinColorByIndex(int32_t raceId, int32_t sexId, int32_t index, COMPONENT_SELECTION selection) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto numColors = ComponentGetNumColors(varArray, raceId, sexId, VARIATION_SKIN, 0);

    int32_t validIndex = 0;

    for (int32_t i = 0; i < numColors; i++) {
        auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_SKIN, 0, i, nullptr);

        if (rec && ComponentCheckSectionFlags(rec->m_flags, selection)) {
            if (index == validIndex) {
                return rec->m_colorIndex;
            }

            validIndex++;
        }
    }

    return -1;
}

CharSectionsRec* ComponentGetSectionsRecord(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex, int32_t colorIndex, bool* found) {
    if (!ComponentValidateBase(varArray, raceId, sexId, sectionIndex, variationIndex, colorIndex)) {
        if (found) {
            *found = false;
        }

        return nullptr;
    }

    auto& section = varArray[(raceId * UNITSEX_NUM_SEXES + sexId)].sections[sectionIndex];

    if (found) {
        *found = true;
    }

    return section.variationArray[variationIndex].colorArray[colorIndex].rec;
}

int32_t ComponentValidateBase(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex, int32_t colorIndex) {
    if (sectionIndex >= NUM_COMPONENT_VARIATIONS || variationIndex < 0) {
        return 0;
    }

    auto& section = varArray[(raceId * UNITSEX_NUM_SEXES + sexId)].sections[sectionIndex];

    if (variationIndex >= section.variationCount || section.variationCount == 0) {
        return 0;
    }

    if (colorIndex < 0) {
        return 0;
    }

    auto& variation = section.variationArray[variationIndex];

    if (colorIndex >= variation.colorCount || variation.colorCount == 0) {
        return 0;
    }

    return 1;
}

bool ComponentValidateFace(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, int32_t faceId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;

    if (!ComponentValidateBase(varArray, raceId, sexId, VARIATION_FACE, faceId, skinColorId)) {
        return false;
    }

    auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_FACE, faceId, skinColorId, nullptr);

    if (!rec) {
        return false;
    }

    return ComponentCheckSectionFlags(rec->m_flags, GetSelectionFromContext(context, classId));
}

bool ComponentValidateFacialHair(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t facialHairStyleId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;
    auto listIndex = raceId * UNITSEX_NUM_SEXES + sexId;

    if (facialHairStyleId < 0 || facialHairStyleId > static_cast<int32_t>(CCharacterComponent::s_characterFacialHairStylesList[listIndex])) {
        return false;
    }

    // Facial features without sections (e.g. tauren horns) only need to be within the style count
    auto& section = varArray[listIndex].sections[VARIATION_FACIAL_HAIR];

    if (facialHairStyleId >= section.variationCount || section.variationCount == 0 || hairColorId < 0) {
        return true;
    }

    auto& variation = section.variationArray[facialHairStyleId];

    if (hairColorId >= variation.colorCount || variation.colorCount == 0) {
        return true;
    }

    auto rec = variation.colorArray[hairColorId].rec;

    if (!rec) {
        return false;
    }

    return ComponentCheckSectionFlags(rec->m_flags, GetSelectionFromContext(context, classId));
}

bool ComponentValidateHair(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t hairStyleId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;

    if (!ComponentValidateBase(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId, hairColorId)) {
        return false;
    }

    auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_HAIR, hairStyleId, hairColorId, nullptr);

    if (!rec) {
        return false;
    }

    return ComponentCheckSectionFlags(rec->m_flags, GetSelectionFromContext(context, classId));
}

bool ComponentValidateSkin(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, COMPONENT_CONTEXT context) {
    auto varArray = CCharacterComponent::s_chrVarArray;

    if (!ComponentValidateBase(varArray, raceId, sexId, VARIATION_SKIN, 0, skinColorId)) {
        return false;
    }

    auto rec = ComponentGetSectionsRecord(varArray, raceId, sexId, VARIATION_SKIN, 0, skinColorId, nullptr);

    if (!rec) {
        return false;
    }

    return ComponentCheckSectionFlags(rec->m_flags, GetSelectionFromContext(context, classId));
}

int32_t CountFacialFeatures(uint32_t varArrayLength, uint32_t** featuresListPtr) {
    auto featuresList = static_cast<uint32_t*>(STORM_ALLOC_ZERO(sizeof(uint32_t) * varArrayLength));

    if (g_characterFacialHairStylesDB.GetNumRecords() <= 0) {
        *featuresListPtr = featuresList;

        return 1;
    }

    for (int32_t i = 0; i < g_characterFacialHairStylesDB.GetNumRecords(); i++) {
        auto facialHairStyleRec = g_characterFacialHairStylesDB.GetRecordByIndex(i);
        auto listIndex =  facialHairStyleRec->m_raceID * 2 + facialHairStyleRec->m_sexID;

        featuresList[listIndex]++;
    }

    *featuresListPtr = featuresList;

    return 1;
}

COMPONENT_CONTEXT GetContextFromSelection(COMPONENT_SELECTION selection) {
    switch (selection) {
        case SELECTION_2:
        case SELECTION_3:
            return CONTEXT_1;

        case SELECTION_4:
            return CONTEXT_2;

        case SELECTION_5:
        case SELECTION_6:
            return CONTEXT_3;

        default:
            return CONTEXT_CHAR_CREATE;
    }
}

COMPONENT_SELECTION GetSelectionFromContext(COMPONENT_CONTEXT context, int32_t classID) {
    switch (context) {
        case CONTEXT_1:
            return classID == 6 ? SELECTION_3 : SELECTION_2;

        case CONTEXT_2:
            return SELECTION_4;

        case CONTEXT_3:
            return classID == 6 ? SELECTION_6 : SELECTION_5;

        default:
            return classID == 6 ? SELECTION_1 : SELECTION_0;
    }
}
