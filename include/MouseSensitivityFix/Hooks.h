#pragma once

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"

#include <utility>

namespace msf
{
    enum class HookRegistrationPoint
    {
        FirstPersonMouseLook,
        SmoothingRemoval
    };

    class HookCoordinator
    {
    public:
        bool Install(const CompatibilityPolicy& policy, const ConfigValues& config);
        void Remove();

        // Shared transform entry point for eventual patch callbacks.
        std::pair<float, float> ApplyTransform(float deltaX, float deltaY, const ConfigValues& config, bool isGamepad) const;

    private:
        bool RegisterHookPoint(HookRegistrationPoint point);
        bool InstallLookHandlerMouseMoveHook();
        void RemoveLookHandlerMouseMoveHook();
        bool InstallThirdPersonSmoothingHook();
        void RemoveThirdPersonSmoothingHook();
        bool _installed{ false };
        bool _firstPersonRegistered{ false };
        bool _smoothingRemovalRegistered{ false };
        CompatibilityPolicy _activePolicy{};
    };
}
