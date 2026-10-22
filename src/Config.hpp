#pragma once

#include <string>

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
    static std::wstring GetAppDirectory();
    static std::wstring GetConfigPath();
    static AppConfig Load();
    static void Save(const AppConfig& config);
    static void SetStartup(bool enable);
    static bool IsStartupEnabled();

private:
    static AppConfig s_cached;
    static bool s_hasCached;

    static std::wstring Utf8ToWide(const std::string& str);
    static std::string WideToUtf8(const std::wstring& wstr);
    static std::string EscapeJsonString(const std::string& str);
    static std::string UnescapeJsonString(const std::string& str);
};
