#ifndef WORLD_WEATHER_HPP
#define WORLD_WEATHER_HPP

#include "net/Types.hpp"
#include <cstdint>

class CDataStore;

class Weather {
    public:
        // Member functions
        Weather();
};

// SMSG_WEATHER: Weather.dbc id, intensity 0..1, abrupt flag. Resolves the effect type, colour and
// sprite through Weather.dbc and hands them to the world's weather particle system.
int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg);

#endif
