#ifndef UI_INPUT_CONTROL_HPP
#define UI_INPUT_CONTROL_HPP

#include <cstdint>

class CVar;

// The reference's uiutil/InputControl.h singleton (0x70 bytes). Field names follow their use
// where it has been seen; the rest carry their offsets until the code that reads them is ported.
class CInputControl {
    public:
        // Member variables
        uint32_t m_lastTimeMs = 0;      // +0x00 OsGetAsyncTimeMs() at construction
        uint32_t m_unk04 = 0;
        uint32_t m_unk08 = 0;
        uint32_t m_unk0C = 0;
        uint32_t m_unk10 = 0;
        uint32_t m_unk14 = 0;
        uint32_t m_unk18 = 0;
        uint8_t m_unk1C[0x28] = {};    // +0x1c a TSHashTable, constructed by FUN_005fcd70 (12 slots)
        uint32_t m_unk44 = 0;
        uint32_t m_unk48 = 0;
        uint32_t m_unk4C = 0;
        uint32_t m_unk50 = 0;
        uint32_t m_unk54 = 0;
        int32_t m_unk58 = 3;
        uint32_t m_unk5C = 0;
        int32_t m_unk60 = 3;
        uint32_t m_unk64 = 0;
        uint32_t m_unk68 = 0;
        void* m_wowMouse = nullptr;     // +0x6c the SteelSeries mouse driver object (FUN_008c2f50)

        // Member functions
        CInputControl();
        void SetWowMouseEnabled(bool enabled);
};

extern CInputControl* s_inputControl;

void InputControlInitialize();

#endif
