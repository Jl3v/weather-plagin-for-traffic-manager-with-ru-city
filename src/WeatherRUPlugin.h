#pragma once

#include "PluginInterface.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
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

    void SetValueText(const std::wstring& text);

private:
    std::wstring m_value_text = L"-- °C";
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
    void WorkerLoop();

    void LoadConfig();
    void SaveConfig();
    std::wstring GetConfigFilePath() const;
    std::wstring FormatTemperature(double temp_c, bool show_plus, bool round_temp) const;

    CWeatherRUItem m_item;
    std::wstring m_config_dir;

    std::mutex m_mutex;
    PluginConfig m_config;
    bool m_has_temp = false;
    double m_last_temp = 0.0;
    std::wstring m_last_update_str = L"ещё не обновлялось";
    std::wstring m_last_error;

    std::thread m_worker;
    std::condition_variable m_cv;
    bool m_worker_started = false;
    bool m_stop = false;
    bool m_force_refresh = true;
    std::chrono::steady_clock::time_point m_next_fetch_time{};

    static HMODULE s_hModule;
};
