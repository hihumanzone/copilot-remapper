#pragma once

#include <windows.h>
#include <shellapi.h>
#include <functional>

class TrayIcon {
public:
    TrayIcon(HWND messageHwnd, UINT callbackMsg);
    ~TrayIcon();

    bool Add(HICON hIcon, const wchar_t* tooltip);
    void Remove();
    void HandleMessage(WPARAM wParam, LPARAM lParam);

    void SetOnSettingsClicked(std::function<void()> callback) { m_onSettings = callback; }
    void SetOnExitClicked(std::function<void()> callback) { m_onExit = callback; }

private:
    void ShowContextMenu();

    HWND m_hwnd = nullptr;
    UINT m_callbackMsg = 0;
    NOTIFYICONDATAW m_nid = { sizeof(m_nid) };
    bool m_added = false;

    std::function<void()> m_onSettings = nullptr;
    std::function<void()> m_onExit = nullptr;
};
