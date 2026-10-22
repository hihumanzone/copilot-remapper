#include "Theme.hpp"
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1
#define DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1 19
#endif

enum PreferredAppMode {
    Default = 0,
    AllowDark = 1,
    ForceDark = 2,
    ForceLight = 3,
    Max = 4
};

using pfnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode);
using pfnAllowDarkModeForWindow = BOOL(WINAPI*)(HWND, BOOL);
using pfnShouldAppsUseDarkMode = BOOL(WINAPI*)();

static pfnSetPreferredAppMode s_fnSetPreferredAppMode = nullptr;
static pfnAllowDarkModeForWindow s_fnAllowDarkModeForWindow = nullptr;
static pfnShouldAppsUseDarkMode s_fnShouldAppsUseDarkMode = nullptr;

HBRUSH Theme::s_hDarkBgBrush = nullptr;
HBRUSH Theme::s_hDarkCtrlBrush = nullptr;

static const COLORREF COLOR_DARK_BG = RGB(32, 32, 32);
static const COLORREF COLOR_DARK_CTRL = RGB(45, 45, 45);
static const COLORREF COLOR_DARK_TEXT = RGB(245, 245, 245);
static const COLORREF COLOR_DARK_SUBTEXT = RGB(175, 175, 175);

void Theme::Initialize() {
    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryW(L"uxtheme.dll");

    if (hUxTheme) {
        s_fnSetPreferredAppMode = reinterpret_cast<pfnSetPreferredAppMode>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135)));
        s_fnAllowDarkModeForWindow = reinterpret_cast<pfnAllowDarkModeForWindow>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133)));
        s_fnShouldAppsUseDarkMode = reinterpret_cast<pfnShouldAppsUseDarkMode>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(132)));

        if (s_fnSetPreferredAppMode) {
            s_fnSetPreferredAppMode(AllowDark);
        }
    }

    if (!s_hDarkBgBrush) {
        s_hDarkBgBrush = CreateSolidBrush(COLOR_DARK_BG);
    }
    if (!s_hDarkCtrlBrush) {
        s_hDarkCtrlBrush = CreateSolidBrush(COLOR_DARK_CTRL);
    }
}

void Theme::Cleanup() {
    if (s_hDarkBgBrush) {
        DeleteObject(s_hDarkBgBrush);
        s_hDarkBgBrush = nullptr;
    }
    if (s_hDarkCtrlBrush) {
        DeleteObject(s_hDarkCtrlBrush);
        s_hDarkCtrlBrush = nullptr;
    }
}

bool Theme::IsSystemDarkMode() {
    if (s_fnShouldAppsUseDarkMode) {
        return s_fnShouldAppsUseDarkMode() != FALSE;
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        DWORD appsUseLightTheme = 1;
        DWORD bufferSize = sizeof(appsUseLightTheme);
        if (RegQueryValueExW(hKey, L"AppsUseLightTheme", nullptr, nullptr, reinterpret_cast<LPBYTE>(&appsUseLightTheme), &bufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return (appsUseLightTheme == 0);
        }
        RegCloseKey(hKey);
    }
    return false;
}

void Theme::ApplyWindowTheme(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return;

    BOOL darkMode = IsSystemDarkMode() ? TRUE : FALSE;

    HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
    if (FAILED(hr)) {
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1, &darkMode, sizeof(darkMode));
    }

    if (s_fnAllowDarkModeForWindow) {
        s_fnAllowDarkModeForWindow(hwnd, darkMode);
    }

    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
}

void Theme::ApplyThemeToWindowAndChildren(HWND hwndParent) {
    if (!hwndParent || !IsWindow(hwndParent)) return;

    bool dark = IsSystemDarkMode();
    ApplyWindowTheme(hwndParent);

    HWND hChild = GetWindow(hwndParent, GW_CHILD);
    while (hChild) {
        if (s_fnAllowDarkModeForWindow) {
            s_fnAllowDarkModeForWindow(hChild, dark ? TRUE : FALSE);
        }

        wchar_t className[64] = { 0 };
        GetClassNameW(hChild, className, 64);
        if (_wcsicmp(className, L"ComboBox") == 0) {
            SetWindowTheme(hChild, dark ? L"DarkMode_CFD" : nullptr, nullptr);
            COMBOBOXINFO cbi = { sizeof(COMBOBOXINFO) };
            if (GetComboBoxInfo(hChild, &cbi)) {
                if (cbi.hwndList) {
                    if (s_fnAllowDarkModeForWindow) s_fnAllowDarkModeForWindow(cbi.hwndList, dark ? TRUE : FALSE);
                    SetWindowTheme(cbi.hwndList, dark ? L"DarkMode_CFD" : nullptr, nullptr);
                }
                if (cbi.hwndItem) {
                    if (s_fnAllowDarkModeForWindow) s_fnAllowDarkModeForWindow(cbi.hwndItem, dark ? TRUE : FALSE);
                    SetWindowTheme(cbi.hwndItem, dark ? L"DarkMode_CFD" : nullptr, nullptr);
                }
            }
        } else if (_wcsicmp(className, L"Edit") == 0) {
            // Edit controls use custom 1px border subclassing in dark mode
            SetWindowTheme(hChild, L"", L"");
        } else {
            SetWindowTheme(hChild, dark ? L"DarkMode_Explorer" : nullptr, nullptr);
        }

        hChild = GetWindow(hChild, GW_HWNDNEXT);
    }

    RedrawWindow(hwndParent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void Theme::HandleSettingChange(HWND hwnd, LPARAM lParam) {
    if (lParam) {
        auto* str = reinterpret_cast<LPCWSTR>(lParam);
        if (wcscmp(str, L"ImmersiveColorSet") == 0 || wcscmp(str, L"WindowsTheme") == 0) {
            ApplyThemeToWindowAndChildren(hwnd);
        }
    } else {
        ApplyThemeToWindowAndChildren(hwnd);
    }
}

HBRUSH Theme::GetDarkBackgroundBrush() {
    if (!s_hDarkBgBrush) s_hDarkBgBrush = CreateSolidBrush(COLOR_DARK_BG);
    return s_hDarkBgBrush;
}

HBRUSH Theme::GetDarkControlBrush() {
    if (!s_hDarkCtrlBrush) s_hDarkCtrlBrush = CreateSolidBrush(COLOR_DARK_CTRL);
    return s_hDarkCtrlBrush;
}

COLORREF Theme::GetDarkBackgroundColor() { return COLOR_DARK_BG; }
COLORREF Theme::GetDarkControlColor() { return COLOR_DARK_CTRL; }
COLORREF Theme::GetDarkTextColor() { return COLOR_DARK_TEXT; }
COLORREF Theme::GetDarkSubtextColor() { return COLOR_DARK_SUBTEXT; }
