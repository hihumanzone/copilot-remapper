#pragma once

#include <windows.h>
#include <functional>
#include <thread>
#include <atomic>

class SingleInstance {
public:
    SingleInstance();
    ~SingleInstance();

    bool TryAcquire();
    static void SignalExistingInstance();
    void StartWakeupListener(std::function<void()> onWakeup);
    void StopWakeupListener();

private:
    HANDLE m_hMutex = nullptr;
    HANDLE m_hWakeupEvent = nullptr;
    std::thread m_listenerThread;
    std::atomic<bool> m_running{ false };
};
