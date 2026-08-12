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
    // Undo global slow-time on an already half-rate-restored yaw delta so wall-clock
    // mouse-to-yaw matches freelook (Eagle Eye uses ~0.25 time mult).
    float CompensateTimeDilatedYawDelta(
        float yawDelta,
        float globalTimeMult) noexcept;
    struct FirstPersonYawCorrectionResult
    {
        float yawDelta{ 0.0F };
        bool halfRateRestored{ false };
        bool timeCompensated{ false };
    };
    FirstPersonYawCorrectionResult ApplyFirstPersonYawCorrection(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        float globalTimeMult,
        bool eligible) noexcept;
    bool ShouldRestoreHalfRateFirstPersonYaw(
        bool enabled,
        bool firstPersonHookEnabled,
        bool inThirdPerson,
        bool sprinting,
        bool bowAiming) noexcept;
    // Bow aim mouse reconstruction is first-person only. Third-person camera mods
    // own their own look pipeline once a ranged weapon is out.
    bool ShouldApplyBowAimMousePath(bool inThirdPerson, bool bowAiming) noexcept;
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
    float NormalizePitchTargetDelta(
        float postTransformLookY,
        float engineTargetPitchDelta,
        float freelookPitchPerLook,
        bool eligible) noexcept;
    bool ShouldUpdateFreelookSampledScale(
        bool rangedWeaponEquipped,
        bool weaponFullySheathed,
        bool bowAiming,
        bool bowZoomFlag) noexcept;
    bool UpdateFreelookScaleSample(
        float rawPixelDelta,
        float engineDelta,
        float& currentScale,
        float& pendingScale,
        std::uint32_t& pendingCount) noexcept;
    bool ShouldUpdateNormalAimFov(
        bool rangedWeaponActive,
        bool bowAiming,
        bool bowZoomFlag,
        float renderedFovDegrees) noexcept;
    float FovDegreesFromFrustumEdges(float positiveEdge, float negativeEdge) noexcept;
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
        bool InstallFirstPersonTelemetryHook();
        void RemoveFirstPersonTelemetryHook();
        bool InstallThirdPersonSmoothingHook();
        void RemoveThirdPersonSmoothingHook();
        bool _installed{ false };
        bool _firstPersonRegistered{ false };
        bool _playerYawRegistered{ false };
        bool _firstPersonTelemetryRegistered{ false };
        bool _smoothingRemovalRegistered{ false };
        mutable std::mutex _policyLock;
        CompatibilityPolicy _activePolicy{};
        std::atomic<std::uint8_t> _policyFlags{ 0x03 };
    };
}
