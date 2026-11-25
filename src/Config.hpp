#pragma once

#include <windows.h>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>
#include "ComHelpers.hpp"

struct AppConfig {
    std::wstring actionType = L"LaunchApp";
    std::wstring targetPath = L"wt.exe";
    std::wstring arguments = L"";
    std::wstring workingDirectory = L"";
    bool runAsAdmin = false;
    bool startWithWindows = false;
    int debounceMs = 300;
};

struct ResolvedConfig : public RefCounted<ResolvedConfig> {
    AppConfig config;
    std::wstring expandedTarget;
    std::wstring expandedArgs;
    std::wstring expandedWorkDir;
    std::wstring cleanTarget;
    std::wstring targetExe;
    bool isOpenUrl = false;
    bool isRunCommand = false;
};

class ConfigManager {
public:
    static const std::wstring& GetAppDirectory();
    static const std::wstring& GetConfigPath();
    static AppConfig Load();
    static AppConfig GetCached();
    static RefPtr<const ResolvedConfig> GetResolvedSnapshot();
    static ResolvedConfig ResolveConfig(const AppConfig& config);
    static void Save(const AppConfig& config);
    static void SetStartup(bool enable);
    static bool IsStartupEnabled();

    // Lock-free accessors for the WH_KEYBOARD_LL hot path
    static uint32_t GetDebounceMs() noexcept { return s_debounceMs.load(std::memory_order_relaxed); }
    static bool HasTarget() noexcept { return s_hasTarget.load(std::memory_order_relaxed); }

private:
    static RefPtr<const ResolvedConfig> s_resolved;
    static SRWLOCK s_configLock;
    static std::atomic<uint32_t> s_debounceMs;
    static std::atomic<bool> s_hasTarget;
    static INIT_ONCE s_appDirOnce;
    static INIT_ONCE s_configPathOnce;
    static std::wstring s_appDir;
    static std::wstring s_configPath;

    static void UpdateCachedLocked(const AppConfig& cfg);
    static std::wstring Utf8ToWide(std::string_view str);
    static void AppendUtf8(std::string& out, std::wstring_view wstr);
    static void AppendJsonEscaped(std::string& out, std::wstring_view wstr);
    static std::string UnescapeJsonString(std::string_view str);
};
