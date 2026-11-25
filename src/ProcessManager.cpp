#include "ProcessManager.hpp"
#include "ComHelpers.hpp"
#include <shobjidl.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <winternl.h>
#include <array>
#include <atomic>
#include <iterator>
#include <vector>

namespace {

inline const wchar_t* GetBaseName(const wchar_t* path) noexcept {
    if (!path) return L"";
    const wchar_t* pSlash = wcsrchr(path, L'\\');
    const wchar_t* pFwd = wcsrchr(path, L'/');
    if (pFwd && (!pSlash || pFwd > pSlash)) {
        pSlash = pFwd;
    }
    return pSlash ? pSlash + 1 : path;
}

inline bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b) noexcept {
    if (a.size() != b.size()) return false;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

inline bool EndsWithIgnoreCase(std::wstring_view str, std::wstring_view suffix) noexcept {
    if (str.size() < suffix.size()) return false;
    return EqualsIgnoreCase(str.substr(str.size() - suffix.size()), suffix);
}

using PfnNtQuerySystemInformation = NTSTATUS(NTAPI*)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);

PfnNtQuerySystemInformation GetNtQuerySystemInformation() noexcept {
    static std::atomic<void*> s_cached{ nullptr };
    void* fn = s_cached.load(std::memory_order_acquire);
    if (!fn) {
        if (HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll")) {
            fn = reinterpret_cast<void*>(GetProcAddress(hNtDll, "NtQuerySystemInformation"));
            if (fn) {
                s_cached.store(fn, std::memory_order_release);
            }
        }
    }
    return reinterpret_cast<PfnNtQuerySystemInformation>(fn);
}

bool FetchProcessSnapshot(std::vector<BYTE>& buffer) {
    const PfnNtQuerySystemInformation pNtQuery = GetNtQuerySystemInformation();
    if (!pNtQuery) return false;

    ULONG size = 128 * 1024;
    constexpr NTSTATUS kStatusInfoLengthMismatch = static_cast<NTSTATUS>(0xC0000004L);

    for (int attempt = 0; attempt < 5; ++attempt) {
        buffer.resize(size);
        ULONG needed = 0;
        NTSTATUS st = pNtQuery(SystemProcessInformation, buffer.data(), size, &needed);
        if (st >= 0) {
            return true;
        }
        if (st != kStatusInfoLengthMismatch) {
            buffer.clear();
            return false;
        }
        size = (needed > size) ? (needed + 16384) : (size * 2);
    }
    buffer.clear();
    return false;
}

std::wstring_view FindProcessNameInSnapshot(const std::vector<BYTE>& buffer, DWORD pid) noexcept {
    if (buffer.empty()) return {};
    size_t offset = 0;
    while (offset + sizeof(SYSTEM_PROCESS_INFORMATION) <= buffer.size()) {
        const auto* spi = reinterpret_cast<const SYSTEM_PROCESS_INFORMATION*>(buffer.data() + offset);
        const DWORD entryPid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(spi->UniqueProcessId));
        if (entryPid == pid) {
            if (spi->ImageName.Buffer && spi->ImageName.Length > 0) {
                return std::wstring_view(spi->ImageName.Buffer, spi->ImageName.Length / sizeof(wchar_t));
            }
            return {};
        }
        if (spi->NextEntryOffset == 0) break;
        offset += spi->NextEntryOffset;
    }
    return {};
}

std::wstring GetProcessNameToolhelp(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return L"";

    PROCESSENTRY32W pe = {};
    pe.dwSize = sizeof(pe);
    std::wstring name;
    if (Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == pid) {
                name = pe.szExeFile;
                break;
            }
        } while (Process32NextW(hSnap, &pe));
    }
    CloseHandle(hSnap);
    return name;
}

struct PidMatchCache {
    DWORD pid;
    bool isMatch;
    bool isElevated;
};

struct WindowSearchContext {
    std::wstring_view targetExe;
    std::wstring_view targetCleanPath;
    DWORD selfPid = GetCurrentProcessId();
    bool mustBeElevated = false;
    HWND bestHwnd = nullptr;
    bool bestElevated = false;

    static constexpr size_t kInlineCacheCap = 64;
    std::array<PidMatchCache, kInlineCacheCap> inlineCache{};
    size_t inlineCount = 0;
    std::vector<PidMatchCache> overflowCache;

    bool snapshotAttempted = false;
    bool hasSnapshot = false;
    std::vector<BYTE> processSnapshot;

    bool LookupPid(DWORD pid, bool& isMatch, bool& isElevated) const noexcept {
        for (size_t i = 0; i < inlineCount; ++i) {
            if (inlineCache[i].pid == pid) {
                isMatch = inlineCache[i].isMatch;
                isElevated = inlineCache[i].isElevated;
                return true;
            }
        }
        for (const auto& entry : overflowCache) {
            if (entry.pid == pid) {
                isMatch = entry.isMatch;
                isElevated = entry.isElevated;
                return true;
            }
        }
        return false;
    }

    void StorePid(DWORD pid, bool isMatch, bool isElevated) {
        if (inlineCount < kInlineCacheCap) {
            inlineCache[inlineCount++] = { pid, isMatch, isElevated };
        } else {
            overflowCache.push_back({ pid, isMatch, isElevated });
        }
    }

    std::wstring_view LookupProtectedProcessName(DWORD pid) {
        if (!snapshotAttempted) {
            snapshotAttempted = true;
            hasSnapshot = FetchProcessSnapshot(processSnapshot);
        }
        if (hasSnapshot) {
            return FindProcessNameInSnapshot(processSnapshot, pid);
        }
        return {};
    }
};

BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lParam);

    // Fast reject invisible non-minimized windows and owned/tool windows
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;
    if (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == ctx->selfPid) return TRUE;

    bool isMatch = false;
    bool isElevated = false;
    if (!ctx->LookupPid(pid, isMatch, isElevated)) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t imagePath[1024];
            DWORD size = static_cast<DWORD>(std::size(imagePath));
            if (QueryFullProcessImageNameW(hProc, 0, imagePath, &size)) {
                const wchar_t* exeName = GetBaseName(imagePath);
                isMatch = EqualsIgnoreCase(exeName, ctx->targetExe);
                if (!isMatch && !ctx->targetCleanPath.empty()) {
                    isMatch = EqualsIgnoreCase(std::wstring_view(imagePath, size), ctx->targetCleanPath);
                }
            }

            if (isMatch) {
                HANDLE hToken = nullptr;
                if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                    TOKEN_ELEVATION elev = {};
                    DWORD dwSize = sizeof(elev);
                    if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &dwSize)) {
                        isElevated = (elev.TokenIsElevated != 0);
                    }
                    CloseHandle(hToken);
                }
            }
            CloseHandle(hProc);
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            isElevated = true;
            std::wstring_view pName = ctx->LookupProtectedProcessName(pid);
            if (!pName.empty()) {
                isMatch = EqualsIgnoreCase(pName, ctx->targetExe);
            } else if (!ctx->hasSnapshot) {
                std::wstring fallback = GetProcessNameToolhelp(pid);
                isMatch = EqualsIgnoreCase(fallback, ctx->targetExe);
            }
        }

        ctx->StorePid(pid, isMatch, isElevated);
    }

    if (!isMatch) return TRUE;

    // Filter out cloaked UWP/virtual-desktop windows only after confirming process match
    int cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0) {
        return TRUE;
    }

    const bool hasTitle = (GetWindowTextLengthW(hwnd) > 0);
    if (ctx->mustBeElevated) {
        if (isElevated && hasTitle) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = true;
            return FALSE;
        }
        if (isElevated && ctx->bestHwnd == nullptr) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = true;
        } else if (ctx->bestHwnd == nullptr || (hasTitle && !ctx->bestElevated)) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = isElevated;
        }
    } else {
        if (hasTitle) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = isElevated;
            return FALSE;
        }
        if (ctx->bestHwnd == nullptr) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = isElevated;
        }
    }

    return TRUE;
}

void BringWindowToFrontAttach(HWND hwnd, DWORD foreThread, DWORD targetThread, DWORD curThread) noexcept {
    bool attachedFore = false;
    bool attachedTarget = false;

    if (foreThread != 0 && foreThread != curThread) {
        attachedFore = AttachThreadInput(curThread, foreThread, TRUE) != FALSE;
    }
    if (targetThread != 0 && targetThread != curThread && targetThread != foreThread) {
        attachedTarget = AttachThreadInput(curThread, targetThread, TRUE) != FALSE;
    }

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    if (attachedFore) {
        AttachThreadInput(curThread, foreThread, FALSE);
    }
    if (attachedTarget) {
        AttachThreadInput(curThread, targetThread, FALSE);
    }
}

} // namespace

std::wstring ProcessManager::TrimQuotes(std::wstring_view str) {
    if (str.size() >= 2 && str.front() == L'"' && str.back() == L'"') {
        str.remove_prefix(1);
        str.remove_suffix(1);
    }
    return std::wstring(str);
}

std::wstring ProcessManager::GetProcessNameFromPid(DWORD pid) {
    std::vector<BYTE> buffer;
    if (FetchProcessSnapshot(buffer)) {
        std::wstring_view name = FindProcessNameInSnapshot(buffer, pid);
        if (!name.empty()) {
            return std::wstring(name);
        }
    }
    return GetProcessNameToolhelp(pid);
}

bool ProcessManager::ResolveShortcut(std::wstring_view shortcutPath, std::wstring& targetPath) {
    if (shortcutPath.empty()) return false;

    CoInitGuard co;
    ComPtr<IShellLinkW> pShellLink;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, pShellLink.PutVoid()))) {
        return false;
    }

    ComPtr<IPersistFile> pPersistFile;
    if (FAILED(pShellLink->QueryInterface(IID_IPersistFile, pPersistFile.PutVoid()))) {
        return false;
    }

    std::wstring pathNullTerm(shortcutPath);
    if (FAILED(pPersistFile->Load(pathNullTerm.c_str(), STGM_READ))) {
        return false;
    }

    wchar_t szPath[1024] = {};
    if (SUCCEEDED(pShellLink->GetPath(szPath, static_cast<int>(std::size(szPath)), nullptr, SLGP_RAWPATH)) && szPath[0] != L'\0') {
        targetPath = szPath;
        return true;
    }
    return false;
}

std::wstring ProcessManager::ExtractExeName(std::wstring_view targetPath) {
    std::wstring cleaned = TrimQuotes(targetPath);
    if (cleaned.empty()) return L"";

    if (EndsWithIgnoreCase(cleaned, L".lnk")) {
        std::wstring resolved;
        if (ResolveShortcut(cleaned, resolved)) {
            cleaned = std::move(resolved);
        }
    }

    std::wstring exeName = GetBaseName(cleaned.c_str());
    if (exeName.find(L'.') == std::wstring::npos) {
        exeName += L".exe";
    }
    return exeName;
}

bool ProcessManager::IsProcessElevated(DWORD pid, bool& isElevated) {
    isElevated = false;
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            isElevated = true;
            return true;
        }
        return false;
    }

    bool success = false;
    HANDLE hToken = nullptr;
    if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
        TOKEN_ELEVATION elev = {};
        DWORD dwSize = sizeof(elev);
        if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &dwSize)) {
            isElevated = (elev.TokenIsElevated != 0);
            success = true;
        }
        CloseHandle(hToken);
    }
    CloseHandle(hProc);
    return success;
}

bool ProcessManager::IsForegroundProcessTargetResolved(std::wstring_view targetExe, std::wstring_view cleanTarget) {
    if (targetExe.empty()) return false;

    HWND hFore = GetForegroundWindow();
    if (!hFore) return false;

    DWORD forePid = 0;
    GetWindowThreadProcessId(hFore, &forePid);
    if (forePid == 0 || forePid == GetCurrentProcessId()) return false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, forePid);
    if (!hProc) return false;

    wchar_t fullPath[1024];
    DWORD size = static_cast<DWORD>(std::size(fullPath));
    bool matched = false;
    if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
        const wchar_t* foreExe = GetBaseName(fullPath);
        matched = EqualsIgnoreCase(foreExe, targetExe) ||
                  (!cleanTarget.empty() && EqualsIgnoreCase(std::wstring_view(fullPath, size), cleanTarget));
    }
    CloseHandle(hProc);
    return matched;
}

bool ProcessManager::IsForegroundProcessTarget(std::wstring_view targetPath) {
    std::wstring targetExe = ExtractExeName(targetPath);
    std::wstring cleanTarget = TrimQuotes(targetPath);
    return IsForegroundProcessTargetResolved(targetExe, cleanTarget);
}

HWND ProcessManager::FindTargetWindowResolved(std::wstring_view targetExe, std::wstring_view cleanTarget, bool mustBeElevated, bool& foundElevated) {
    foundElevated = false;
    if (targetExe.empty()) return nullptr;

    WindowSearchContext ctx;
    ctx.targetExe = targetExe;
    ctx.targetCleanPath = cleanTarget;
    ctx.mustBeElevated = mustBeElevated;

    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));

    foundElevated = ctx.bestElevated;
    return ctx.bestHwnd;
}

HWND ProcessManager::FindTargetWindow(std::wstring_view targetPath, bool mustBeElevated, bool& foundElevated) {
    std::wstring targetExe = ExtractExeName(targetPath);
    std::wstring cleanTarget = TrimQuotes(targetPath);
    return FindTargetWindowResolved(targetExe, cleanTarget, mustBeElevated, foundElevated);
}

bool ProcessManager::ActivateWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    AllowSetForegroundWindow(ASFW_ANY);

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
    }

    HWND hFore = GetForegroundWindow();
    if (hFore == hwnd) {
        return true;
    }

    // Fast path: directly request foreground when our recent input grants foreground lock
    SetForegroundWindow(hwnd);
    if (GetForegroundWindow() == hwnd) {
        return true;
    }

    const DWORD curThread = GetCurrentThreadId();
    const DWORD foreThread = hFore ? GetWindowThreadProcessId(hFore, nullptr) : 0;
    const DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);

    BringWindowToFrontAttach(hwnd, foreThread, targetThread, curThread);
    if (GetForegroundWindow() == hwnd) {
        return true;
    }

    // Final fallback: simulate an Alt key tap only if Windows still denied focus transfer
    INPUT ip[2] = {};
    ip[0].type = INPUT_KEYBOARD;
    ip[0].ki.wVk = VK_MENU;
    ip[1].type = INPUT_KEYBOARD;
    ip[1].ki.wVk = VK_MENU;
    ip[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, ip, sizeof(INPUT));

    BringWindowToFrontAttach(hwnd, foreThread, targetThread, curThread);
    return true;
}
