#pragma once

#include "Config.hpp"

class ActionRunner {
public:
    static void MaskStartMenu();
    static void ReleaseModifiers();
    static void Execute(const AppConfig& config);

private:
    static std::wstring ExpandEnv(const std::wstring& input);
};
