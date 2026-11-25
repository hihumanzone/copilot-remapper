#pragma once

#include <windows.h>
#include <string>
#include <string_view>

class ProcessManager {
public:
    // Removes matching surrounding double quotes if present.
    static std::wstring TrimQuotes(std::wstring_view str);

    // Extracts clean .exe filename from a full path or command name.
    // If the path is a .lnk shortcut, resolves it to the underlying .exe.
    static std::wstring ExtractExeName(std::wstring_view targetPath);

    // Resolves a .lnk shortcut file to its target executable path.
    static bool ResolveShortcut(std::wstring_view shortcutPath, std::wstring& targetPath);

    // Queries process executable name (NtQuerySystemInformation with Toolhelp32 fallback).
    static std::wstring GetProcessNameFromPid(DWORD pid);

    // Checks if the current foreground window belongs to the target application.
    static bool IsForegroundProcessTarget(std::wstring_view targetPath);

    // Faster overload using pre-resolved executable name and clean target path.
    static bool IsForegroundProcessTargetResolved(std::wstring_view targetExe, std::wstring_view cleanTarget);

    // Finds an existing top-level window for the target application.
    // If mustBeElevated is true, prioritizes or requires an elevated instance.
    // Sets foundElevated to whether the discovered instance is running elevated.
    static HWND FindTargetWindow(std::wstring_view targetPath, bool mustBeElevated, bool& foundElevated);

    // Faster overload using pre-resolved executable name and clean target path.
    static HWND FindTargetWindowResolved(std::wstring_view targetExe, std::wstring_view cleanTarget, bool mustBeElevated, bool& foundElevated);

    // Brings an existing window to the foreground, restoring it if minimized,
    // and bypassing Windows focus-stealing restrictions.
    static bool ActivateWindow(HWND hwnd);

    // Checks whether a given process ID is running with elevated administrator privileges.
    static bool IsProcessElevated(DWORD pid, bool& isElevated);
};
