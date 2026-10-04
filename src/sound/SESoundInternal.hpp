#ifndef SOUND_SE_SOUND_INTERNAL_HPP
#define SOUND_SE_SOUND_INTERNAL_HPP

#include "util/guid/Types.hpp"
#include <storm/List.hpp>
#include <tempest/Vector.hpp>
#include <fmod.hpp>
#include <cstdint>

class SESound;
class SEUserData;
class SFile;

class SoundCacheNode : public TSLinkedNode<SoundCacheNode> {
    public:
        // Member variables
        FMOD::Sound* m_fmodSound = nullptr;
        int32_t m_loaded = 0;
        char m_filename[128];
        uint32_t m_hashval = 0;
        // TODO dword94
        // TODO dword98

        // Member functions
        SoundCacheNode();
};

class SESoundInternal : public TSLinkedNode<SESoundInternal> {
    public:
        // Member variables
        // TODO
        FMOD::System* m_fmodSystem;
        FMOD::Channel* m_fmodChannel = nullptr;
        SESound* m_sound = nullptr;
        SEUserData* m_userData = nullptr;
        float m_volume = 1.0f;
        float m_fadeVolume = 0.0f;
        float m_fadeInTime = 0.0f;
        float m_fadeOutTime = 0.0f;
        // +0x70: the channel's frequency when it started, taken on the first change; +0x74 the
        // scale on it (SetFrequencyScale).
        float m_baseFrequency = 0.0f;
        float m_frequencyScale = 1.0f;
        uint8_t m_fadeIn = 0;
        uint8_t m_fadeOut = 0;
        // TODO
        int32_t m_useCache = 0;
        int32_t m_type = 0;
        // TODO
        C3Vector m_position;        // ref +0x50
        // TODO
        int32_t m_channelGroup = 0;
        FMOD_MODE m_fmodMode = FMOD_DEFAULT;
        uint8_t m_playing = 0;
        uint8_t m_stopped = 0;
        // +0xc8: the object this sound follows, set by SESound::SetObjectGUID, and its link in
        // SESound::s_ObjectSounds (the list at 0x00b1d6bc).
        WOWGUID m_objectGUID = 0;
        TSLink<SESoundInternal> m_objectLink;
        // TODO
        int32_t m_nonblockingReady = 0;
        // TODO
        uint32_t m_uniqueID;

        // Member functions
        SESoundInternal();
        float GetVolume();
        void Play();
        void UpdateVolume();
};

class SEDiskSound : public SESoundInternal {
    public:
        // Member variables
        // TODO
        TSLink<SEDiskSound> m_readyLink;
        SFile* m_file = nullptr;
        // TODO
        FMOD::Sound* m_fmodSound = nullptr;
        // TODO
        SoundCacheNode* m_cacheNode = nullptr;

        // Member functions
        SEDiskSound();
        void Abort(FMOD_RESULT result);
        void CompleteNonBlockingLoad();
};

class SEMemorySound : public SESoundInternal {
    public:
        // TODO
};

class SEStreamedSound : public SESoundInternal {
    public:
        // TODO
};

#endif
