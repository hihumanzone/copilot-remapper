#pragma once

#include <windows.h>
#include <string>
#include <string_view>

struct AppConfig {
    std::wstring actionType = L"LaunchApp";
    std::wstring targetPath = L"wt.exe";
    std::wstring arguments = L"";
    std::wstring workingDirectory = L"";
    bool runAsAdmin = false;
    bool startWithWindows = false;
    int debounceMs = 300;
};

class ConfigManager {
public:
    static const std::wstring& GetAppDirectory();
    static const std::wstring& GetConfigPath();
    static AppConfig Load();
    static AppConfig GetCached();
    static void Save(const AppConfig& config);
    static void SetStartup(bool enable);
    static bool IsStartupEnabled();

private:
    static AppConfig s_cached;
    static bool s_hasCached;
    static SRWLOCK s_configLock;
    static std::wstring s_appDir;
    static std::wstring s_configPath;

    static std::wstring Utf8ToWide(std::string_view str);
    static std::string WideToUtf8(std::wstring_view wstr);
    static std::string EscapeJsonString(std::string_view str);
    static std::string UnescapeJsonString(std::string_view str);
};
