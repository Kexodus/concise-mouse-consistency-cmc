#include "MouseSensitivityFix/Config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
    std::string Trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return value.substr(first, (last - first) + 1);
    }

    bool ParseBool(const std::string& value, bool fallback)
    {
        const auto normalized = Trim(value);
        if (normalized == "1" || normalized == "true" || normalized == "True") {
            return true;
        }
        if (normalized == "0" || normalized == "false" || normalized == "False") {
            return false;
        }
        return fallback;
    }

    double ParseDouble(const std::string& value, double fallback)
    {
        try {
            return std::stod(Trim(value));
        } catch (...) {
            return fallback;
        }
    }

    int ParseInt(const std::string& value, int fallback)
    {
        try {
            return std::stoi(Trim(value));
        } catch (...) {
            return fallback;
        }
    }
}

namespace msf
{
    ConfigManager& ConfigManager::Get()
    {
        static ConfigManager instance;
        return instance;
    }

    void ConfigManager::SetConfigPath(std::filesystem::path iniPath)
    {
        std::scoped_lock guard(_lock);
        _configPath = std::move(iniPath);
    }

    bool ConfigManager::LoadFromIni(const std::filesystem::path& iniPath)
    {
        std::ifstream input(iniPath);
        if (!input.is_open()) {
            return false;
        }

        auto loaded = _values;

        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }
            if (line[0] == '[') {
                continue;
            }

            const auto delimiter = line.find('=');
            if (delimiter == std::string::npos) {
                continue;
            }

            const auto key = Trim(line.substr(0, delimiter));
            const auto value = Trim(line.substr(delimiter + 1));

            if (key == "bEnabled") {
                loaded.enabled = ParseBool(value, loaded.enabled);
            } else if (key == "bHotDisable") {
                loaded.hotDisable = ParseBool(value, loaded.hotDisable);
            } else if (key == "bVerboseLogging") {
                loaded.verboseLogging = ParseBool(value, loaded.verboseLogging);
            } else if (key == "bEnableFirstPersonHook") {
                loaded.enableFirstPersonHook = ParseBool(value, loaded.enableFirstPersonHook);
            } else if (key == "bEnableThirdPersonHook") {
                loaded.enableThirdPersonHook = ParseBool(value, loaded.enableThirdPersonHook);
            } else if (key == "bEnableSmoothingRemovalHook") {
                loaded.enableSmoothingRemovalHook = ParseBool(value, loaded.enableSmoothingRemovalHook);
            } else if (key == "bDisableInMenus") {
                loaded.disableInMenus = ParseBool(value, loaded.disableInMenus);
            } else if (key == "bDisableWhenLookControlsDisabled") {
                loaded.disableWhenLookControlsDisabled = ParseBool(value, loaded.disableWhenLookControlsDisabled);
            } else if (key == "bAffectGamepadLook") {
                loaded.affectGamepadLook = ParseBool(value, loaded.affectGamepadLook);
            } else if (key == "bSuppressFocusSpike") {
                loaded.suppressFocusSpike = ParseBool(value, loaded.suppressFocusSpike);
            } else if (key == "bUseCompatibilityPresets") {
                loaded.useCompatibilityPresets = ParseBool(value, loaded.useCompatibilityPresets);
            } else if (key == "bPresetImprovedCamera") {
                loaded.presetImprovedCamera = ParseBool(value, loaded.presetImprovedCamera);
            } else if (key == "bPresetSmoothCam") {
                loaded.presetSmoothCam = ParseBool(value, loaded.presetSmoothCam);
            } else if (key == "bDelegateThirdPersonWhenSmoothCam") {
                loaded.delegateThirdPersonWhenSmoothCam = ParseBool(value, loaded.delegateThirdPersonWhenSmoothCam);
            } else if (key == "bDelegateThirdPersonWhenImprovedCamera") {
                loaded.delegateThirdPersonWhenImprovedCamera = ParseBool(value, loaded.delegateThirdPersonWhenImprovedCamera);
            } else if (key == "bForceOverrideSmoothCam") {
                loaded.forceOverrideSmoothCam = ParseBool(value, loaded.forceOverrideSmoothCam);
            } else if (key == "bForceOverrideImprovedCamera") {
                loaded.forceOverrideImprovedCamera = ParseBool(value, loaded.forceOverrideImprovedCamera);
            } else if (key == "fGlobalSensitivity") {
                loaded.globalSensitivity = std::clamp(ParseDouble(value, loaded.globalSensitivity), 0.01, 20.0);
            } else if (key == "fMouseXAxisMultiplier") {
                loaded.mouseXAxisMultiplier = std::clamp(ParseDouble(value, loaded.mouseXAxisMultiplier), 0.01, 20.0);
            } else if (key == "fMouseYAxisMultiplier") {
                loaded.mouseYAxisMultiplier = std::clamp(ParseDouble(value, loaded.mouseYAxisMultiplier), 0.01, 20.0);
            } else if (key == "fGamepadXAxisMultiplier") {
                loaded.gamepadXAxisMultiplier = std::clamp(ParseDouble(value, loaded.gamepadXAxisMultiplier), 0.01, 20.0);
            } else if (key == "fGamepadYAxisMultiplier") {
                loaded.gamepadYAxisMultiplier = std::clamp(ParseDouble(value, loaded.gamepadYAxisMultiplier), 0.01, 20.0);
            } else if (key == "fBowAimMouseXMultiplier") {
                loaded.bowAimMouseXMultiplier = std::clamp(ParseDouble(value, loaded.bowAimMouseXMultiplier), 0.01, 20.0);
            } else if (key == "fBowAimMouseYMultiplier") {
                loaded.bowAimMouseYMultiplier = std::clamp(ParseDouble(value, loaded.bowAimMouseYMultiplier), 0.01, 20.0);
            } else if (key == "fBowAimGamepadXMultiplier") {
                loaded.bowAimGamepadXMultiplier = std::clamp(ParseDouble(value, loaded.bowAimGamepadXMultiplier), 0.01, 20.0);
            } else if (key == "fBowAimGamepadYMultiplier") {
                loaded.bowAimGamepadYMultiplier = std::clamp(ParseDouble(value, loaded.bowAimGamepadYMultiplier), 0.01, 20.0);
            }
        }

        std::scoped_lock guard(_lock);
        _values = loaded;
        _configPath = iniPath;

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(iniPath, ec);
        _lastWriteTime = ec ? std::optional<std::filesystem::file_time_type>{} : std::make_optional(writeTime);
        return true;
    }

    bool ConfigManager::SaveToIni(const std::filesystem::path& iniPath) const
    {
        std::scoped_lock guard(_lock);

        std::ofstream output(iniPath, std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }

        output << "[General]\n";
        output << "bEnabled=" << (_values.enabled ? "true" : "false") << "\n";
        output << "bHotDisable=" << (_values.hotDisable ? "true" : "false") << "\n";
        output << "fGlobalSensitivity=" << _values.globalSensitivity << "\n";
        output << "bEnableFirstPersonHook=" << (_values.enableFirstPersonHook ? "true" : "false") << "\n";
        output << "bEnableThirdPersonHook=" << (_values.enableThirdPersonHook ? "true" : "false") << "\n";
        output << "bEnableSmoothingRemovalHook=" << (_values.enableSmoothingRemovalHook ? "true" : "false") << "\n";
        output << "bDisableInMenus=" << (_values.disableInMenus ? "true" : "false") << "\n";
        output << "bDisableWhenLookControlsDisabled=" << (_values.disableWhenLookControlsDisabled ? "true" : "false") << "\n";
        output << "bAffectGamepadLook=" << (_values.affectGamepadLook ? "true" : "false") << "\n";
        output << "bSuppressFocusSpike=" << (_values.suppressFocusSpike ? "true" : "false") << "\n";
        output << "[Advanced]\n";
        output << "fMouseXAxisMultiplier=" << _values.mouseXAxisMultiplier << "\n";
        output << "fMouseYAxisMultiplier=" << _values.mouseYAxisMultiplier << "\n";
        output << "fGamepadXAxisMultiplier=" << _values.gamepadXAxisMultiplier << "\n";
        output << "fGamepadYAxisMultiplier=" << _values.gamepadYAxisMultiplier << "\n";
        output << "fBowAimMouseXMultiplier=" << _values.bowAimMouseXMultiplier << "\n";
        output << "fBowAimMouseYMultiplier=" << _values.bowAimMouseYMultiplier << "\n";
        output << "fBowAimGamepadXMultiplier=" << _values.bowAimGamepadXMultiplier << "\n";
        output << "fBowAimGamepadYMultiplier=" << _values.bowAimGamepadYMultiplier << "\n";
        output << "bVerboseLogging=" << (_values.verboseLogging ? "true" : "false") << "\n";
        output << "[Compatibility]\n";
        output << "bUseCompatibilityPresets=" << (_values.useCompatibilityPresets ? "true" : "false") << "\n";
        output << "bPresetImprovedCamera=" << (_values.presetImprovedCamera ? "true" : "false") << "\n";
        output << "bPresetSmoothCam=" << (_values.presetSmoothCam ? "true" : "false") << "\n";
        output << "bDelegateThirdPersonWhenSmoothCam=" << (_values.delegateThirdPersonWhenSmoothCam ? "true" : "false") << "\n";
        output << "bDelegateThirdPersonWhenImprovedCamera=" << (_values.delegateThirdPersonWhenImprovedCamera ? "true" : "false") << "\n";
        output << "bForceOverrideSmoothCam=" << (_values.forceOverrideSmoothCam ? "true" : "false") << "\n";
        output << "bForceOverrideImprovedCamera=" << (_values.forceOverrideImprovedCamera ? "true" : "false") << "\n";

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(iniPath, ec);
        _lastWriteTime = ec ? std::optional<std::filesystem::file_time_type>{} : std::make_optional(writeTime);
        return true;
    }

    bool ConfigManager::ReloadIfChanged()
    {
        std::filesystem::path iniPath;
        std::optional<std::filesystem::file_time_type> knownWriteTime;
        {
            std::scoped_lock guard(_lock);
            iniPath = _configPath;
            knownWriteTime = _lastWriteTime;
        }

        if (iniPath.empty()) {
            return false;
        }

        std::error_code ec;
        const auto currentWriteTime = std::filesystem::last_write_time(iniPath, ec);
        if (ec) {
            return false;
        }

        if (knownWriteTime && *knownWriteTime == currentWriteTime) {
            return false;
        }

        return LoadFromIni(iniPath);
    }

    ConfigValues ConfigManager::GetSnapshot() const
    {
        std::scoped_lock guard(_lock);
        return _values;
    }

    void ConfigManager::ApplyUiUpdate(const ConfigValues& updatedValues)
    {
        std::scoped_lock guard(_lock);
        _values.enabled = updatedValues.enabled;
        _values.hotDisable = updatedValues.hotDisable;
        _values.verboseLogging = updatedValues.verboseLogging;
        _values.enableFirstPersonHook = updatedValues.enableFirstPersonHook;
        _values.enableThirdPersonHook = updatedValues.enableThirdPersonHook;
        _values.enableSmoothingRemovalHook = updatedValues.enableSmoothingRemovalHook;
        _values.disableInMenus = updatedValues.disableInMenus;
        _values.disableWhenLookControlsDisabled = updatedValues.disableWhenLookControlsDisabled;
        _values.affectGamepadLook = updatedValues.affectGamepadLook;
        _values.suppressFocusSpike = updatedValues.suppressFocusSpike;
        _values.useCompatibilityPresets = updatedValues.useCompatibilityPresets;
        _values.presetImprovedCamera = updatedValues.presetImprovedCamera;
        _values.presetSmoothCam = updatedValues.presetSmoothCam;
        _values.delegateThirdPersonWhenSmoothCam = updatedValues.delegateThirdPersonWhenSmoothCam;
        _values.delegateThirdPersonWhenImprovedCamera = updatedValues.delegateThirdPersonWhenImprovedCamera;
        _values.forceOverrideSmoothCam = updatedValues.forceOverrideSmoothCam;
        _values.forceOverrideImprovedCamera = updatedValues.forceOverrideImprovedCamera;
        _values.globalSensitivity = std::clamp(updatedValues.globalSensitivity, 0.01, 20.0);
        _values.mouseXAxisMultiplier = std::clamp(updatedValues.mouseXAxisMultiplier, 0.01, 20.0);
        _values.mouseYAxisMultiplier = std::clamp(updatedValues.mouseYAxisMultiplier, 0.01, 20.0);
        _values.gamepadXAxisMultiplier = std::clamp(updatedValues.gamepadXAxisMultiplier, 0.01, 20.0);
        _values.gamepadYAxisMultiplier = std::clamp(updatedValues.gamepadYAxisMultiplier, 0.01, 20.0);
        _values.bowAimMouseXMultiplier = std::clamp(updatedValues.bowAimMouseXMultiplier, 0.01, 20.0);
        _values.bowAimMouseYMultiplier = std::clamp(updatedValues.bowAimMouseYMultiplier, 0.01, 20.0);
        _values.bowAimGamepadXMultiplier = std::clamp(updatedValues.bowAimGamepadXMultiplier, 0.01, 20.0);
        _values.bowAimGamepadYMultiplier = std::clamp(updatedValues.bowAimGamepadYMultiplier, 0.01, 20.0);
    }
}
