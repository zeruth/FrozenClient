#include "object/client/ShipPath.hpp"
#include "db/Db.hpp"
#include <cmath>
#include <new>

namespace {

// ref: FUN_007f7ad0
// The first TaxiPathNode row of a path (the table is sorted by path), or -1.
int32_t FindFirstPathNode(int32_t pathID) {
    int32_t lo = 0;
    int32_t hi = g_taxiPathNodeDB.GetNumRecords();

    while (lo < hi) {
        int32_t mid = (lo + hi) / 2;

        if (g_taxiPathNodeDB.GetRecordByIndex(mid)->m_pathID < pathID) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    if (lo < g_taxiPathNodeDB.GetNumRecords() && g_taxiPathNodeDB.GetRecordByIndex(lo)->m_pathID == pathID) {
        return lo;
    }

    return -1;
}

// The time an accelerating start (or a decelerating end) takes over `distance`: up to the speed
// and on at it, or never reaching it.
float TimeFromRest(float distance, float speed, float accel) {
    float inv = 1.0f / accel;
    float rampTime = speed * inv;
    float rampDistance = speed * 0.5f * rampTime;

    if (distance <= rampDistance) {
        float d = inv * distance;
        return std::sqrt(d + d);
    }

    return (distance - rampDistance) / speed + rampTime;
}

int32_t RoundMs(float seconds) {
    return static_cast<int32_t>(std::nearbyint(seconds * 1000.0f));
}

} // namespace

ShipPath::~ShipPath() {
    this->SetLegCount(0);
}

// The legs array grown with fresh legs or cut back, the cut ones destroyed (FUN_007f8fa0; the
// array's own storage is Storm's).
void ShipPath::SetLegCount(uint32_t count) {
    uint32_t current = this->m_legs.Count();

    if (count < current) {
        for (uint32_t i = count; i < current; i++) {
            this->m_legs[i].~Leg();
        }

        this->m_legs.m_count = count;
        return;
    }

    this->m_legs.SetCount(count);
}

// ref: FUN_007f77d0
// Whether `time` falls in (from, to] on the looping path; just `to` when the two are the same.
bool ShipPath::InRange(uint32_t from, uint32_t to, uint32_t time) {
    if (to == from) {
        return to == time;
    }

    if (from < to) {
        return from < time && time <= to;
    }

    return time <= to || from < time;
}

// ref: FUN_007f7810
uint32_t ShipPath::GetPathTime(uint32_t time) {
    if (this->m_length == 0) {
        return 0;
    }

    uint32_t pathTime = (this->m_timeOffset + time) % this->m_length;

    if (this->m_stopped) {
        pathTime = this->m_stopTime;
    }

    return pathTime;
}

// ref: FUN_007f7840
// The path time at `time`; a transport due to stop holds there once it has passed the stop in the
// last `elapsed` ms.
uint32_t ShipPath::GetTime(uint32_t time, uint32_t elapsed, int32_t raw) {
    uint32_t length = this->m_length;

    if (length == 0) {
        return 0;
    }

    if (raw) {
        return time % length;
    }

    uint32_t pathTime = (this->m_timeOffset + time) % length;

    if (this->m_stopping) {
        if (this->m_stopped) {
            return this->m_stopTime;
        }

        uint32_t stop = this->m_stopTime;

        if (ShipPath::InRange(stop, (elapsed + stop) % length, pathTime)) {
            this->m_stopped = 1;
            pathTime = stop;
        }
    }

    return pathTime;
}

// ref: FUN_007f78c0
void ShipPath::SetTimeOffsetTo(uint32_t time, uint32_t pathTime) {
    time = time % this->m_length;

    if (time < pathTime) {
        this->m_timeOffset = pathTime - time;
        return;
    }

    this->m_timeOffset = (this->m_length - time) + pathTime;
}

// ref: FUN_007f7a60
int32_t ShipPath::TimeBetweenStops(float distance) {
    float inv = 1.0f / this->m_accel;
    float rampTime = inv * this->m_speed;
    float rampDistance = this->m_speed * 0.5f * rampTime;
    float seconds;

    if (distance * 0.5f <= rampDistance) {
        seconds = std::sqrt(distance * inv);
        seconds = seconds + seconds;
    } else {
        seconds = (distance - rampDistance * 2.0f) / this->m_speed + rampTime * 2.0f;
    }

    return RoundMs(seconds);
}

// ref: FUN_007f7660
// From the leg's start into its first stop: at speed (sequence 0xa3), then braking (0xa4).
float ShipPath::TravelIntoStop(float elapsed, float duration, uint32_t* sequence, float* speed) {
    float rampTime = this->m_speed / this->m_accel;

    if (duration < rampTime) {
        rampTime = duration;
    }

    float cruise = duration - rampTime;

    if (elapsed <= cruise) {
        *sequence = 0xa3;
        *speed = this->m_speed;
        return elapsed * this->m_speed;
    }

    float v0 = this->m_accel * rampTime;
    float dt = elapsed - cruise;
    *speed = v0 - this->m_accel * dt;
    *sequence = 0xa4;

    return cruise * this->m_speed + ((v0 + v0) - this->m_accel * dt) * 0.5f * dt;
}

// ref: FUN_007f76f0
// From a stop to the next: speeding up (0xa2), at speed (0xa3), braking (0xa4).
float ShipPath::TravelBetweenStops(float elapsed, float duration, uint32_t* sequence, float* speed) {
    float rampTime = this->m_speed / this->m_accel;
    float ramp = duration * 0.5f;

    if (rampTime <= duration * 0.5f) {
        ramp = rampTime;
    }

    if (duration - ramp < elapsed) {
        float v0 = this->m_accel * ramp;
        float dt = elapsed - (duration - ramp);
        *speed = v0 - this->m_accel * dt;
        *sequence = 0xa4;

        return (duration - 2.0f * ramp) * this->m_speed + (v0 * 2.0f - this->m_accel * dt) * 0.5f * dt + v0 * 0.5f * ramp;
    }

    if (ramp < elapsed) {
        *sequence = 0xa3;
        *speed = this->m_speed;

        return (elapsed - rampTime) * this->m_speed + this->m_speed * 0.5f * rampTime;
    }

    float v = this->m_accel * elapsed;
    *speed = v;
    *sequence = 0xa2;

    return elapsed * v * 0.5f;
}

// ref: FUN_007f7d30
// The leg and segment a path time falls in: the first segment whose stop (with its wait) it has
// not passed.
void ShipPath::FindSegment(uint32_t time, uint32_t* leg, uint32_t* segment) {
    for (uint32_t i = 0; i < this->m_legs.Count(); i++) {
        auto& current = this->m_legs[i];

        if (time < current.m_endTime && current.m_segments.Count() != 1) {
            for (uint32_t j = 0; j < current.m_segments.Count() - 1; j++) {
                auto& s = current.m_segments[j];

                if (time < s.m_time + s.m_delay) {
                    *leg = i;
                    *segment = j;
                    return;
                }
            }
        }
    }

    *leg = 0;
    *segment = 0;
}

// ref: FUN_007f7b30
// The physics row's rocking: a bob on the swell, a roll that also banks into the turn, and a
// pitch, each a sine over its own period, all damped at low speed.
void ShipPath::Bob(uint32_t time, float speed, float turn, C3Vector* position, float* roll, float* pitch) {
    auto physics = this->m_physics;
    float damp = 1.0f;

    if (speed < physics->m_speedDampThresh) {
        float s = std::sqrt(1.0f / physics->m_speedDampThresh * speed);
        damp = (1.0f - physics->m_speedDamp) * s * s + physics->m_speedDamp;
    }

    const float twoPi = 6.2831855f;

    if (2.384185791015625e-07f <= std::fabs(physics->m_waveTimeScale)) {
        uint32_t period = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(6283.1855f / physics->m_waveTimeScale)));
        float phase = static_cast<float>(time % period);
        position->z = std::sin(phase * twoPi / static_cast<float>(period)) * physics->m_waveAmp * damp + position->z;
    }

    if (physics->m_maxBankTurnSpeed < turn) {
        turn = physics->m_maxBankTurnSpeed;
    } else if (turn < -physics->m_maxBankTurnSpeed) {
        turn = -physics->m_maxBankTurnSpeed;
    }

    if (std::fabs(physics->m_rollTimeScale) < 2.384185791015625e-07f) {
        *roll = 0.0f;
    } else {
        uint32_t period = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(6283.1855f / physics->m_rollTimeScale)));
        float phase = static_cast<float>(time % period);
        *roll = (std::sin(phase * twoPi / static_cast<float>(period) + 0.5f) * physics->m_rollAmp
                 + (physics->m_maxBank / physics->m_maxBankTurnSpeed) * turn) * damp;
    }

    if (std::fabs(physics->m_pitchTimeScale) < 2.384185791015625e-07f) {
        *pitch = 0.0f;
        return;
    }

    uint32_t period = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(6283.1855f / physics->m_pitchTimeScale)));
    float phase = static_cast<float>(time % period);
    *pitch = std::sin(phase * twoPi / static_cast<float>(period) + 0.2f) * physics->m_pitchAmp * damp;
}

// ref: FUN_007f7dd0
// How hard it is turning: the heading change over the four points 0.45 s apart behind it on the
// spline, at a speed eased in between 15% and 30% of the damping threshold; then the rocking.
void ShipPath::Bank(float speed, float facing, float distance, uint32_t leg, float turn, uint32_t time,
                    C3Vector* position, float* roll, float* pitch) {
    if (0.0f < speed) {
        float lo = this->m_physics->m_speedDampThresh * 0.15f;
        float hi = this->m_physics->m_speedDampThresh * 0.3f;
        float eased = 0.0f;

        if (lo <= speed) {
            eased = speed;

            if (speed < hi) {
                float f = (speed - lo) / (hi - lo);
                eased = f * f * speed;
            }
        }

        speed = eased;

        auto& spline = this->m_legs[leg].m_spline;

        for (int32_t i = 1; i < 5; i++) {
            float back = distance - static_cast<float>(i) * speed * 0.45f;

            if (back < 0.0f) {
                back = 0.0f;
            }

            float t = back / spline.m_length;
            float clamped = 0.0f;

            if (0.0f <= t) {
                clamped = t < 1.0f ? t : 1.0f;
            }

            C3Vector tangent = { 0.0f, 0.0f, 0.0f };
            spline.Tangent(clamped, tangent, 1);

            float len = tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z;

            if (2.384185791015625e-07f < len) {
                float inv = 1.0f / std::sqrt(len);
                tangent.x *= inv;
                tangent.y *= inv;
            }

            float heading = std::atan2(-tangent.y, -tangent.x);
            float delta = facing - heading;

            if (3.1415927f < delta) {
                delta -= 6.2831855f;
            } else if (delta < -3.1415927f) {
                delta += 6.2831855f;
            }

            turn = delta * 2.2222223f + turn;
            facing = heading;
        }

        turn *= 0.2f;
    }

    this->Bob(time, speed, turn, position, roll, pitch);
}

// ref: FUN_007f7fc0
void ShipPath::SetLength(uint32_t length) {
    this->m_length = length;

    if (this->m_legs.Count()) {
        this->m_legs[this->m_legs.Count() - 1].m_endTime = length;
    }
}

// ref: FUN_007f8000
void ShipPath::SetStopped(uint32_t time, bool stop) {
    uint32_t length = this->m_length;

    if (length == 0 || (this->m_stopping != 0) == stop) {
        return;
    }

    bool stopped = this->m_stopped != 0;
    uint32_t at = stopped ? this->m_stopTime - 1 + length : this->m_timeOffset + time;

    uint32_t leg = 0;
    uint32_t segment = 0;
    this->FindSegment(at % length, &leg, &segment);

    auto& s = this->m_legs[leg].m_segments[segment];
    uint32_t stopEnd = s.m_delay + s.m_time;

    if (!stop) {
        if (stopped) {
            this->SetTimeOffsetTo(time, stopEnd);
        }
    } else {
        this->m_stopTime = stopEnd % length;
    }

    this->m_stopping = stop ? 1 : 0;
    this->m_stopped = 0;
    this->m_flag3E = 0;
}

// ref: FUN_007f80a0
void ShipPath::StopNow(uint32_t time) {
    uint32_t length = this->m_length;

    if (length == 0 || this->m_stopped) {
        return;
    }

    uint32_t leg = 0;
    uint32_t segment = 0;
    this->FindSegment((this->m_timeOffset + length - 1 + time) % length, &leg, &segment);

    auto& s = this->m_legs[leg].m_segments[segment];

    this->m_stopping = 1;
    this->m_stopped = 1;
    this->m_flag3E = 0;
    this->m_stopTime = (s.m_delay + s.m_time) % length;
}

// ref: FUN_007f8120
void ShipPath::SetProgress(uint32_t time, float progress) {
    uint32_t length = this->m_length;

    if (length == 0) {
        return;
    }

    uint32_t target = static_cast<uint32_t>(static_cast<int64_t>(std::nearbyint(static_cast<float>(length) * progress)));
    time = time % length;

    if (time < target) {
        this->m_timeOffset = target - time;
        return;
    }

    this->m_timeOffset = (length - time) + target;
}

// ref: FUN_007f82b0
// Where the transport is at `time`: the leg and the segment, how far along it the speed profile
// has brought it (and which sequence that plays), the spline's point and heading there, and, with
// a physics row, its roll and pitch.
void ShipPath::Evaluate(uint32_t time, uint32_t elapsed, int32_t* mapID, uint32_t* sequence, C3Vector* position,
                        float* facing, uint32_t* leg, float* roll, float* pitch, int32_t raw) {
    uint32_t t = this->GetTime(time, elapsed, raw);
    float speed = 0.0f;

    *sequence = 0;
    *position = { 0.0f, 0.0f, 0.0f };
    *facing = 0.0f;

    if (roll && pitch) {
        *pitch = 0.0f;
        *roll = 0.0f;
    }

    *leg = 0;

    if (this->m_legs.Count() == 0) {
        return;
    }

    while (this->m_legs[*leg].m_endTime <= t) {
        *leg = *leg + 1;

        if (this->m_legs.Count() <= *leg) {
            return;
        }
    }

    auto& current = this->m_legs[*leg];
    auto& segments = current.m_segments;
    uint32_t prevEnd = current.m_startTime;
    float travelled = 0.0f;
    float distance = 0.0f;
    uint32_t segment = 0;
    bool waiting = false;

    if (segments.Count() != 1) {
        for (; segment < segments.Count() - 1; segment++) {
            auto& s = segments[segment];

            if (t < s.m_time) {
                break;
            }

            if (t - s.m_time < s.m_delay) {
                distance = s.m_distance;
                *sequence = 0;
                waiting = true;
                break;
            }

            travelled = s.m_distance;
            prevEnd = s.m_time + s.m_delay;
        }
    }

    if (!waiting) {
        float into = static_cast<float>(t - prevEnd) * 0.001f;
        float duration = static_cast<float>(segments[segment].m_time - prevEnd) * 0.001f;
        bool notLast = segment != segments.Count() - 1;

        if (segment == 0) {
            if (notLast) {
                distance = this->TravelIntoStop(into, duration, sequence, &speed);
            } else {
                *sequence = 0xa3;
                speed = this->m_speed;
                distance = into * this->m_speed;
            }
        } else if (notLast) {
            distance = this->TravelBetweenStops(into, duration, sequence, &speed);
        } else {
            // From the last stop to the leg's end: speeding up, then at speed.
            float rampTime = this->m_speed / this->m_accel;

            if (duration < rampTime) {
                rampTime = duration;
            }

            if (into <= rampTime) {
                *sequence = 0xa2;
                speed = this->m_accel * into;
                distance = this->m_accel * into * 0.5f * into;
            } else {
                *sequence = 0xa3;
                distance = (into - rampTime) * this->m_speed + this->m_speed * 0.5f * rampTime;
                speed = this->m_speed;
            }
        }

        distance += travelled;
    }

    float u = distance / current.m_spline.m_length;
    float clamped = 0.0f;

    if (0.0f <= u) {
        clamped = u < 1.0f ? u : 1.0f;
    }

    C3SplineFrame frame;
    current.m_spline.Frame(clamped, frame, 1);

    float fx = -frame.forward.x;
    float fy = -frame.forward.y;
    float len = fx * fx + fy * fy;

    if (2.384185791015625e-07f < len) {
        float inv = 1.0f / std::sqrt(len);
        fx = inv * fx;
        fy = inv * fy;
    }

    *facing = std::atan2(fy, fx);
    *position = frame.position;

    if (this->m_physics && roll && pitch) {
        this->Bank(speed, *facing, distance, *leg, 0.0f, t, position, roll, pitch);
    }

    *mapID = current.m_mapID;
}

// ref: FUN_007f8660
// One leg from the nodes collected for it: its spline, a segment per stop timed by the speed
// profile (a start or end ramp at the path's ends, the full profile between stops), the
// arrival and departure events at their times, and the leg placed after the ones before it.
void ShipPath::BuildLeg(const TSGrowableArray<C3Vector>& points, const TSGrowableArray<Stop>& stops,
                        const TSGrowableArray<NodeEvent>& events, uint32_t legIndex) {
    auto& leg = this->m_legs[legIndex];

    leg.m_spline.SetPoints(points.Ptr(), points.Count());
    leg.m_segments.SetCount(stops.Count() + 1);
    leg.m_duration = 0;

    float distance = 0.0f;
    uint32_t eventIndex = this->m_events.Count();
    uint32_t waited = 0;
    uint32_t nextEvent = 0;
    uint32_t stop = 0;

    this->m_events.SetCount(eventIndex + events.Count() * 2);

    auto addEvents = [&](const NodeEvent& node, uint32_t at, uint32_t departureDelay) {
        if (node.m_arrival) {
            this->m_events[eventIndex].m_time = this->m_length + at;
            this->m_events[eventIndex].m_eventID = node.m_arrival;
            eventIndex++;
        }

        if (node.m_departure) {
            this->m_events[eventIndex].m_time = this->m_length + at + departureDelay;
            this->m_events[eventIndex].m_eventID = node.m_departure;
            eventIndex++;
        }
    };

    for (; stop < stops.Count(); stop++) {
        if (points.Count() - 1 <= stops[stop].m_point) {
            break;
        }

        for (; nextEvent < events.Count(); nextEvent++) {
            auto& node = events[nextEvent];

            if (stops[stop].m_point < node.m_point) {
                break;
            }

            float d = leg.m_spline.LengthToPoint(node.m_point) - distance;
            float seconds;

            if (stop == 0) {
                seconds = TimeFromRest(d, this->m_speed, this->m_accel);
            } else {
                float inv = 1.0f / this->m_accel;
                float rampTime = this->m_speed * inv;
                float rampDistance = this->m_speed * 0.5f * rampTime;

                if (d * 0.5f <= rampDistance) {
                    seconds = std::sqrt(inv * d) + std::sqrt(inv * d);
                } else {
                    seconds = (d - rampDistance * 2.0f) / this->m_speed + rampTime * 2.0f;
                }
            }

            uint32_t departureDelay = node.m_point == stops[stop].m_point ? stops[stop].m_delay : 0;
            addEvents(node, leg.m_duration + RoundMs(seconds) + waited, departureDelay);
        }

        float at = leg.m_spline.LengthToPoint(stops[stop].m_point);
        float d = at - distance;
        int32_t ms = stop == 0 ? RoundMs(TimeFromRest(d, this->m_speed, this->m_accel)) : this->TimeBetweenStops(d);

        leg.m_duration += ms;
        leg.m_segments[stop].m_time = leg.m_duration + waited;
        leg.m_segments[stop].m_distance = at;
        leg.m_segments[stop].m_delay = stops[stop].m_delay;
        waited += stops[stop].m_delay;
        distance = at;
    }

    for (; nextEvent < events.Count(); nextEvent++) {
        auto& node = events[nextEvent];
        float d = leg.m_spline.LengthToPoint(node.m_point) - distance;
        float seconds = stop == 0 ? d / this->m_speed : TimeFromRest(d, this->m_speed, this->m_accel);

        addEvents(node, leg.m_duration + RoundMs(seconds) + waited, 0);
    }

    this->m_events.SetCount(eventIndex);

    float d = leg.m_spline.m_length - distance;
    float seconds = stop == 0 ? d / this->m_speed : TimeFromRest(d, this->m_speed, this->m_accel);

    leg.m_duration += RoundMs(seconds);
    leg.m_segments[stop].m_time = leg.m_duration + waited;
    leg.m_segments[stop].m_distance = leg.m_spline.m_length;
    leg.m_segments[stop].m_delay = 0;

    leg.m_startTime = this->m_length;
    this->m_length += leg.m_segments[stop].m_time;

    for (uint32_t i = 0; i < leg.m_segments.Count(); i++) {
        leg.m_segments[i].m_time += leg.m_startTime;
    }

    leg.m_endTime = this->m_length;
}

// ref: FUN_007f90f0
// The path's legs from its TaxiPathNode rows: a new leg wherever the map changes or a node
// teleports (flag 0x1) to the next; a node with flag 0x2 is a stop with its delay; nodes with
// events are kept for their leg's times.
void ShipPath::Build() {
    TSGrowableArray<C3Vector> points;
    TSGrowableArray<Stop> stops;
    TSGrowableArray<NodeEvent> events;

    this->m_length = 0;
    this->m_events.SetCount(0);

    int32_t index = FindFirstPathNode(this->m_pathID);

    if (index == -1) {
        return;
    }

    int32_t legIndex = -1;
    uint32_t teleported = 0;
    auto node = g_taxiPathNodeDB.GetRecordByIndex(index);

    do {
        bool newLeg = legIndex < 0;

        if (!newLeg && (node->m_mapID != this->m_legs[legIndex].m_mapID || teleported)) {
            this->BuildLeg(points, stops, events, static_cast<uint32_t>(legIndex));
            newLeg = true;
        }

        if (newLeg) {
            this->SetLegCount(static_cast<uint32_t>(legIndex + 2));
            legIndex++;
            this->m_legs[legIndex].m_mapID = node->m_mapID;
            points.SetCount(0);
            stops.SetCount(0);
            events.SetCount(0);
        }

        if ((node->m_flags & 0x2) && points.Count() != 0) {
            Stop s = { points.Count(), node->m_delay * 1000 };
            stops.Add(1, &s);
        }

        if (node->m_arrivalEventID || node->m_departureEventID) {
            NodeEvent e = { points.Count(), node->m_arrivalEventID, node->m_departureEventID };
            events.Add(1, &e);
        }

        C3Vector point = { node->m_loc[0], node->m_loc[1], node->m_loc[2] };
        points.Add(1, &point);

        teleported = node->m_flags & 0x1;
        index++;
    } while (index < g_taxiPathNodeDB.GetNumRecords()
             && (node = g_taxiPathNodeDB.GetRecordByIndex(index), node->m_pathID == this->m_pathID));

    if (points.Count() != 0) {
        this->BuildLeg(points, stops, events, static_cast<uint32_t>(legIndex));
        return;
    }

    this->SetLegCount(static_cast<uint32_t>(legIndex));
}

// ref: FUN_007f92f0
void ShipPath::Initialize(int32_t pathID, float speed, float accel, const TransportPhysicsRec* physics) {
    this->m_speed = speed;
    this->m_pathID = pathID;
    this->m_length = 0;
    this->m_accel = accel;
    this->Build();
    this->m_physics = physics;
}
