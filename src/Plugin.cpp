#include "MouseSensitivityFix/Plugin.h"

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"
#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"
#include "MouseSensitivityFix/MenuFrameworkBridge.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
    msf::CompatibilityManager g_compatibilityManager;
    msf::HookCoordinator g_hookCoordinator;
    msf::MenuFrameworkBridge g_menuFrameworkBridge;
    std::filesystem::path g_runtimeLockPath = std::filesystem::path("Data/SKSE/Plugins/MouseSensitivityFix.runtime.lock");
    std::filesystem::path g_crashStatePath = std::filesystem::path("Data/SKSE/Plugins/MouseSensitivityFix.crashguard.state");

    int ReadCrashCounter(const std::filesystem::path& path, std::int64_t& lastEpoch)
    {
        std::ifstream in(path);
        if (!in.is_open()) {
            lastEpoch = 0;
            return 0;
        }

        int count = 0;
        in >> count >> lastEpoch;
        if (!in.good() && !in.eof()) {
            lastEpoch = 0;
            return 0;
        }
        return count;
    }

    void WriteCrashCounter(const std::filesystem::path& path, int count, std::int64_t epoch)
    {
        std::ofstream out(path, std::ios::trunc);
        if (out.is_open()) {
            out << count << ' ' << epoch;
        }
    }
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

        if (config.enableCrashGuard) {
            const auto nowEpoch = std::chrono::duration_cast<std::chrono::seconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
            if (std::filesystem::exists(g_runtimeLockPath)) {
                std::int64_t lastEpoch = 0;
                int count = ReadCrashCounter(g_crashStatePath, lastEpoch);
                if ((nowEpoch - lastEpoch) <= config.crashWindowSeconds) {
                    ++count;
                } else {
                    count = 1;
                }
                WriteCrashCounter(g_crashStatePath, count, nowEpoch);
                if (count >= config.crashDisableThreshold) {
                    config.enabled = false;
                    configManager.ApplyUiUpdate(config);
                    LogError("Crash-guard threshold reached; disabling plugin behavior for this session.");
                }
            } else {
                WriteCrashCounter(g_crashStatePath, 0, nowEpoch);
            }

            std::ofstream lockFile(g_runtimeLockPath, std::ios::trunc);
            if (lockFile.is_open()) {
                lockFile << nowEpoch;
            }
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
        std::error_code ec;
        std::filesystem::remove(g_runtimeLockPath, ec);
        LogInfo("Shutdown complete.");
    }
}
