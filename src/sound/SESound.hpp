#ifndef SOUND_SE_SOUND_HPP
#define SOUND_SE_SOUND_HPP

#include "sound/SEChannelGroup.hpp"
#include "sound/SESoundInternal.hpp"
#include "sound/SEUserData.hpp"
#include <cstdint>
#include <fmod.hpp>

// FMOD 2.03 dropped the callback calling convention macro
#if !defined(F_CALLBACK)
    #define F_CALLBACK F_CALL
#endif
#include <storm/Hash.hpp>
#include <storm/Thread.hpp>

struct SOUND_INTERNAL_LOOKUP : TSHashObject<SOUND_INTERNAL_LOOKUP, HASHKEY_NONE> {
    SESoundInternal* m_internal;
};

#define SE_DRIVER_NAME_LENGTH 256

struct SEDRIVERNAME {
    char name[SE_DRIVER_NAME_LENGTH];
};

class SESound {
    public:
        // Public static variables
        static STORM_LIST(SoundCacheNode) s_CacheList;
        static TSGrowableArray<SEChannelGroup> s_ChannelGroups;
        static SCritSect s_CritSect3;
        static int32_t s_Initialized;
        static SCritSect s_InternalCritSect;
        static STORM_LIST(SESoundInternal) s_InternalList;
        static TSHashTable<SOUND_INTERNAL_LOOKUP, HASHKEY_NONE> s_InternalLookupTable;
        static HASHKEY_NONE s_InternalLookupKey;
        static SCritSect s_LoadingCritSect;
        static FMOD::System* s_pGameSystem;

        // Driver name tables, rebuilt whenever FMOD reports a device list change. Index 0 is the
        // localized "System Default" entry, so a table holds one more entry than FMOD reports.
        static TSGrowableArray<SEDRIVERNAME> s_GameOutputDrivers;
        static TSGrowableArray<SEDRIVERNAME> s_ChatOutputDrivers;
        static TSGrowableArray<SEDRIVERNAME> s_ChatInputDrivers;
        static void (*s_DeviceListChangedCallback)();
        static STORM_EXPLICIT_LIST(SEDiskSound, m_readyLink) s_ReadyDiskSounds;
        static uint32_t s_UniqueID;
        // Scales every sound but a type-3 one by a factor chosen from its user data. Nothing
        // installs it yet.
        static float (*s_VolumeCallback)(SEUserData* userData);

        // Public static functions
        static FMOD::SoundGroup* CreateSoundGroup(const char* name, int32_t maxAudible);
        static SEChannelGroup* GetChannelGroup(const char* name, bool create, bool createInMaster);
        static float GetChannelGroupVolume(const char* name);
        static void EnumerateDrivers();
        static int32_t GetInputDriverName(int32_t index, char* buffer, size_t bufferSize);
        static int32_t GetNumInputDrivers();
        static int32_t GetNumOutputDrivers(int32_t chatSystem);
        static int32_t GetOutputDriverName(int32_t index, char* buffer, size_t bufferSize, int32_t chatSystem);
        static int32_t Heartbeat(const void* data, void* param);
        static void Init(int32_t maxChannels, int32_t (*a2), int32_t enableReverb, int32_t enableSoftwareHRTF, int32_t* numChannels, int32_t* outputDriverIndex, const char* outputDriverName, void (*deviceListChangedCallback)(), int32_t a9);
        static void SetDeviceListChangedCallback(void (*callback)());
        static int32_t IsInitialized();
        static void Log_Write(int32_t line, const char* file, FMOD_RESULT result, const char* fmt, ...);
        static void MuteChannelGroup(const char* name, bool mute);
        static void SetChannelGroupVolume(const char* name, float volume);
        static void SetMasterVolume(float volume);
        static void LockInternal();
        static void UnlockInternal();

        // Public member functions
        void BeginFadeIn();
        void BeginFadeOut();
        void CompleteLoad();
        void Detach();
        void DetachWithLoopFade();
        SEUserData* GetUserData();
        bool Is3D();
        bool IsLooping();
        bool IsPlaying();
        int32_t Load(const char* filename, int32_t a3, FMOD::SoundGroup* soundGroup1, FMOD::SoundGroup* soundGroup2, bool a6, bool a7, uint32_t a8, int32_t a9, uint32_t a10);
        void Play();
        void SetChannelGroup(const char* name, bool inMaster);
        void SetFadeInTime(float fadeInTime);
        void SetFadeOutTime(float fadeOutTime);
        void SetPosition(const C3Vector& position);
        void SetUserData(SEUserData* userData);
        void SetVolume(float volume);
        void StopOrFadeOut(int32_t stop, float fadeOutTime);

    private:
        // Private static functions
        static void CreateMasterChannelGroup();
        static int32_t LoadDiskSound(FMOD::System* fmodSystem, const char* filename, FMOD_MODE fmodMode, SESound* sound, FMOD::SoundGroup* fmodSoundGroup1, FMOD::SoundGroup* fmodSoundGroup2, bool a7, int32_t a8, uint32_t a9, int32_t a10, uint32_t decodeBufferSize, int32_t a12, float a13, float a14, float a15, float* a16);
        static void ProcessReadyDiskSounds();
        static void ProcessVolumeUpdates();

        // Private member variables
        SESoundInternal* m_internal = nullptr;
};

#endif
