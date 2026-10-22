#include "TrayIcon.hpp"
#include "../resource.h"

TrayIcon::TrayIcon(HWND messageHwnd, UINT callbackMsg)
    : m_hwnd(messageHwnd), m_callbackMsg(callbackMsg) {
}

TrayIcon::~TrayIcon() {
    Remove();
}

bool TrayIcon::Add(HICON hIcon, const wchar_t* tooltip) {
    if (m_added) Remove();

    ZeroMemory(&m_nid, sizeof(m_nid));
    m_nid.cbSize = sizeof(m_nid);
    m_nid.hWnd = m_hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    m_nid.uCallbackMessage = m_callbackMsg;
    m_nid.hIcon = hIcon;

    if (tooltip) {
        wcsncpy_s(m_nid.szTip, tooltip, _countof(m_nid.szTip) - 1);
    }

    m_added = Shell_NotifyIconW(NIM_ADD, &m_nid) != FALSE;
    return m_added;
}

void TrayIcon::Remove() {
    if (m_added) {
        Shell_NotifyIconW(NIM_DELETE, &m_nid);
        m_added = false;
    }
}

void TrayIcon::HandleMessage(WPARAM wParam, LPARAM lParam) {
    switch (LOWORD(lParam)) {
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        ShowContextMenu();
        break;

    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
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
