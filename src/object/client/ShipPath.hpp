#ifndef OBJECT_CLIENT_SHIP_PATH_HPP
#define OBJECT_CLIENT_SHIP_PATH_HPP

#include "util/C3Spline.hpp"
#include <storm/Array.hpp>
#include <tempest/Vector.hpp>
#include <cstdint>

class TransportPhysicsRec;

// A ship's or zeppelin's route (the reference's RTTI names ShipPath::Leg, ::Segment and
// ::TimeEvent; the functions sit at 0x007f77d0 .. 0x007f92f0). A TaxiPath's nodes are cut into
// legs, one per map (or per teleporting node), each a Catmull-Rom spline through its nodes; a leg's
// stops cut it into segments, travelled at the transport's speed with its acceleration at either
// end and its delay at each stop. The whole path loops over its length in ms, and a time offset
// moves the transport along it. Its physics row rocks it on the water.
class ShipPath {
    public:
        // A stretch between stops: when it ends (path ms), how far along the leg its stop is, and
        // how long the transport waits there.
        struct Segment {
            uint32_t m_time = 0;
            float m_distance = 0.0f;
            uint32_t m_delay = 0;
        };

        // A node's arrival or departure event, at a path time.
        struct TimeEvent {
            uint32_t m_time = 0;
            uint32_t m_eventID = 0;
        };

        // 0x1e4 bytes in the reference: the map, the spline at +0x04 (its length at +0x08), and
        // when the leg starts, how long it takes and when it ends (+0x1c8 .. +0x1d0), then its
        // segments (+0x1d4).
        struct Leg {
            int32_t m_mapID = 0;
            C3Spline_CatmullRom m_spline;
            uint32_t m_startTime = 0;
            uint32_t m_duration = 0;
            uint32_t m_endTime = 0;
            TSGrowableArray<Segment> m_segments;
        };

        // A stop on the nodes as the builder collects them: the point it is at and its delay.
        struct Stop {
            uint32_t m_point;
            uint32_t m_delay;
        };

        // An event-carrying node as the builder collects them.
        struct NodeEvent {
            uint32_t m_point;
            uint32_t m_arrival;
            uint32_t m_departure;
        };

        ~ShipPath();

        // ref: FUN_007f92f0
        void Initialize(int32_t pathID, float speed, float accel, const TransportPhysicsRec* physics);
        // ref: FUN_007f7fc0
        // The server's period for the whole path; the last leg ends with it.
        void SetLength(uint32_t length);
        // ref: FUN_007f7810
        uint32_t GetPathTime(uint32_t time);
        // ref: FUN_007f82b0
        void Evaluate(uint32_t time, uint32_t elapsed, int32_t* mapID, uint32_t* sequence, C3Vector* position,
                      float* facing, uint32_t* leg, float* roll, float* pitch, int32_t raw);
        // ref: FUN_007f8000
        // Stop at the next stop (`stop`), or set off again from the one it waits at.
        void SetStopped(uint32_t time, bool stop);
        // ref: FUN_007f80a0
        // Stop where it is now: at the stop the current segment ends at.
        void StopNow(uint32_t time);
        // ref: FUN_007f8120
        // Put the transport `progress` (0..1) of the way round the path at `time`.
        void SetProgress(uint32_t time, float progress);

        // ref: FUN_007f77d0
        static bool InRange(uint32_t from, uint32_t to, uint32_t time);

        // Member variables
        const TransportPhysicsRec* m_physics = nullptr;  // +0x00
        int32_t m_pathID = 0;                       // +0x04
        TSGrowableArray<Leg> m_legs;                // +0x08
        TSGrowableArray<TimeEvent> m_events;        // +0x18
        float m_speed = 0.0f;                       // +0x28, yards per second
        uint32_t m_length = 0;                      // +0x2c, the path's length in ms
        float m_accel = 0.0f;                       // +0x30, yards per second per second
        uint32_t m_stopTime = 0;                    // +0x34, the path time it stops at
        uint32_t m_timeOffset = 0;                  // +0x38, added to the time to give the path time
        uint8_t m_stopping = 0;                     // +0x3c, it is to stop at m_stopTime
        uint8_t m_stopped = 0;                      // +0x3d, it has
        uint8_t m_flag3E = 0;                       // +0x3e

    private:
        // ref: FUN_007f7840
        uint32_t GetTime(uint32_t time, uint32_t elapsed, int32_t raw);
        // ref: FUN_007f78c0
        void SetTimeOffsetTo(uint32_t time, uint32_t pathTime);
        // ref: FUN_007f7a60
        // How long a stretch of `distance` takes from rest to rest.
        int32_t TimeBetweenStops(float distance);
        // ref: FUN_007f7660
        float TravelIntoStop(float elapsed, float duration, uint32_t* sequence, float* speed);
        // ref: FUN_007f76f0
        float TravelBetweenStops(float elapsed, float duration, uint32_t* sequence, float* speed);
        // ref: FUN_007f7d30
        void FindSegment(uint32_t time, uint32_t* leg, uint32_t* segment);
        // ref: FUN_007f7b30
        void Bob(uint32_t time, float speed, float turn, C3Vector* position, float* roll, float* pitch);
        // ref: FUN_007f7dd0
        void Bank(float speed, float facing, float distance, uint32_t leg, float turn, uint32_t time,
                  C3Vector* position, float* roll, float* pitch);
        // ref: FUN_007f8660
        void BuildLeg(const TSGrowableArray<C3Vector>& points, const TSGrowableArray<Stop>& stops,
                      const TSGrowableArray<NodeEvent>& events, uint32_t legIndex);
        // ref: FUN_007f90f0
        void Build();
        void SetLegCount(uint32_t count);
};

#endif
