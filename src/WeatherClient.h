#pragma once

#include <string>
#include <vector>

struct CityPreset
{
    const wchar_t* name;
    double lat;
    double lon;
};

struct GeoResult
{
    bool ok = false;
    std::wstring resolved_name;
    double lat = 0.0;
    double lon = 0.0;
    std::wstring error;
};

struct WeatherResult
{
    bool ok = false;
    double temperature_c = 0.0;
    int weather_code = 0;
    bool is_day = true;
    std::wstring error;
};

namespace WeatherClient
{
    const std::vector<CityPreset>& GetRussianCityPresets();
    bool FindPreset(const std::wstring& city_name, double& lat, double& lon, std::wstring& canonical_name);
    GeoResult GeocodeRussianCity(const std::wstring& city_name);
    WeatherResult FetchCurrentTemperature(double lat, double lon);
    std::string WideToUtf8(const std::wstring& wstr);
    std::wstring Utf8ToWide(const std::string& str);
}
