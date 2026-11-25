#include "WeatherClient.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#pragma comment(lib, "winhttp.lib")

namespace WeatherClient
{

std::string WideToUtf8(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &out[0], size, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& str)
{
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (size <= 0) return {};
    std::wstring out(size, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &out[0], size);
    return out;
}

static std::string UrlEncodeUtf8(const std::string& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            out.push_back((char)c);
        }
        else
        {
            out.push_back('%');
            out.push_back(hex[(c >> 4) & 0x0F]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

static std::wstring ToLowerRu(const std::wstring& s)
{
    std::wstring out = s;
    CharLowerBuffW(&out[0], (DWORD)out.size());
    return out;
}

static std::wstring Trim(const std::wstring& s)
{
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return {};
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

const std::vector<CityPreset>& GetRussianCityPresets()
{
    static const std::vector<CityPreset> presets = {
        { L"Москва", 55.7558, 37.6173 },
        { L"Санкт-Петербург", 59.9343, 30.3351 },
        { L"Новосибирск", 55.0084, 82.9357 },
        { L"Екатеринбург", 56.8389, 60.6057 },
        { L"Казань", 55.7961, 49.1064 },
        { L"Нижний Новгород", 56.3269, 44.0059 },
        { L"Челябинск", 55.1644, 61.4368 },
        { L"Красноярск", 56.0153, 92.8932 },
        { L"Самара", 53.1959, 50.1002 },
        { L"Уфа", 54.7388, 55.9721 },
        { L"Ростов-на-Дону", 47.2357, 39.7015 },
        { L"Омск", 54.9885, 73.3242 },
        { L"Краснодар", 45.0355, 38.9753 },
        { L"Воронеж", 51.6720, 39.1843 },
        { L"Пермь", 58.0105, 56.2502 },
        { L"Волгоград", 48.7080, 44.5133 },
        { L"Саратов", 51.5336, 46.0343 },
        { L"Тюмень", 57.1522, 65.5272 },
        { L"Тольятти", 53.5078, 49.4204 },
        { L"Ижевск", 56.8526, 53.2045 },
        { L"Барнаул", 53.3548, 83.7698 },
        { L"Ульяновск", 54.3142, 48.4031 },
        { L"Иркутск", 52.2869, 104.3050 },
        { L"Хабаровск", 48.4802, 135.0719 },
        { L"Ярославль", 57.6261, 39.8845 },
        { L"Владивосток", 43.1155, 131.8855 },
        { L"Махачкала", 42.9849, 47.5047 },
        { L"Томск", 56.4846, 84.9476 },
        { L"Оренбург", 51.7682, 55.0969 },
        { L"Кемерово", 55.3547, 86.0873 },
        { L"Рязань", 54.6292, 39.7364 },
        { L"Астрахань", 46.3479, 48.0336 },
        { L"Набережные Челны", 55.7436, 52.3958 },
        { L"Пенза", 53.1951, 45.0183 },
        { L"Киров", 58.6035, 49.6680 },
        { L"Липецк", 52.6088, 39.5992 },
        { L"Калининград", 54.7104, 20.4522 },
        { L"Тула", 54.1931, 37.6173 },
        { L"Сочи", 43.5855, 39.7231 },
        { L"Мурманск", 68.9585, 33.0827 },
        { L"Архангельск", 64.5401, 40.5433 },
        { L"Якутск", 62.0355, 129.6755 },
        { L"Сургут", 61.2540, 73.3962 },
        { L"Белгород", 50.5954, 36.5873 },
        { L"Курск", 51.7304, 36.1926 },
        { L"Тверь", 56.8587, 35.9176 },
        { L"Щёлково", 55.9202, 37.9915 },
        { L"Балашиха", 55.7963, 37.9382 },
        { L"Химки", 55.8894, 37.4449 },
        { L"Подольск", 55.4312, 37.5458 }
    };
    return presets;
}

bool FindPreset(const std::wstring& city_name, double& lat, double& lon, std::wstring& canonical_name)
{
    std::wstring needle = ToLowerRu(Trim(city_name));
    if (needle.empty()) return false;
    for (const auto& p : GetRussianCityPresets())
    {
        if (ToLowerRu(p.name) == needle)
        {
            lat = p.lat;
            lon = p.lon;
            canonical_name = p.name;
            return true;
        }
    }
    return false;
}

static bool HttpGetHttps(const std::wstring& host, const std::wstring& path, std::string& response, std::wstring& error)
{
    HINTERNET hSession = WinHttpOpen(L"TrafficMonitor-WeatherRU/1.0",
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        error = L"WinHttpOpen failed";
        return false;
    }

    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        error = L"Нет соединения с сервером";
        return false;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        error = L"WinHttpOpenRequest failed";
        return false;
    }

    bool success = false;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr))
    {
        DWORD status_code = 0;
        DWORD status_size = sizeof(status_code);
        WinHttpQueryHeaders(hRequest,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status_code, &status_size, WINHTTP_NO_HEADER_INDEX);

        if (status_code >= 200 && status_code < 300)
        {
            DWORD avail = 0;
            while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
            {
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (WinHttpReadData(hRequest, &chunk[0], avail, &read) && read > 0)
                {
                    response.append(chunk.data(), read);
                }
                else
                {
                    break;
                }
            }
            success = !response.empty();
            if (!success) error = L"Пустой ответ сервера";
        }
        else
        {
            error = L"HTTP ошибка: " + std::to_wstring(status_code);
        }
    }
    else
    {
        error = L"Ошибка сети (нет интернета или таймаут)";
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return success;
}

// Locale-independent simple double parser
static bool ParseDouble(const std::string& s, size_t pos, double& out)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n'))
        ++pos;
    if (pos >= s.size()) return false;

    bool neg = false;
    if (s[pos] == '-') { neg = true; ++pos; }
    else if (s[pos] == '+') { ++pos; }

    bool has_digits = false;
    double val = 0.0;
    while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
    {
        has_digits = true;
        val = val * 10.0 + (s[pos] - '0');
        ++pos;
    }
    if (pos < s.size() && s[pos] == '.')
    {
        ++pos;
        double frac = 0.1;
        while (pos < s.size() && s[pos] >= '0' && s[pos] <= '9')
        {
            has_digits = true;
            val += (s[pos] - '0') * frac;
            frac *= 0.1;
            ++pos;
        }
    }
    if (!has_digits) return false;
    out = neg ? -val : val;
    return true;
}

static bool ExtractJsonNumber(const std::string& obj, const std::string& key, double& out)
{
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + pattern.size());
    if (pos == std::string::npos) return false;
    return ParseDouble(obj, pos + 1, out);
}

static bool ExtractJsonString(const std::string& obj, const std::string& key, std::string& out)
{
    std::string pattern = "\"" + key + "\"";
    size_t pos = obj.find(pattern);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos + pattern.size());
    if (pos == std::string::npos) return false;
    pos = obj.find('"', pos + 1);
    if (pos == std::string::npos) return false;
    ++pos;
    std::string res;
    while (pos < obj.size())
    {
        char c = obj[pos++];
        if (c == '\\' && pos < obj.size())
        {
            char esc = obj[pos++];
            if (esc == '"' || esc == '\\' || esc == '/') res.push_back(esc);
            else if (esc == 'n') res.push_back('\n');
            else if (esc == 't') res.push_back('\t');
            else res.push_back(esc);
        }
        else if (c == '"')
        {
            out = res;
            return true;
        }
        else
        {
            res.push_back(c);
        }
    }
    return false;
}

GeoResult GeocodeRussianCity(const std::wstring& city_name)
{
    GeoResult res;
    std::wstring trimmed = Trim(city_name);
    if (trimmed.empty())
    {
        res.error = L"Введите название города";
        return res;
    }

    // Check local preset list first (instant, works offline for coordinates)
    if (FindPreset(trimmed, res.lat, res.lon, res.resolved_name))
    {
        res.ok = true;
        return res;
    }

    std::string encoded = UrlEncodeUtf8(WideToUtf8(trimmed));
    std::wstring path = L"/v1/search?name=" + Utf8ToWide(encoded) + L"&count=10&language=ru&format=json";

    std::string body;
    if (!HttpGetHttps(L"geocoding-api.open-meteo.com", path, body, res.error))
    {
        return res;
    }

    size_t arr_pos = body.find("\"results\"");
    if (arr_pos == std::string::npos)
    {
        res.error = L"Город не найден";
        return res;
    }

    // Iterate over objects in "results":[{...},{...}]
    size_t pos = body.find('[', arr_pos);
    if (pos == std::string::npos)
    {
        res.error = L"Ошибка разбора ответа";
        return res;
    }

    bool found_any = false;
    GeoResult fallback;

    while (true)
    {
        size_t obj_start = body.find('{', pos);
        if (obj_start == std::string::npos) break;
        int depth = 0;
        size_t obj_end = obj_start;
        bool in_str = false;
        for (; obj_end < body.size(); ++obj_end)
        {
            char c = body[obj_end];
            if (c == '"' && (obj_end == 0 || body[obj_end - 1] != '\\')) in_str = !in_str;
            if (!in_str)
            {
                if (c == '{') ++depth;
                else if (c == '}')
                {
                    --depth;
                    if (depth == 0) break;
                }
            }
        }
        if (obj_end >= body.size()) break;

        std::string obj = body.substr(obj_start, obj_end - obj_start + 1);
        pos = obj_end + 1;

        double lat = 0.0, lon = 0.0;
        std::string name, country_code, admin1;
        if (ExtractJsonNumber(obj, "latitude", lat) &&
            ExtractJsonNumber(obj, "longitude", lon) &&
            ExtractJsonString(obj, "name", name))
        {
            ExtractJsonString(obj, "country_code", country_code);
            ExtractJsonString(obj, "admin1", admin1);

            std::wstring display = Utf8ToWide(name);
            if (!admin1.empty() && admin1 != name)
            {
                display += L" (" + Utf8ToWide(admin1) + L")";
            }

            if (country_code == "RU")
            {
                res.ok = true;
                res.lat = lat;
                res.lon = lon;
                res.resolved_name = display;
                return res;
            }
            if (!found_any)
            {
                found_any = true;
                fallback.ok = true;
                fallback.lat = lat;
                fallback.lon = lon;
                fallback.resolved_name = display;
            }
        }

        // Stop if we hit closing bracket of results array
        size_t next_comma = body.find_first_of(",]", pos);
        if (next_comma == std::string::npos || body[next_comma] == ']') break;
        pos = next_comma + 1;
    }

    if (found_any)
    {
        return fallback;
    }

    res.error = L"Город не найден";
    return res;
}

WeatherResult FetchCurrentTemperature(double lat, double lon)
{
    WeatherResult res;

    char lat_buf[32], lon_buf[32];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.4f", lat);
    std::snprintf(lon_buf, sizeof(lon_buf), "%.4f", lon);

    std::string lat_s(lat_buf), lon_s(lon_buf);
    std::replace(lat_s.begin(), lat_s.end(), ',', '.');
    std::replace(lon_s.begin(), lon_s.end(), ',', '.');

    std::string path_utf8 = "/v1/forecast?latitude=" + lat_s + "&longitude=" + lon_s +
                            "&current=temperature_2m,weather_code,is_day&timezone=auto";

    std::string body;
    if (!HttpGetHttps(L"api.open-meteo.com", Utf8ToWide(path_utf8), body, res.error))
    {
        return res;
    }

    size_t cur_pos = body.find("\"current\":{");
    if (cur_pos == std::string::npos)
    {
        cur_pos = body.find("\"current\" :");
    }
    if (cur_pos == std::string::npos)
    {
        res.error = L"Нет блока current в ответе";
        return res;
    }

    std::string sub = body.substr(cur_pos);
    if (!ExtractJsonNumber(sub, "temperature_2m", res.temperature_c))
    {
        res.error = L"Не удалось прочитать температуру";
        return res;
    }

    double wmo = 0.0;
    if (ExtractJsonNumber(sub, "weather_code", wmo))
        res.weather_code = static_cast<int>(wmo + 0.5);

    double day_flag = 1.0;
    if (ExtractJsonNumber(sub, "is_day", day_flag))
        res.is_day = (day_flag > 0.5);

    res.ok = true;
    return res;
}

} // namespace WeatherClient
