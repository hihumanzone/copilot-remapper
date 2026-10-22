#include "Config.hpp"
#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <cctype>
#include <vector>

AppConfig ConfigManager::s_cached;
bool ConfigManager::s_hasCached = false;

std::wstring ConfigManager::Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

std::string ConfigManager::WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string result(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], size, nullptr, nullptr);
    return result;
}

std::string ConfigManager::EscapeJsonString(const std::string& str) {
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

std::string ConfigManager::UnescapeJsonString(const std::string& str) {
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

std::wstring ConfigManager::GetAppDirectory() {
    wchar_t exePath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return L".";
    
    std::wstring dir = exePath;
    size_t pos = dir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dir = dir.substr(0, pos);
    }
    return dir;
}

std::wstring ConfigManager::GetConfigPath() {
    std::wstring dir = GetAppDirectory();
    std::wstring localConfig = dir + L"\\copilot_config.json";
    std::wstring testFile = localConfig + L".tmp";

    // Test writability in application directory
    HANDLE hFile = CreateFileW(testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
        return localConfig;
    }

    // Fallback to %APPDATA%\CopilotRemapper\copilot_config.json
    wchar_t appData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        std::wstring fallbackDir = std::wstring(appData) + L"\\CopilotRemapper";
        CreateDirectoryW(fallbackDir.c_str(), nullptr);
        return fallbackDir + L"\\copilot_config.json";
    }

    return localConfig;
}

AppConfig ConfigManager::Load() {
    if (s_hasCached) return s_cached;

    std::wstring path = GetConfigPath();
    AppConfig cfg;

    std::ifstream file(path.c_str(), std::ios::binary);
    if (file.is_open()) {
        std::stringstream ss;
        ss << file.rdbuf();
        std::string json = ss.str();
        file.close();

        // Lightweight parser for JSON key-value pairs
        size_t idx = 0;
        auto skipWs = [&]() {
            while (idx < json.size() && (std::isspace(static_cast<unsigned char>(json[idx])) || json[idx] == ',' || json[idx] == '{' || json[idx] == '}')) {
                idx++;
            }
        };

        auto parseString = [&]() -> std::string {
            if (idx >= json.size() || json[idx] != '\"') return "";
            idx++; // skip opening quote
            size_t start = idx;
            while (idx < json.size()) {
                if (json[idx] == '\\') {
                    idx += 2;
                } else if (json[idx] == '\"') {
                    std::string val = json.substr(start, idx - start);
                    idx++; // skip closing quote
                    return UnescapeJsonString(val);
                } else {
                    idx++;
                }
            }
            return "";
        };

        while (idx < json.size()) {
            skipWs();
            if (idx >= json.size()) break;
            if (json[idx] != '\"') {
                idx++;
                continue;
            }

            std::string key = parseString();
            skipWs();
            if (idx < json.size() && json[idx] == ':') idx++;
            skipWs();

            if (idx < json.size() && json[idx] == '\"') {
                std::string val = parseString();
                if (key == "ActionType") cfg.actionType = Utf8ToWide(val);
                else if (key == "TargetPath") cfg.targetPath = Utf8ToWide(val);
                else if (key == "Arguments") cfg.arguments = Utf8ToWide(val);
                else if (key == "WorkingDirectory") cfg.workingDirectory = Utf8ToWide(val);
            } else if (idx < json.size()) {
                size_t valStart = idx;
                while (idx < json.size() && json[idx] != ',' && json[idx] != '}' && !std::isspace(static_cast<unsigned char>(json[idx]))) {
                    idx++;
                }
                std::string token = json.substr(valStart, idx - valStart);
                if (key == "RunAsAdmin") {
                    cfg.runAsAdmin = (token == "true" || token == "1");
                } else if (key == "StartWithWindows") {
                    cfg.startWithWindows = (token == "true" || token == "1");
                } else if (key == "DebounceMs") {
                    try { cfg.debounceMs = std::stoi(token); } catch (...) {}
                }
            }
        }
        s_cached = cfg;
        s_hasCached = true;
        return cfg;
    }

    // Default configuration and save
    Save(cfg);
    s_cached = cfg;
    s_hasCached = true;
    return cfg;
}

void ConfigManager::Save(const AppConfig& cfg) {
    s_cached = cfg;
    s_hasCached = true;

    std::wstring path = GetConfigPath();
    std::ofstream file(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return;

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"ActionType\": \"" << EscapeJsonString(WideToUtf8(cfg.actionType)) << "\",\n";
    ss << "  \"TargetPath\": \"" << EscapeJsonString(WideToUtf8(cfg.targetPath)) << "\",\n";
    ss << "  \"Arguments\": \"" << EscapeJsonString(WideToUtf8(cfg.arguments)) << "\",\n";
    ss << "  \"WorkingDirectory\": \"" << EscapeJsonString(WideToUtf8(cfg.workingDirectory)) << "\",\n";
    ss << "  \"RunAsAdmin\": " << (cfg.runAsAdmin ? "true" : "false") << ",\n";
    ss << "  \"StartWithWindows\": " << (cfg.startWithWindows ? "true" : "false") << ",\n";
    ss << "  \"DebounceMs\": " << cfg.debounceMs << "\n";
    ss << "}\n";

    std::string out = ss.str();
    file.write(out.data(), out.size());
    file.close();
}

static bool RunProcessSilently(const std::wstring& cmd, DWORD* outExitCode = nullptr) {
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = { 0 };

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
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
    // 1. Manage Windows Task Scheduler task (for elevated silent auto-start at logon without UAC prompts)
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
            std::ofstream xmlFile(xmlPath.c_str(), std::ios::binary | std::ios::trunc);
            if (xmlFile.is_open()) {
                xmlFile.write(xmlContent.data(), xmlContent.size());
                xmlFile.close();

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

    // 2. Clean up legacy HKCU\...\Run registry entry to prevent duplicate launches
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, L"CopilotRemapper");
        RegCloseKey(hKey);
    }
}

bool ConfigManager::IsStartupEnabled() {
    // Check Task Scheduler first
    DWORD exitCode = 1;
    if (RunProcessSilently(L"schtasks.exe /Query /TN \"CopilotRemapper\"", &exitCode)) {
        if (exitCode == 0) {
            return true;
        }
    }

    // Fallback: check legacy registry Run key
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
