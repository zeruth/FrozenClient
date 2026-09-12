// A silent stand in for FMOD Core on platforms where its library is not vendored (Android for
// now). Every call succeeds without doing anything, so the sound engine initializes and the client
// runs without audio. Only the entry points the client uses are provided.

#include <fmod.hpp>

namespace {
    // The FMOD classes cannot be instantiated, and none of the stubbed methods touch their object,
    // so opaque handles are handed out as pointers to plain storage
    char s_system[16];
    char s_soundGroup[16];
}

extern "C" {

FMOD_RESULT F_API FMOD_Memory_Initialize(void* poolmem, int poollen, FMOD_MEMORY_ALLOC_CALLBACK useralloc, FMOD_MEMORY_REALLOC_CALLBACK userrealloc, FMOD_MEMORY_FREE_CALLBACK userfree, FMOD_MEMORY_TYPE memtypeflags) {
    return FMOD_OK;
}

FMOD_RESULT F_API FMOD_System_Create(FMOD_SYSTEM** system, unsigned int headerversion) {
    *system = reinterpret_cast<FMOD_SYSTEM*>(s_system);
    return FMOD_OK;
}

FMOD_RESULT F_API FMOD_Sound_GetUserData(FMOD_SOUND* sound, void** userdata) {
    *userdata = nullptr;
    return FMOD_OK;
}

}

namespace FMOD {

FMOD_RESULT F_API System::setOutput(FMOD_OUTPUTTYPE output) {
    return FMOD_OK;
}

FMOD_RESULT F_API System::getNumDrivers(int* numdrivers) {
    *numdrivers = 0;
    return FMOD_OK;
}

FMOD_RESULT F_API System::getDriverInfo(int id, char* name, int namelen, FMOD_GUID* guid, int* systemrate, FMOD_SPEAKERMODE* speakermode, int* speakermodechannels) {
    return FMOD_ERR_INVALID_PARAM;
}

FMOD_RESULT F_API System::setCallback(FMOD_SYSTEM_CALLBACK callback, FMOD_SYSTEM_CALLBACK_TYPE callbackmask) {
    return FMOD_OK;
}

FMOD_RESULT F_API System::init(int maxchannels, FMOD_INITFLAGS flags, void* extradriverdata) {
    return FMOD_OK;
}

FMOD_RESULT F_API System::update() {
    return FMOD_OK;
}

FMOD_RESULT F_API System::set3DSettings(float dopplerscale, float distancefactor, float rolloffscale) {
    return FMOD_OK;
}

FMOD_RESULT F_API System::getVersion(unsigned int* version) {
    *version = FMOD_VERSION;
    return FMOD_OK;
}

FMOD_RESULT F_API System::createSound(const char* name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO* exinfo, Sound** sound) {
    *sound = nullptr;
    return FMOD_ERR_UNSUPPORTED;
}

FMOD_RESULT F_API System::createStream(const char* name_or_data, FMOD_MODE mode, FMOD_CREATESOUNDEXINFO* exinfo, Sound** sound) {
    *sound = nullptr;
    return FMOD_ERR_UNSUPPORTED;
}

FMOD_RESULT F_API System::createSoundGroup(const char* name, SoundGroup** soundgroup) {
    *soundgroup = reinterpret_cast<SoundGroup*>(s_soundGroup);
    return FMOD_OK;
}

FMOD_RESULT F_API System::playSound(Sound* sound, ChannelGroup* channelgroup, bool paused, Channel** channel) {
    *channel = nullptr;
    return FMOD_ERR_UNSUPPORTED;
}

FMOD_RESULT F_API SoundGroup::setMaxAudible(int maxaudible) {
    return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::stop() {
    return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::setPaused(bool paused) {
    return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::setVolume(float volume) {
    return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::isPlaying(bool* isplaying) {
    *isplaying = false;
    return FMOD_OK;
}

FMOD_RESULT F_API ChannelControl::setUserData(void* userdata) {
    return FMOD_OK;
}

}
