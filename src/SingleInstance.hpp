#pragma once

#include <windows.h>

class SingleInstance {
public:
    using WakeupCallback = void(*)(void* context);

    SingleInstance() noexcept = default;
    ~SingleInstance();

    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;

    bool TryAcquire();
    static void SignalExistingInstance();
    void StartWakeupListener(WakeupCallback onWakeup, void* context);
    void StopWakeupListener();

private:
    static VOID CALLBACK WakeupWaitCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

    HANDLE m_hMutex = nullptr;
    HANDLE m_hWakeupEvent = nullptr;
    HANDLE m_hWaitHandle = nullptr;
    WakeupCallback m_onWakeup = nullptr;
    void* m_wakeupContext = nullptr;
};
