#pragma once

#include <windows.h>
#include <shellapi.h>

class TrayIcon {
public:
    using Callback = void(*)(void* context);

    TrayIcon(HWND messageHwnd, UINT callbackMsg) noexcept;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    bool Add(HICON hIcon, const wchar_t* tooltip);
    void Remove();
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    void OnTaskbarCreated();
    void OnRetryTimer();
    void EnsureAdded();
    bool IsAdded() const noexcept { return m_added; }

    void SetOnSettingsClicked(Callback callback, void* context) noexcept {
        m_onSettings = callback;
        m_onSettingsCtx = context;
    }
    void SetOnExitClicked(Callback callback, void* context) noexcept {
        m_onExit = callback;
        m_onExitCtx = context;
    }

private:
    bool TryAdd();
    void StartRetryTimer();
    void StopRetryTimer();
    void ShowContextMenu();

    HWND m_hwnd = nullptr;
    UINT m_callbackMsg = 0;
    NOTIFYICONDATAW m_nid = {};
    bool m_added = false;
    int m_retryCount = 0;
    HICON m_savedIcon = nullptr;
    wchar_t m_savedTooltip[128] = {};

    Callback m_onSettings = nullptr;
    void* m_onSettingsCtx = nullptr;
    Callback m_onExit = nullptr;
    void* m_onExitCtx = nullptr;
};
