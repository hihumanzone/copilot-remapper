#pragma once

#include <windows.h>
#include <functional>

class KeyboardHook {
public:
    KeyboardHook();
    ~KeyboardHook();

    bool Install();
    void Uninstall();
    bool IsInstalled() const;
    void SetCallback(std::function<void()> callback);

private:
    static LRESULT CALLBACK HookCallback(int nCode, WPARAM wParam, LPARAM lParam);

    static HHOOK s_hook;
    static std::function<void()> s_callback;
    static bool s_hasTriggered;
    static ULONGLONG s_lastTriggerTick;
};
