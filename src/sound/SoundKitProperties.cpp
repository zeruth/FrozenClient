#include "sound/SoundKitProperties.hpp"
#include "util/SFile.hpp"

// ref: FUN_004c5990
// Every default for a sound kit play request. 54 callers in the reference, which is why it was
// worth finishing: this ran with seven of the twenty-six fields set and the rest left as TODO, so
// every caller was handing PlaySoundKit a block that was mostly uninitialised stack.
//
// Two of the seven it did set were also wrong -- both fade times were 0.0f where the reference uses
// -1.0f and 1.0f -- and a zero fade is not a harmless stand-in for the -1.0f that means no fade.
//
// The streaming flag is the only value that is computed rather than constant, and it is forced to
// 0 or 1 rather than carrying SFile::IsStreamingMode's own return value.
//
// Written in the reference's own order, which is not offset order -- the compiler interleaved the
// stores and there is nothing to gain by tidying it into something it is not.
void SoundKitProperties::ResetToDefaults() {
    this->m_fadeInTime = -1.0f;
    this->m_fadeOutTime = 1.0f;
    this->int0c = -1;
    this->int20 = -1;
    this->uint10 = 0;
    this->uint14 = 0;
    this->m_type = 0;
    this->uint18 = 2;
    this->float2c = -1.0f;
    this->uint1c = 0;
    this->uint24 = 0;
    this->float34 = 0.5f;
    this->uint28 = 0;
    this->int30 = 1;
    this->m_coneInsideAngle = 360.0f;
    this->uint3C = 1;
    this->m_coneOutsideAngle = 360.0f;
    this->byte38 = 0;
    this->uint40 = 0;
    this->uint44 = 0;
    this->uint48 = 0;
    this->float54 = 1.0f;
    this->uint58 = 0;

    int32_t streaming = SFile::IsStreamingMode();

    this->uint60 = 0;
    this->uinte4 = 0;
    this->m_streaming = streaming != 0;
}
