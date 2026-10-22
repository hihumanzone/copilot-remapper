#pragma once

#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <string>

class TrayIcon {
public:
    TrayIcon(HWND messageHwnd, UINT callbackMsg);
    ~TrayIcon();

    bool Add(HICON hIcon, const wchar_t* tooltip);
    void Remove();
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    void OnTaskbarCreated();
    void OnRetryTimer();
    void EnsureAdded();
    bool IsAdded() const { return m_added; }

    void SetOnSettingsClicked(std::function<void()> callback) { m_onSettings = callback; }
    void SetOnExitClicked(std::function<void()> callback) { m_onExit = callback; }

private:
    bool TryAdd();
    void StartRetryTimer();
    void StopRetryTimer();
    void ShowContextMenu();

    HWND m_hwnd = nullptr;
    UINT m_callbackMsg = 0;
    NOTIFYICONDATAW m_nid = { sizeof(m_nid) };
    bool m_added = false;
    int m_retryCount = 0;
    HICON m_savedIcon = nullptr;
    std::wstring m_savedTooltip;

    std::function<void()> m_onSettings = nullptr;
    std::function<void()> m_onExit = nullptr;
};
