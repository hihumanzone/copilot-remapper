#include "KeyboardHook.hpp"
#include "ActionRunner.hpp"
#include "Config.hpp"

std::atomic<HHOOK> KeyboardHook::s_hook{ nullptr };
std::atomic<KeyboardHook::NotifyCallback> KeyboardHook::s_notifyFn{ nullptr };
std::atomic<void*> KeyboardHook::s_notifyCtx{ nullptr };
bool KeyboardHook::s_hasTriggered = false;
ULONGLONG KeyboardHook::s_lastTriggerTick = 0;

namespace {

struct HookThreadStartData {
    HANDLE hReadyEvent;
    bool installed;
};

} // namespace

KeyboardHook::KeyboardHook() {
    Install();
}

KeyboardHook::~KeyboardHook() {
    Uninstall();
}

bool KeyboardHook::Install() {
    if (m_hThread) {
        return s_hook.load(std::memory_order_acquire) != nullptr;
    }

    HANDLE hReadyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!hReadyEvent) return false;

    HookThreadStartData startData{ hReadyEvent, false };
    m_hThread = CreateThread(
        nullptr,
        65536, // 64 KB stack reservation for dedicated hook pump
        HookThreadProc,
        &startData,
        STACK_SIZE_PARAM_IS_A_RESERVATION,
        &m_threadId);

    if (!m_hThread) {
        CloseHandle(hReadyEvent);
        return false;
    }

    WaitForSingleObject(hReadyEvent, INFINITE);
    CloseHandle(hReadyEvent);

    if (!startData.installed) {
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
        m_threadId = 0;
        return false;
    }
    return true;
}

void KeyboardHook::Uninstall() {
    if (m_hThread) {
        if (m_threadId != 0) {
            PostThreadMessageW(m_threadId, WM_QUIT, 0, 0);
        }
        WaitForSingleObject(m_hThread, 2000);
        CloseHandle(m_hThread);
        m_hThread = nullptr;
        m_threadId = 0;
    }
}

bool KeyboardHook::IsInstalled() const noexcept {
    return s_hook.load(std::memory_order_acquire) != nullptr;
}

void KeyboardHook::SetCallback(NotifyCallback callback, void* context) noexcept {
    s_notifyCtx.store(context, std::memory_order_relaxed);
    s_notifyFn.store(callback, std::memory_order_release);
}

DWORD WINAPI KeyboardHook::HookThreadProc(LPVOID lpParam) {
    auto* startData = static_cast<HookThreadStartData*>(lpParam);

    // Keep hook thread responsive and ensure its message queue exists before signaling
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    MSG msg;
    PeekMessageW(&msg, nullptr, 0, 0, PM_NOREMOVE);

    s_hasTriggered = false;
    s_lastTriggerTick = 0;

    HHOOK hHook = SetWindowsHookExW(WH_KEYBOARD_LL, HookCallback, GetModuleHandleW(nullptr), 0);
    s_hook.store(hHook, std::memory_order_release);
    startData->installed = (hHook != nullptr);
    SetEvent(startData->hReadyEvent);

    if (!hHook) {
        return 1;
    }

    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    s_hook.store(nullptr, std::memory_order_release);
    UnhookWindowsHookEx(hHook);
    return 0;
}

LRESULT CALLBACK KeyboardHook::HookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode < 0) {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    const auto* kb = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    // Intercept physical Copilot key: VK_F23 (0x86) or OEM VK_F24 (0x87)
    if (kb->vkCode != 0x86 && kb->vkCode != 0x87) [[likely]] {
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        ActionRunner::MaskStartMenu();

        const ULONGLONG now = GetTickCount64();
        const ULONGLONG debounce = ConfigManager::GetDebounceMs();

        if (!s_hasTriggered || (now - s_lastTriggerTick) >= debounce) {
            s_hasTriggered = true;
            s_lastTriggerTick = now;
            ActionRunner::ExecuteSaved();

            if (NotifyCallback fn = s_notifyFn.load(std::memory_order_acquire)) {
                fn(s_notifyCtx.load(std::memory_order_relaxed));
            }
        }
        return 1;
    }

    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
        return 1;
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}
