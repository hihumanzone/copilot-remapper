#include "ProcessManager.hpp"
#include <shobjidl.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <vector>

static inline const wchar_t* GetBaseName(const wchar_t* path) {
    if (!path) return L"";
    const wchar_t* pSlash = wcsrchr(path, L'\\');
    if (!pSlash) pSlash = wcsrchr(path, L'/');
    return pSlash ? pSlash + 1 : path;
}

static std::wstring TrimQuotes(const std::wstring& str) {
    if (str.length() >= 2 && str.front() == L'"' && str.back() == L'"') {
        return str.substr(1, str.length() - 2);
    }
    return str;
}

std::wstring ProcessManager::GetProcessNameFromPid(DWORD pid) {
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return L"";

    PROCESSENTRY32W pe = { sizeof(pe) };
    std::wstring name = L"";
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

bool ProcessManager::ResolveShortcut(const std::wstring& shortcutPath, std::wstring& targetPath) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    bool needCoUninit = SUCCEEDED(hrCo);

    IShellLinkW* pShellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&pShellLink));
    if (FAILED(hr)) {
        if (needCoUninit) CoUninitialize();
        return false;
    }

    bool success = false;
    IPersistFile* pPersistFile = nullptr;
    hr = pShellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pPersistFile));
    if (SUCCEEDED(hr)) {
        hr = pPersistFile->Load(shortcutPath.c_str(), STGM_READ);
        if (SUCCEEDED(hr)) {
            wchar_t szPath[MAX_PATH] = { 0 };
            hr = pShellLink->GetPath(szPath, MAX_PATH, nullptr, SLGP_RAWPATH);
            if (SUCCEEDED(hr) && szPath[0] != L'\0') {
                targetPath = szPath;
                success = true;
            }
        }
        pPersistFile->Release();
    }
    pShellLink->Release();

    if (needCoUninit) {
        CoUninitialize();
    }
    return success;
}

std::wstring ProcessManager::ExtractExeName(const std::wstring& targetPath) {
    std::wstring cleaned = TrimQuotes(targetPath);
    if (cleaned.empty()) return L"";

    // If it is a shortcut (.lnk), resolve to the underlying executable once
    if (cleaned.length() >= 4 && _wcsicmp(cleaned.c_str() + cleaned.length() - 4, L".lnk") == 0) {
        std::wstring resolved;
        if (ResolveShortcut(cleaned, resolved)) {
            cleaned = resolved;
        }
    }

    const wchar_t* pBase = GetBaseName(cleaned.c_str());
    std::wstring exeName = pBase;

    // Append .exe if not present and no other extension exists
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
        TOKEN_ELEVATION elev = { 0 };
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

bool ProcessManager::IsForegroundProcessTarget(const std::wstring& targetPath) {
    HWND hFore = GetForegroundWindow();
    if (!hFore) return false;

    DWORD forePid = 0;
    GetWindowThreadProcessId(hFore, &forePid);
    if (forePid == 0 || forePid == GetCurrentProcessId()) return false;

    std::wstring targetExe = ExtractExeName(targetPath);
    std::wstring cleanTarget = TrimQuotes(targetPath);

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, forePid);
    if (hProc) {
        wchar_t fullPath[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
            const wchar_t* foreExe = GetBaseName(fullPath);
            if (_wcsicmp(foreExe, targetExe.c_str()) == 0 ||
                (!cleanTarget.empty() && _wcsicmp(fullPath, cleanTarget.c_str()) == 0)) {
                CloseHandle(hProc);
                return true;
            }
        }
        CloseHandle(hProc);
    }

    return false;
}

struct PidMatchCache {
    DWORD pid;
    bool isMatch;
    bool isElevated;
};

struct WindowSearchContext {
    std::wstring targetExe;
    std::wstring targetCleanPath;
    bool mustBeElevated = false;
    HWND bestHwnd = nullptr;
    bool bestElevated = false;
    std::vector<PidMatchCache> pidCache;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lParam);

    // Fast reject non-windows and invisible windows
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return TRUE;

    // Must be a top-level unowned window
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    // Filter out tool windows
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;

    // Fast PID lookup in local enumeration cache
    bool isMatch = false;
    bool isElevated = false;
    bool foundInCache = false;

    for (const auto& entry : ctx->pidCache) {
        if (entry.pid == pid) {
            foundInCache = true;
            isMatch = entry.isMatch;
            isElevated = entry.isElevated;
            break;
        }
    }

    if (!foundInCache) {
        HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (hProc) {
            wchar_t imagePath[MAX_PATH] = { 0 };
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(hProc, 0, imagePath, &size)) {
                const wchar_t* exeName = GetBaseName(imagePath);
                isMatch = (_wcsicmp(exeName, ctx->targetExe.c_str()) == 0);
                if (!isMatch && !ctx->targetCleanPath.empty()) {
                    isMatch = (_wcsicmp(imagePath, ctx->targetCleanPath.c_str()) == 0);
                }
            }

            if (isMatch) {
                HANDLE hToken = nullptr;
                if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
                    TOKEN_ELEVATION elev = { 0 };
                    DWORD dwSize = sizeof(elev);
                    if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &dwSize)) {
                        isElevated = (elev.TokenIsElevated != 0);
                    }
                    CloseHandle(hToken);
                }
            }
            CloseHandle(hProc);
        } else {
            if (GetLastError() == ERROR_ACCESS_DENIED) {
                isElevated = true;
                // Query process name from single lookup only if needed
                std::wstring pName = ProcessManager::GetProcessNameFromPid(pid);
                isMatch = (_wcsicmp(pName.c_str(), ctx->targetExe.c_str()) == 0);
            }
        }

        ctx->pidCache.push_back({ pid, isMatch, isElevated });
    }

    // Skip windows not belonging to target process
    if (!isMatch) return TRUE;

    // Filter out cloaked windows only after confirming process match
    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    // Check window title
    int titleLen = GetWindowTextLengthW(hwnd);
    bool hasTitle = (titleLen > 0);

    if (ctx->mustBeElevated) {
        if (isElevated && hasTitle) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = true;
            return FALSE; // Match found: elevated main window
        } else if (isElevated && ctx->bestHwnd == nullptr) {
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
            return FALSE; // Match found
        } else if (ctx->bestHwnd == nullptr) {
            ctx->bestHwnd = hwnd;
            ctx->bestElevated = isElevated;
        }
    }

    return TRUE;
}

HWND ProcessManager::FindTargetWindow(const std::wstring& targetPath, bool mustBeElevated, bool& foundElevated) {
    foundElevated = false;
    WindowSearchContext ctx;
    ctx.targetExe = ExtractExeName(targetPath);
    ctx.targetCleanPath = TrimQuotes(targetPath);
    ctx.mustBeElevated = mustBeElevated;
    ctx.pidCache.reserve(32);

    EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));

    foundElevated = ctx.bestElevated;
    return ctx.bestHwnd;
}

bool ProcessManager::ActivateWindow(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd)) return false;

    AllowSetForegroundWindow(ASFW_ANY);

    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }

    HWND hFore = GetForegroundWindow();
    if (hFore == hwnd) {
        return true;
    }

    DWORD curThread = GetCurrentThreadId();
    DWORD foreThread = hFore ? GetWindowThreadProcessId(hFore, nullptr) : 0;
    DWORD targetThread = GetWindowThreadProcessId(hwnd, nullptr);

    bool attachedFore = false;
    bool attachedTarget = false;

    if (foreThread != 0 && foreThread != curThread) {
        attachedFore = AttachThreadInput(curThread, foreThread, TRUE);
    }
    if (targetThread != 0 && targetThread != curThread && targetThread != foreThread) {
        attachedTarget = AttachThreadInput(curThread, targetThread, TRUE);
    }

    // Simulate Alt key tap to unlock focus stealing
    INPUT ip[2] = {};
    ip[0].type = INPUT_KEYBOARD;
    ip[0].ki.wVk = VK_MENU;
    ip[1].type = INPUT_KEYBOARD;
    ip[1].ki.wVk = VK_MENU;
    ip[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, ip, sizeof(INPUT));

    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);

    if (attachedFore) {
        AttachThreadInput(curThread, foreThread, FALSE);
    }
    if (attachedTarget) {
        AttachThreadInput(curThread, targetThread, FALSE);
    }

    return true;
}
