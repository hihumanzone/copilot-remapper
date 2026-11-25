#include <windows.h>
#include <shellapi.h>
#include <string>
#include <string_view>
#include "resource.h"
#include "Config.hpp"
#include "ActionRunner.hpp"
#include "KeyboardHook.hpp"
#include "IconHelper.hpp"
#include "SettingsWindow.hpp"
#include "TrayIcon.hpp"
#include "SingleInstance.hpp"
#include "Theme.hpp"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "taskschd.lib")
#endif

namespace {

SettingsWindow* g_pSettingsWindow = nullptr;
TrayIcon* g_pTrayIcon = nullptr;
UINT g_uMsgTaskbarCreated = 0;

void SetupConsoleOutput() {
    HANDLE hExistingOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hExistingOut || hExistingOut == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            HANDLE hOut = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hOut != INVALID_HANDLE_VALUE) {
                SetStdHandle(STD_OUTPUT_HANDLE, hOut);
            }
            HANDLE hErr = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hErr != INVALID_HANDLE_VALUE) {
                SetStdHandle(STD_ERROR_HANDLE, hErr);
            }
        }
    }
}

void OutputConsole(std::wstring_view text) {
    SetupConsoleOutput();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) {
        hOut = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            DWORD written = 0;
            WriteConsoleW(hOut, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            return;
        }
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        if (size > 0) {
            std::string utf8(static_cast<size_t>(size), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(hOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            FlushFileBuffers(hOut);
        }
    }
}

void EnableHighDpi() noexcept {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        using SetProcessDpiAwarenessContextProc = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto pSetDpi = reinterpret_cast<SetProcessDpiAwarenessContextProc>(reinterpret_cast<void*>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext")));
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware();
}

LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    if (g_uMsgTaskbarCreated != 0 && uMsg == g_uMsgTaskbarCreated) {
        if (g_pTrayIcon) {
            g_pTrayIcon->OnTaskbarCreated();
        }
        return 0;
    }

    switch (uMsg) {
    case WM_APP_TRAY_CALLBACK:
        if (g_pTrayIcon) {
            g_pTrayIcon->HandleMessage(wParam, lParam);
        }
        return 0;

    case WM_TIMER:
        if (wParam == IDT_TRAY_RETRY && g_pTrayIcon) {
            g_pTrayIcon->OnRetryTimer();
            return 0;
        }
        break;

    case WM_APP_COPILOT_PRESSED:
        if (g_pSettingsWindow) {
            g_pSettingsWindow->OnCopilotKeyPressed();
        }
        return 0;

    case WM_APP_WAKEUP:
        if (g_pSettingsWindow) {
            g_pSettingsWindow->Wakeup();
        }
        if (g_pTrayIcon) {
            g_pTrayIcon->EnsureAdded();
        }
        return 0;

    case WM_COMMAND: {
        const WORD id = LOWORD(wParam);
        if (id == IDM_TRAY_SETTINGS && g_pSettingsWindow) {
            g_pSettingsWindow->Show();
        } else if (id == IDM_TRAY_EXIT) {
            PostQuitMessage(0);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

bool IsCurrentProcessElevated() noexcept {
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation = {};
        DWORD dwSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return isElevated != FALSE;
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    bool startMinimized = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            const wchar_t* arg = argv[i];
            if (_wcsicmp(arg, L"--silent") == 0 || _wcsicmp(arg, L"-s") == 0) {
                startMinimized = true;
            } else if (_wcsicmp(arg, L"--test") == 0 || _wcsicmp(arg, L"-t") == 0) {
                AppConfig cfg = ConfigManager::Load();
                std::wstring line = L"[Self-Test] Configuration loaded: " + cfg.actionType + L" -> " + cfg.targetPath + L"\n";
                OutputConsole(line);
                LocalFree(argv);
                return 0;
            } else if (_wcsicmp(arg, L"--install-startup") == 0) {
                ConfigManager::SetStartup(true);
                OutputConsole(L"Startup enabled.\n");
                LocalFree(argv);
                return 0;
            } else if (_wcsicmp(arg, L"--uninstall-startup") == 0) {
                ConfigManager::SetStartup(false);
                OutputConsole(L"Startup disabled.\n");
                LocalFree(argv);
                return 0;
            }
        }
        LocalFree(argv);
    }

    if (!IsCurrentProcessElevated()) {
        wchar_t szExe[1024];
        if (GetModuleFileNameW(nullptr, szExe, 1024)) {
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.lpVerb = L"runas";
            sei.lpFile = szExe;
            sei.lpParameters = lpCmdLine;
            sei.nShow = nCmdShow;
            if (ShellExecuteExW(&sei)) {
                return 0;
            }
        }
    }

    SingleInstance singleInstance;
    if (!singleInstance.TryAcquire()) {
        SingleInstance::SignalExistingInstance();
        return 0;
    }

    EnableHighDpi();
    Theme::Initialize();
    IconHelper::Initialize();

    // Warm config cache once before hook starts so first keypress has 0 disk I/O
    ConfigManager::GetResolvedSnapshot();

    constexpr wchar_t CLASS_NAME[] = L"CopilotRemapperMessageWindowClass";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hMsgWnd = CreateWindowExW(0, CLASS_NAME, L"CopilotRemapperMessageWindow", 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    g_uMsgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Allow TaskbarCreated and WM_APP_TRAY_CALLBACK across UIPI integrity boundaries
    ChangeWindowMessageFilterEx(hMsgWnd, g_uMsgTaskbarCreated, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(hMsgWnd, WM_APP_TRAY_CALLBACK, MSGFLT_ALLOW, nullptr);

    SettingsWindow settingsWindow;
    g_pSettingsWindow = &settingsWindow;

    TrayIcon trayIcon(hMsgWnd, WM_APP_TRAY_CALLBACK);
    g_pTrayIcon = &trayIcon;

    trayIcon.SetOnSettingsClicked([](void* ctx) {
        static_cast<SettingsWindow*>(ctx)->Show();
    }, &settingsWindow);
    trayIcon.SetOnExitClicked([](void*) {
        PostQuitMessage(0);
    }, nullptr);
    trayIcon.Add(IconHelper::GetSmallAppIcon(), L"Copilot Key Remapper");

    ActionRunner::Initialize();

    KeyboardHook hook;
    hook.SetCallback([](void* ctx) {
        if (SettingsWindow::IsOpen()) {
            PostMessageW(static_cast<HWND>(ctx), WM_APP_COPILOT_PRESSED, 0, 0);
        }
    }, hMsgWnd);

    singleInstance.StartWakeupListener([](void* ctx) {
        PostMessageW(static_cast<HWND>(ctx), WM_APP_WAKEUP, 0, 0);
    }, hMsgWnd);

    if (!startMinimized) {
        settingsWindow.Show();
    } else {
        SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        HWND hSettings = settingsWindow.GetHwnd();
        if (hSettings && IsDialogMessageW(hSettings, &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    trayIcon.Remove();
    hook.Uninstall();
    ActionRunner::Shutdown();
    singleInstance.StopWakeupListener();
    IconHelper::Shutdown();
    Theme::Cleanup();

    if (hMsgWnd && IsWindow(hMsgWnd)) {
        DestroyWindow(hMsgWnd);
    }
    UnregisterClassW(CLASS_NAME, hInstance);

    g_pSettingsWindow = nullptr;
    g_pTrayIcon = nullptr;

    return static_cast<int>(msg.wParam);
}
