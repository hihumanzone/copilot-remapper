#include "IconHelper.hpp"
#include <gdiplus.h>

#pragma comment(lib, "gdiplus.lib")

HICON IconHelper::s_appIcon = nullptr;
ULONG_PTR IconHelper::s_gdiplusToken = 0;

void IconHelper::Initialize() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, nullptr);
}

void IconHelper::Shutdown() {
    if (s_appIcon) {
        DestroyIcon(s_appIcon);
        s_appIcon = nullptr;
    }
    if (s_gdiplusToken) {
        Gdiplus::GdiplusShutdown(s_gdiplusToken);
        s_gdiplusToken = 0;
    }
}

HICON IconHelper::GetAppIcon() {
    if (s_appIcon) return s_appIcon;

    using namespace Gdiplus;

    const int size = 32;
    Bitmap bmp(size, size, PixelFormat32bppARGB);
    Graphics g(&bmp);

    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.Clear(Color(0, 0, 0, 0));

    GraphicsPath path;
    int r = 6;
    int d = r * 2;
    int x = 1;
    int y = 1;
    int w = 29;
    int h = 29;

    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();

    SolidBrush bgBrush(Color(255, 0, 103, 192));
    g.FillPath(&bgBrush, &path);

    Pen borderPen(Color(255, 180, 215, 255), 1.5f);
    g.DrawPath(&borderPen, &path);

    FontFamily fontFamily(L"Segoe UI");
    Font font(&fontFamily, 14.0f, FontStyleBold, UnitPixel);
    SolidBrush textBrush(Color(255, 255, 255, 255));

    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);

    RectF layoutRect(0.0f, 1.0f, static_cast<REAL>(size), static_cast<REAL>(size));
    g.DrawString(L"C", 1, &font, layoutRect, &sf, &textBrush);

    bmp.GetHICON(&s_appIcon);
    return s_appIcon;
}
