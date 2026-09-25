#ifndef UI_GAME_CLASS_TRAINER_FRAME_HPP
#define UI_GAME_CLASS_TRAINER_FRAME_HPP

#include <cstdint>

// One service a trainer offers. Only the fields read by ported code are named.
struct TrainerService {
    int32_t id;                 // +0x00
    uint32_t unk04;
    int32_t moneyCost;          // +0x08
    int32_t talentCost;         // +0x0C
    int32_t professionCost;     // +0x10
    uint8_t levelReq;           // +0x14
    uint8_t unk15[0x0F];
    int32_t abilityReq[3];      // +0x24
};

void ClassTrainerFrameRegisterScriptFunctions();

uint32_t TrainerGetSelectionIndex();

TrainerService* TrainerGetService(uint32_t index);

#endif
