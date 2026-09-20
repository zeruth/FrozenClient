#include "sound/CVarHandlers.hpp"
#include "console/CVar.hpp"
#include "sound/SESound.hpp"
#include "util/Unimplemented.hpp"
#include <storm/String.hpp>

bool EnableMicrophoneHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool EnableReverbHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool EnableVoiceChatHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool InboundChatVolumeHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool OutboundChatVolumeHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool OutputDriverHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool PushToTalkButtonHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool SelfMuteHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool VoiceActivationSensitivityHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool VoiceChatInputDriverHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool VoiceChatModeHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool VoiceChatOutputDriverHandler(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    WHOA_UNIMPLEMENTED(true);
}

bool AmbienceVolume_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto volume = SStrToFloat(value);

    SESound::SetChannelGroupVolume("AMBIENCE", volume);

    return true;
}

bool EnableAllSound_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // Enable

    if (SStrToInt(value)) {
        static auto enableSfxCVar = CVar::Lookup("Sound_EnableSFX");
        static auto enableMusicCVar = CVar::Lookup("Sound_EnableMusic");
        static auto enableAmbienceCVar = CVar::Lookup("Sound_EnableAmbience");

        SESound::MuteChannelGroup("SFX", enableSfxCVar->GetInt() == 0);
        SESound::MuteChannelGroup("MUSIC", enableMusicCVar->GetInt() == 0);
        SESound::MuteChannelGroup("AMBIENCE", enableAmbienceCVar->GetInt() == 0);

        return true;
    }

    // Disable

    SESound::MuteChannelGroup("SFX", true);
    SESound::MuteChannelGroup("MUSIC", true);
    SESound::MuteChannelGroup("AMBIENCE", true);

    return true;
}

// ref: FUN_004d1530
//
// Sound_EnableAmbience gates the AMBIENCE channel group, and until now it did nothing at all, so
// the group kept whatever mute state it was initialised with no matter what the CVar said. The
// login screen's ambient loop runs through that group.
bool EnableAmbience_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto allSoundVar = CVar::Lookup("Sound_EnableAllSound");

    if (allSoundVar) {
        // The reference also forces the mute when a third global is set (DAT_00bd0800), which is
        // not identified yet. EnableMusic_CVarCallback has the same gap.
        bool mute = !SStrToInt(value) || !allSoundVar->GetInt();

        SESound::MuteChannelGroup("AMBIENCE", mute);

        if (mute) {
            // TODO the reference stops whatever ambience is playing here (FUN_004c85f0), rather
            // than leaving a muted sound running.
        }
    }

    return true;
}

bool EnableMusic_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto allSoundVar = CVar::Lookup("Sound_EnableAllSound");

    if (allSoundVar) {
        bool mute = !SStrToInt(value) || !allSoundVar->GetInt();

        SESound::MuteChannelGroup("MUSIC", mute);

        if (mute) {
            // TODO
        }
    }

    return true;
}

bool EnableSFX_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto allSoundVar = CVar::Lookup("Sound_EnableAllSound");

    if (allSoundVar) {
        bool mute = !SStrToInt(value) || !allSoundVar->GetInt();

        SESound::MuteChannelGroup("SFX", mute);
    }

    return true;
}

bool MasterVolume_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto volume = SStrToFloat(value);

    SESound::SetMasterVolume(volume);

    return true;
}

// ref: FUN_004d0eb0
bool MusicVolume_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // The reference parses the value once per channel group.
    SESound::SetChannelGroupVolume("MUSIC", SStrToFloat(value));
    SESound::SetChannelGroupVolume("SCRIPTMUSIC", SStrToFloat(value));

    return true;
}

bool SFXVolume_CVarCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    auto volume = SStrToFloat(value);

    SESound::SetChannelGroupVolume("SFX", volume);

    return true;
}
