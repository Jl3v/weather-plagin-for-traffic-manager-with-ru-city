#pragma once

#include "PluginInterface.h"
#include <string>
#include <windows.h>

struct PluginConfig
{
    std::wstring city_name = L"Москва";
    double lat = 55.7558;
    double lon = 37.6173;
    int update_interval_min = 15;
    bool show_plus = true;
    bool round_temp = true;
};

class CWeatherRUItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;

    void SetValueText(const wchar_t* text);

private:
    wchar_t m_value_buf[32] = L"-- °C";
};

class CWeatherRUIconItem : public IPluginItem
{
public:
    virtual const wchar_t* GetItemName() const override;
    virtual const wchar_t* GetItemId() const override;
    virtual const wchar_t* GetItemLableText() const override;
    virtual const wchar_t* GetItemValueText() const override;
    virtual const wchar_t* GetItemValueSampleText() const override;
    virtual bool IsCustomDraw() const override { return true; }
    virtual int GetItemWidth() const override { return 32; }
    virtual void DrawItem(void* hDC, int x, int y, int w, int h, bool dark_mode) override;

    void SetWeatherState(int wmo_code, bool is_day, bool has_data);

private:
    int m_wmo_code = 0;
    bool m_is_day = true;
    bool m_has_data = false;
};

class CWeatherRUPlugin : public ITMPlugin
{
public:
    static CWeatherRUPlugin& Instance();

    virtual IPluginItem* GetItem(int index) override;
    virtual void DataRequired() override;
    virtual OptionReturn ShowOptionsDialog(void* hParent) override;
    virtual const wchar_t* GetInfo(PluginInfoIndex index) override;
    virtual void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;

    void TriggerImmediateRefresh();
    PluginConfig GetConfigCopy();
    void ApplyConfig(const PluginConfig& cfg);
    std::wstring GetStatusSummary();

    static void SetModuleHandle(HMODULE hMod);
    static HMODULE GetModuleHandle();

private:
    CWeatherRUPlugin();
    ~CWeatherRUPlugin();

    void StartWorkerIfNeeded();
    void StopWorker();
    static DWORD WINAPI WorkerThreadProc(LPVOID param);
    void WorkerLoop();

    void LoadConfig();
    void SaveConfig();
    std::wstring GetModuleDir() const;
    std::wstring GetConfigFilePath() const;
    void FormatTemperature(double temp_c, bool show_plus, bool round_temp, wchar_t* out, size_t out_len) const;

    CWeatherRUItem m_item;
    CWeatherRUIconItem m_icon_item;
    std::wstring m_config_dir;

    mutable CRITICAL_SECTION m_cs;
    PluginConfig m_config;
    bool m_has_temp = false;
    double m_last_temp = 0.0;
    int m_last_wmo = 0;
    bool m_last_is_day = true;
    std::wstring m_last_update_str = L"ещё не обновлялось";
    std::wstring m_last_error;

    HANDLE m_thread = nullptr;
    HANDLE m_stop_event = nullptr;
    HANDLE m_wake_event = nullptr;
    bool m_worker_started = false;

    static HMODULE s_hModule;
};
