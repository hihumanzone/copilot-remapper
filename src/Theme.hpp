#pragma once

#include <windows.h>

class Theme {
public:
    static void Initialize();
    static void Cleanup();
    static bool IsSystemDarkMode();
    static void ApplyWindowTheme(HWND hwnd);
    static void ApplyThemeToWindowAndChildren(HWND hwndParent);
    static void HandleSettingChange(HWND hwnd, LPARAM lParam);

    static HBRUSH GetDarkBackgroundBrush();
    static HBRUSH GetDarkControlBrush();
    static COLORREF GetDarkBackgroundColor();
    static COLORREF GetDarkControlColor();
    static COLORREF GetDarkTextColor();
    static COLORREF GetDarkSubtextColor();

private:
    static HBRUSH s_hDarkBgBrush;
    static HBRUSH s_hDarkCtrlBrush;
};
