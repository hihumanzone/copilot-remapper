#include "TrayIcon.hpp"
#include "../resource.h"

TrayIcon::TrayIcon(HWND messageHwnd, UINT callbackMsg)
    : m_hwnd(messageHwnd), m_callbackMsg(callbackMsg) {
}

TrayIcon::~TrayIcon() {
    StopRetryTimer();
    Remove();
}

bool TrayIcon::TryAdd() {
    if (!m_savedIcon || !m_hwnd || !IsWindow(m_hwnd)) return false;

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = m_callbackMsg;
    m_nid.hIcon = m_savedIcon;

    if (!m_savedTooltip.empty()) {
        wcsncpy_s(m_nid.szTip, m_savedTooltip.c_str(), _countof(m_nid.szTip) - 1);
    }

    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        m_added = true;
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
        StopRetryTimer();
        return true;
    }

    // If NIM_ADD failed, an icon entry might already linger from an earlier session, try NIM_MODIFY
    if (Shell_NotifyIconW(NIM_MODIFY, &m_nid)) {
        m_added = true;
        StopRetryTimer();
        return true;
    }

    m_added = false;
    return false;
}

void TrayIcon::StartRetryTimer() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        SetTimer(m_hwnd, IDT_TRAY_RETRY, 1000, nullptr);
    }
}

void TrayIcon::StopRetryTimer() {
    if (m_hwnd && IsWindow(m_hwnd)) {
        KillTimer(m_hwnd, IDT_TRAY_RETRY);
    }
    m_retryCount = 0;
}

bool TrayIcon::Add(HICON hIcon, const wchar_t* tooltip) {
    if (m_added) Remove();

    m_savedIcon = hIcon;
    m_savedTooltip = tooltip ? tooltip : L"";
    m_retryCount = 0;

    if (TryAdd()) {
        return true;
    }

    // Initial add failed (likely Windows Explorer taskbar still initializing).
    // Start retry timer to poll every 1 second until Explorer is ready.
    StartRetryTimer();
    return false;
}

void TrayIcon::Remove() {
    StopRetryTimer();
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
}

void TrayIcon::OnTaskbarCreated() {
    // When Explorer restarts or finishes initializing, it broadcasts TaskbarCreated.
    // Re-add our notification icon.
    m_added = false;
    m_retryCount = 0;
    if (!TryAdd()) {
        StartRetryTimer();
    }
}

void TrayIcon::OnRetryTimer() {
    m_retryCount++;
    if (TryAdd()) {
        StopRetryTimer();
        return;
    }
    // Stop after 60 seconds of retries to avoid indefinite polling if Explorer is absent
    if (m_retryCount >= 60) {
        StopRetryTimer();
    }
}

void TrayIcon::EnsureAdded() {
    if (!m_added && m_savedIcon) {
        if (!TryAdd()) {
            StartRetryTimer();
        }
    }
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    UINT msg = LOWORD(lParam);
    switch (msg) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;

    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case NIN_SELECT:
    case NIN_KEYSELECT:
        if (m_onSettings) {
            m_onSettings();
        }
        break;

    default:
        break;
    }
}

void TrayIcon::ShowContextMenu() {
    HMENU hMenu = LoadMenuW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDR_TRAY_MENU));
    if (!hMenu) return;

    HMENU hSubMenu = GetSubMenu(hMenu, 0);
    if (hSubMenu) {
        POINT pt;
        GetCursorPos(&pt);
        SetForegroundWindow(m_hwnd);

        UINT cmd = TrackPopupMenu(
            hSubMenu,
            TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            pt.x, pt.y, 0, m_hwnd, nullptr
        );

        PostMessageW(m_hwnd, WM_NULL, 0, 0);

        if (cmd == IDM_TRAY_SETTINGS && m_onSettings) {
            m_onSettings();
        } else if (cmd == IDM_TRAY_EXIT && m_onExit) {
            m_onExit();
        }
    }
    DestroyMenu(hMenu);
}
