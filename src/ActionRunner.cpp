#include "ActionRunner.hpp"
#include "ProcessManager.hpp"
#include "Config.hpp"
#include <windows.h>
#include <shellapi.h>
#include <memory>
#include <vector>

void ActionRunner::MaskStartMenu() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0xE8; // VK_UNASSIGNED_E8 masks Windows Start Menu cleanly
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0xE8;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void ActionRunner::ReleaseModifiers() {
    bool shiftDown = (GetKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_LSHIFT) & 0x8000);
    bool winDown = (GetKeyState(VK_LWIN) & 0x8000) || (GetAsyncKeyState(VK_LWIN) & 0x8000);

    if (shiftDown || winDown) {
        INPUT inputs[2] = {};
        UINT count = 0;
        if (shiftDown) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = VK_LSHIFT;
            inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
            count++;
        }
        if (winDown) {
            inputs[count].type = INPUT_KEYBOARD;
            inputs[count].ki.wVk = VK_LWIN;
            inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
            count++;
        }
        SendInput(count, inputs, sizeof(INPUT));
    }
}

std::wstring ActionRunner::ExpandEnv(const std::wstring& input) {
    if (input.empty()) return L"";
    if (input.find(L'%') == std::wstring::npos) return input;

    DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0) return input;
    std::wstring result(needed, 0);
    ExpandEnvironmentStringsW(input.c_str(), &result[0], needed);
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

static inline void LogDebugActivity(const wchar_t* msg) {
#ifdef _DEBUG
    OutputDebugStringW(L"[CopilotRemapper] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
#else
    (void)msg;
#endif
}

VOID CALLBACK ActionRunner::ThreadPoolActionCallback(PTP_CALLBACK_INSTANCE /*Instance*/, PVOID Context) {
    std::unique_ptr<AppConfig> pCfg(static_cast<AppConfig*>(Context));
    if (!pCfg) return;

    try {
        const AppConfig& cfg = *pCfg;
        std::wstring target = ExpandEnv(cfg.targetPath);
        std::wstring args = ExpandEnv(cfg.arguments);
        std::wstring workDir = ExpandEnv(cfg.workingDirectory);
        LPCWSTR lpWorkDir = workDir.empty() ? nullptr : workDir.c_str();

        if (cfg.actionType == L"OpenUrl") {
            ReleaseModifiers();
            if (target.find(L"://") == std::wstring::npos) {
                target = L"https://" + target;
            }
            LogDebugActivity((L"OpenUrl: " + target).c_str());
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
            LogDebugActivity((L"RunCommand: " + target).c_str());
            ShellExecuteExW(&sei);
        } else {
            // LaunchApp
            // 1. If target is already active in foreground, do nothing
            if (ProcessManager::IsForegroundProcessTarget(target)) {
                LogDebugActivity(L"Target application is already active in the foreground.");
                return;
            }

            // 2. Search for existing running window
            bool foundElevated = false;
            HWND hExisting = ProcessManager::FindTargetWindow(target, cfg.runAsAdmin, foundElevated);

            if (cfg.runAsAdmin) {
                if (hExisting && foundElevated) {
                    // Already elevated: bring to foreground directly without another UAC prompt
                    LogDebugActivity(L"Found existing elevated instance. Bringing to foreground directly.");
                    ReleaseModifiers();
                    ProcessManager::ActivateWindow(hExisting);
                    return;
                }

                // Not running elevated: display normal UAC confirmation prompt
                LogDebugActivity(L"Prompting UAC to launch as administrator.");
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
                    LogDebugActivity(L"Found existing instance. Bringing to foreground directly.");
                    ReleaseModifiers();
                    ProcessManager::ActivateWindow(hExisting);
                    return;
                }

                // Launch standard instance
                LogDebugActivity(L"Launching standard instance.");
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
}

void ActionRunner::Execute(const AppConfig& config) {
    if (config.targetPath.empty()) return;

    auto* pContext = new AppConfig(config);
    if (!TrySubmitThreadpoolCallback(ThreadPoolActionCallback, pContext, nullptr)) {
        // Fallback: run on caller or delete
        ThreadPoolActionCallback(nullptr, pContext);
    }
}
