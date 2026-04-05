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
}

namespace msf
{
    bool Plugin::Initialize()
    {
        LogInfo("Initializing plugin scaffold.");

        auto& configManager = ConfigManager::Get();
        const auto iniPath = std::filesystem::path("Data/SKSE/Plugins/MouseSensitivityFix.ini");
        configManager.SetConfigPath(iniPath);
        const bool loaded = configManager.LoadFromIni(iniPath);
        if (!loaded) {
            LogWarn("INI not found yet, using defaults.");
        }
        auto config = configManager.GetSnapshot();

        if (config.hotDisable) {
            config.enabled = false;
            configManager.ApplyUiUpdate(config);
            LogWarn("Hot-disable is active from INI. Plugin behavior disabled for this session.");
        }

        g_compatibilityManager.ScanInstalledCameraMods(std::filesystem::path("Data/SKSE/Plugins"));

        const auto policy = g_compatibilityManager.EvaluatePolicy(configManager.GetSnapshot());
        LogInfo("Compatibility policy mode=" + std::to_string(static_cast<int>(policy.mode)) +
                " reason=" + policy.reason);

        if (!g_hookCoordinator.Install(policy, configManager.GetSnapshot())) {
            LogError("Failed to install hooks.");
            return false;
        }

        if (!g_menuFrameworkBridge.Initialize()) {
            LogError("Failed to initialize menu framework bridge.");
            return false;
        }

        LogInfo("Initialization complete.");
        return true;
    }

    void Plugin::Shutdown()
    {
        g_menuFrameworkBridge.Shutdown();
        g_hookCoordinator.Remove();
        LogInfo("Shutdown complete.");
    }
}
