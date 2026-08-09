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

    float RestoreHalfRateYawDelta(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        bool eligible) noexcept;
    bool ShouldRestoreHalfRateFirstPersonYaw(
        bool enabled,
        bool hotDisabled,
        bool firstPersonHookEnabled,
        bool inThirdPerson,
        bool sprinting,
        bool bowAiming) noexcept;
    std::pair<float, float> ApplyBowAimMouseDeltas(
        float rawPixelX,
        float engineDeltaX,
        float engineDeltaY,
        float sampledScaleX,
        float bowXMultiplier,
        float bowYMultiplier) noexcept;
    float CalculateBowAimVerticalMultiplier(
        bool bowAiming,
        float configuredBowYMultiplier) noexcept;
    bool ShouldUpdateFreelookSampledScale(
        bool bowOut,
        bool bowAiming,
        bool bowZoomFlag) noexcept;
    bool ShouldUpdateNormalAimFov(
        bool bowOut,
        bool bowAiming,
        bool bowZoomFlag,
        float renderedFovDegrees) noexcept;
    float VerticalFovDegreesFromFrustum(float fTop, float fNear) noexcept;
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
        bool InstallPlayerYawHook();
        void RemovePlayerYawHook();
        bool InstallThirdPersonSmoothingHook();
        void RemoveThirdPersonSmoothingHook();
        bool _installed{ false };
        bool _firstPersonRegistered{ false };
        bool _playerYawRegistered{ false };
        bool _smoothingRemovalRegistered{ false };
        mutable std::mutex _policyLock;
        CompatibilityPolicy _activePolicy{};
        std::atomic<std::uint8_t> _policyFlags{ 0x07 };
    };
}
