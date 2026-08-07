#include "MouseSensitivityFix/Plugin.h"

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"
#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"
#include "MouseSensitivityFix/MenuFrameworkBridge.h"

#include <filesystem>
#include <string>

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
}

namespace msf
{
    bool Plugin::Initialize()
    {
        LogInfo("Initializing Concise Mouse Consistency.");

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
