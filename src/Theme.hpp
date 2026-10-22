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
    static HBRUSH GetDarkComboSelectionBrush();
    static HBRUSH GetAccentBrush();

    static HPEN GetDarkEditBorderPen();
    static HPEN GetDarkComboBorderPen();
    static HPEN GetDarkComboArrowPen();
    static HPEN GetDarkCheckboxBorderPen();
    static HPEN GetAccentPen();
    static HPEN GetWhiteCheckPen();

    static COLORREF GetDarkBackgroundColor();
    static COLORREF GetDarkControlColor();
    static COLORREF GetDarkTextColor();
    static COLORREF GetDarkSubtextColor();

private:
    static HBRUSH s_hDarkBgBrush;
    static HBRUSH s_hDarkCtrlBrush;
    static HBRUSH s_hDarkComboSelBrush;
    static HBRUSH s_hAccentBrush;

    static HPEN s_hDarkEditBorderPen;
    static HPEN s_hDarkComboBorderPen;
    static HPEN s_hDarkComboArrowPen;
    static HPEN s_hDarkCheckboxBorderPen;
    static HPEN s_hAccentPen;
    static HPEN s_hWhiteCheckPen;
};
