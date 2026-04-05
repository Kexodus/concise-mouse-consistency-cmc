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
        bool verboseLogging{ false };
        bool enableFirstPersonHook{ true };
        bool enableThirdPersonHook{ true };
        bool enableSmoothingRemovalHook{ true };
        bool disableInMenus{ false };
        bool disableWhenLookControlsDisabled{ false };
        bool affectGamepadLook{ true };
        bool suppressFocusSpike{ true };

        bool useCompatibilityPresets{ true };
        bool presetImprovedCamera{ true };
        bool presetSmoothCam{ true };
        bool delegateThirdPersonWhenSmoothCam{ true };
        bool delegateThirdPersonWhenImprovedCamera{ true };
        bool forceOverrideSmoothCam{ false };
        bool forceOverrideImprovedCamera{ false };

        int focusSpikeGapMs{ 350 };
        double globalSensitivity{ 1.0 };
        double mouseXAxisMultiplier{ 1.0 };
        double mouseYAxisMultiplier{ 1.0 };
        double gamepadXAxisMultiplier{ 1.0 };
        double gamepadYAxisMultiplier{ 0.55 };
        double bowAimMouseXMultiplier{ 1.0 };
        double bowAimMouseYMultiplier{ 1.0 };
        double bowAimGamepadXMultiplier{ 1.0 };
        double bowAimGamepadYMultiplier{ 1.0 };
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
