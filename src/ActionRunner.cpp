#include "ActionRunner.hpp"
#include "ProcessManager.hpp"
#include "Config.hpp"
#include <windows.h>
#include <shellapi.h>
#include <thread>
#include <vector>

void ActionRunner::MaskStartMenu() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0xE8;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0xE8;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void ActionRunner::ReleaseModifiers() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LSHIFT;
    inputs[0].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_LWIN;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

std::wstring ActionRunner::ExpandEnv(const std::wstring& input) {
    if (input.empty()) return L"";
    DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0) return input;
    std::wstring result(needed, 0);
    ExpandEnvironmentStringsW(input.c_str(), &result[0], needed);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

static void LogActivity(const wchar_t* msg) {
    std::wstring logPath = ConfigManager::GetAppDirectory() + L"\\copilot_activity.log";
    FILE* f = _wfopen(logPath.c_str(), L"a, ccs=UTF-8");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        fwprintf(f, L"[%04d-%02d-%02d %02d:%02d:%02d.%03d] %ls\n",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 msg);
        fclose(f);
    }
}

void ActionRunner::Execute(const AppConfig& config) {
    if (config.targetPath.empty()) return;

    AppConfig cfg = config;

    std::thread([cfg]() {
        try {
            struct DesktopScope {
                HDESK h = nullptr;
                DesktopScope() {
                    h = OpenInputDesktop(0, FALSE, GENERIC_ALL);
                    if (h) SetThreadDesktop(h);
                }
                ~DesktopScope() {
                    if (h) CloseDesktop(h);
                }
            } desktopScope;

            std::wstring target = ExpandEnv(cfg.targetPath);
            std::wstring args = ExpandEnv(cfg.arguments);
            std::wstring workDir = ExpandEnv(cfg.workingDirectory);
            LPCWSTR lpWorkDir = workDir.empty() ? nullptr : workDir.c_str();

            if (cfg.actionType == L"OpenUrl") {
                ReleaseModifiers();
                if (target.find(L"://") == std::wstring::npos) {
                    target = L"https://" + target;
                }
                LogActivity((L"OpenUrl: " + target).c_str());
                ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            } else if (cfg.actionType == L"RunCommand") {
                ReleaseModifiers();
                std::wstring cmd = L"-NoProfile -ExecutionPolicy Bypass -Command \"" + target + L"\"";
                SHELLEXECUTEINFOW sei = { sizeof(sei) };
                sei.fMask = cfg.runAsAdmin ? 0 : SEE_MASK_FLAG_NO_UI;
                sei.lpVerb = cfg.runAsAdmin ? L"runas" : L"open";
                sei.lpFile = L"powershell.exe";
                sei.lpParameters = cmd.c_str();
                sei.lpDirectory = lpWorkDir;
                sei.nShow = SW_HIDE;
                LogActivity((L"RunCommand: " + target).c_str());
                ShellExecuteExW(&sei);
            } else {
                // LaunchApp
                // 1. If target is already in foreground, do nothing and do not inject modifiers
                if (ProcessManager::IsForegroundProcessTarget(target)) {
                    LogActivity(L"Target application is already active in the foreground. No action needed.");
                    return;
                }

                // 2. Search for existing running window
                bool foundElevated = false;
                HWND hExisting = ProcessManager::FindTargetWindow(target, cfg.runAsAdmin, foundElevated);

                if (cfg.runAsAdmin) {
                    if (hExisting && foundElevated) {
                        // Already elevated! Bring to foreground without another UAC prompt
                        LogActivity(L"Found existing elevated instance. Bringing to foreground directly (no UAC).");
                        ReleaseModifiers();
                        ProcessManager::ActivateWindow(hExisting);
                        return;
                    }

                    // Not running elevated: display normal UAC confirmation prompt
                    LogActivity(L"No existing elevated instance found. Prompting UAC to launch as administrator.");
                    ReleaseModifiers();
                    SHELLEXECUTEINFOW sei = { sizeof(sei) };
                    sei.fMask = 0;
                    sei.lpVerb = L"runas";
                    sei.lpFile = target.c_str();
                    sei.lpParameters = args.empty() ? nullptr : args.c_str();
                    sei.lpDirectory = lpWorkDir;
                    sei.nShow = SW_SHOWNORMAL;
                    ShellExecuteExW(&sei);
                    return;
                } else {
                    if (hExisting) {
                        // Standard instance found: bring to foreground without duplicate instance
                        LogActivity(L"Found existing instance. Bringing to foreground directly.");
                        ReleaseModifiers();
                        ProcessManager::ActivateWindow(hExisting);
                        return;
                    }

                    // Launch standard instance
                    LogActivity(L"No existing instance found. Launching standard instance.");
                    ReleaseModifiers();
                    SHELLEXECUTEINFOW sei = { sizeof(sei) };
                    sei.fMask = SEE_MASK_FLAG_NO_UI;
                    sei.lpVerb = nullptr;
                    sei.lpFile = target.c_str();
                    sei.lpParameters = args.empty() ? nullptr : args.c_str();
                    sei.lpDirectory = lpWorkDir;
                    sei.nShow = SW_SHOWNORMAL;
                    ShellExecuteExW(&sei);
                    return;
                }
            }
        } catch (...) {
        }
    }).detach();
}
