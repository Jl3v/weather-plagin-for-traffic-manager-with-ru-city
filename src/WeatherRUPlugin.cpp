#include "WeatherRUPlugin.h"
#include "WeatherClient.h"
#include "resource.h"

#include <objidl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using std::min;
using std::max;
#include <gdiplus.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "gdiplus.lib")

HMODULE CWeatherRUPlugin::s_hModule = nullptr;

static ULONG_PTR g_gdiplusToken = 0;
static void EnsureGdiPlus()
{
    if (g_gdiplusToken == 0)
    {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    }
}

// ================= CWeatherRUItem (Температура) =================

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
    return m_value_buf;
}

const wchar_t* CWeatherRUItem::GetItemValueSampleText() const
{
    return L"-15 °C";
}

void CWeatherRUItem::SetValueText(const wchar_t* text)
{
    if (!text) return;
    size_t i = 0;
    for (; i < 31 && text[i] != L'\0'; ++i)
        m_value_buf[i] = text[i];
    m_value_buf[i] = L'\0';
}

// ================= CWeatherRUIconItem (Динамическая иконка) =================

const wchar_t* CWeatherRUIconItem::GetItemName() const
{
    return L"Погода (РФ) - Иконка";
}

const wchar_t* CWeatherRUIconItem::GetItemId() const
{
    return L"WeatherRU_Icon";
}

const wchar_t* CWeatherRUIconItem::GetItemLableText() const
{
    return L"";
}

const wchar_t* CWeatherRUIconItem::GetItemValueText() const
{
    return L"";
}

const wchar_t* CWeatherRUIconItem::GetItemValueSampleText() const
{
    return L"";
}

void CWeatherRUIconItem::SetWeatherState(int wmo_code, bool is_day, bool has_data)
{
    m_wmo_code = wmo_code;
    m_is_day = is_day;
    m_has_data = has_data;
}

static void DrawSun(Gdiplus::Graphics& g, float cx, float cy, float r, bool with_rays)
{
    Gdiplus::Color outlineCol(255, 34, 36, 46);
    Gdiplus::Color sunFill(255, 255, 198, 56);
    Gdiplus::Color sunRay(255, 255, 174, 42);

    if (with_rays)
    {
        Gdiplus::Pen rayPen(sunRay, max(1.3f, r * 0.24f));
        rayPen.SetStartCap(Gdiplus::LineCapRound);
        rayPen.SetEndCap(Gdiplus::LineCapRound);
        for (int i = 0; i < 8; ++i)
        {
            float ang = i * 3.14159265f / 4.0f;
            float r1 = r * 1.25f;
            float r2 = r * 1.68f;
            g.DrawLine(&rayPen, cx + std::cos(ang) * r1, cy + std::sin(ang) * r1,
                                cx + std::cos(ang) * r2, cy + std::sin(ang) * r2);
        }
    }

    Gdiplus::SolidBrush brush(sunFill);
    Gdiplus::Pen pen(outlineCol, max(1.2f, r * 0.18f));
    g.FillEllipse(&brush, cx - r, cy - r, r * 2.0f, r * 2.0f);
    g.DrawEllipse(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f);
}

static void DrawGreyMoon(Gdiplus::Graphics& g, float cx, float cy, float r)
{
    Gdiplus::Color moonFill(255, 184, 192, 204);
    Gdiplus::Color outlineCol(255, 34, 36, 46);

    Gdiplus::GraphicsPath outerPath;
    outerPath.AddEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);

    Gdiplus::GraphicsPath cutPath;
    float ox = cx + r * 0.48f;
    float oy = cy - r * 0.28f;
    float cr = r * 0.88f;
    cutPath.AddEllipse(ox - cr, oy - cr, cr * 2.0f, cr * 2.0f);

    Gdiplus::Region moonReg(&outerPath);
    moonReg.Exclude(&cutPath);

    Gdiplus::SolidBrush brush(moonFill);
    g.FillRegion(&brush, &moonReg);

    // Аккуратная обводка внешнего края полумесяца
    Gdiplus::Pen pen(outlineCol, max(1.1f, r * 0.16f));
    g.DrawArc(&pen, cx - r, cy - r, r * 2.0f, r * 2.0f, 35.0f, 235.0f);
}

static void DrawCloud(Gdiplus::Graphics& g, float x, float y, float w, float h, Gdiplus::Color fillCol)
{
    Gdiplus::Color outlineCol(255, 34, 36, 46);
    float rL = h * 0.36f;
    float rM = h * 0.48f;
    float rR = h * 0.34f;
    float baseY = y + h * 0.82f;

    Gdiplus::GraphicsPath path;
    path.AddEllipse(x + w * 0.04f, baseY - rL * 2.0f, rL * 2.0f, rL * 2.0f);
    path.AddEllipse(x + w * 0.22f, baseY - rM * 2.15f, rM * 2.0f, rM * 2.0f);
    path.AddEllipse(x + w * 0.56f, baseY - rR * 2.0f, rR * 2.0f, rR * 2.0f);
    path.AddRectangle(Gdiplus::RectF(x + w * 0.04f + rL, baseY - rL * 1.4f,
                                     (x + w * 0.56f + rR) - (x + w * 0.04f + rL), rL * 1.4f));

    Gdiplus::SolidBrush brush(fillCol);
    Gdiplus::Pen pen(outlineCol, max(1.2f, h * 0.09f));
    pen.SetLineJoin(Gdiplus::LineJoinRound);

    // Рисуем обводку под заливкой, чтобы внутри облака не было лишних швов
    Gdiplus::Pen thickOutline(outlineCol, max(2.2f, h * 0.18f));
    thickOutline.SetLineJoin(Gdiplus::LineJoinRound);
    g.DrawPath(&thickOutline, &path);
    g.FillPath(&brush, &path);
}

void CWeatherRUIconItem::DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode)
{
    if (!hDC || w <= 4 || h <= 4) return;
    EnsureGdiPlus();

    Gdiplus::Graphics g(reinterpret_cast<HDC>(hDC));
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    float s = static_cast<float>(min(w, h));
    float ox = x + (w - s) * 0.5f;
    float oy = y + (h - s) * 0.5f;

    int code = m_has_data ? m_wmo_code : 2;
    bool is_day = m_has_data ? m_is_day : true;

    // Классификация WMO-кодов Open-Meteo
    bool is_clear = (code == 0);
    bool is_partly = (code == 1 || code == 2);
    bool is_overcast = (code == 3);
    bool is_fog = (code == 45 || code == 48);
    bool is_light_rain = (code == 51 || code == 53 || code == 55 || code == 56 || code == 57 || code == 61 || code == 80);
    bool is_heavy_rain = (code == 63 || code == 65 || code == 66 || code == 67 || code == 81 || code == 82);
    bool is_snow = (code == 71 || code == 73 || code == 75 || code == 77 || code == 85 || code == 86);
    bool is_storm = (code == 95 || code == 96 || code == 99);

    if (is_clear)
    {
        if (is_day)
            DrawSun(g, ox + s * 0.50f, oy + s * 0.50f, s * 0.26f, true);
        else
            DrawGreyMoon(g, ox + s * 0.52f, oy + s * 0.50f, s * 0.34f);
        return;
    }

    if (is_fog)
    {
        if (is_day)
            DrawSun(g, ox + s * 0.50f, oy + s * 0.34f, s * 0.21f, false);
        else
            DrawGreyMoon(g, ox + s * 0.52f, oy + s * 0.34f, s * 0.24f);

        Gdiplus::Pen fogPen(Gdiplus::Color(235, 175, 186, 202), max(1.8f, s * 0.085f));
        fogPen.SetStartCap(Gdiplus::LineCapRound);
        fogPen.SetEndCap(Gdiplus::LineCapRound);
        g.DrawLine(&fogPen, ox + s * 0.14f, oy + s * 0.56f, ox + s * 0.86f, oy + s * 0.56f);
        g.DrawLine(&fogPen, ox + s * 0.22f, oy + s * 0.72f, ox + s * 0.78f, oy + s * 0.72f);
        g.DrawLine(&fogPen, ox + s * 0.16f, oy + s * 0.88f, ox + s * 0.84f, oy + s * 0.88f);
        return;
    }

    // Для всех облачных/осадочных состояний: солнце или серая луна выглядывают справа сверху
    if (is_day)
        DrawSun(g, ox + s * 0.68f, oy + s * 0.30f, s * 0.20f, is_partly);
    else
        DrawGreyMoon(g, ox + s * 0.68f, oy + s * 0.30f, s * 0.22f);

    // Цвет облака в стиле YuLu_Dark
    Gdiplus::Color cloudCol = Gdiplus::Color(255, 56, 168, 232); // яркий голубой (как в оригинале)
    if (is_overcast) cloudCol = Gdiplus::Color(255, 118, 152, 184);
    else if (is_heavy_rain || is_storm) cloudCol = Gdiplus::Color(255, 86, 118, 152);

    float cloudH = (is_partly || is_overcast) ? s * 0.68f : s * 0.56f;
    float cloudY = (is_partly || is_overcast) ? oy + s * 0.24f : oy + s * 0.16f;
    DrawCloud(g, ox + s * 0.04f, cloudY, s * 0.86f, cloudH, cloudCol);

    if (is_light_rain || is_heavy_rain)
    {
        Gdiplus::Pen rainPen(Gdiplus::Color(255, 72, 204, 255), max(1.5f, s * 0.075f));
        rainPen.SetStartCap(Gdiplus::LineCapRound);
        rainPen.SetEndCap(Gdiplus::LineCapRound);
        int count = is_heavy_rain ? 4 : 3;
        float startX = is_heavy_rain ? 0.22f : 0.26f;
        float stepX = is_heavy_rain ? 0.17f : 0.22f;
        for (int i = 0; i < count; ++i)
        {
            float rx = ox + s * (startX + i * stepX);
            float ry = oy + s * (i % 2 == 0 ? 0.72f : 0.77f);
            g.DrawLine(&rainPen, rx, ry, rx - s * 0.06f, ry + s * 0.16f);
        }
    }
    else if (is_snow)
    {
        Gdiplus::SolidBrush snowBrush(Gdiplus::Color(255, 226, 242, 255));
        float rDot = max(1.5f, s * 0.07f);
        float xs[3] = { 0.26f, 0.48f, 0.70f };
        float ys[3] = { 0.78f, 0.85f, 0.78f };
        for (int i = 0; i < 3; ++i)
        {
            g.FillEllipse(&snowBrush, ox + s * xs[i] - rDot, oy + s * ys[i] - rDot, rDot * 2.0f, rDot * 2.0f);
        }
    }
    else if (is_storm)
    {
        Gdiplus::PointF bolt[6] = {
            { ox + s * 0.48f, oy + s * 0.56f },
            { ox + s * 0.36f, oy + s * 0.76f },
            { ox + s * 0.47f, oy + s * 0.76f },
            { ox + s * 0.40f, oy + s * 0.96f },
            { ox + s * 0.62f, oy + s * 0.70f },
            { ox + s * 0.50f, oy + s * 0.70f }
        };
        Gdiplus::SolidBrush boltBrush(Gdiplus::Color(255, 255, 214, 42));
        Gdiplus::Pen boltOutline(Gdiplus::Color(255, 34, 36, 46), max(1.0f, s * 0.04f));
        g.FillPolygon(&boltBrush, bolt, 6);
        g.DrawPolygon(&boltOutline, bolt, 6);
    }
}

// ================= Options Dialog =================

struct DialogState
{
    CWeatherRUPlugin* plugin = nullptr;
    PluginConfig cfg;
    bool changed = false;
    HFONT link_font = nullptr;
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
            wchar_t label[32];
            wsprintfW(label, L"%d мин", kIntervals[i]);
            SendMessageW(hInt, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
            if (kIntervals[i] == state->cfg.update_interval_min)
                selected_idx = i;
        }
        SendMessageW(hInt, CB_SETCURSEL, selected_idx, 0);

        CheckDlgButton(hDlg, IDC_CHECK_SHOW_PLUS, state->cfg.show_plus ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CHECK_ROUND_TEMP, state->cfg.round_temp ? BST_CHECKED : BST_UNCHECKED);

        SetDlgItemTextW(hDlg, IDC_STATIC_STATUS, state->plugin->GetStatusSummary().c_str());

        HFONT hFont = reinterpret_cast<HFONT>(SendDlgItemMessageW(hDlg, IDC_STATIC_HINT, WM_GETFONT, 0, 0));
        if (!hFont) hFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        LOGFONTW lf{};
        if (hFont && GetObjectW(hFont, sizeof(LOGFONTW), &lf))
        {
            lf.lfUnderline = TRUE;
            state->link_font = CreateFontIndirectW(&lf);
            if (state->link_font)
                SendDlgItemMessageW(hDlg, IDC_STATIC_HINT, WM_SETFONT, reinterpret_cast<WPARAM>(state->link_font), TRUE);
        }
        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    {
        if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hDlg, IDC_STATIC_HINT))
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetTextColor(hdc, RGB(0, 102, 204));
            SetBkMode(hdc, TRANSPARENT);
            return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
        }
        break;
    }

    case WM_SETCURSOR:
    {
        if (reinterpret_cast<HWND>(wParam) == GetDlgItem(hDlg, IDC_STATIC_HINT))
        {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, TRUE);
            return TRUE;
        }
        break;
    }

    case WM_COMMAND:
    {
        WORD id = LOWORD(wParam);
        if (id == IDC_STATIC_HINT && HIWORD(wParam) == STN_CLICKED)
        {
            ShellExecuteW(hDlg, L"open", L"https://open-meteo.com/", nullptr, nullptr, SW_SHOWNORMAL);
            return TRUE;
        }
        else if (id == IDC_BTN_TEST_CITY)
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
                    int tenths = (int)(w.temperature_c >= 0.0 ? (w.temperature_c * 10.0 + 0.5) : (w.temperature_c * 10.0 - 0.5));
                    bool neg = (tenths < 0);
                    int abs_t = neg ? -tenths : tenths;
                    wchar_t tbuf[64];
                    wsprintfW(tbuf, neg ? L"-%d.%d °C" : L"%d.%d °C", abs_t / 10, abs_t % 10);
                    std::wstring ok_msg = L"Найден: " + geo.resolved_name + L"\nСейчас: " + tbuf + (w.is_day ? L" (день)" : L" (ночь)");
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

    case WM_DESTROY:
    {
        if (state && state->link_font)
        {
            DeleteObject(state->link_font);
            state->link_font = nullptr;
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
    InitializeCriticalSection(&m_cs);
    m_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_wake_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

CWeatherRUPlugin::~CWeatherRUPlugin()
{
    StopWorker();
    if (m_stop_event) CloseHandle(m_stop_event);
    if (m_wake_event) CloseHandle(m_wake_event);
}

IPluginItem* CWeatherRUPlugin::GetItem(int index)
{
    if (index == 0)
        return &m_item;
    if (index == 1)
        return &m_icon_item;
    return nullptr;
}

void CWeatherRUPlugin::StartWorkerIfNeeded()
{
    EnterCriticalSection(&m_cs);
    if (!m_worker_started)
    {
        m_worker_started = true;
        ResetEvent(m_stop_event);
        SetEvent(m_wake_event);
        m_thread = CreateThread(nullptr, 0, &CWeatherRUPlugin::WorkerThreadProc, this, 0, nullptr);
    }
    LeaveCriticalSection(&m_cs);
}

void CWeatherRUPlugin::StopWorker()
{
    EnterCriticalSection(&m_cs);
    bool started = m_worker_started;
    m_worker_started = false;
    HANDLE hThread = m_thread;
    m_thread = nullptr;
    LeaveCriticalSection(&m_cs);

    if (started && m_stop_event)
        SetEvent(m_stop_event);
    if (hThread)
        CloseHandle(hThread);
}

void CWeatherRUPlugin::TriggerImmediateRefresh()
{
    if (m_wake_event)
        SetEvent(m_wake_event);
}

DWORD WINAPI CWeatherRUPlugin::WorkerThreadProc(LPVOID param)
{
    auto* self = reinterpret_cast<CWeatherRUPlugin*>(param);
    if (self)
        self->WorkerLoop();
    return 0;
}

void CWeatherRUPlugin::WorkerLoop()
{
    HANDLE events[2] = { m_stop_event, m_wake_event };
    DWORD wait_ms = 0;

    while (true)
    {
        DWORD wr = WaitForMultipleObjects(2, events, FALSE, wait_ms);
        if (wr == WAIT_OBJECT_0)
            break;

        PluginConfig cfg_snapshot = GetConfigCopy();
        auto res = WeatherClient::FetchCurrentTemperature(cfg_snapshot.lat, cfg_snapshot.lon);

        EnterCriticalSection(&m_cs);
        if (res.ok)
        {
            m_has_temp = true;
            m_last_temp = res.temperature_c;
            m_last_wmo = res.weather_code;
            m_last_is_day = res.is_day;
            m_last_error.clear();

            SYSTEMTIME st{};
            GetLocalTime(&st);
            wchar_t time_buf[32];
            wsprintfW(time_buf, L"%02d:%02d", (int)st.wHour, (int)st.wMinute);
            m_last_update_str = time_buf;

            int interval = m_config.update_interval_min;
            if (interval < 5) interval = 5;
            wait_ms = (DWORD)interval * 60U * 1000U;
        }
        else
        {
            m_last_error = res.error;
            wait_ms = 2U * 60U * 1000U;
        }
        LeaveCriticalSection(&m_cs);
    }
}

void CWeatherRUPlugin::FormatTemperature(double temp_c, bool show_plus, bool round_temp, wchar_t* out, size_t out_len) const
{
    if (!out || out_len < 16) return;

    if (round_temp)
    {
        int t = (int)(temp_c >= 0.0 ? (temp_c + 0.5) : (temp_c - 0.5));
        if (t > 0 && show_plus)
            wsprintfW(out, L"+%d °C", t);
        else
            wsprintfW(out, L"%d °C", t);
    }
    else
    {
        int tenths = (int)(temp_c >= 0.0 ? (temp_c * 10.0 + 0.5) : (temp_c * 10.0 - 0.5));
        bool neg = (tenths < 0);
        int abs_t = neg ? -tenths : tenths;
        int whole = abs_t / 10;
        int frac = abs_t % 10;

        if (neg)
            wsprintfW(out, L"-%d.%d °C", whole, frac);
        else if (tenths > 0 && show_plus)
            wsprintfW(out, L"+%d.%d °C", whole, frac);
        else
            wsprintfW(out, L"%d.%d °C", whole, frac);
    }
}

void CWeatherRUPlugin::DataRequired()
{
    StartWorkerIfNeeded();

    wchar_t display[32] = L"-- °C";
    int wmo = 0;
    bool is_day = true;
    bool has_data = false;

    EnterCriticalSection(&m_cs);
    if (m_has_temp)
    {
        has_data = true;
        wmo = m_last_wmo;
        is_day = m_last_is_day;
        FormatTemperature(m_last_temp, m_config.show_plus, m_config.round_temp, display, 32);
    }
    LeaveCriticalSection(&m_cs);

    m_item.SetValueText(display);
    m_icon_item.SetWeatherState(wmo, is_day, has_data);
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
    EnterCriticalSection(&m_cs);
    PluginConfig copy = m_config;
    LeaveCriticalSection(&m_cs);
    return copy;
}

void CWeatherRUPlugin::ApplyConfig(const PluginConfig& cfg)
{
    EnterCriticalSection(&m_cs);
    m_config = cfg;
    LeaveCriticalSection(&m_cs);

    SaveConfig();
    TriggerImmediateRefresh();
}

std::wstring CWeatherRUPlugin::GetStatusSummary()
{
    EnterCriticalSection(&m_cs);
    std::wstring s = L"Город: " + m_config.city_name + L"\n";
    if (!m_last_error.empty())
    {
        s += L"Ошибка: " + m_last_error;
    }
    else if (m_has_temp)
    {
        wchar_t tbuf[32] = {};
        FormatTemperature(m_last_temp, m_config.show_plus, m_config.round_temp, tbuf, 32);
        s += L"Обновлено в " + m_last_update_str + L" (" + tbuf + L")";
    }
    else
    {
        s += L"Ожидание первого обновления...";
    }
    LeaveCriticalSection(&m_cs);
    return s;
}

const wchar_t* CWeatherRUPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return L"Погода РФ (WeatherRU)";
    case TMI_DESCRIPTION: return L"Отображает актуальную температуру и динамическую иконку погоды для городов России через Open-Meteo.";
    case TMI_AUTHOR:      return L"Custom Plugin";
    case TMI_COPYRIGHT:   return L"MIT License";
    case TMI_VERSION:     return L"1.0.0";
    case TMI_URL:         return L"https://open-meteo.com/";
    default:              return L"";
    }
}

void CWeatherRUPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr && data[0] != L'\0')
    {
        m_config_dir = data;
        LoadConfig();
    }
}

std::wstring CWeatherRUPlugin::GetModuleDir() const
{
    std::wstring dir;
    wchar_t path[MAX_PATH] = {};
    if (GetModuleFileNameW(s_hModule, path, MAX_PATH))
    {
        std::wstring full(path);
        size_t slash = full.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            dir = full.substr(0, slash + 1);
    }
    return dir;
}

std::wstring CWeatherRUPlugin::GetConfigFilePath() const
{
    std::wstring dir = m_config_dir;
    if (dir.empty())
        dir = GetModuleDir();
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/')
        dir.push_back(L'\\');
    return dir + L"WeatherRU.ini";
}

void CWeatherRUPlugin::LoadConfig()
{
    std::wstring path = GetConfigFilePath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    DWORD size = GetFileSize(hFile, nullptr);
    if (size == INVALID_FILE_SIZE || size == 0 || size > 65536)
    {
        CloseHandle(hFile);
        return;
    }

    std::string content(size, '\0');
    DWORD read = 0;
    ReadFile(hFile, &content[0], size, &read, nullptr);
    CloseHandle(hFile);
    content.resize(read);

    EnterCriticalSection(&m_cs);
    size_t pos = 0;
    while (pos < content.size())
    {
        size_t nl = content.find('\n', pos);
        if (nl == std::string::npos) nl = content.size();
        std::string line = content.substr(pos, nl - pos);
        pos = nl + 1;

        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        if (key == "city") m_config.city_name = WeatherClient::Utf8ToWide(val);
        else if (key == "lat") m_config.lat = std::strtod(val.c_str(), nullptr);
        else if (key == "lon") m_config.lon = std::strtod(val.c_str(), nullptr);
        else if (key == "interval")
        {
            int iv = std::atoi(val.c_str());
            m_config.update_interval_min = (iv < 5) ? 5 : iv;
        }
        else if (key == "show_plus") m_config.show_plus = (val == "1");
        else if (key == "round_temp") m_config.round_temp = (val == "1");
    }
    LeaveCriticalSection(&m_cs);
}

void CWeatherRUPlugin::SaveConfig()
{
    PluginConfig cfg = GetConfigCopy();
    char lat_buf[32], lon_buf[32];
    std::snprintf(lat_buf, sizeof(lat_buf), "%.5f", cfg.lat);
    std::snprintf(lon_buf, sizeof(lon_buf), "%.5f", cfg.lon);
    std::string lat_s(lat_buf), lon_s(lon_buf);
    std::replace(lat_s.begin(), lat_s.end(), ',', '.');
    std::replace(lon_s.begin(), lon_s.end(), ',', '.');

    std::string data;
    data += "city=" + WeatherClient::WideToUtf8(cfg.city_name) + "\n";
    data += "lat=" + lat_s + "\n";
    data += "lon=" + lon_s + "\n";
    data += "interval=" + std::to_string(cfg.update_interval_min) + "\n";
    data += "show_plus=" + std::string(cfg.show_plus ? "1" : "0") + "\n";
    data += "round_temp=" + std::string(cfg.round_temp ? "1" : "0") + "\n";

    std::wstring path = GetConfigFilePath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(hFile, data.data(), (DWORD)data.size(), &written, nullptr);
        CloseHandle(hFile);
    }
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
