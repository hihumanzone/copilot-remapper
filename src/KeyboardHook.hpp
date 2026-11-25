#pragma once

#include <windows.h>
#include <atomic>

class KeyboardHook {
public:
    using NotifyCallback = void(*)(void* context);

    KeyboardHook();
    ~KeyboardHook();

    KeyboardHook(const KeyboardHook&) = delete;
    KeyboardHook& operator=(const KeyboardHook&) = delete;

    bool Install();
    void Uninstall();
    bool IsInstalled() const noexcept;
    void SetCallback(NotifyCallback callback, void* context) noexcept;

private:
    static DWORD WINAPI HookThreadProc(LPVOID lpParam);
    static LRESULT CALLBACK HookCallback(int nCode, WPARAM wParam, LPARAM lParam);

    HANDLE m_hThread = nullptr;
    DWORD m_threadId = 0;

    static std::atomic<HHOOK> s_hook;
    static std::atomic<NotifyCallback> s_notifyFn;
    static std::atomic<void*> s_notifyCtx;
    static bool s_hasTriggered;
    static ULONGLONG s_lastTriggerTick;
};
