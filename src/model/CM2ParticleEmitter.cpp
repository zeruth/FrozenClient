#include "model/CM2ParticleEmitter.hpp"
#include <cmath>

// The z source arrives from the driver as the animated value of M2Particle::zsourceTrack, and
// anything under a thousandth is stored as a hard zero rather than kept.
//
// That is not tidiness. The z source is used as a divisor downstream, so a denormal left in here
// turns into an enormous velocity rather than the "effectively off" the author meant. The constant
// is the 0.001 at 0x009e1134, read out of the binary -- the same one the reference uses to convert
// scene milliseconds to seconds, which is why it turns up in two unrelated places.
//
// ref: FUN_00978da0
void CM2ParticleEmitter::SetZSource(float zSource) {
    this->m_zSource = zSource;

    if (fabsf(zSource) < 0.001f) {
        this->m_zSource = 0.0f;
    }
}
