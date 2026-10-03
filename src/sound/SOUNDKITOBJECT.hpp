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
