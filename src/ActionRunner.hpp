#pragma once

#include "Config.hpp"
#include <string>

class ActionRunner {
public:
    static void MaskStartMenu();
    static void ReleaseModifiers();
    static void Execute(const AppConfig& config);

private:
    static std::wstring ExpandEnv(const std::wstring& input);
    static VOID CALLBACK ThreadPoolActionCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context);
};
