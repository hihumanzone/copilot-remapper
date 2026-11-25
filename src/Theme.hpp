#pragma once

#include <windows.h>

class Theme {
public:
    static void Initialize();
    static void EnsureResources();
    static void ReleaseResources();
    static void Cleanup();
    static bool IsSystemDarkMode() noexcept { return s_isDarkMode; }
    static void ApplyWindowTheme(HWND hwnd);
    static void ApplyThemeToWindowAndChildren(HWND hwndParent);
    static void HandleSettingChange(HWND hwnd, LPARAM lParam);

    static HBRUSH GetDarkBackgroundBrush() noexcept { return s_hDarkBgBrush; }
    static HBRUSH GetDarkControlBrush() noexcept { return s_hDarkCtrlBrush; }
    static HBRUSH GetDarkComboSelectionBrush() noexcept { return s_hDarkComboSelBrush; }
    static HBRUSH GetAccentBrush() noexcept { return s_hAccentBrush; }

    static HPEN GetDarkEditBorderPen() noexcept { return s_hDarkEditBorderPen; }
    static HPEN GetDarkComboBorderPen() noexcept { return s_hDarkComboBorderPen; }
    static HPEN GetDarkComboArrowPen() noexcept { return s_hDarkComboArrowPen; }
    static HPEN GetDarkCheckboxBorderPen() noexcept { return s_hDarkCheckboxBorderPen; }
    static HPEN GetAccentPen() noexcept { return s_hAccentPen; }
    static HPEN GetWhiteCheckPen() noexcept { return s_hWhiteCheckPen; }

    static COLORREF GetDarkBackgroundColor() noexcept;
    static COLORREF GetDarkControlColor() noexcept;
    static COLORREF GetDarkTextColor() noexcept;
    static COLORREF GetDarkSubtextColor() noexcept;

private:
    static bool QuerySystemDarkMode();

    static bool s_initialized;
    static bool s_isDarkMode;

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
