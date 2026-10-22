#pragma once

#include <windows.h>
#include <string>

class ProcessManager {
public:
    // Extracts clean .exe filename from a full path or command name.
    // If the path is a .lnk shortcut, resolves it to the underlying .exe.
    static std::wstring ExtractExeName(const std::wstring& targetPath);

    // Resolves a .lnk shortcut file to its target executable path.
    static bool ResolveShortcut(const std::wstring& shortcutPath, std::wstring& targetPath);

    // Queries process executable name using Toolhelp32 snapshot (fallback)
    static std::wstring GetProcessNameFromPid(DWORD pid);

    // Checks if the current foreground window belongs to the target application.
    static bool IsForegroundProcessTarget(const std::wstring& targetPath);

    // Finds an existing top-level window for the target application.
    // If mustBeElevated is true, prioritizes or requires an elevated instance.
    // Sets foundElevated to whether the discovered instance is running elevated.
    static HWND FindTargetWindow(const std::wstring& targetPath, bool mustBeElevated, bool& foundElevated);

    // Brings an existing window to the foreground, restoring it if minimized,
    // and bypassing Windows focus-stealing restrictions.
    static bool ActivateWindow(HWND hwnd);

    // Checks whether a given process ID is running with elevated administrator privileges.
    static bool IsProcessElevated(DWORD pid, bool& isElevated);
};
