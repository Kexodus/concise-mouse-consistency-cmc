#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <initializer_list>
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
            if (ToLower(dirEntry.path().extension().string()) == ".dll") {
                entries.insert(ToLower(fileName));
            }
        }
        return entries;
    }

    bool ContainsAny(
        const std::set<std::string>& installed,
        std::initializer_list<const char*> signatures)
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

        // Release package names from mwilsnd/SkyrimSE-SmoothCam: SmoothCamSSE.dll,
        // SmoothCamAE.dll, SmoothCamAEPre629.dll. SmoothCam.dll is the local build name.
        // SmoothCamSE.dll is not a real package artifact.
        _smoothCamDetected = ContainsAny(installedDlls, {
            "SmoothCam.dll",
            "SmoothCamSSE.dll",
            "SmoothCamAE.dll",
            "SmoothCamAEPre629.dll",
            "SmoothCamVR.dll",
            "SmoothCamNG.dll",
            "TrueDirectionalMovement-SmoothCam.dll"
        });

        LogInfo(DescribeDetectedMods());
    }

    CompatibilityPolicy CompatibilityManager::EvaluatePolicy(const ConfigValues& config) const
    {
        CompatibilityPolicy policy{};
        const bool cameraModDetected = _improvedCameraDetected || _smoothCamDetected;

        if (!cameraModDetected) {
            policy.mode = CompatibilityMode::Safe;
            policy.reason = "No known camera stack conflicts detected.";
            return policy;
        }

        if (config.keepThirdPersonSmoothingRemovalWithCameraMods) {
            policy.mode = CompatibilityMode::Safe;
            policy.allowThirdPersonSmoothingIntervention = true;
            policy.reason = "Camera mod detected; CMC keeps third-person smoothing removal.";
            return policy;
        }

        policy.mode = CompatibilityMode::ReducedIntervention;
        policy.allowThirdPersonSmoothingIntervention = false;
        if (_smoothCamDetected && _improvedCameraDetected) {
            policy.reason = "SmoothCam and Improved Camera detected; third-person smoothing delegated.";
        } else if (_smoothCamDetected) {
            policy.reason = "SmoothCam detected; third-person smoothing delegated.";
        } else {
            policy.reason = "Improved Camera detected; third-person smoothing delegated.";
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
