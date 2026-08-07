#pragma once

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <utility>

namespace msf
{
    enum class HookRegistrationPoint
    {
        InputLook,
        SmoothingRemoval
    };

    float RestoreHalfRateSprintYawDelta(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        bool eligible) noexcept;
    bool ShouldEmitSampledLog(
        bool enabled,
        std::uint64_t count,
        std::uint64_t interval,
        bool includeFirst = false) noexcept;

    class HookCoordinator
    {
    public:
        bool Install();
        void Remove();
        bool UpdatePolicy(const CompatibilityPolicy& policy);

        CompatibilityPolicy GetPolicySnapshot() const;
        bool ShouldApplyInputTransform(const ConfigValues& config, bool inThirdPerson, bool isGamepad) const;
        bool ShouldRemoveThirdPersonSmoothing(const ConfigValues& config) const;

        std::pair<float, float> ApplyTransform(float deltaX, float deltaY, const ConfigValues& config, bool isGamepad) const;

    private:
        bool RegisterHookPoint(HookRegistrationPoint point);
        bool InstallLookHandlerMouseMoveHook();
        void RemoveLookHandlerMouseMoveHook();
        bool InstallPlayerSprintYawHook();
        void RemovePlayerSprintYawHook();
        bool InstallThirdPersonSmoothingHook();
        void RemoveThirdPersonSmoothingHook();
        bool _installed{ false };
        bool _firstPersonRegistered{ false };
        bool _playerSprintYawRegistered{ false };
        bool _smoothingRemovalRegistered{ false };
        mutable std::mutex _policyLock;
        CompatibilityPolicy _activePolicy{};
        std::atomic<std::uint8_t> _policyFlags{ 0x07 };
    };
}
