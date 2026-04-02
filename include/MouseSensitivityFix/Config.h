#pragma once

#include <filesystem>
#include <optional>
#include <mutex>
#include <string>

namespace msf
{
    struct ConfigValues
    {
        bool enabled{ true };
        bool hotDisable{ false };
        bool enableCrashGuard{ true };
        bool strictRawInputMode{ true };
        bool verboseLogging{ false };
        bool enableFirstPersonHook{ true };
        bool enableThirdPersonHook{ true };
        bool enableSmoothingRemovalHook{ true };
        bool disableInMenus{ true };
        bool disableWhenLookControlsDisabled{ true };
        bool allowInFirstPerson{ true };
        bool allowInThirdPerson{ true };
        bool allowInIronSights{ true };
        bool allowInBowZoom{ true };
        bool allowInCombat{ true };
        bool affectGamepadLook{ true };
        bool suppressFocusSpike{ true };

        bool useCompatibilityPresets{ true };
        bool presetImprovedCamera{ true };
        bool presetSmoothCam{ true };
        bool delegateThirdPersonWhenSmoothCam{ true };
        bool delegateThirdPersonWhenImprovedCamera{ true };
        bool forceOverrideSmoothCam{ false };
        bool forceOverrideImprovedCamera{ false };

        int overrideDetectedImprovedCamera{ -1 };  // -1 auto, 0 false, 1 true
        int overrideDetectedSmoothCam{ -1 };       // -1 auto, 0 false, 1 true

        int crashDisableThreshold{ 3 };
        int crashWindowSeconds{ 600 };
        int focusSpikeGapMs{ 350 };
        double globalSensitivity{ 1.0 };
        double xAxisMultiplier{ 1.0 };
        double yAxisMultiplier{ 1.0 };
        double maxDeltaPerFrame{ 1000.0 };
    };

    class ConfigManager
    {
    public:
        static ConfigManager& Get();

        void SetConfigPath(std::filesystem::path iniPath);
        bool LoadFromIni(const std::filesystem::path& iniPath);
        bool SaveToIni(const std::filesystem::path& iniPath) const;
        bool ReloadIfChanged();

        ConfigValues GetSnapshot() const;
        void ApplyUiUpdate(const ConfigValues& updatedValues);

    private:
        mutable std::mutex _lock;
        ConfigValues _values{};
        mutable std::filesystem::path _configPath{};
        mutable std::optional<std::filesystem::file_time_type> _lastWriteTime{};
    };
}
