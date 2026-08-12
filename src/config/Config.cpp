#include "MouseSensitivityFix/Config.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <utility>

namespace
{
    constexpr auto kReloadPollInterval = std::chrono::milliseconds(250);

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
            const double parsed = std::stod(Trim(value));
            if (!std::isfinite(parsed)) {
                return fallback;
            }
            return parsed;
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

    double FiniteOr(double value, double fallback) noexcept
    {
        return std::isfinite(value) ? value : fallback;
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
        _lastReloadPoll = {};
    }

    void ConfigManager::SetChangeCallback(ChangeCallback callback)
    {
        std::scoped_lock notificationGuard(_notificationLock);
        std::scoped_lock guard(_lock);
        _changeCallback = std::move(callback);
    }

    bool ConfigManager::LoadFromIni(const std::filesystem::path& iniPath)
    {
        std::ifstream input(iniPath);
        if (!input.is_open()) {
            return false;
        }

        std::scoped_lock notificationGuard(_notificationLock);
        ConfigValues loaded;
        {
            std::scoped_lock guard(_lock);
            loaded = _values;
        }

        bool sawReplacementCompatibilityKey = false;
        bool sawLegacyCompatibilityKey = false;
        bool legacyUsePresets = true;
        bool legacyPresetImprovedCamera = true;
        bool legacyPresetSmoothCam = true;
        bool legacyDelegateSmoothCam = true;
        bool legacyDelegateImprovedCamera = true;
        bool legacyForceOverrideSmoothCam = false;
        bool legacyForceOverrideImprovedCamera = false;

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
                // Preserve the old emergency-disable behavior during upgrades.
                if (ParseBool(value, false)) {
                    loaded.enabled = false;
                }
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
            } else if (key == "iFocusSpikeGapMs") {
                loaded.focusSpikeGapMs = std::clamp(ParseInt(value, loaded.focusSpikeGapMs), 50, 5000);
            } else if (key == "bKeepThirdPersonSmoothingRemovalWithCameraMods") {
                sawReplacementCompatibilityKey = true;
                loaded.keepThirdPersonSmoothingRemovalWithCameraMods =
                    ParseBool(value, loaded.keepThirdPersonSmoothingRemovalWithCameraMods);
            } else if (key == "bUseCompatibilityPresets") {
                sawLegacyCompatibilityKey = true;
                legacyUsePresets = ParseBool(value, legacyUsePresets);
            } else if (key == "bPresetImprovedCamera") {
                sawLegacyCompatibilityKey = true;
                legacyPresetImprovedCamera = ParseBool(value, legacyPresetImprovedCamera);
            } else if (key == "bPresetSmoothCam") {
                sawLegacyCompatibilityKey = true;
                legacyPresetSmoothCam = ParseBool(value, legacyPresetSmoothCam);
            } else if (key == "bDelegateThirdPersonWhenSmoothCam") {
                sawLegacyCompatibilityKey = true;
                legacyDelegateSmoothCam = ParseBool(value, legacyDelegateSmoothCam);
            } else if (key == "bDelegateThirdPersonWhenImprovedCamera") {
                sawLegacyCompatibilityKey = true;
                legacyDelegateImprovedCamera = ParseBool(value, legacyDelegateImprovedCamera);
            } else if (key == "bForceOverrideSmoothCam") {
                sawLegacyCompatibilityKey = true;
                legacyForceOverrideSmoothCam = ParseBool(value, legacyForceOverrideSmoothCam);
            } else if (key == "bForceOverrideImprovedCamera") {
                sawLegacyCompatibilityKey = true;
                legacyForceOverrideImprovedCamera = ParseBool(value, legacyForceOverrideImprovedCamera);
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

        if (!sawReplacementCompatibilityKey && sawLegacyCompatibilityKey) {
            const bool delegatedSmoothCam =
                legacyUsePresets &&
                legacyPresetSmoothCam &&
                legacyDelegateSmoothCam &&
                !legacyForceOverrideSmoothCam;
            const bool delegatedImprovedCamera =
                legacyUsePresets &&
                legacyPresetImprovedCamera &&
                legacyDelegateImprovedCamera &&
                !legacyForceOverrideImprovedCamera;
            loaded.keepThirdPersonSmoothingRemovalWithCameraMods =
                !(delegatedSmoothCam || delegatedImprovedCamera);
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(iniPath, ec);
        ChangeCallback callback;
        {
            std::scoped_lock guard(_lock);
            _values = loaded;
            _configPath = iniPath;
            // On mtime failure keep prior tracking so ReloadIfChanged does not
            // treat every 250ms poll as dirty once mtime becomes readable again.
            if (!ec) {
                _lastWriteTime = writeTime;
            }
            callback = _changeCallback;
        }
        if (callback) {
            callback(loaded);
        }
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
        output << "fGlobalSensitivity=" << _values.globalSensitivity << "\n";
        output << "fMouseXAxisMultiplier=" << _values.mouseXAxisMultiplier << "\n";
        output << "fMouseYAxisMultiplier=" << _values.mouseYAxisMultiplier << "\n";
        output << "fGamepadXAxisMultiplier=" << _values.gamepadXAxisMultiplier << "\n";
        output << "fGamepadYAxisMultiplier=" << _values.gamepadYAxisMultiplier << "\n";
        output << "bEnableSmoothingRemovalHook=" << (_values.enableSmoothingRemovalHook ? "true" : "false") << "\n";
        output << "bSuppressFocusSpike=" << (_values.suppressFocusSpike ? "true" : "false") << "\n";
        output << "bAffectGamepadLook=" << (_values.affectGamepadLook ? "true" : "false") << "\n";
        output << "[Advanced]\n";
        output << "bEnableFirstPersonHook=" << (_values.enableFirstPersonHook ? "true" : "false") << "\n";
        output << "bEnableThirdPersonHook=" << (_values.enableThirdPersonHook ? "true" : "false") << "\n";
        output << "bDisableInMenus=" << (_values.disableInMenus ? "true" : "false") << "\n";
        output << "bDisableWhenLookControlsDisabled=" << (_values.disableWhenLookControlsDisabled ? "true" : "false") << "\n";
        output << "iFocusSpikeGapMs=" << _values.focusSpikeGapMs << "\n";
        output << "fBowAimMouseXMultiplier=" << _values.bowAimMouseXMultiplier << "\n";
        output << "fBowAimMouseYMultiplier=" << _values.bowAimMouseYMultiplier << "\n";
        output << "fBowAimGamepadXMultiplier=" << _values.bowAimGamepadXMultiplier << "\n";
        output << "fBowAimGamepadYMultiplier=" << _values.bowAimGamepadYMultiplier << "\n";
        output << "bVerboseLogging=" << (_values.verboseLogging ? "true" : "false") << "\n";
        output << "[Compatibility]\n";
        output << "bKeepThirdPersonSmoothingRemovalWithCameraMods="
               << (_values.keepThirdPersonSmoothingRemovalWithCameraMods ? "true" : "false") << "\n";

        output.flush();
        if (!output) {
            return false;
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(iniPath, ec);
        if (!ec) {
            _lastWriteTime = writeTime;
        }
        return true;
    }

    bool ConfigManager::ReloadIfChanged()
    {
        std::filesystem::path iniPath;
        std::optional<std::filesystem::file_time_type> knownWriteTime;
        {
            std::scoped_lock guard(_lock);
            const auto now = std::chrono::steady_clock::now();
            if (_lastReloadPoll.time_since_epoch().count() != 0 &&
                (now - _lastReloadPoll) < kReloadPollInterval) {
                return false;
            }
            _lastReloadPoll = now;
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
        std::scoped_lock notificationGuard(_notificationLock);
        ConfigValues previous;
        {
            std::scoped_lock guard(_lock);
            previous = _values;
        }

        auto normalized = updatedValues;
        normalized.focusSpikeGapMs = std::clamp(normalized.focusSpikeGapMs, 50, 5000);
        normalized.globalSensitivity = std::clamp(
            FiniteOr(normalized.globalSensitivity, previous.globalSensitivity), 0.01, 20.0);
        normalized.mouseXAxisMultiplier = std::clamp(
            FiniteOr(normalized.mouseXAxisMultiplier, previous.mouseXAxisMultiplier), 0.01, 20.0);
        normalized.mouseYAxisMultiplier = std::clamp(
            FiniteOr(normalized.mouseYAxisMultiplier, previous.mouseYAxisMultiplier), 0.01, 20.0);
        normalized.gamepadXAxisMultiplier = std::clamp(
            FiniteOr(normalized.gamepadXAxisMultiplier, previous.gamepadXAxisMultiplier), 0.01, 20.0);
        normalized.gamepadYAxisMultiplier = std::clamp(
            FiniteOr(normalized.gamepadYAxisMultiplier, previous.gamepadYAxisMultiplier), 0.01, 20.0);
        normalized.bowAimMouseXMultiplier = std::clamp(
            FiniteOr(normalized.bowAimMouseXMultiplier, previous.bowAimMouseXMultiplier), 0.01, 20.0);
        normalized.bowAimMouseYMultiplier = std::clamp(
            FiniteOr(normalized.bowAimMouseYMultiplier, previous.bowAimMouseYMultiplier), 0.01, 20.0);
        normalized.bowAimGamepadXMultiplier = std::clamp(
            FiniteOr(normalized.bowAimGamepadXMultiplier, previous.bowAimGamepadXMultiplier), 0.01, 20.0);
        normalized.bowAimGamepadYMultiplier = std::clamp(
            FiniteOr(normalized.bowAimGamepadYMultiplier, previous.bowAimGamepadYMultiplier), 0.01, 20.0);

        ChangeCallback callback;
        {
            std::scoped_lock guard(_lock);
            _values = normalized;
            callback = _changeCallback;
        }
        if (callback) {
            callback(normalized);
        }
    }
}
