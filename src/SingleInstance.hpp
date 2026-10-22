#pragma once

#include <windows.h>
#include <functional>

class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();

    bool TryAcquire();
    static void SignalExistingInstance();
    void StartWakeupListener(std::function<void()> onWakeup);
    void StopWakeupListener();

private:
    static VOID CALLBACK WakeupWaitCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired);

    HANDLE m_hMutex = nullptr;
    HANDLE m_hWakeupEvent = nullptr;
    HANDLE m_hWaitHandle = nullptr;
    std::function<void()> m_onWakeup = nullptr;
};
