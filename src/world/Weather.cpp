#include "world/Weather.hpp"
#include "world/Terrain.hpp"
#include "console/CVar.hpp"
#include "db/Db.hpp"
#include <common/DataStore.hpp>
#include <cstdio>

static CVar* s_useShadersCvar;

bool WeatherDensityCallback(CVar* var, const char* oldValue, const char* value, void* arg) {
    // TODO
    return true;
}

int32_t ReceiveWeather(void* param, NETMESSAGE msgId, uint32_t time, CDataStore* msg) {
    uint32_t weatherID = 0;
    float intensity = 0.0f;
    uint8_t abrupt = 0;

    msg->Get(weatherID);
    msg->Get(intensity);
    msg->Get(abrupt);

    // The reference logs "Weather changed to %d, intensity %f" (FUN_00526530) and applies the
    // Weather.dbc row: effectType 1 rain, 2 snow, 3 sand/mist, 0 or unknown = clear.
    auto rec = g_weatherDB.GetRecord(static_cast<int32_t>(weatherID));
    int32_t effectType = rec ? rec->m_effectType : 0;
    const float* color = rec ? rec->m_effectColor : nullptr;
    const char* texture = (rec && rec->m_effectTexture && *rec->m_effectTexture) ? rec->m_effectTexture : nullptr;

    fprintf(stderr, "Weather changed to %u, intensity %f\n", weatherID, intensity);

    TerrainSetWeather(effectType, intensity, color, texture, abrupt != 0);

    return 1;
}

Weather::Weather() {
    // TODO

    CVar::Register(
        "weatherDensity",
        nullptr,
        0x0,
        "2",
        &WeatherDensityCallback,
        DEFAULT
    );

    s_useShadersCvar = CVar::Register(
        "useWeatherShaders",
        nullptr,
        0x0,
        "1",
        nullptr,
        DEFAULT
    );
}
