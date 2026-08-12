#pragma once

#include "MouseSensitivityFix/Config.h"

#include <filesystem>
#include <string>

namespace msf
{
    enum class CompatibilityMode
    {
        Safe,
        ReducedIntervention
    };

    struct CompatibilityPolicy
    {
        CompatibilityMode mode{ CompatibilityMode::Safe };
        bool installInputHooks{ true };
        // Single gate for third-person smoothing removal. Sensitivity transforms
        // remain governed by installInputHooks / config toggles separately.
        bool allowThirdPersonSmoothingIntervention{ true };
        std::string reason;
    };

    class CompatibilityManager
    {
    public:
        void ScanInstalledCameraMods(const std::filesystem::path& pluginsDirectory);
        CompatibilityPolicy EvaluatePolicy(const ConfigValues& config) const;
        std::string DescribeDetectedMods() const;

    private:
        bool _improvedCameraDetected{ false };
        bool _smoothCamDetected{ false };
    };
}
