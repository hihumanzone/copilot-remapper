#pragma once

#include <windows.h>
#include <commctrl.h>
#include <atomic>
#include "Config.hpp"

class SettingsWindow {
public:
    SettingsWindow() noexcept = default;
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void Show();
    void Hide();
    bool IsVisible() const noexcept;
    static bool IsOpen() noexcept { return s_isOpen.load(std::memory_order_relaxed); }
    HWND GetHwnd() const noexcept { return m_hwnd; }
    void OnCopilotKeyPressed();
    void Wakeup();

private:
    static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK ComboBoxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);
    static LRESULT CALLBACK CheckboxSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData);

    void OnInitDialog(HWND hwnd);
    void OnCommand(HWND hwnd, WPARAM wParam, LPARAM lParam);
    void UpdateTargetPrompt();
    void OnBrowse(HWND hwnd);
    void LoadConfigToUI();
    void SaveUIToConfig();

    HWND m_hwnd = nullptr;
    AppConfig m_config;
    static std::atomic<bool> s_isOpen;
};
