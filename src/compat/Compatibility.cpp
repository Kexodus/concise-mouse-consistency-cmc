#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <set>
#include <sstream>
#include <string>

namespace
{
    std::string ToLower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    std::set<std::string> EnumeratePluginDlls(const std::filesystem::path& directory)
    {
        std::set<std::string> entries;
        std::error_code ec;
        if (!std::filesystem::exists(directory, ec)) {
            return entries;
        }

        for (const auto& dirEntry : std::filesystem::directory_iterator(directory, ec)) {
            if (ec || !dirEntry.is_regular_file(ec)) {
                continue;
            }

            const auto fileName = dirEntry.path().filename().string();
            if (dirEntry.path().extension() == ".dll") {
                entries.insert(ToLower(fileName));
            }
        }
        return entries;
    }

    bool ContainsAny(const std::set<std::string>& installed, const std::array<const char*, 6>& signatures)
    {
        for (const auto* signature : signatures) {
            if (installed.contains(ToLower(signature))) {
                return true;
            }
        }
        return false;
    }

}

namespace msf
{
    void CompatibilityManager::ScanInstalledCameraMods(const std::filesystem::path& pluginsDirectory)
    {
        const auto installedDlls = EnumeratePluginDlls(pluginsDirectory);

        _improvedCameraDetected = ContainsAny(installedDlls, {
            "ImprovedCameraSE.dll",
            "ImprovedCameraAE.dll",
            "ImprovedCamera.dll",
            "ImprovedCameraBeta4.dll",
            "ImprovedCameraNG.dll",
            "ImprovedCameraSE-NG.dll"
        });

        _smoothCamDetected = ContainsAny(installedDlls, {
            "SmoothCam.dll",
            "SmoothCamSE.dll",
            "SmoothCamAE.dll",
            "SmoothCamVR.dll",
            "SmoothCamNG.dll",
            "TrueDirectionalMovement-SmoothCam.dll"
        });

        LogInfo(DescribeDetectedMods());
    }

    CompatibilityPolicy CompatibilityManager::EvaluatePolicy(const ConfigValues& config) const
    {
        CompatibilityPolicy policy{};
        const bool improvedDetected = _improvedCameraDetected;
        const bool smoothDetected = _smoothCamDetected;

        if (!config.useCompatibilityPresets) {
            policy.reason = "Compatibility presets disabled by user.";
            return policy;
        }

        if (smoothDetected && config.presetSmoothCam) {
            policy.mode = CompatibilityMode::ReducedIntervention;
            if (!config.forceOverrideSmoothCam) {
                policy.installSmoothingRemovalHooks = false;
            }
            if (config.delegateThirdPersonWhenSmoothCam && !config.forceOverrideSmoothCam) {
                policy.allowThirdPersonIntervention = false;
            }
            policy.reason = "SmoothCam preset active.";
        }

        if (improvedDetected && config.presetImprovedCamera) {
            policy.mode = CompatibilityMode::ReducedIntervention;
            if (config.delegateThirdPersonWhenImprovedCamera && !config.forceOverrideImprovedCamera) {
                policy.allowThirdPersonIntervention = false;
            }
            if (policy.reason.empty()) {
                policy.reason = "Improved Camera preset active.";
            } else {
                policy.reason += " Improved Camera preset active.";
            }
        }

        if (!improvedDetected && !smoothDetected) {
            policy.mode = CompatibilityMode::Safe;
            policy.reason = "No known camera stack conflicts detected.";
        }

        return policy;
    }

    std::string CompatibilityManager::DescribeDetectedMods() const
    {
        std::ostringstream stream;
        stream << "Detected mods:";
        stream << " ImprovedCamera=" << (_improvedCameraDetected ? "yes" : "no");
        stream << " SmoothCam=" << (_smoothCamDetected ? "yes" : "no");
        return stream.str();
    }
}
