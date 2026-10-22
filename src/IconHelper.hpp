#pragma once

#include <windows.h>

class IconHelper {
public:
    static void Initialize();
    static void Shutdown();
    static HICON GetAppIcon();

private:
    static HICON s_appIcon;
    static ULONG_PTR s_gdiplusToken;
};
