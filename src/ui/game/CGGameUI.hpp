#ifndef UI_GAME_C_G_GAME_UI_HPP
#define UI_GAME_C_G_GAME_UI_HPP

#include "util/guid/Types.hpp"

class CScriptObject;
class CSimpleTop;

class CGGameUI {
    public:
        // Static variables
        static CScriptObject* s_gameTooltip;
        static CSimpleTop* s_simpleTop;

        // Static functions
        static void EnterWorld();
        static WOWGUID& GetCurrentObjectTrack();
        static uint32_t GetCursorMoney();
        static WOWGUID& GetLockedTarget();
        static void DisplayError(uint32_t errorCode, ...);
        static char s_lastError[3000];  // the last formatted error text (reference DAT_00bcfb90)
        static void Initialize();
        static void InitializeGame();
        static bool IsLoggingIn();
        static bool IsInWorld();
        static int32_t IsRaidMember(const WOWGUID& guid);
        static int32_t IsRaidMemberOrPet(const WOWGUID& guid);
        static void RegisterFrameFactories();
        static void RegisterGameCVars();

    private:
        static WOWGUID s_currentObjectTrack;
        static uint32_t s_cursorMoney;
        static bool s_inWorld;
        static WOWGUID s_lockedTarget;
        static bool s_loggingIn;
};

#endif
