#include "Config.hpp"
#include "ComHelpers.hpp"
#include "ProcessManager.hpp"
#include <windows.h>
#include <shlobj.h>
#include <taskschd.h>
#include <charconv>
#include <iterator>
#include <string>
#include <vector>

RefPtr<const ResolvedConfig> ConfigManager::s_resolved;
SRWLOCK ConfigManager::s_configLock = SRWLOCK_INIT;
std::atomic<uint32_t> ConfigManager::s_debounceMs{ 300 };
std::atomic<bool> ConfigManager::s_hasTarget{ true };
INIT_ONCE ConfigManager::s_appDirOnce = INIT_ONCE_STATIC_INIT;
INIT_ONCE ConfigManager::s_configPathOnce = INIT_ONCE_STATIC_INIT;
std::wstring ConfigManager::s_appDir;
std::wstring ConfigManager::s_configPath;

namespace {

constexpr wchar_t kTaskName[] = L"CopilotRemapper";
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

inline bool IsJsonWhitespace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::wstring ExpandEnv(const std::wstring& input) {
    if (input.empty() || input.find(L'%') == std::wstring::npos) {
        return input;
    }

    DWORD needed = ExpandEnvironmentStringsW(input.c_str(), nullptr, 0);
    if (needed == 0) return input;

    std::wstring result(needed, L'\0');
    DWORD written = ExpandEnvironmentStringsW(input.c_str(), result.data(), needed);
    if (written == 0) return input;
    if (!result.empty() && result.back() == L'\0') {
        result.pop_back();
    }
    return result;
}

std::wstring GetCurrentExePath() {
    wchar_t stackBuf[1024];
    DWORD len = GetModuleFileNameW(nullptr, stackBuf, static_cast<DWORD>(std::size(stackBuf)));
    if (len > 0 && len < std::size(stackBuf)) {
        return std::wstring(stackBuf, len);
    }
    if (len == std::size(stackBuf)) {
        std::wstring buf(32768, L'\0');
        len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len > 0 && len < buf.size()) {
            buf.resize(len);
            return buf;
        }
    }
    return {};
}

bool ConnectTaskScheduler(ComPtr<ITaskService>& outService, ComPtr<ITaskFolder>& outRootFolder) {
    if (FAILED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_ITaskService, outService.PutVoid()))) {
        return false;
    }

    VARIANT emptyVar;
    VariantInit(&emptyVar);
    if (FAILED(outService->Connect(emptyVar, emptyVar, emptyVar, emptyVar))) {
        return false;
    }

    Bstr rootPath(L"\\");
    return SUCCEEDED(outService->GetFolder(rootPath, outRootFolder.GetAddressOf()));
}

} // namespace

std::wstring ConfigManager::Utf8ToWide(std::string_view str) {
    if (str.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (size <= 0) return {};
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), size);
    return result;
}

void ConfigManager::AppendUtf8(std::string& out, std::wstring_view wstr) {
    if (wstr.empty()) return;
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return;
    const size_t oldLen = out.size();
    out.resize(oldLen + static_cast<size_t>(size));
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), out.data() + oldLen, size, nullptr, nullptr);
}

void ConfigManager::AppendJsonEscaped(std::string& out, std::wstring_view wstr) {
    if (wstr.empty()) return;

    char stackUtf8[512];
    std::string heapUtf8;
    std::string_view utf8;

    int needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return;

    if (static_cast<size_t>(needed) <= sizeof(stackUtf8)) {
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), stackUtf8, needed, nullptr, nullptr);
        utf8 = std::string_view(stackUtf8, static_cast<size_t>(needed));
    } else {
        heapUtf8.resize(static_cast<size_t>(needed));
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), heapUtf8.data(), needed, nullptr, nullptr);
        utf8 = heapUtf8;
    }

    for (char c : utf8) {
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
    InitOnceExecuteOnce(&s_appDirOnce, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        std::wstring exePath = GetCurrentExePath();
        const size_t slashPos = exePath.rfind(L'\\');
        if (slashPos != std::wstring::npos) {
            exePath.resize(slashPos);
            s_appDir = std::move(exePath);
        } else {
            s_appDir = L".";
        }
        return TRUE;
    }, nullptr, nullptr);
    return s_appDir;
}

const std::wstring& ConfigManager::GetConfigPath() {
    InitOnceExecuteOnce(&s_configPathOnce, [](PINIT_ONCE, PVOID, PVOID*) -> BOOL {
        const std::wstring& dir = GetAppDirectory();
        std::wstring localConfig = dir + L"\\copilot_config.json";

        if (GetFileAttributesW(localConfig.c_str()) != INVALID_FILE_ATTRIBUTES) {
            s_configPath = std::move(localConfig);
            return TRUE;
        }

        std::wstring testFile = localConfig + L".tmp";
        HANDLE hFile = CreateFileW(
            testFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            CloseHandle(hFile);
            s_configPath = std::move(localConfig);
            return TRUE;
        }

        PWSTR appDataPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appDataPath)) && appDataPath) {
            std::wstring fallbackDir = std::wstring(appDataPath) + L"\\CopilotRemapper";
            CoTaskMemFree(appDataPath);
            CreateDirectoryW(fallbackDir.c_str(), nullptr);
            s_configPath = fallbackDir + L"\\copilot_config.json";
            return TRUE;
        }
        if (appDataPath) CoTaskMemFree(appDataPath);

        s_configPath = std::move(localConfig);
        return TRUE;
    }, nullptr, nullptr);
    return s_configPath;
}

ResolvedConfig ConfigManager::ResolveConfig(const AppConfig& config) {
    ResolvedConfig rc;
    rc.config = config;
    rc.expandedTarget = ExpandEnv(config.targetPath);
    rc.expandedArgs = ExpandEnv(config.arguments);
    rc.expandedWorkDir = ExpandEnv(config.workingDirectory);
    rc.isOpenUrl = (config.actionType == L"OpenUrl");
    rc.isRunCommand = (!rc.isOpenUrl && config.actionType == L"RunCommand");
    if (!rc.isOpenUrl && !rc.isRunCommand && !rc.expandedTarget.empty()) {
        rc.cleanTarget = ProcessManager::TrimQuotes(rc.expandedTarget);
        rc.targetExe = ProcessManager::ExtractExeName(rc.expandedTarget);
    }
    return rc;
}

void ConfigManager::UpdateCachedLocked(const AppConfig& cfg) {
    RefPtr<const ResolvedConfig> resolved(new ResolvedConfig(ResolveConfig(cfg)));
    const uint32_t clampedDebounce = (cfg.debounceMs >= 50) ? static_cast<uint32_t>(cfg.debounceMs) : 300u;
    const bool hasTarget = !cfg.targetPath.empty();

    AcquireSRWLockExclusive(&s_configLock);
    s_resolved = std::move(resolved);
    ReleaseSRWLockExclusive(&s_configLock);

    s_debounceMs.store(clampedDebounce, std::memory_order_relaxed);
    s_hasTarget.store(hasTarget, std::memory_order_relaxed);
}

RefPtr<const ResolvedConfig> ConfigManager::GetResolvedSnapshot() {
    AcquireSRWLockShared(&s_configLock);
    RefPtr<const ResolvedConfig> snap = s_resolved;
    ReleaseSRWLockShared(&s_configLock);

    if (snap) return snap;

    Load();
    AcquireSRWLockShared(&s_configLock);
    snap = s_resolved;
    ReleaseSRWLockShared(&s_configLock);
    return snap;
}

AppConfig ConfigManager::GetCached() {
    return GetResolvedSnapshot()->config;
}

AppConfig ConfigManager::Load() {
    const std::wstring& path = GetConfigPath();
    AppConfig cfg;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER liSize = {};
        if (GetFileSizeEx(hFile, &liSize) && liSize.QuadPart > 0 && liSize.QuadPart < 1024 * 1024) {
            const DWORD dwSize = static_cast<DWORD>(liSize.QuadPart);
            std::string json(dwSize, '\0');
            DWORD dwRead = 0;
            if (ReadFile(hFile, json.data(), dwSize, &dwRead, nullptr) && dwRead > 0) {
                std::string_view sv(json.data(), dwRead);

                size_t idx = 0;
                auto skipWsAndDelims = [&]() noexcept {
                    while (idx < sv.size() && (IsJsonWhitespace(sv[idx]) || sv[idx] == ',' || sv[idx] == '{' || sv[idx] == '}')) {
                        ++idx;
                    }
                };

                auto parseString = [&]() noexcept -> std::string_view {
                    if (idx >= sv.size() || sv[idx] != '\"') return {};
                    ++idx;
                    const size_t start = idx;
                    while (idx < sv.size()) {
                        if (sv[idx] == '\\' && idx + 1 < sv.size()) {
                            idx += 2;
                        } else if (sv[idx] == '\"') {
                            std::string_view val = sv.substr(start, idx - start);
                            ++idx;
                            return val;
                        } else {
                            ++idx;
                        }
                    }
                    return {};
                };

                auto decodeValue = [&](std::string_view raw) -> std::wstring {
                    if (raw.find('\\') == std::string_view::npos) {
                        return Utf8ToWide(raw);
                    }
                    return Utf8ToWide(UnescapeJsonString(raw));
                };

                while (idx < sv.size()) {
                    skipWsAndDelims();
                    if (idx >= sv.size()) break;
                    if (sv[idx] != '\"') {
                        ++idx;
                        continue;
                    }

                    std::string_view key = parseString();
                    skipWsAndDelims();
                    if (idx < sv.size() && sv[idx] == ':') ++idx;
                    skipWsAndDelims();

                    if (idx < sv.size() && sv[idx] == '\"') {
                        std::string_view rawVal = parseString();
                        if (key == "ActionType") cfg.actionType = decodeValue(rawVal);
                        else if (key == "TargetPath") cfg.targetPath = decodeValue(rawVal);
                        else if (key == "Arguments") cfg.arguments = decodeValue(rawVal);
                        else if (key == "WorkingDirectory") cfg.workingDirectory = decodeValue(rawVal);
                    } else if (idx < sv.size()) {
                        const size_t valStart = idx;
                        while (idx < sv.size() && sv[idx] != ',' && sv[idx] != '}' && !IsJsonWhitespace(sv[idx])) {
                            ++idx;
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

        UpdateCachedLocked(cfg);
        return cfg;
    }

    Save(cfg);
    return cfg;
}

void ConfigManager::Save(const AppConfig& cfg) {
    UpdateCachedLocked(cfg);

    const std::wstring& path = GetConfigPath();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    std::string out;
    out.reserve(512);
    out += "{\n  \"ActionType\": \"";
    AppendJsonEscaped(out, cfg.actionType);
    out += "\",\n  \"TargetPath\": \"";
    AppendJsonEscaped(out, cfg.targetPath);
    out += "\",\n  \"Arguments\": \"";
    AppendJsonEscaped(out, cfg.arguments);
    out += "\",\n  \"WorkingDirectory\": \"";
    AppendJsonEscaped(out, cfg.workingDirectory);
    out += "\",\n  \"RunAsAdmin\": ";
    out += (cfg.runAsAdmin ? "true" : "false");
    out += ",\n  \"StartWithWindows\": ";
    out += (cfg.startWithWindows ? "true" : "false");
    out += ",\n  \"DebounceMs\": ";

    char numBuf[16];
    auto [ptr, ec] = std::to_chars(numBuf, numBuf + sizeof(numBuf), cfg.debounceMs);
    if (ec == std::errc()) {
        out.append(numBuf, ptr);
    } else {
        out += "300";
    }
    out += "\n}\n";

    DWORD dwWritten = 0;
    WriteFile(hFile, out.data(), static_cast<DWORD>(out.size()), &dwWritten, nullptr);
    CloseHandle(hFile);
}

void ConfigManager::SetStartup(bool enable) {
    {
        CoInitGuard co;
        ComPtr<ITaskService> pService;
        ComPtr<ITaskFolder> pRootFolder;
        if (ConnectTaskScheduler(pService, pRootFolder)) {
            Bstr taskName(kTaskName);
            if (!enable) {
                pRootFolder->DeleteTask(taskName, 0);
            } else {
                const std::wstring exePath = GetCurrentExePath();
                ComPtr<ITaskDefinition> pTaskDef;
                if (!exePath.empty() && SUCCEEDED(pService->NewTask(0, pTaskDef.GetAddressOf()))) {
                    ComPtr<IRegistrationInfo> pRegInfo;
                    if (SUCCEEDED(pTaskDef->get_RegistrationInfo(pRegInfo.GetAddressOf()))) {
                        pRegInfo->put_Author(Bstr(L"CopilotRemapper"));
                        pRegInfo->put_Description(Bstr(L"Launches Copilot Remapper silently on user logon with administrator privileges"));
                    }

                    ComPtr<IPrincipal> pPrincipal;
                    if (SUCCEEDED(pTaskDef->get_Principal(pPrincipal.GetAddressOf()))) {
                        pPrincipal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
                        pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
                    }

                    ComPtr<ITaskSettings> pSettings;
                    if (SUCCEEDED(pTaskDef->get_Settings(pSettings.GetAddressOf()))) {
                        pSettings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
                        pSettings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
                        pSettings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW);
                        pSettings->put_ExecutionTimeLimit(Bstr(L"PT0S"));
                    }

                    ComPtr<ITriggerCollection> pTriggers;
                    if (SUCCEEDED(pTaskDef->get_Triggers(pTriggers.GetAddressOf()))) {
                        ComPtr<ITrigger> pTrigger;
                        pTriggers->Create(TASK_TRIGGER_LOGON, pTrigger.GetAddressOf());
                    }

                    ComPtr<IActionCollection> pActions;
                    if (SUCCEEDED(pTaskDef->get_Actions(pActions.GetAddressOf()))) {
                        ComPtr<IAction> pAction;
                        if (SUCCEEDED(pActions->Create(TASK_ACTION_EXEC, pAction.GetAddressOf()))) {
                            ComPtr<IExecAction> pExecAction;
                            if (SUCCEEDED(pAction->QueryInterface(IID_IExecAction, pExecAction.PutVoid()))) {
                                pExecAction->put_Path(Bstr(exePath));
                                pExecAction->put_Arguments(Bstr(L"--silent"));
                            }
                        }
                    }

                    VARIANT emptyVar;
                    VariantInit(&emptyVar);
                    ComPtr<IRegisteredTask> pRegistered;
                    pRootFolder->RegisterTaskDefinition(
                        taskName,
                        pTaskDef.Get(),
                        TASK_CREATE_OR_UPDATE,
                        emptyVar,
                        emptyVar,
                        TASK_LOGON_INTERACTIVE_TOKEN,
                        emptyVar,
                        pRegistered.GetAddressOf());
                }
            }
        }
    }

    // Clean up legacy HKCU\...\Run registry entry
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
        RegDeleteValueW(hKey, kTaskName);
        RegCloseKey(hKey);
    }
}

bool ConfigManager::IsStartupEnabled() {
    {
        CoInitGuard co;
        ComPtr<ITaskService> pService;
        ComPtr<ITaskFolder> pRootFolder;
        if (ConnectTaskScheduler(pService, pRootFolder)) {
            ComPtr<IRegisteredTask> pTask;
            if (SUCCEEDED(pRootFolder->GetTask(Bstr(kTaskName), pTask.GetAddressOf())) && pTask) {
                VARIANT_BOOL enabled = VARIANT_FALSE;
                if (SUCCEEDED(pTask->get_Enabled(&enabled))) {
                    return enabled == VARIANT_TRUE;
                }
                return true;
            }
        }
    }

    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        DWORD type = 0;
        DWORD size = 0;
        LONG res = RegQueryValueExW(hKey, kTaskName, nullptr, &type, nullptr, &size);
        RegCloseKey(hKey);
        return (res == ERROR_SUCCESS);
    }
    return false;
}
