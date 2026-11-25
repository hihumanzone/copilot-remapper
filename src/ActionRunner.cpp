#include "ActionRunner.hpp"
#include "ComHelpers.hpp"
#include "ProcessManager.hpp"
#include <shellapi.h>
#include <string>

PTP_WORK ActionRunner::s_tpWork = nullptr;

namespace {

inline void LogDebugActivity(const wchar_t* msg) noexcept {
#ifdef _DEBUG
    OutputDebugStringW(L"[CopilotRemapper] ");
    OutputDebugStringW(msg);
    OutputDebugStringW(L"\n");
#else
    (void)msg;
#endif
}

} // namespace

void ActionRunner::Initialize() noexcept {
    if (!s_tpWork) {
        s_tpWork = CreateThreadpoolWork(SavedWorkCallback, nullptr, nullptr);
    }
}

void ActionRunner::Shutdown() noexcept {
    if (s_tpWork) {
        WaitForThreadpoolWorkCallbacks(s_tpWork, TRUE);
        CloseThreadpoolWork(s_tpWork);
        s_tpWork = nullptr;
    }
}

void ActionRunner::MaskStartMenu() noexcept {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = 0xE8; // VK_UNASSIGNED_E8 masks Windows Start Menu cleanly
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 0xE8;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

void ActionRunner::ReleaseModifiers() noexcept {
    const bool shiftDown = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
    const bool winDown = (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0;

    if (!shiftDown && !winDown) return;

    INPUT inputs[2] = {};
    UINT count = 0;
    if (shiftDown) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_LSHIFT;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }
    if (winDown) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_LWIN;
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        ++count;
    }
    SendInput(count, inputs, sizeof(INPUT));
}

void ActionRunner::ExecuteResolved(const ResolvedConfig& rc) {
    if (rc.expandedTarget.empty()) return;

    const wchar_t* lpWorkDir = rc.expandedWorkDir.empty() ? nullptr : rc.expandedWorkDir.c_str();

    if (rc.isOpenUrl) {
        ReleaseModifiers();
        CoInitGuard co;

        if (rc.expandedTarget.find(L"://") == std::wstring::npos) {
            std::wstring url = L"https://" + rc.expandedTarget;
            LogDebugActivity((L"OpenUrl: " + url).c_str());
            ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        } else {
            LogDebugActivity((L"OpenUrl: " + rc.expandedTarget).c_str());
            ShellExecuteW(nullptr, L"open", rc.expandedTarget.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return;
    }

    if (rc.isRunCommand) {
        ReleaseModifiers();
        CoInitGuard co;

        std::wstring cmd;
        cmd.reserve(rc.expandedTarget.size() + 48);
        cmd = L"-NoProfile -ExecutionPolicy Bypass -Command \"";
        cmd += rc.expandedTarget;
        cmd += L"\"";

        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = rc.config.runAsAdmin ? 0 : SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = rc.config.runAsAdmin ? L"runas" : L"open";
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = cmd.c_str();
        sei.lpDirectory = lpWorkDir;
        sei.nShow = SW_HIDE;
        LogDebugActivity((L"RunCommand: " + rc.expandedTarget).c_str());
        ShellExecuteExW(&sei);
        return;
    }

    // LaunchApp
    if (ProcessManager::IsForegroundProcessTargetResolved(rc.targetExe, rc.cleanTarget)) {
        LogDebugActivity(L"Target application is already active in the foreground.");
        return;
    }

    bool foundElevated = false;
    HWND hExisting = ProcessManager::FindTargetWindowResolved(rc.targetExe, rc.cleanTarget, rc.config.runAsAdmin, foundElevated);

    if (rc.config.runAsAdmin) {
        if (hExisting && foundElevated) {
            LogDebugActivity(L"Found existing elevated instance. Bringing to foreground directly.");
            ReleaseModifiers();
            ProcessManager::ActivateWindow(hExisting);
            return;
        }

        LogDebugActivity(L"Prompting UAC to launch as administrator.");
        ReleaseModifiers();
        CoInitGuard co;
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = 0;
        sei.lpVerb = L"runas";
        sei.lpFile = rc.expandedTarget.c_str();
        sei.lpParameters = rc.expandedArgs.empty() ? nullptr : rc.expandedArgs.c_str();
        sei.lpDirectory = lpWorkDir;
        sei.nShow = SW_SHOWNORMAL;
        ShellExecuteExW(&sei);
        return;
    }

    if (hExisting) {
        LogDebugActivity(L"Found existing instance. Bringing to foreground directly.");
        ReleaseModifiers();
        ProcessManager::ActivateWindow(hExisting);
        return;
    }

    LogDebugActivity(L"Launching standard instance.");
    ReleaseModifiers();
    CoInitGuard co;
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = nullptr;
    sei.lpFile = rc.expandedTarget.c_str();
    sei.lpParameters = rc.expandedArgs.empty() ? nullptr : rc.expandedArgs.c_str();
    sei.lpDirectory = lpWorkDir;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

VOID CALLBACK ActionRunner::SavedWorkCallback(PTP_CALLBACK_INSTANCE /*Instance*/, PVOID /*Context*/, PTP_WORK /*Work*/) {
    RefPtr<const ResolvedConfig> snap = ConfigManager::GetResolvedSnapshot();
    if (snap) {
        ExecuteResolved(*snap);
    }
}

VOID CALLBACK ActionRunner::AdHocWorkCallback(PTP_CALLBACK_INSTANCE /*Instance*/, PVOID Context) {
    RefPtr<const ResolvedConfig> pResolved(static_cast<const ResolvedConfig*>(Context));
    if (pResolved) {
        ExecuteResolved(*pResolved);
    }
}

void ActionRunner::ExecuteSaved() noexcept {
    if (!ConfigManager::HasTarget()) return;

    if (s_tpWork) {
        SubmitThreadpoolWork(s_tpWork);
    } else {
        SavedWorkCallback(nullptr, nullptr, nullptr);
    }
}

void ActionRunner::Execute(const AppConfig& config) {
    if (config.targetPath.empty()) return;

    auto* pContext = new ResolvedConfig(ConfigManager::ResolveConfig(config));
    if (!TrySubmitThreadpoolCallback(AdHocWorkCallback, pContext, nullptr)) {
        AdHocWorkCallback(nullptr, pContext);
    }
}
