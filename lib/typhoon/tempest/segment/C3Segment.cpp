#include "tempest/segment/C3Segment.hpp"

// ref: FUN_0078f480
C3Segment::C3Segment(const C3Vector& start, const C3Vector& end) {
    this->start = { 0.0f, 0.0f, 0.0f };
    this->end = { 0.0f, 0.0f, 0.0f };

    this->start = start;
    this->end = end;
}

// ref: FUN_0078f4d0
void C3Segment::Lerp(C3Vector& out, float t) const {
    out.x = this->start.x + (this->end.x - this->start.x) * t;
    out.y = (this->end.y - this->start.y) * t + this->start.y;
    out.z = t * (this->end.z - this->start.z) + this->start.z;
}
