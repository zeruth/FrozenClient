#ifndef SOUND_SI2_HPP
#define SOUND_SI2_HPP

#include "sound/SI2Script.hpp"
#include "ui/Types.hpp"
#include <storm/Array.hpp>
#include <storm/Hash.hpp>
#include "util/guid/Types.hpp"
#include <tempest/Vector.hpp>
#include <cstdint>

class SOUNDKITDEF;
struct SOUNDKITLOOKUP;
class SOUNDKITOBJECT;
class SoundKitProperties;

class SI2 {
    public:
        // Static variables
        static FrameScript_Method s_ScriptFunctions[NUM_SCRIPT_FUNCTIONS_SI2];
        static TSGrowableArray<SOUNDKITDEF*> s_SoundKitDefs;
        static TSHashTable<SOUNDKITLOOKUP, HASHKEY_CONSTSTRI> s_SoundKitLookupTable;
        static char s_ChannelGroupNames[32][18];

        // Static functions
        static int32_t CreditsMusicUpdate(const void* data, void* param);
        static int32_t GetInputDriverName(int32_t index, char* buffer, size_t bufferSize, int32_t chatSystem);
        static int32_t GetNumInputDrivers(int32_t chatSystem);
        static int32_t GetNumOutputDrivers(int32_t chatSystem);
        static int32_t GetOutputDriverName(int32_t index, char* buffer, size_t bufferSize, int32_t chatSystem);
        static SOUNDKITDEF* GetSoundKitDef(int32_t id);
        static int32_t GetSoundKitID(const char* name);
        static int32_t GlueMusicUpdate(const void* data, void* param);
        static int32_t Init(int32_t a1);
        static void InitSoundKitDefs();
        static void InitSoundKitGroups();
        static bool IsPlaying(SOUNDKITOBJECT* object);
        static int32_t PlaySoundKit(const char *name, const C3Vector* position, SOUNDKITOBJECT* object, SoundKitProperties* properties);
        static int32_t PlaySoundKit(int32_t id, const C3Vector* position, SOUNDKITOBJECT* object, SoundKitProperties* properties, int32_t a5,  void* a6, int32_t a7, int32_t a8);

        // ref: FUN_004c5bf0
        // The kit's SoundKitDef has flag 0x200: it plays at the listener rather than the source.
        static uint32_t SoundKitFollowsListener(int32_t id);

        // ref: FUN_004c5d60
        // The position SESound asks for when a sound follows an object.
        static bool GetObjectPosition(WOWGUID guid, C3Vector* position);
        static void PlayUISound(int32_t id);
        static void RegisterCVars();
        static void RegisterUserCVars();
        static void RegisterScriptFunctions();
        static void StartCreditsMusic(const char* name);
        static void StartGlueAmbience(const char* name, float fadeInTime);
        static void StartGlueMusic(const char* name);
        static int32_t StopCreditsMusic();
        static int32_t StopGlueAmbience(float fadeOutTime);
        static int32_t StopGlueMusic(float fadeOutTime);
        static int32_t StopOrFadeOut(SOUNDKITOBJECT* object, int32_t stop, float fadeOutTime, int32_t a4);
};

#endif
