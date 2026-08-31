#include "MouseSensitivityFix/Config.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <ostream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
    constexpr auto kReloadPollInterval = std::chrono::milliseconds(250);

    std::string Trim(std::string_view value)
    {
        const auto first = value.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }

        const auto last = value.find_last_not_of(" \t\r\n");
        return std::string(value.substr(first, (last - first) + 1));
    }

    std::string FoldAsciiLower(std::string_view value)
    {
        std::string folded;
        folded.reserve(value.size());
        for (const unsigned char ch : value) {
            folded.push_back(static_cast<char>(std::tolower(ch)));
        }
        return folded;
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

    double ClampAxisMultiplier(double value, double fallback) noexcept
    {
        return std::clamp(FiniteOr(value, fallback), 0.01, 20.0);
    }

    void ClampStateLookOverride(msf::StateLookOverride& overlay, const msf::StateLookOverride& previous) noexcept
    {
        overlay.xSensitivity = ClampAxisMultiplier(overlay.xSensitivity, previous.xSensitivity);
        overlay.ySensitivity = ClampAxisMultiplier(overlay.ySensitivity, previous.ySensitivity);
    }

    void WriteBool(std::ostream& output, const std::string& key, bool value)
    {
        output << key << '=' << (value ? "true" : "false") << '\n';
    }

    void WriteStateOverrideSection(
        std::ostream& output,
        const char* section,
        const char* prefix,
        const char* help,
        const msf::StateLookOverride& overlay)
    {
        output << '\n';
        output << '[' << section << "]\n";
        output << help;
        WriteBool(output, std::string("b") + prefix + "Disabled", overlay.disabled);
        output << "; Horizontal / vertical overlay scale. Clamp 0.01-20. Shared by mouse and gamepad.\n";
        output << 'f' << prefix << "XSensitivity=" << overlay.xSensitivity << '\n';
        output << 'f' << prefix << "YSensitivity=" << overlay.ySensitivity << '\n';
        WriteBool(output, std::string("b") + prefix + "ApplyFirstPerson", overlay.applyFirstPerson);
        WriteBool(output, std::string("b") + prefix + "ApplyThirdPerson", overlay.applyThirdPerson);
    }

    bool TryApplyStateOverrideKey(
        std::string_view key,
        std::string_view value,
        std::string_view prefix,
        msf::StateLookOverride& overlay)
    {
        const auto disabledKey = std::string("b") + std::string(prefix) + "Disabled";
        const auto xKey = std::string("f") + std::string(prefix) + "XSensitivity";
        const auto yKey = std::string("f") + std::string(prefix) + "YSensitivity";
        const auto fpKey = std::string("b") + std::string(prefix) + "ApplyFirstPerson";
        const auto tpKey = std::string("b") + std::string(prefix) + "ApplyThirdPerson";
        if (key == disabledKey) {
            overlay.disabled = msf::ParseBool(value, overlay.disabled);
            return true;
        }
        if (key == xKey) {
            overlay.xSensitivity = ClampAxisMultiplier(
                ParseDouble(std::string(value), overlay.xSensitivity),
                overlay.xSensitivity);
            return true;
        }
        if (key == yKey) {
            overlay.ySensitivity = ClampAxisMultiplier(
                ParseDouble(std::string(value), overlay.ySensitivity),
                overlay.ySensitivity);
            return true;
        }
        if (key == fpKey) {
            overlay.applyFirstPerson = msf::ParseBool(value, overlay.applyFirstPerson);
            return true;
        }
        if (key == tpKey) {
            overlay.applyThirdPerson = msf::ParseBool(value, overlay.applyThirdPerson);
            return true;
        }
        return false;
    }

    void WriteIniContents(std::ostream& output, const msf::ConfigValues& values)
    {
        output << "[General]\n";
        output << "; Master enable for all plugin behavior. Set false to bypass transforms while keeping the plugin loaded.\n";
        output << "bEnabled=" << (values.enabled ? "true" : "false") << "\n";
        output << "; Global sensitivity scalar applied before per-device multipliers.\n";
        output << "fGlobalSensitivity=" << values.globalSensitivity << "\n";
        output << "; Mouse horizontal axis multiplier.\n";
        output << "fMouseXAxisMultiplier=" << values.mouseXAxisMultiplier << "\n";
        output << "; Mouse vertical axis multiplier.\n";
        output << "fMouseYAxisMultiplier=" << values.mouseYAxisMultiplier << "\n";
        output << "; Gamepad horizontal axis multiplier.\n";
        output << "fGamepadXAxisMultiplier=" << values.gamepadXAxisMultiplier << "\n";
        output << "; Gamepad vertical axis multiplier. Default 1.0 (same as mouse); lower if vertical look feels too fast.\n";
        output << "fGamepadYAxisMultiplier=" << values.gamepadYAxisMultiplier << "\n";
        output << "; Remove third-person camera smoothing/interpolation.\n";
        output << "bEnableSmoothingRemovalHook=" << (values.enableSmoothingRemovalHook ? "true" : "false") << "\n";
        output << "; Suppress large input spike on focus regain (alt-tab).\n";
        output << "bSuppressFocusSpike=" << (values.suppressFocusSpike ? "true" : "false") << "\n";
        output << "; Apply sensitivity transform to right-stick gamepad look.\n";
        output << "bAffectGamepadLook=" << (values.affectGamepadLook ? "true" : "false") << "\n";
        output << "\n";
        output << "[Advanced]\n";
        output << "; First-person look transforms, pitch normalize, half-rate yaw restore, and slow-time yaw compensation.\n";
        output << "bEnableFirstPersonHook=" << (values.enableFirstPersonHook ? "true" : "false") << "\n";
        output << "; Third-person look transforms and slow-time yaw compensation. Never writes freeRotation.y / TP pitch normalize.\n";
        output << "bEnableThirdPersonHook=" << (values.enableThirdPersonHook ? "true" : "false") << "\n";
        output << "; Disable transforms while game is paused or system menu is open.\n";
        output << "bDisableInMenus=" << (values.disableInMenus ? "true" : "false") << "\n";
        output << "; Set false if look controls are incorrectly reported as disabled by mods (e.g. camera mods that manage their own input).\n";
        output << "bDisableWhenLookControlsDisabled=" << (values.disableWhenLookControlsDisabled ? "true" : "false") << "\n";
        output << "; Treat the first mouse event after this idle gap as a focus-regain spike.\n";
        output << "iFocusSpikeGapMs=" << values.focusSpikeGapMs << "\n";
        output << "; First-person mouse bow only: X reconstructs from raw pixels * sampled freelook scale * this multiplier.\n";
        output << "; 1.0 = freelook-equivalent reconstructed X. Does not scale by zoom/FOV. Third-person mouse bow is not reconstructed.\n";
        output << "fBowAimMouseXMultiplier=" << values.bowAimMouseXMultiplier << "\n";
        output << "; First-person mouse bow only: multiplies the live engine Y delta. 1.0 = keep engine Y. No FOV/eagleEyeY scale.\n";
        output << "fBowAimMouseYMultiplier=" << values.bowAimMouseYMultiplier << "\n";
        output << "; Gamepad bow aim multipliers (both perspectives). INI-only; not reconstructed from pixels.\n";
        output << "fBowAimGamepadXMultiplier=" << values.bowAimGamepadXMultiplier << "\n";
        output << "fBowAimGamepadYMultiplier=" << values.bowAimGamepadYMultiplier << "\n";
        output << "; Enables low-frequency sampled hook counters for troubleshooting. Keep false for normal play.\n";
        output << "bVerboseLogging=" << (values.verboseLogging ? "true" : "false") << "\n";
        output << "\n";
        output << "[Compatibility]\n";
        output << "; When true, CMC keeps removing third-person smoothing even if SmoothCam / Improved Camera are detected.\n";
        output << "; When false, CMC skips third-person smoothing intervention if either camera mod is present.\n";
        output << "bKeepThirdPersonSmoothingRemovalWithCameraMods="
               << (values.keepThirdPersonSmoothingRemovalWithCameraMods ? "true" : "false") << "\n";

        constexpr auto kDisabledDefaultHelp =
            "; Disabled=true (default) keeps 0.53b feel for this event. Uncheck / set false to apply X/Y.\n"
            "; Exact first- or third-person only. Neither/both person flags skip the overlay. FOV is never a multiplier.\n";
        WriteStateOverrideSection(
            output,
            "Walking",
            "Walking",
            kDisabledDefaultHelp,
            values.walking);
        WriteStateOverrideSection(
            output,
            "Running",
            "Running",
            kDisabledDefaultHelp,
            values.running);
        WriteStateOverrideSection(
            output,
            "Sprinting",
            "Sprinting",
            kDisabledDefaultHelp,
            values.sprinting);
        WriteStateOverrideSection(
            output,
            "BowAim",
            "BowAim",
            "; Bow pullback/aiming, including Eagle Eye frames that are still bowAim.\n"
            "; Disabled=true (default) leaves fBowAim* exactly as 0.53b.\n"
            "; Enabled replaces fBowAim* for this event (does not multiply 0.35x0.35). Reconstruct X + engine Y still run.\n"
            "; Exact first- or third-person only. FOV is never a multiplier.\n",
            values.bowAim);
        WriteStateOverrideSection(
            output,
            "MagicUse",
            "MagicUse",
            "; Charging/casting only (not merely a spell or staff equipped).\n"
            "; Disabled=true (default) keeps 0.53b feel. FOV is never a multiplier.\n",
            values.magicUse);
        WriteStateOverrideSection(
            output,
            "OneHand",
            "OneHand",
            "; One-handed weapon in use; other hand empty or shield. Not bow/staff/two-hander.\n"
            "; Disabled=true (default) keeps 0.53b feel. FOV is never a multiplier.\n",
            values.oneHand);
        WriteStateOverrideSection(
            output,
            "TwoHanded",
            "TwoHanded",
            "; Two-handed melee (greatsword/battleaxe/warhammer). Not bow, staff, or dual wield.\n"
            "; Disabled=true (default) keeps 0.53b feel. FOV is never a multiplier.\n",
            values.twoHanded);
        WriteStateOverrideSection(
            output,
            "DualWielding",
            "DualWielding",
            "; One-handed weapons in both hands, drawn. Disabled=true (default) keeps 0.53b feel.\n"
            "; FOV is never a multiplier.\n",
            values.dualWielding);
    }

    bool ReplaceFileAtomically(
        const std::filesystem::path& from,
        const std::filesystem::path& to)
    {
#ifdef _WIN32
        return ::MoveFileExW(
                   from.c_str(),
                   to.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code ec;
        std::filesystem::rename(from, to, ec);
        return !ec;
#endif
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
                loaded.focusSpikeGapMs = ClampFocusSpikeGapMs(ParseInt(value, loaded.focusSpikeGapMs));
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
            } else if (
                TryApplyStateOverrideKey(key, value, "Walking", loaded.walking) ||
                TryApplyStateOverrideKey(key, value, "Running", loaded.running) ||
                TryApplyStateOverrideKey(key, value, "Sprinting", loaded.sprinting) ||
                TryApplyStateOverrideKey(key, value, "BowAim", loaded.bowAim) ||
                TryApplyStateOverrideKey(key, value, "MagicUse", loaded.magicUse) ||
                TryApplyStateOverrideKey(key, value, "OneHand", loaded.oneHand) ||
                TryApplyStateOverrideKey(key, value, "TwoHanded", loaded.twoHanded) ||
                TryApplyStateOverrideKey(key, value, "DualWielding", loaded.dualWielding)) {
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
            _dirty = false;
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

        auto tempPath = iniPath;
        tempPath += ".tmp";

        {
            std::ofstream output(tempPath, std::ios::trunc);
            if (!output.is_open()) {
                return false;
            }

            WriteIniContents(output, _values);
            output.flush();
            if (!output) {
                output.close();
                std::error_code removeEc;
                std::filesystem::remove(tempPath, removeEc);
                return false;
            }
        }

        if (!ReplaceFileAtomically(tempPath, iniPath)) {
            std::error_code removeEc;
            std::filesystem::remove(tempPath, removeEc);
            return false;
        }

        std::error_code ec;
        const auto writeTime = std::filesystem::last_write_time(iniPath, ec);
        if (!ec) {
            _lastWriteTime = writeTime;
        }
        _dirty = false;
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
            if (_dirty) {
                return false;
            }
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
        normalized.focusSpikeGapMs = ClampFocusSpikeGapMs(normalized.focusSpikeGapMs);
        normalized.globalSensitivity = ClampAxisMultiplier(
            normalized.globalSensitivity, previous.globalSensitivity);
        normalized.mouseXAxisMultiplier = ClampAxisMultiplier(
            normalized.mouseXAxisMultiplier, previous.mouseXAxisMultiplier);
        normalized.mouseYAxisMultiplier = ClampAxisMultiplier(
            normalized.mouseYAxisMultiplier, previous.mouseYAxisMultiplier);
        normalized.gamepadXAxisMultiplier = ClampAxisMultiplier(
            normalized.gamepadXAxisMultiplier, previous.gamepadXAxisMultiplier);
        normalized.gamepadYAxisMultiplier = ClampAxisMultiplier(
            normalized.gamepadYAxisMultiplier, previous.gamepadYAxisMultiplier);
        normalized.bowAimMouseXMultiplier = ClampAxisMultiplier(
            normalized.bowAimMouseXMultiplier, previous.bowAimMouseXMultiplier);
        normalized.bowAimMouseYMultiplier = ClampAxisMultiplier(
            normalized.bowAimMouseYMultiplier, previous.bowAimMouseYMultiplier);
        normalized.bowAimGamepadXMultiplier = ClampAxisMultiplier(
            normalized.bowAimGamepadXMultiplier, previous.bowAimGamepadXMultiplier);
        normalized.bowAimGamepadYMultiplier = ClampAxisMultiplier(
            normalized.bowAimGamepadYMultiplier, previous.bowAimGamepadYMultiplier);
        ClampStateLookOverride(normalized.walking, previous.walking);
        ClampStateLookOverride(normalized.running, previous.running);
        ClampStateLookOverride(normalized.sprinting, previous.sprinting);
        ClampStateLookOverride(normalized.bowAim, previous.bowAim);
        ClampStateLookOverride(normalized.magicUse, previous.magicUse);
        ClampStateLookOverride(normalized.oneHand, previous.oneHand);
        ClampStateLookOverride(normalized.twoHanded, previous.twoHanded);
        ClampStateLookOverride(normalized.dualWielding, previous.dualWielding);

        ChangeCallback callback;
        {
            std::scoped_lock guard(_lock);
            _values = normalized;
            _dirty = true;
            callback = _changeCallback;
        }
        if (callback) {
            callback(normalized);
        }
    }

    bool ConfigManager::HasUnsavedChanges() const
    {
        std::scoped_lock guard(_lock);
        return _dirty;
    }

    int ClampFocusSpikeGapMs(int value) noexcept
    {
        return std::clamp(value, 50, 5000);
    }

    bool ParseBool(std::string_view value, bool fallback)
    {
        const auto folded = FoldAsciiLower(Trim(value));
        if (folded == "1" || folded == "true") {
            return true;
        }
        if (folded == "0" || folded == "false") {
            return false;
        }
        return fallback;
    }
}
