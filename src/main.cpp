#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <iostream>
#include "resource.h"
#include "Config.hpp"
#include "ActionRunner.hpp"
#include "KeyboardHook.hpp"
#include "IconHelper.hpp"
#include "SettingsWindow.hpp"
#include "TrayIcon.hpp"
#include "SingleInstance.hpp"
#include "Theme.hpp"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")

static SettingsWindow* g_pSettingsWindow = nullptr;
static TrayIcon* g_pTrayIcon = nullptr;
static UINT g_uMsgTaskbarCreated = 0;

static void SetupConsoleOutput() {
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

static void OutputConsole(const std::wstring& text) {
    SetupConsoleOutput();
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!hOut || hOut == INVALID_HANDLE_VALUE) {
        hOut = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    }
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        DWORD mode = 0;
        if (GetConsoleMode(hOut, &mode)) {
            DWORD written = 0;
            WriteConsoleW(hOut, text.c_str(), static_cast<DWORD>(text.length()), &written, nullptr);
            return;
        } else {
            DWORD written = 0;
            int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), nullptr, 0, nullptr, nullptr);
            if (size > 0) {
                std::string utf8(size, 0);
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.length()), &utf8[0], size, nullptr, nullptr);
                WriteFile(hOut, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
                FlushFileBuffers(hOut);
            }
            return;
        }
    }
}

static void EnableHighDpi() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI* SetProcessDpiAwarenessContextProc)(DPI_AWARENESS_CONTEXT);
        auto pSetDpi = reinterpret_cast<SetProcessDpiAwarenessContextProc>(GetProcAddress(hUser32, "SetProcessDpiAwarenessContext"));
        if (pSetDpi) {
            pSetDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            return;
        }
    }
    SetProcessDPIAware();
}

static LRESULT CALLBACK MessageWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
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
        WORD id = LOWORD(wParam);
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
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

static bool IsCurrentProcessElevated() {
    BOOL isElevated = FALSE;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elevation = { 0 };
        DWORD dwSize = sizeof(elevation);
        if (GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
            isElevated = elevation.TokenIsElevated;
        }
        CloseHandle(hToken);
    }
    return isElevated != FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    EnableHighDpi();

    INITCOMMONCONTROLSEX icex = { sizeof(icex) };
    icex.dwICC = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icex);

    Theme::Initialize();

    bool startMinimized = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv) {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (_wcsicmp(arg.c_str(), L"--silent") == 0 || _wcsicmp(arg.c_str(), L"-s") == 0) {
                startMinimized = true;
            } else if (_wcsicmp(arg.c_str(), L"--test") == 0 || _wcsicmp(arg.c_str(), L"-t") == 0) {
                AppConfig cfg = ConfigManager::Load();
                OutputConsole(L"[Self-Test] Configuration loaded: " + cfg.actionType + L" -> " + cfg.targetPath + L"\n");
                LocalFree(argv);
                return 0;
            } else if (_wcsicmp(arg.c_str(), L"--install-startup") == 0) {
                ConfigManager::SetStartup(true);
                OutputConsole(L"Startup enabled.\n");
                LocalFree(argv);
                return 0;
            } else if (_wcsicmp(arg.c_str(), L"--uninstall-startup") == 0) {
                ConfigManager::SetStartup(false);
                OutputConsole(L"Startup disabled.\n");
                LocalFree(argv);
                return 0;
            }
        }
        LocalFree(argv);
    }

    if (!IsCurrentProcessElevated()) {
        wchar_t szExe[MAX_PATH];
        if (GetModuleFileNameW(nullptr, szExe, MAX_PATH)) {
            SHELLEXECUTEINFOW sei = { sizeof(sei) };
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

    IconHelper::Initialize();

    // Register message window class
    const wchar_t CLASS_NAME[] = L"CopilotRemapperMessageWindowClass";
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MessageWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassExW(&wc);

    HWND hMsgWnd = CreateWindowExW(0, CLASS_NAME, L"CopilotRemapperMessageWindow", 0, 0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);

    g_uMsgTaskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

    // Allow TaskbarCreated and WM_APP_TRAY_CALLBACK across UIPI integrity boundaries (Explorer is Medium IL, we are High IL)
    typedef BOOL(WINAPI* pfnChangeWindowMessageFilterEx)(HWND, UINT, DWORD, void*);
    typedef BOOL(WINAPI* pfnChangeWindowMessageFilter)(UINT, DWORD);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        auto pChangeFilterEx = reinterpret_cast<pfnChangeWindowMessageFilterEx>(GetProcAddress(hUser32, "ChangeWindowMessageFilterEx"));
        if (pChangeFilterEx) {
            pChangeFilterEx(hMsgWnd, g_uMsgTaskbarCreated, 1 /* MSGFLT_ALLOW */, nullptr);
            pChangeFilterEx(hMsgWnd, WM_APP_TRAY_CALLBACK, 1 /* MSGFLT_ALLOW */, nullptr);
        }
        auto pChangeFilter = reinterpret_cast<pfnChangeWindowMessageFilter>(GetProcAddress(hUser32, "ChangeWindowMessageFilter"));
        if (pChangeFilter) {
            pChangeFilter(g_uMsgTaskbarCreated, 1 /* MSGFLT_ADD */);
            pChangeFilter(WM_APP_TRAY_CALLBACK, 1 /* MSGFLT_ADD */);
        }
    }

    SettingsWindow settingsWindow;
    g_pSettingsWindow = &settingsWindow;

    TrayIcon trayIcon(hMsgWnd, WM_APP_TRAY_CALLBACK);
    g_pTrayIcon = &trayIcon;

    trayIcon.SetOnSettingsClicked([&settingsWindow]() {
        settingsWindow.Show();
    });
    trayIcon.SetOnExitClicked([]() {
        PostQuitMessage(0);
    });
    trayIcon.Add(IconHelper::GetAppIcon(), L"Copilot Key Remapper");

    KeyboardHook hook;
    hook.SetCallback([hMsgWnd]() {
        PostMessageW(hMsgWnd, WM_APP_COPILOT_PRESSED, 0, 0);
    });

    singleInstance.StartWakeupListener([hMsgWnd]() {
        PostMessageW(hMsgWnd, WM_APP_WAKEUP, 0, 0);
    });

    if (!startMinimized) {
        settingsWindow.Show();
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (settingsWindow.GetHwnd() && IsWindow(settingsWindow.GetHwnd()) && IsDialogMessageW(settingsWindow.GetHwnd(), &msg)) {
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    trayIcon.Remove();
    hook.Uninstall();
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
