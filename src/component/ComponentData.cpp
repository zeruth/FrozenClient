#include "component/ComponentData.hpp"
#include "component/Types.hpp"

// ref: FUN_004dfda0
// The reference clears only the two low flag bits and the first byte of the texture path, leaving
// the rest of both as it found them; the member initializers zero them first here, so those
// stores land on zeroes.
ComponentData::ComponentData() {
    this->flags &= ~0x3;

    this->raceID = 0;
    this->sexID = 0;
    this->classID = 0;
    this->hairColorID = 0;
    this->skinColorID = 0;
    this->faceID = 0;
    this->facialHairStyleID = 0;
    this->hairStyleID = 0;
    this->npcBakedTexturePath[0] = '\0';
    this->model = nullptr;

    for (int32_t i = 0; i < NUM_GEOSET; i++) {
        this->geosets[i] = i * 100 + 1;
    }

    this->geosets[GEOSET_EARS] = 702;
}

void ComponentData::SetPreferences(CharacterPreferences* preferences) {
    if (!preferences) {
        return;
    }

    this->raceID = preferences->raceID;
    this->sexID = preferences->sexID;
    this->classID = preferences->classID;
    this->hairColorID = preferences->hairColorID;
    this->skinColorID = preferences->skinColorID;
    this->faceID = preferences->faceID;
    this->facialHairStyleID = preferences->facialHairStyleID;
    this->hairStyleID = preferences->hairStyleID;
}
