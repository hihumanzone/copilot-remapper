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

bool Theme::s_initialized = false;
bool Theme::s_isDarkMode = false;

HBRUSH Theme::s_hDarkBgBrush = nullptr;
HBRUSH Theme::s_hDarkCtrlBrush = nullptr;
HBRUSH Theme::s_hDarkComboSelBrush = nullptr;
HBRUSH Theme::s_hAccentBrush = nullptr;

HPEN Theme::s_hDarkEditBorderPen = nullptr;
HPEN Theme::s_hDarkComboBorderPen = nullptr;
HPEN Theme::s_hDarkComboArrowPen = nullptr;
HPEN Theme::s_hDarkCheckboxBorderPen = nullptr;
HPEN Theme::s_hAccentPen = nullptr;
HPEN Theme::s_hWhiteCheckPen = nullptr;

static constexpr COLORREF COLOR_DARK_BG = RGB(32, 32, 32);
static constexpr COLORREF COLOR_DARK_CTRL = RGB(45, 45, 45);
static constexpr COLORREF COLOR_DARK_TEXT = RGB(245, 245, 245);
static constexpr COLORREF COLOR_DARK_SUBTEXT = RGB(175, 175, 175);

void Theme::Initialize() {
    if (s_initialized) return;
    s_initialized = true;

    HMODULE hUxTheme = GetModuleHandleW(L"uxtheme.dll");
    if (!hUxTheme) hUxTheme = LoadLibraryW(L"uxtheme.dll");

    if (hUxTheme) {
        s_fnSetPreferredAppMode = reinterpret_cast<pfnSetPreferredAppMode>(reinterpret_cast<void*>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(135))));
        s_fnAllowDarkModeForWindow = reinterpret_cast<pfnAllowDarkModeForWindow>(reinterpret_cast<void*>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(133))));
        s_fnShouldAppsUseDarkMode = reinterpret_cast<pfnShouldAppsUseDarkMode>(reinterpret_cast<void*>(GetProcAddress(hUxTheme, MAKEINTRESOURCEA(132))));

        if (s_fnSetPreferredAppMode) {
            s_fnSetPreferredAppMode(AllowDark);
        }
    }

    s_isDarkMode = QuerySystemDarkMode();
}

void Theme::EnsureResources() {
    Initialize();
    if (!s_isDarkMode) return;

    if (!s_hDarkBgBrush) s_hDarkBgBrush = CreateSolidBrush(COLOR_DARK_BG);
    if (!s_hDarkCtrlBrush) s_hDarkCtrlBrush = CreateSolidBrush(COLOR_DARK_CTRL);
    if (!s_hDarkComboSelBrush) s_hDarkComboSelBrush = CreateSolidBrush(RGB(60, 60, 60));
    if (!s_hAccentBrush) s_hAccentBrush = CreateSolidBrush(RGB(0, 103, 192));

    if (!s_hDarkEditBorderPen) s_hDarkEditBorderPen = CreatePen(PS_SOLID, 1, RGB(160, 160, 160));
    if (!s_hDarkComboBorderPen) s_hDarkComboBorderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
    if (!s_hDarkComboArrowPen) s_hDarkComboArrowPen = CreatePen(PS_SOLID, 2, RGB(180, 180, 180));
    if (!s_hDarkCheckboxBorderPen) s_hDarkCheckboxBorderPen = CreatePen(PS_SOLID, 1, RGB(130, 130, 130));
    if (!s_hAccentPen) s_hAccentPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
    if (!s_hWhiteCheckPen) s_hWhiteCheckPen = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
}

void Theme::ReleaseResources() {
    auto SafeDeleteObj = [](auto& hObj) noexcept {
        if (hObj) {
            DeleteObject(hObj);
            hObj = nullptr;
        }
    };

    SafeDeleteObj(s_hDarkBgBrush);
    SafeDeleteObj(s_hDarkCtrlBrush);
    SafeDeleteObj(s_hDarkComboSelBrush);
    SafeDeleteObj(s_hAccentBrush);

    SafeDeleteObj(s_hDarkEditBorderPen);
    SafeDeleteObj(s_hDarkComboBorderPen);
    SafeDeleteObj(s_hDarkComboArrowPen);
    SafeDeleteObj(s_hDarkCheckboxBorderPen);
    SafeDeleteObj(s_hAccentPen);
    SafeDeleteObj(s_hWhiteCheckPen);
}

void Theme::Cleanup() {
    ReleaseResources();
}

bool Theme::QuerySystemDarkMode() {
    if (s_fnShouldAppsUseDarkMode) {
        return s_fnShouldAppsUseDarkMode() != FALSE;
    }

    HKEY hKey = nullptr;
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

    const BOOL darkMode = s_isDarkMode ? TRUE : FALSE;

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

    Initialize();
    s_isDarkMode = QuerySystemDarkMode();
    if (s_isDarkMode) {
        EnsureResources();
    } else {
        ReleaseResources();
    }

    const bool dark = s_isDarkMode;
    ApplyWindowTheme(hwndParent);

    HWND hChild = GetWindow(hwndParent, GW_CHILD);
    while (hChild) {
        if (s_fnAllowDarkModeForWindow) {
            s_fnAllowDarkModeForWindow(hChild, dark ? TRUE : FALSE);
        }

        wchar_t className[64] = {};
        GetClassNameW(hChild, className, 64);
        if (_wcsicmp(className, L"ComboBox") == 0) {
            SetWindowTheme(hChild, dark ? L"DarkMode_CFD" : nullptr, nullptr);
            COMBOBOXINFO cbi = {};
            cbi.cbSize = sizeof(COMBOBOXINFO);
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
        if (wcscmp(str, L"ImmersiveColorSet") != 0 && wcscmp(str, L"WindowsTheme") != 0) {
            return;
        }
    }

    const bool wasDark = s_isDarkMode;
    const bool nowDark = QuerySystemDarkMode();
    if (lParam != 0 && wasDark == nowDark) {
        return;
    }

    s_isDarkMode = nowDark;
    ApplyThemeToWindowAndChildren(hwnd);
}

COLORREF Theme::GetDarkBackgroundColor() noexcept { return COLOR_DARK_BG; }
COLORREF Theme::GetDarkControlColor() noexcept { return COLOR_DARK_CTRL; }
COLORREF Theme::GetDarkTextColor() noexcept { return COLOR_DARK_TEXT; }
COLORREF Theme::GetDarkSubtextColor() noexcept { return COLOR_DARK_SUBTEXT; }
