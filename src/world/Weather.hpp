#ifndef WORLD_WEATHER_HPP
#define WORLD_WEATHER_HPP

#include "net/Types.hpp"
#include <cstdint>
#include <tempest/Vector.hpp>

class CDataStore;

class Weather {
    public:
        // Member functions
        Weather();
};

// SMSG_WEATHER: Weather.dbc id, intensity 0..1, abrupt flag. Resolves the effect type, colour and
// sprite through Weather.dbc and hands them to the world's weather particle system.
int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

// Push the camera position the particle field is built around. Called where the terrain already
// tracks it, so the field sees exactly the value, at exactly the moment, it saw before the move.
void WeatherSetCameraPos(const C3Vector& cameraPos);

// The falling or blowing particle field, drawn in the world frame's transparent block.
void WeatherRender();

// The full-screen tint and caustic overlay while the camera is submerged.
void UnderwaterOverlayRender();

#endif
