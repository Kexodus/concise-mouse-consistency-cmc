#pragma once

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace msf
{
    enum class HookRegistrationPoint
    {
        InputLook,
        SmoothingRemoval
    };

    struct RuntimeLookInputSample
    {
        std::uint64_t eventCount{ 0 };
        std::uint64_t sprintEventCount{ 0 };
        float rawPixelX{ 0.0F };
        float rawPixelY{ 0.0F };
        float engineX{ 0.0F };
        float engineY{ 0.0F };
        float outputX{ 0.0F };
        float outputY{ 0.0F };
    };

    class RuntimeLookTelemetryAccumulator
    {
    public:
        void Record(
            float rawPixelX,
            float rawPixelY,
            float engineX,
            float engineY,
            float outputX,
            float outputY,
            bool sprinting) noexcept;
        std::optional<RuntimeLookInputSample> Consume() noexcept;

    private:
        RuntimeLookInputSample _sample{};
    };

    float WrappedAngleDelta(float current, float previous) noexcept;
    float RestoreHalfRateSprintYawDelta(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        bool eligible) noexcept;

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
        bool InstallPlayerMovementTraceHook();
        void RemovePlayerMovementTraceHook();
        bool InstallThirdPersonSmoothingHook();
        void RemoveThirdPersonSmoothingHook();
        bool _installed{ false };
        bool _firstPersonRegistered{ false };
        bool _playerMovementTraceRegistered{ false };
        bool _smoothingRemovalRegistered{ false };
        mutable std::mutex _policyLock;
        CompatibilityPolicy _activePolicy{};
        std::atomic<std::uint8_t> _policyFlags{ 0x07 };
    };
}
