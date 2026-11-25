#pragma once

#include "Config.hpp"
#include <windows.h>

class ActionRunner {
public:
    static void Initialize() noexcept;
    static void Shutdown() noexcept;
    static void MaskStartMenu() noexcept;
    static void ReleaseModifiers() noexcept;
    static void ExecuteSaved() noexcept;
    static void Execute(const AppConfig& config);

private:
    static void ExecuteResolved(const ResolvedConfig& rc);
    static VOID CALLBACK SavedWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context, PTP_WORK Work);
    static VOID CALLBACK AdHocWorkCallback(PTP_CALLBACK_INSTANCE Instance, PVOID Context);

    static PTP_WORK s_tpWork;
};
