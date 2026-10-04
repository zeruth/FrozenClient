#ifndef SOUND_SOUND_KIT_PROPERTIES_HPP
#define SOUND_SOUND_KIT_PROPERTIES_HPP

#include <cstdint>

// The property block handed to SI2::PlaySoundKit. ResetToDefaults below is the reference's
// FUN_004c5990 and is the only thing that fills it, so every default in this class comes from
// there -- and the field list is what that function writes, recovered 2026-09-27.
//
// NOT OFFSET-EXACT, deliberately. The reference block runs to at least 0xe8 bytes and leaves
// 0x64 through 0xe0 untouched, so reproducing the byte layout would mean declaring thirty-odd
// dwords nothing reads. Everything here is reached by NAME, the same way the class already worked,
// and the offset in each comment is the reference's rather than this class's.
class SoundKitProperties {
    public:
        // Member variables
        uint32_t m_type;            // +0x00
        // +0x04 and +0x08. THE IN/OUT SPLIT IS NOT VERIFIED -- it is inherited from the names this
        // class already carried, in declaration order. What is established is the pair of values
        // the reference defaults them to, and that SI2::PlaySoundKit reads NEITHER of them, which
        // is what puts them on the fade path rather than the play path. -1.0f as a fade time is a
        // sentinel this codebase already uses by name: SI2's GLUE_AMBIENCE_DEFAULT_FADE is -1.0f,
        // and SESound treats a negative fade-out as no fade at all.
        float m_fadeInTime;         // +0x04
        float m_fadeOutTime;        // +0x08
        int32_t int0c;              // +0x0c, and PlaySoundKit passes it to the volume helper
        uint32_t uint10;            // +0x10
        uint32_t uint14;            // +0x14
        uint32_t uint18;            // +0x18, the other argument to that same volume helper
        uint32_t uint1c;            // +0x1c
        int32_t int20;              // +0x20
        uint32_t uint24;            // +0x24
        uint32_t uint28;            // +0x28
        float float2c;              // +0x2c
        int32_t int30;              // +0x30
        float float34;              // +0x34
        uint8_t byte38;             // +0x38, a BYTE in the reference and not a dword
        uint32_t uint3C;            // +0x3c
        uint32_t uint40;            // +0x40
        uint32_t uint44;            // +0x44
        uint32_t uint48;            // +0x48
        // +0x4c and +0x50. Both default to 360, read out of 0x009f22b0, and a pair of angles that
        // both default to a full circle is the 3D cone: 360 degrees inside and out is what an
        // omnidirectional source looks like.
        float m_coneInsideAngle;
        float m_coneOutsideAngle;
        float float54;              // +0x54
        uint32_t uint58;            // +0x58
        int32_t m_streaming;        // +0x5c
        uint32_t uint60;            // +0x60, 1 when m_voiceName picks the voice
        // +0x64: the voice a Death Knight's sounds use ("Death Knight Human Male", FUN_004cda20).
        // PlaySoundKit does not read it yet.
        char m_voiceName[0x80] = {};
        uint32_t uinte4;            // +0xe4, and PlaySoundKit gates a whole block on it

        // Member functions

        // ref: FUN_004c5990
        void ResetToDefaults();
};

#endif
