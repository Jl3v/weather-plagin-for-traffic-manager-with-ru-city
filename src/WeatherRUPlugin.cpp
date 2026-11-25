#include "WeatherRUPlugin.h"
#include "WeatherClient.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

HMODULE CWeatherRUPlugin::s_hModule = nullptr;

// ================= CWeatherRUItem =================

const wchar_t* CWeatherRUItem::GetItemName() const
{
    return L"Погода (РФ)";
}

const wchar_t* CWeatherRUItem::GetItemId() const
{
    return L"WeatherRU_Temperature";
}

const wchar_t* CWeatherRUItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CWeatherRUItem::GetItemValueText() const
{
    return m_value_text.c_str();
}

const wchar_t* CWeatherRUItem::GetItemValueSampleText() const
{
    return L"-15 °C";
}

void CWeatherRUItem::SetValueText(const std::wstring& text)
{
    m_value_text = text;
}

// ================= Options Dialog =================

struct DialogState
{
    CWeatherRUPlugin* plugin = nullptr;
    PluginConfig cfg;
    bool changed = false;
};

static const int kIntervals[] = { 10, 15, 30, 60 };

static INT_PTR CALLBACK OptionsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    DialogState* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hDlg, GWLP_USERDATA));

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        state = reinterpret_cast<DialogState*>(lParam);
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));

        HWND hCity = GetDlgItem(hDlg, IDC_COMBO_CITY);
        for (const auto& preset : WeatherClient::GetRussianCityPresets())
        {
            SendMessageW(hCity, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(preset.name));
        }
        SetWindowTextW(hCity, state->cfg.city_name.c_str());

        HWND hInt = GetDlgItem(hDlg, IDC_COMBO_INTERVAL);
        int selected_idx = 1;
        for (int i = 0; i < 4; ++i)
        {
            std::wstring label = std::to_wstring(kIntervals[i]) + L" мин";
            SendMessageW(hInt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            if (kIntervals[i] == state->cfg.update_interval_min)
                selected_idx = i;
        }
        SendMessageW(hInt, CB_SETCURSEL, selected_idx, 0);

        CheckDlgButton(hDlg, IDC_CHECK_SHOW_PLUS, state->cfg.show_plus ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ROUND_TEMP, state->cfg.round_temp ? BST_CHECKED : BST_UNCHECKED);

        SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, state->plugin->GetStatusSummary().c_str());
        return TRUE;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        if (id == IDC_BTN_TEST_CITY)
        {
            wchar_t city_buf[256] = {};
            GetDlgItemTextW(hDlg, IDC_COMBO_CITY, city_buf, 255);
            SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Поиск города и запрос погоды...");
            UpdateWindow(hDlg);

            auto geo = WeatherClient::GeocodeRussianCity(city_buf);
            if (!geo.ok)
            {
                std::wstring msg_err = L"Ошибка: " + geo.error;
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, msg_err.c_str());
            }
            else
            {
                state->cfg.lat = geo.lat;
                state->cfg.lon = geo.lon;
                auto w = WeatherClient::FetchCurrentTemperature(geo.lat, geo.lon);
                if (w.ok)
                {
                    wchar_t tbuf[64];
                    std::swprintf(tbuf, 64, L"%.1f °C", w.temperature_c);
                    std::wstring ok_msg = L"Найден: " + geo.resolved_name + L"\nСейчас: " + tbuf;
                    SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, ok_msg.c_str());
                }
                else
                {
                    std::wstring ok_msg = L"Найден: " + geo.resolved_name + L"\nПогода: " + w.error;
                    SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, ok_msg.c_str());
                }
            }
            return TRUE;
        }
        else if (id == IDOK)
        {
            wchar_t city_buf[256] = {};
            GetDlgItemTextW(hDlg, IDC_COMBO_CITY, city_buf, 255);
            std::wstring new_city = city_buf;

            if (new_city.empty())
            {
                MessageBoxW(hDlg, L"Укажите название города.", L"Погода (РФ)", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            if (new_city != state->cfg.city_name)
            {
                SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, L"Определение координат города...");
                UpdateWindow(hDlg);
                auto geo = WeatherClient::GeocodeRussianCity(new_city);
                if (!geo.ok)
                {
                    std::wstring err = L"Не удалось найти город «" + new_city + L"»:\n" + geo.error;
                    MessageBoxW(hDlg, err.c_str(), L"Погода (РФ)", MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                state->cfg.city_name = new_city;
                state->cfg.lat = geo.lat;
                state->cfg.lon = geo.lon;
            }

            int idx = (int)SendDlgItemMessageW(hDlg, IDC_COMBO_INTERVAL, CB_GETCURSEL, 0, 0);
            if (idx >= 0 && idx < 4)
                state->cfg.update_interval_min = kIntervals[idx];

            state->cfg.show_plus = (IsDlgButtonChecked(hDlg, IDC_CHECK_SHOW_PLUS) == BST_CHECKED);
            state->cfg.round_temp = (IsDlgButtonChecked(hDlg, IDC_CHECK_ROUND_TEMP) == BST_CHECKED);

            state->changed = true;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        else if (id == IDCANCEL)
        {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

// ================= CWeatherRUPlugin =================

CWeatherRUPlugin& CWeatherRUPlugin::Instance()
{
    static CWeatherRUPlugin instance;
    return instance;
}

void CWeatherRUPlugin::SetModuleHandle(HMODULE hMod)
{
    s_hModule = hMod;
}

HMODULE CWeatherRUPlugin::GetModuleHandle()
{
    return s_hModule;
}

CWeatherRUPlugin::CWeatherRUPlugin()
{
}

CWeatherRUPlugin::~CWeatherRUPlugin()
{
    StopWorker();
}

IPluginItem* CWeatherRUPlugin::GetItem(int index)
{
    if (index == 0)
        return &m_item;
    return nullptr;
}

void CWeatherRUPlugin::StartWorkerIfNeeded()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_worker_started)
    {
        m_worker_started = true;
        m_stop = false;
        m_force_refresh = true;
        m_worker = std::thread(&CWeatherRUPlugin::WorkerLoop, this);
    }
}

void CWeatherRUPlugin::StopWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_worker_started) return;
        m_stop = true;
    }
    m_cv.notify_all();
    // Не вызываем join() при выгрузке DLL, чтобы исключить Loader Lock deadlock
    if (m_worker.joinable())
        m_worker.detach();
    m_worker_started = false;
}

void CWeatherRUPlugin::TriggerImmediateRefresh()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_force_refresh = true;
    }
    m_cv.notify_all();
}

void CWeatherRUPlugin::WorkerLoop()
{
    try
    {
        while (true)
        {
            PluginConfig cfg_snapshot;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                if (m_stop) break;

                if (!m_force_refresh)
                {
                    m_cv.wait_until(lock, m_next_fetch_time, [this]() {
                        return m_stop || m_force_refresh;
                    });
                    if (m_stop) break;
                }
                m_force_refresh = false;
                cfg_snapshot = m_config;
            }

            auto res = WeatherClient::FetchCurrentTemperature(cfg_snapshot.lat, cfg_snapshot.lon);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stop) break;

                if (res.ok)
                {
                    m_has_temp = true;
                    m_last_temp = res.temperature_c;
                    m_last_error.clear();

                    SYSTEMTIME st{};
                    GetLocalTime(&st);
                    wchar_t time_buf[32];
                    std::swprintf(time_buf, 32, L"%02d:%02d", st.wHour, st.wMinute);
                    m_last_update_str = time_buf;

                    int interval = (std::max)(5, m_config.update_interval_min);
                    m_next_fetch_time = std::chrono::steady_clock::now() + std::chrono::minutes(interval);
                }
                else
                {
                    m_last_error = res.error;
                    m_next_fetch_time = std::chrono::steady_clock::now() + std::chrono::minutes(2);
                }
            }
        }
    }
    catch (...)
    {
    }
}

std::wstring CWeatherRUPlugin::FormatTemperature(double temp_c, bool show_plus, bool round_temp) const
{
    wchar_t buf[32];
    if (round_temp)
    {
        int t = static_cast<int>(std::lround(temp_c));
        if (t > 0 && show_plus)
            std::swprintf(buf, 32, L"+%d °C", t);
        else
            std::swprintf(buf, 32, L"%d °C", t);
    }
    else
    {
        double t = std::round(temp_c * 10.0) / 10.0;
        if (t == -0.0) t = 0.0;
        if (t > 0.0 && show_plus)
            std::swprintf(buf, 32, L"+%.1f °C", t);
        else
            std::swprintf(buf, 32, L"%.1f °C", t);
    }
    return buf;
}

void CWeatherRUPlugin::DataRequired()
{
    try
    {
        StartWorkerIfNeeded();

        std::wstring display;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_has_temp)
                display = FormatTemperature(m_last_temp, m_config.show_plus, m_config.round_temp);
            else
                display = L"-- °C";
        }
        m_item.SetValueText(display);
    }
    catch (...)
    {
    }
}

ITMPlugin::OptionReturn CWeatherRUPlugin::ShowOptionsDialog(void* hParent)
{
    DialogState state;
    state.plugin = this;
    state.cfg = GetConfigCopy();

    INT_PTR ret = DialogBoxParamW(s_hModule,
                                  MAKEINTRESOURCEW(IDD_OPTIONS_DIALOG),
                                  reinterpret_cast<HWND>(hParent),
                                  OptionsDlgProc,
                                  reinterpret_cast<LPARAM>(&state));

    if (ret == IDOK && state.changed)
    {
        ApplyConfig(state.cfg);
        return OR_OPTION_CHANGED;
    }
    return OR_OPTION_UNCHANGED;
}

PluginConfig CWeatherRUPlugin::GetConfigCopy()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_config;
}

void CWeatherRUPlugin::ApplyConfig(const PluginConfig& cfg)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = cfg;
    }
    SaveConfig();
    TriggerImmediateRefresh();
}

std::wstring CWeatherRUPlugin::GetStatusSummary()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring s = L"Город: " + m_config.city_name + L"\n";
    if (!m_last_error.empty())
        s += L"Ошибка: " + m_last_error;
    else if (m_has_temp)
        s += L"Обновлено в " + m_last_update_str + L" (" + FormatTemperature(m_last_temp, m_config.show_plus, m_config.round_temp) + L")";
    else
        s += L"Ожидание первого обновления...";
    return s;
}

const wchar_t* CWeatherRUPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return L"Погода РФ (WeatherRU)";
    case TMI_DESCRIPTION: return L"Отображает актуальную температуру для городов России через Open-Meteo (без API-ключа).";
    case TMI_AUTHOR:      return L"Custom Plugin";
    case TMI_COPYRIGHT:   return L"MIT License";
    case TMI_VERSION:     return L"1.0.1";
    case TMI_URL:         return L"https://open-meteo.com/";
    default:              return L"";
    }
}

void CWeatherRUPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    try
    {
        if (index == EI_CONFIG_DIR && data != nullptr && data[0] != L'\0')
        {
            m_config_dir = data;
            LoadConfig();
        }
    }
    catch (...)
    {
    }
}

std::wstring CWeatherRUPlugin::GetConfigFilePath() const
{
    std::wstring dir = m_config_dir;
    if (dir.empty())
    {
        wchar_t path[MAX_PATH] = {};
        if (GetModuleFileNameW(s_hModule, path, MAX_PATH))
        {
            std::wstring full(path);
            size_t slash = full.find_last_of(L"\\/");
            if (slash != std::wstring::npos)
                dir = full.substr(0, slash);
        }
    }
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
        dir.push_back(L'\\');
    return dir + L"WeatherRU.ini";
}

void CWeatherRUPlugin::LoadConfig()
{
    std::wstring path = GetConfigFilePath();
    std::ifstream in(path);
    if (!in.is_open()) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    std::string line;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "city") m_config.city_name = WeatherClient::Utf8ToWide(val);
        else if (key == "lat") m_config.lat = std::strtod(val.c_str(), nullptr);
        else if (key == "lon") m_config.lon = std::strtod(val.c_str(), nullptr);
        else if (key == "interval") m_config.update_interval_min = (std::max)(5, std::atoi(val.c_str()));
        else if (key == "show_plus") m_config.show_plus = (val == "1");
        else if (key == "round_temp") m_config.round_temp = (val == "1");
    }
}

void CWeatherRUPlugin::SaveConfig()
{
    std::wstring path = GetConfigFilePath();
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return;

    PluginConfig cfg = GetConfigCopy();
    char lat_buf[32], lon_buf[32];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.5f", cfg.lat);
    std::snprintf(lon_buf, sizeof(lon_buf), "%.5f", cfg.lon);
    std::string lat_s(lat_buf), lon_s(lon_buf);
    std::replace(lat_s.begin(), lat_s.end(), ',', '.');
    std::replace(lon_s.begin(), lon_s.end(), ',', '.');

    out << "city=" << WeatherClient::WideToUtf8(cfg.city_name) << "\n";
    out << "lat=" << lat_s << "\n";
    out << "lon=" << lon_s << "\n";
    out << "interval=" << cfg.update_interval_min << "\n";
    out << "show_plus=" << (cfg.show_plus ? "1" : "0") << "\n";
    out << "round_temp=" << (cfg.round_temp ? "1" : "0") << "\n";
}

// ================= DLL Exports =================

extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CWeatherRUPlugin::Instance();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        CWeatherRUPlugin::SetModuleHandle(hModule);
    }
    return TRUE;
}
