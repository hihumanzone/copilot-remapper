#include "Config.hpp"
#include <windows.h>
#include <shlobj.h>
#include <cctype>
#include <charconv>
#include <vector>

AppConfig ConfigManager::s_cached;
bool ConfigManager::s_hasCached = false;
SRWLOCK ConfigManager::s_configLock = SRWLOCK_INIT;
std::wstring ConfigManager::s_appDir;
std::wstring ConfigManager::s_configPath;

std::wstring ConfigManager::Utf8ToWide(std::string_view str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), &result[0], size);
    return result;
}

std::string ConfigManager::WideToUtf8(std::wstring_view wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &result[0], size, nullptr, nullptr);
    return result;
}

std::string ConfigManager::EscapeJsonString(std::string_view str) {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:   out += c; break;
        }
    }
    return out;
}

std::string ConfigManager::UnescapeJsonString(std::string_view str) {
    std::string out;
    out.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '\\' && i + 1 < str.size()) {
            ++i;
            switch (str[i]) {
                case '\"': out += '\"'; break;
                case '\\': out += '\\'; break;
                case '/':  out += '/'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                default:   out += str[i]; break;
            }
        } else {
            out += str[i];
        }
    }
    return out;
}

const std::wstring& ConfigManager::GetAppDirectory() {
    if (!s_appDir.empty()) return s_appDir;

    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) != 0) {
        wchar_t* pSlash = wcsrchr(exePath, L'\\');
        if (pSlash) {
            *pSlash = L'\0';
            s_appDir = exePath;
            return s_appDir;
        }
    }
    s_appDir = L".";
    return s_appDir;
}

const std::wstring& ConfigManager::GetConfigPath() {
    if (!s_configPath.empty()) return s_configPath;

    const std::wstring& dir = GetAppDirectory();
    std::wstring localConfig = dir + L"\\copilot_config.json";

    // If file already exists locally, use it directly (0 disk probe)
    DWORD dwAttrib = GetFileAttributesW(localConfig.c_str());
    if (dwAttrib != INVALID_FILE_ATTRIBUTES) {
        s_configPath = localConfig;
        return s_configPath;
    }

    // Test writability in application directory
    std::wstring testFile = localConfig + L".tmp";
    HANDLE hFile = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        s_configPath = localConfig;
        return s_configPath;
    }

    // Fallback to %APPDATA%\CopilotRemapper\copilot_config.json
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::wstring fallbackDir = std::wstring(appData) + L"\\CopilotRemapper";
        CreateDirectoryW(fallbackDir.c_str(), nullptr);
        s_configPath = fallbackDir + L"\\copilot_config.json";
        return s_configPath;
    }

    s_configPath = localConfig;
    return s_configPath;
}

AppConfig ConfigManager::GetCached() {
    AcquireSRWLockShared(&s_configLock);
    bool hasCached = s_hasCached;
    AppConfig cfg = s_cached;
    ReleaseSRWLockShared(&s_configLock);

    if (hasCached) return cfg;
    return Load();
}

AppConfig ConfigManager::Load() {
    AcquireSRWLockShared(&s_configLock);
    if (s_hasCached) {
        AppConfig cfg = s_cached;
        ReleaseSRWLockShared(&s_configLock);
        return cfg;
    }
    ReleaseSRWLockShared(&s_configLock);

    const std::wstring& path = GetConfigPath();
    AppConfig cfg;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER liSize;
        if (GetFileSizeEx(hFile, &liSize) && liSize.QuadPart > 0 && liSize.QuadPart < 1024 * 1024) {
            DWORD dwSize = static_cast<DWORD>(liSize.QuadPart);
            std::string json(dwSize, '\0');
            DWORD dwRead = 0;
            if (ReadFile(hFile, &json[0], dwSize, &dwRead, nullptr) && dwRead > 0) {
                json.resize(dwRead);
                std::string_view sv(json);

                size_t idx = 0;
                auto skipWs = [&]() {
                    while (idx < sv.size() && (std::isspace(static_cast<unsigned char>(sv[idx])) || sv[idx] == ',' || sv[idx] == '{' || sv[idx] == '}')) {
                        idx++;
                    }
                };

                auto parseString = [&]() -> std::string_view {
                    if (idx >= sv.size() || sv[idx] != '\"') return "";
                    idx++;
                    size_t start = idx;
                    while (idx < sv.size()) {
                        if (sv[idx] == '\\' && idx + 1 < sv.size()) {
                            idx += 2;
                        } else if (sv[idx] == '\"') {
                            std::string_view val = sv.substr(start, idx - start);
                            idx++;
                            return val;
                        } else {
                            idx++;
                        }
                    }
                    return "";
                };

                while (idx < sv.size()) {
                    skipWs();
                    if (idx >= sv.size()) break;
                    if (sv[idx] != '\"') {
                        idx++;
                        continue;
                    }

                    std::string_view key = parseString();
                    skipWs();
                    if (idx < sv.size() && sv[idx] == ':') idx++;
                    skipWs();

                    if (idx < sv.size() && sv[idx] == '\"') {
                        std::string unescaped = UnescapeJsonString(parseString());
                        if (key == "ActionType") cfg.actionType = Utf8ToWide(unescaped);
                        else if (key == "TargetPath") cfg.targetPath = Utf8ToWide(unescaped);
                        else if (key == "Arguments") cfg.arguments = Utf8ToWide(unescaped);
                        else if (key == "WorkingDirectory") cfg.workingDirectory = Utf8ToWide(unescaped);
                    } else if (idx < sv.size()) {
                        size_t valStart = idx;
                        while (idx < sv.size() && sv[idx] != ',' && sv[idx] != '}' && !std::isspace(static_cast<unsigned char>(sv[idx]))) {
                            idx++;
                        }
                        std::string_view token = sv.substr(valStart, idx - valStart);
                        if (key == "RunAsAdmin") {
                            cfg.runAsAdmin = (token == "true" || token == "1");
                        } else if (key == "StartWithWindows") {
                            cfg.startWithWindows = (token == "true" || token == "1");
                        } else if (key == "DebounceMs") {
                            int val = 300;
                            auto res = std::from_chars(token.data(), token.data() + token.size(), val);
                            if (res.ec == std::errc()) {
                                cfg.debounceMs = val;
                            }
                        }
                    }
                }
            }
        }
        CloseHandle(hFile);

        AcquireSRWLockExclusive(&s_configLock);
        s_cached = cfg;
        s_hasCached = true;
        ReleaseSRWLockExclusive(&s_configLock);
        return cfg;
    }

    // Default configuration and save
    Save(cfg);
    return cfg;
}

void ConfigManager::Save(const AppConfig& cfg) {
    AcquireSRWLockExclusive(&s_configLock);
    s_cached = cfg;
    s_hasCached = true;
    ReleaseSRWLockExclusive(&s_configLock);

    const std::wstring& path = GetConfigPath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    std::string out;
    out.reserve(512);
    out += "{\n";
    out += "  \"ActionType\": \"" + EscapeJsonString(WideToUtf8(cfg.actionType)) + "\",\n";
    out += "  \"TargetPath\": \"" + EscapeJsonString(WideToUtf8(cfg.targetPath)) + "\",\n";
    out += "  \"Arguments\": \"" + EscapeJsonString(WideToUtf8(cfg.arguments)) + "\",\n";
    out += "  \"WorkingDirectory\": \"" + EscapeJsonString(WideToUtf8(cfg.workingDirectory)) + "\",\n";
    out += "  \"RunAsAdmin\": " + std::string(cfg.runAsAdmin ? "true" : "false") + ",\n";
    out += "  \"StartWithWindows\": " + std::string(cfg.startWithWindows ? "true" : "false") + ",\n";
    out += "  \"DebounceMs\": " + std::to_string(cfg.debounceMs) + "\n";
    out += "}\n";

    DWORD dwWritten = 0;
    WriteFile(hFile, out.data(), static_cast<DWORD>(out.size()), &dwWritten, nullptr);
    CloseHandle(hFile);
}

static bool RunProcessSilently(const std::wstring& cmd, DWORD* outExitCode = nullptr) {
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };

    std::wstring cmdBuf = cmd;
    if (CreateProcessW(nullptr, &cmdBuf[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 5000);
        if (outExitCode) {
            GetExitCodeProcess(pi.hProcess, outExitCode);
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return true;
    }
    return false;
}

void ConfigManager::SetStartup(bool enable) {
    if (enable) {
        wchar_t exePath[MAX_PATH];
        if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
            std::wstring xmlPath = GetAppDirectory() + L"\\copilot_task.xml";
            std::string xmlContent =
                "<?xml version=\"1.0\" encoding=\"UTF-16\"?>\n"
                "<Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\">\n"
                "  <RegistrationInfo>\n"
                "    <Author>CopilotRemapper</Author>\n"
                "    <Description>Launches Copilot Remapper silently on user logon with administrator privileges</Description>\n"
                "  </RegistrationInfo>\n"
                "  <Principals>\n"
                "    <Principal id=\"Author\">\n"
                "      <LogonType>InteractiveToken</LogonType>\n"
                "      <RunLevel>HighestAvailable</RunLevel>\n"
                "    </Principal>\n"
                "  </Principals>\n"
                "  <Settings>\n"
                "    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>\n"
                "    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>\n"
                "    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>\n"
                "    <ExecutionTimeLimit>PT0S</ExecutionTimeLimit>\n"
                "  </Settings>\n"
                "  <Triggers>\n"
                "    <LogonTrigger />\n"
                "  </Triggers>\n"
                "  <Actions Context=\"Author\">\n"
                "    <Exec>\n"
                "      <Command>" + WideToUtf8(exePath) + "</Command>\n"
                "      <Arguments>--silent</Arguments>\n"
                "    </Exec>\n"
                "  </Actions>\n"
                "</Task>\n";

            bool xmlRegistered = false;
            HANDLE hXml = CreateFileW(xmlPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hXml != INVALID_HANDLE_VALUE) {
                DWORD dwWritten = 0;
                WriteFile(hXml, xmlContent.data(), static_cast<DWORD>(xmlContent.size()), &dwWritten, nullptr);
                CloseHandle(hXml);

                DWORD exitCode = 1;
                std::wstring xmlCmd = L"schtasks.exe /Create /TN \"CopilotRemapper\" /XML \"" + xmlPath + L"\" /F";
                if (RunProcessSilently(xmlCmd, &exitCode) && exitCode == 0) {
                    xmlRegistered = true;
                }
                DeleteFileW(xmlPath.c_str());
            }

            if (!xmlRegistered) {
                std::wstring schCmd = L"schtasks.exe /Create /TN \"CopilotRemapper\" /TR \"'" + std::wstring(exePath) + L"' --silent\" /SC ONLOGON /RL HIGHEST /F";
                RunProcessSilently(schCmd);
            }
        }
    } else {
        std::wstring schCmd = L"schtasks.exe /Delete /TN \"CopilotRemapper\" /F";
        RunProcessSilently(schCmd);
    }

    // Clean up legacy HKCU\...\Run registry entry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"CopilotRemapper");
        RegCloseKey(hKey);
    }
}

bool ConfigManager::IsStartupEnabled() {
    DWORD exitCode = 1;
    if (RunProcessSilently(L"schtasks.exe /Query /TN \"CopilotRemapper\"", &exitCode)) {
        if (exitCode == 0) {
            return true;
        }
    }

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        LONG res = RegQueryValueExW(hKey, L"CopilotRemapper", nullptr, &type, nullptr, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}
