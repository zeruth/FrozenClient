#ifndef COMPONENT_UTIL_HPP
#define COMPONENT_UTIL_HPP

#include "component/Types.hpp"
#include <cstdint>

class CharacterFacialHairStylesRec;
class CharSectionsRec;

struct ComponentData;
struct st_variation;

struct st_color {
    CharSectionsRec* rec;
};

struct st_race {
    // The use of "section" here refers to the groups of variations represented in
    // COMPONENT_VARIATIONS. It does NOT refer to COMPONENT_SECTIONS.
    struct {
        int32_t variationCount = 0;
        st_variation* variationArray = nullptr;
    } sections[NUM_COMPONENT_VARIATIONS];
};

struct st_variation {
    int32_t colorCount = 0;
    st_color* colorArray = nullptr;
};

int32_t BuildComponentArray(uint32_t varArrayLength, st_race** varArrayPtr);

bool ComponentCheckSectionFlags(int32_t flags, COMPONENT_SELECTION selection);

int32_t ComponentGetFaceByIndex(int32_t raceId, int32_t sexId, int32_t skinColorId, int32_t index, COMPONENT_SELECTION selection);

int32_t ComponentGetFacialHairStyleByIndex(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t facialHairStyleId, int32_t index, COMPONENT_SELECTION selection);

CharacterFacialHairStylesRec* ComponentGetFacialHairStyleRecord(ComponentData* data);

int32_t ComponentGetHairColorByIndex(int32_t raceId, int32_t sexId, int32_t hairStyleId, int32_t index, COMPONENT_SELECTION selection);

int32_t ComponentGetHairGeoset(ComponentData* data);

int32_t ComponentGetHairStyleByIndex(int32_t raceId, int32_t sexId, int32_t hairColorId, int32_t index, COMPONENT_SELECTION selection);

int32_t ComponentGetNumColors(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex);

int32_t ComponentGetNumFaces(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, COMPONENT_CONTEXT context);

int32_t ComponentGetNumFacialHairStyles(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, COMPONENT_CONTEXT context);

int32_t ComponentGetNumHairColors(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairStyleId, COMPONENT_CONTEXT context);

int32_t ComponentGetNumHairStyles(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, COMPONENT_CONTEXT context);

int32_t ComponentGetNumSkinColors(int32_t raceId, int32_t sexId, int32_t classId, COMPONENT_CONTEXT context);

int32_t ComponentGetNumVariations(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex);

int32_t ComponentGetSkinColorByIndex(int32_t raceId, int32_t sexId, int32_t index, COMPONENT_SELECTION selection);

CharSectionsRec* ComponentGetSectionsRecord(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex, int32_t colorIndex, bool* found);

int32_t ComponentValidateBase(st_race* varArray, int32_t raceId, int32_t sexId, COMPONENT_VARIATIONS sectionIndex, int32_t variationIndex, int32_t colorIndex);

bool ComponentValidateFace(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, int32_t faceId, COMPONENT_CONTEXT context);

bool ComponentValidateFacialHair(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t facialHairStyleId, COMPONENT_CONTEXT context);

bool ComponentValidateHair(int32_t raceId, int32_t sexId, int32_t classId, int32_t hairColorId, int32_t hairStyleId, COMPONENT_CONTEXT context);

bool ComponentValidateSkin(int32_t raceId, int32_t sexId, int32_t classId, int32_t skinColorId, COMPONENT_CONTEXT context);

int32_t CountFacialFeatures(uint32_t varArrayLength, uint32_t** featuresListPtr);

COMPONENT_CONTEXT GetContextFromSelection(COMPONENT_SELECTION selection);

COMPONENT_SELECTION GetSelectionFromContext(COMPONENT_CONTEXT context, int32_t classID);

#endif
