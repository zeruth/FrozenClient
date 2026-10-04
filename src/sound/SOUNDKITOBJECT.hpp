#ifndef SOUND_SOUND_KIT_OBJECT_HPP
#define SOUND_SOUND_KIT_OBJECT_HPP

#include "sound/SESound.hpp"

class SOUNDKITOBJECT {
    public:
        // Member variables
        // TODO
        SESound m_sound;
        // TODO

        // ref: FUN_004c5c40
        bool IsLooping() { return this->m_sound.IsLooping(); }

        // ref: FUN_004cfb70
        void Detach() { this->m_sound.Detach(); }

        // ref: FUN_004c5b90
        // TODO(SoundInterface2): the reference also moves the kit's tracked entry (+0xc, through
        // FUN_004cb2d0), which frozen's SOUNDKITOBJECT does not carry.
        void SetPosition(const C3Vector& position) { this->m_sound.SetPosition(position); }

        // ref: FUN_004cfb50
        void SetFrequencyScale(float scale) { this->m_sound.SetFrequencyScale(scale); }

        // ref: FUN_004c5c80
        // A 3D sound follows the object from now on; anything else is left where it is.
        int32_t SetObjectGUID(WOWGUID guid) {
            if (!this->m_sound.Is3D()) {
                return 0xC;
            }

            this->m_sound.SetObjectGUID(guid);

            return 0;
        }
};

#endif
