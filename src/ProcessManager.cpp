#include "ProcessManager.hpp"
#include <shobjidl.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <dwmapi.h>
#include <algorithm>

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

    // If it is a shortcut (.lnk), resolve to the underlying executable
    if (cleaned.length() >= 4 && _wcsicmp(cleaned.c_str() + cleaned.length() - 4, L".lnk") == 0) {
        std::wstring resolved;
        if (ResolveShortcut(cleaned, resolved)) {
            cleaned = resolved;
        }
    }

    size_t idx = cleaned.find_last_of(L"\\/");
    std::wstring exeName = (idx != std::wstring::npos) ? cleaned.substr(idx + 1) : cleaned;

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
        // If access denied on same user session, it's typically elevated
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
    HDESK hInput = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (hInput) {
        SetThreadDesktop(hInput);
        CloseDesktop(hInput);
    }

    HWND hFore = GetForegroundWindow();
    if (!hFore) return false;

    DWORD forePid = 0;
    GetWindowThreadProcessId(hFore, &forePid);
    if (forePid == 0 || forePid == GetCurrentProcessId()) return false;

    std::wstring targetExe = ExtractExeName(targetPath);
    std::wstring cleanTarget = TrimQuotes(targetPath);

    // Try QueryFullProcessImageNameW first
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, forePid);
    if (hProc) {
        wchar_t fullPath[MAX_PATH] = { 0 };
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProc, 0, fullPath, &size)) {
            std::wstring foreExe = ExtractExeName(fullPath);
            if (_wcsicmp(foreExe.c_str(), targetExe.c_str()) == 0 ||
                (!cleanTarget.empty() && _wcsicmp(fullPath, cleanTarget.c_str()) == 0)) {
                CloseHandle(hProc);
                return true;
            }
        }
        CloseHandle(hProc);
    }

    // Fallback: Toolhelp32 process snapshot (works across integrity levels)
    std::wstring foreExe = GetProcessNameFromPid(forePid);
    if (!foreExe.empty() && _wcsicmp(foreExe.c_str(), targetExe.c_str()) == 0) {
        return true;
    }

    return false;
}

struct WindowSearchContext {
    std::wstring targetExe;
    std::wstring targetCleanPath;
    bool mustBeElevated = false;
    HWND bestHwnd = nullptr;
    bool bestElevated = false;
};

static BOOL CALLBACK EnumWindowsCallback(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowSearchContext*>(lParam);

    // Skip windows that are neither visible nor minimized
    if (!IsWindowVisible(hwnd) && !IsIconic(hwnd)) return TRUE;

    // Must be a top-level unowned window
    if (GetWindow(hwnd, GW_OWNER) != nullptr) return TRUE;

    // Filter out tool windows
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    // Filter out cloaked windows (cloaked by Windows shell or other virtual desktop)
    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return TRUE;

    std::wstring exeName;
    wchar_t imagePath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    bool isElevated = false;

    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProc) {
        if (QueryFullProcessImageNameW(hProc, 0, imagePath, &size)) {
            exeName = ProcessManager::ExtractExeName(imagePath);
        }

        HANDLE hToken = nullptr;
        if (OpenProcessToken(hProc, TOKEN_QUERY, &hToken)) {
            TOKEN_ELEVATION elev = { 0 };
            DWORD dwSize = sizeof(elev);
            if (GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &dwSize)) {
                isElevated = (elev.TokenIsElevated != 0);
            }
            CloseHandle(hToken);
        }
        CloseHandle(hProc);
    } else {
        if (GetLastError() == ERROR_ACCESS_DENIED) {
            isElevated = true; // High IL process in same desktop session
        }
    }

    if (exeName.empty()) {
        exeName = ProcessManager::GetProcessNameFromPid(pid);
    }

    if (exeName.empty()) return TRUE;

    bool isMatch = (_wcsicmp(exeName.c_str(), ctx->targetExe.c_str()) == 0);
    if (!isMatch && !ctx->targetCleanPath.empty() && imagePath[0] != L'\0') {
        isMatch = (_wcsicmp(imagePath, ctx->targetCleanPath.c_str()) == 0);
    }

    if (isMatch) {
        wchar_t title[256] = { 0 };
        GetWindowTextW(hwnd, title, 256);
        bool hasTitle = (title[0] != L'\0');

        if (ctx->mustBeElevated) {
            if (isElevated && hasTitle) {
                ctx->bestHwnd = hwnd;
                ctx->bestElevated = true;
                return FALSE; // Exact match found: elevated main window
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
    }

    return TRUE;
}

HWND ProcessManager::FindTargetWindow(const std::wstring& targetPath, bool mustBeElevated, bool& foundElevated) {
    foundElevated = false;
    WindowSearchContext ctx;
    ctx.targetExe = ExtractExeName(targetPath);
    ctx.targetCleanPath = TrimQuotes(targetPath);
    ctx.mustBeElevated = mustBeElevated;

    HDESK hInput = OpenInputDesktop(0, FALSE, GENERIC_ALL);
    if (hInput) {
        EnumDesktopWindows(hInput, EnumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));
        CloseDesktop(hInput);
    } else {
        EnumWindows(EnumWindowsCallback, reinterpret_cast<LPARAM>(&ctx));
    }

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

    // Simulate Alt key down/up to unlock focus stealing
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
