#include "MouseSensitivityFix/Plugin.h"

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"
#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"
#include "MouseSensitivityFix/MenuFrameworkBridge.h"

#include <filesystem>
#include <string>

#if MSF_USE_COMMONLIBSSE
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#ifndef MSF_PLUGIN_VERSION
#define MSF_PLUGIN_VERSION "unknown"
#endif

namespace
{
    msf::CompatibilityManager g_compatibilityManager;
    msf::HookCoordinator g_hookCoordinator;
    msf::MenuFrameworkBridge g_menuFrameworkBridge;

    void ReconcileRuntimePolicy(const msf::ConfigValues& config)
    {
        const auto policy = g_compatibilityManager.EvaluatePolicy(config);
        if (g_hookCoordinator.UpdatePolicy(policy)) {
            msf::LogInfo("Compatibility policy updated: mode=" +
                         std::to_string(static_cast<int>(policy.mode)) +
                         " reason=" + policy.reason);
        }
    }

    void LogBuildIdentity()
    {
        // Always-on identity so playtest logs can prove which binary loaded.
        // Feature markers distinguish diagnostic builds that share the 0.1.2 package version.
        std::string message =
            std::string("BuildIdentity version=") + MSF_PLUGIN_VERSION +
            " eagleEyeFovY=0 axisParity=1 renderedFovDiag=1";

#if MSF_USE_COMMONLIBSSE
        HMODULE module = nullptr;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&LogBuildIdentity),
                &module) &&
            module) {
            char modulePath[MAX_PATH]{};
            const DWORD pathLength = GetModuleFileNameA(module, modulePath, MAX_PATH);
            if (pathLength > 0 && pathLength < MAX_PATH) {
                const auto path = std::filesystem::path(modulePath);
                std::error_code ec;
                const auto fileSize = std::filesystem::file_size(path, ec);
                message += " module=\"" + path.filename().string() + "\"";
                if (!ec) {
                    message += " bytes=" + std::to_string(fileSize);
                }
            }
        }
#endif

        msf::LogInfo(message);
    }
}

namespace msf
{
    bool Plugin::Initialize()
    {
        LogInfo("Initializing Concise Mouse Consistency.");
        LogBuildIdentity();

        auto& configManager = ConfigManager::Get();
        const auto iniPath = std::filesystem::path("Data/SKSE/Plugins/MouseSensitivityFix.ini");
        configManager.SetConfigPath(iniPath);
        const bool loaded = configManager.LoadFromIni(iniPath);
        if (!loaded) {
            LogWarn("INI not found yet, using defaults.");
        }
        auto config = configManager.GetSnapshot();

        g_compatibilityManager.ScanInstalledCameraMods(std::filesystem::path("Data/SKSE/Plugins"));
        configManager.SetChangeCallback(ReconcileRuntimePolicy);
        ReconcileRuntimePolicy(config);

        if (!g_hookCoordinator.Install()) {
            configManager.SetChangeCallback({});
            LogError("Failed to install hooks.");
            return false;
        }

        if (!g_menuFrameworkBridge.Initialize()) {
            configManager.SetChangeCallback({});
            g_hookCoordinator.Remove();
            LogError("Failed to initialize menu framework bridge.");
            return false;
        }

        LogInfo("Initialization complete.");
        return true;
    }

    void Plugin::Shutdown()
    {
        ConfigManager::Get().SetChangeCallback({});
        g_menuFrameworkBridge.Shutdown();
        g_hookCoordinator.Remove();
        LogInfo("Shutdown complete.");
    }
}
