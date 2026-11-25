#pragma once

#include <windows.h>

class IconHelper {
public:
    static void Initialize() noexcept;
    static void Shutdown() noexcept;
    static HICON GetAppIcon() noexcept;
    static HICON GetSmallAppIcon() noexcept;

private:
    static HICON s_appIcon;
    static HICON s_smallAppIcon;
    static bool s_isCustomCreated;
};
