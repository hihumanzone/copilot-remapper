#include "IconHelper.hpp"
#include "../resource.h"

HICON IconHelper::s_appIcon = nullptr;
bool IconHelper::s_isCustomCreated = false;

void IconHelper::Initialize() {
    // Native GDI / Win32 resource loading replaces GDI+ completely.
    // Zero runtime initialization delay and zero external framework DLLs.
}

void IconHelper::Shutdown() {
    if (s_appIcon && s_isCustomCreated) {
        DestroyIcon(s_appIcon);
    }
    s_appIcon = nullptr;
    s_isCustomCreated = false;
}

HICON IconHelper::GetAppIcon() {
    if (s_appIcon) return s_appIcon;

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    s_appIcon = reinterpret_cast<HICON>(LoadImageW(
        hInst,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        0, 0,
        LR_DEFAULTSIZE | LR_SHARED
    ));

    if (s_appIcon) {
        s_isCustomCreated = false;
        return s_appIcon;
    }

    // High-performance native GDI fallback (if binary was built without embedded resource)
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = 32;
    bmi.bmiHeader.biHeight = 32;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hColorBmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    HBITMAP hMaskBmp = CreateBitmap(32, 32, 1, 1, nullptr);

    if (hColorBmp && hdcMem) {
        HGDIOBJ hOldBmp = SelectObject(hdcMem, hColorBmp);

        RECT rc = { 0, 0, 32, 32 };
        HBRUSH hBg = CreateSolidBrush(RGB(0, 103, 192));
        FillRect(hdcMem, &rc, hBg);
        DeleteObject(hBg);

        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(180, 215, 255));
        HGDIOBJ hOldPen = SelectObject(hdcMem, hPen);
        HGDIOBJ hOldBrush = SelectObject(hdcMem, GetStockObject(HOLLOW_BRUSH));
        RoundRect(hdcMem, 0, 0, 31, 31, 8, 8);
        SelectObject(hdcMem, hOldBrush);
        SelectObject(hdcMem, hOldPen);
        DeleteObject(hPen);

        SetBkMode(hdcMem, TRANSPARENT);
        SetTextColor(hdcMem, RGB(255, 255, 255));
        HFONT hFont = CreateFontW(
            20, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
        );
        HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
        DrawTextW(hdcMem, L"C", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdcMem, hOldFont);
        DeleteObject(hFont);

        SelectObject(hdcMem, hOldBmp);

        ICONINFO ii = {};
        ii.fIcon = TRUE;
        ii.hbmColor = hColorBmp;
        ii.hbmMask = hMaskBmp;
        s_appIcon = CreateIconIndirect(&ii);
        s_isCustomCreated = (s_appIcon != nullptr);
    }

    if (hColorBmp) DeleteObject(hColorBmp);
    if (hMaskBmp) DeleteObject(hMaskBmp);
    if (hdcMem) DeleteDC(hdcMem);
    if (hdcScreen) ReleaseDC(nullptr, hdcScreen);

    return s_appIcon;
}
