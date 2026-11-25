#include "SingleInstance.hpp"

static constexpr wchar_t MUTEX_NAME[] = L"Global\\CopilotRemapper_SingleInstance_Mutex";
static constexpr wchar_t EVENT_NAME[] = L"Global\\CopilotRemapper_Wakeup";
static constexpr wchar_t LOCAL_MUTEX_NAME[] = L"Local\\CopilotRemapper_SingleInstance_Mutex";
static constexpr wchar_t LOCAL_EVENT_NAME[] = L"Local\\CopilotRemapper_Wakeup";

SingleInstance::~SingleInstance() {
    StopWakeupListener();
    if (m_hMutex) {
        CloseHandle(m_hMutex);
        m_hMutex = nullptr;
    }
}

bool SingleInstance::TryAcquire() {
    m_hMutex = CreateMutexW(nullptr, TRUE, MUTEX_NAME);
    if (!m_hMutex && GetLastError() == ERROR_ACCESS_DENIED) {
        m_hMutex = CreateMutexW(nullptr, TRUE, LOCAL_MUTEX_NAME);
    }
    if (!m_hMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (m_hMutex) {
            CloseHandle(m_hMutex);
            m_hMutex = nullptr;
        }
        return false;
    }
    return true;
}

#ifndef ASFW_ANY
#define ASFW_ANY (static_cast<DWORD>(-1))
#endif

void SingleInstance::SignalExistingInstance() {
    AllowSetForegroundWindow(ASFW_ANY);
    HANDLE hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
    if (!hEvent) {
        hEvent = OpenEventW(EVENT_MODIFY_STATE, FALSE, LOCAL_EVENT_NAME);
    }
    if (hEvent) {
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }
}

VOID CALLBACK SingleInstance::WakeupWaitCallback(PVOID lpParameter, BOOLEAN TimerOrWaitFired) {
    if (!TimerOrWaitFired && lpParameter) {
        auto* pThis = static_cast<SingleInstance*>(lpParameter);
        if (pThis && pThis->m_onWakeup) {
            pThis->m_onWakeup(pThis->m_wakeupContext);
        }
    }
}

void SingleInstance::StartWakeupListener(WakeupCallback onWakeup, void* context) {
    m_onWakeup = onWakeup;
    m_wakeupContext = context;

    m_hWakeupEvent = CreateEventW(nullptr, FALSE, FALSE, EVENT_NAME);
    if (!m_hWakeupEvent && GetLastError() == ERROR_ACCESS_DENIED) {
        m_hWakeupEvent = CreateEventW(nullptr, FALSE, FALSE, LOCAL_EVENT_NAME);
    }
    if (!m_hWakeupEvent) return;

    // Register with OS wait threadpool: zero active worker threads and zero CPU polling
    RegisterWaitForSingleObject(
        &m_hWaitHandle,
        m_hWakeupEvent,
        WakeupWaitCallback,
        this,
        INFINITE,
        WT_EXECUTEDEFAULT
    );
}

void SingleInstance::StopWakeupListener() {
    if (m_hWaitHandle) {
        UnregisterWaitEx(m_hWaitHandle, INVALID_HANDLE_VALUE);
        m_hWaitHandle = nullptr;
    }
    if (m_hWakeupEvent) {
        CloseHandle(m_hWakeupEvent);
        m_hWakeupEvent = nullptr;
    }
    m_onWakeup = nullptr;
    m_wakeupContext = nullptr;
}
