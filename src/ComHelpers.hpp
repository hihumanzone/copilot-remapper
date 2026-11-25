#pragma once

#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <string_view>
#include <utility>

// Minimal RAII COM smart pointer (zero STL/exception overhead)
template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ~ComPtr() noexcept { Reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    ComPtr(ComPtr&& other) noexcept : m_ptr(std::exchange(other.m_ptr, nullptr)) {}
    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
        }
        return *this;
    }

    T* Get() const noexcept { return m_ptr; }
    T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

    T** GetAddressOf() noexcept {
        Reset();
        return &m_ptr;
    }

    void** PutVoid() noexcept {
        Reset();
        return reinterpret_cast<void**>(&m_ptr);
    }

    void Reset() noexcept {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

private:
    T* m_ptr = nullptr;
};

// Minimal RAII wrapper for COM BSTR strings
class Bstr {
public:
    Bstr() noexcept = default;
    explicit Bstr(std::wstring_view sv) noexcept {
        if (!sv.empty()) {
            m_bstr = SysAllocStringLen(sv.data(), static_cast<UINT>(sv.size()));
        } else {
            m_bstr = SysAllocStringLen(L"", 0);
        }
    }
    ~Bstr() noexcept {
        if (m_bstr) {
            SysFreeString(m_bstr);
        }
    }

    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;

    BSTR Get() const noexcept { return m_bstr; }
    operator BSTR() const noexcept { return m_bstr; }
    explicit operator bool() const noexcept { return m_bstr != nullptr; }

private:
    BSTR m_bstr = nullptr;
};

// Ensures STA COM is initialized for ShellExecuteEx / Task Scheduler / File Dialogs
class CoInitGuard {
public:
    CoInitGuard() noexcept {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_shouldUninit = SUCCEEDED(hr);
    }
    ~CoInitGuard() noexcept {
        if (m_shouldUninit) {
            CoUninitialize();
        }
    }

    CoInitGuard(const CoInitGuard&) = delete;
    CoInitGuard& operator=(const CoInitGuard&) = delete;

private:
    bool m_shouldUninit = false;
};

// Lightweight non-virtual intrusive atomic refcounting (zero std::shared_ptr / RTTI overhead)
template <typename Derived>
class RefCounted {
public:
    void AddRef() const noexcept { InterlockedIncrement(&m_refCount); }
    void Release() const noexcept {
        if (InterlockedDecrement(&m_refCount) == 0) {
            delete static_cast<const Derived*>(this);
        }
    }

protected:
    RefCounted() noexcept = default;
    RefCounted(const RefCounted&) noexcept {}
    RefCounted(RefCounted&&) noexcept {}
    RefCounted& operator=(const RefCounted&) noexcept { return *this; }
    RefCounted& operator=(RefCounted&&) noexcept { return *this; }
    ~RefCounted() noexcept = default;

private:
    mutable LONG m_refCount = 1;
};

template <typename T>
class RefPtr {
public:
    RefPtr() noexcept = default;
    explicit RefPtr(T* ptr, bool addRef = false) noexcept : m_ptr(ptr) {
        if (m_ptr && addRef) m_ptr->AddRef();
    }
    RefPtr(const RefPtr& other) noexcept : m_ptr(other.m_ptr) {
        if (m_ptr) m_ptr->AddRef();
    }
    RefPtr(RefPtr&& other) noexcept : m_ptr(std::exchange(other.m_ptr, nullptr)) {}

    ~RefPtr() noexcept { Reset(); }

    RefPtr& operator=(const RefPtr& other) noexcept {
        if (this != &other) {
            if (other.m_ptr) other.m_ptr->AddRef();
            if (m_ptr) m_ptr->Release();
            m_ptr = other.m_ptr;
        }
        return *this;
    }

    RefPtr& operator=(RefPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            m_ptr = std::exchange(other.m_ptr, nullptr);
        }
        return *this;
    }

    void Reset() noexcept {
        if (m_ptr) {
            m_ptr->Release();
            m_ptr = nullptr;
        }
    }

    T* Get() const noexcept { return m_ptr; }
    const T& operator*() const noexcept { return *m_ptr; }
    T* operator->() const noexcept { return m_ptr; }
    explicit operator bool() const noexcept { return m_ptr != nullptr; }

private:
    T* m_ptr = nullptr;
};
