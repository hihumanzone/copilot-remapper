#include "KeyboardHook.hpp"
#include "ActionRunner.hpp"
#include "Config.hpp"

HHOOK KeyboardHook::s_hook = nullptr;
std::function<void()> KeyboardHook::s_callback = nullptr;
bool KeyboardHook::s_hasTriggered = false;
ULONGLONG KeyboardHook::s_lastTriggerTick = 0;

KeyboardHook::KeyboardHook() {
    Install();
}

KeyboardHook::~KeyboardHook() {
    Uninstall();
}

bool KeyboardHook::Install() {
    if (s_hook) return true;
    s_hook = SetWindowsHookExW(WH_KEYBOARD_LL, HookCallback, GetModuleHandleW(nullptr), 0);
    return s_hook != nullptr;
}

void KeyboardHook::Uninstall() {
    if (s_hook) {
        UnhookWindowsHookEx(s_hook);
        s_hook = nullptr;
    }
}

bool KeyboardHook::IsInstalled() const {
    return s_hook != nullptr;
}

void KeyboardHook::SetCallback(std::function<void()> callback) {
    s_callback = callback;
}

LRESULT CALLBACK KeyboardHook::HookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && lParam != 0) {
        auto* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Intercept physical Copilot key: VK_F23 (0x86) or OEM VK_F24 (0x87)
        if (kb->vkCode == 0x86 || kb->vkCode == 0x87) {
            if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
                ActionRunner::MaskStartMenu();

                ULONGLONG now = GetTickCount64();
                AppConfig cfg = ConfigManager::GetCached();
                ULONGLONG debounce = static_cast<ULONGLONG>(cfg.debounceMs > 50 ? cfg.debounceMs : 300);

                bool shouldTrigger = false;
                if (!s_hasTriggered) {
                    shouldTrigger = true;
                } else {
                    if ((now - s_lastTriggerTick) >= debounce) {
                        shouldTrigger = true;
                    }
                }

                if (shouldTrigger) {
                    s_hasTriggered = true;
                    s_lastTriggerTick = now;
                    ActionRunner::Execute(cfg);
                    if (s_callback) {
                        s_callback();
                    }
                }
                return 1; // Consume key down
            }

            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                return 1; // Consume key up
            }
        }
    }

    return CallNextHookEx(s_hook, nCode, wParam, lParam);
}
