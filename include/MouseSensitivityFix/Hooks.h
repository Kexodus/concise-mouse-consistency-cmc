#pragma once

#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
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
    // engineYaw / (lookX * delta * pi). NaN when inputs cannot form a scale.
    float ComputeObservedYawScale(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta) noexcept;
    // True when observedScale is in the guarded half-rate band [0.48, 0.52].
    bool IsObservedHalfRateScale(float observedScale) noexcept;
    // Consecutive in-band streak while policy-eligible (0 when ineligible or out of band).
    std::uint32_t UpdateHalfRateBandStreak(
        bool policyEligible,
        bool inBand,
        std::uint32_t previousStreak) noexcept;
    // Policy-eligible half-rate restore: sprint/bow hints may fire on the first
    // in-band frame; orphan (cast/etc.) needs requiredStreak consecutive hits.
    bool ShouldApplyHalfRateRestore(
        bool policyEligible,
        bool inBand,
        std::uint32_t streakAfterUpdate,
        bool sprintOrBowHint,
        std::uint32_t requiredStreak = 2) noexcept;
    // Freelook yaw EMA must never ingest half-rate or casting frames.
    bool ShouldUpdateFreelookYawEma(
        bool trueFreelookEligible,
        bool sprinting,
        bool casting,
        float observedScale) noexcept;
    // Undo global slow-time on an already half-rate-restored yaw delta so wall-clock
    // mouse-to-yaw matches freelook (Eagle Eye uses ~0.25 time mult).
    float CompensateTimeDilatedYawDelta(
        float yawDelta,
        float globalTimeMult) noexcept;
    // True when globalTimeMult is in the open interval (0.05, 0.90).
    bool IsGlobalTimeDilatedForYaw(float globalTimeMult) noexcept;

    // delta / (realTimeDelta * Current). NaN when inputs are unusable.
    float ComputeTimeDeltaAgreement(
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult) noexcept;
    bool IsTimeDeltaAgreementAcceptable(
        float agreement,
        float tolerance = 0.12F) noexcept;
    bool IsGlobalTimeMultStable(
        float currentTimeMult,
        float previousTimeMult,
        bool havePrevious,
        float maxAbsDelta = 0.08F) noexcept;

    enum class TimeCompSkipReason : std::uint8_t
    {
        None = 0,
        NotRequested,
        NotDilated,
        Disagree,
        Unstable,
        InvalidInputs,
        MissingWallClock
    };
    // How to convert game-time yaw to wall-clock during dilation.
    // ScaleByCurrent: yaw / Current when delta≈realTimeDelta*Current.
    // RewriteWallClock: yaw = lookX * wallDelta * π after half-rate (disagree/unstable).
    enum class TimeCompMode : std::uint8_t
    {
        None = 0,
        ScaleByCurrent,
        RewriteWallClock
    };
    // Settled EE: ScaleByCurrent when dilated, Current stable, and (when wall is
    // available) delta agrees with Current. Disagree/unstable with a usable wall
    // → RewriteWallClock (never leave 0.25× yaw while pitch normalize is wall-clock).
    // Missing wall never blind-applies ScaleByCurrent; require stableDilatedStreak
    // of dilated+stable Current frames, otherwise None.
    bool ShouldApplyTimeCompYaw(
        bool policyWantsTimeComp,
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult,
        float previousGlobalTimeMult,
        bool havePreviousTimeMult,
        TimeCompSkipReason* outReason = nullptr,
        float agreementTolerance = 0.12F,
        float stabilityTolerance = 0.08F,
        std::uint32_t stableDilatedStreak = 0,
        std::uint32_t requiredStableDilatedFramesWhenMissingWall = 3) noexcept;
    TimeCompMode ResolveTimeCompMode(
        bool policyWantsTimeComp,
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult,
        float previousGlobalTimeMult,
        bool havePreviousTimeMult,
        TimeCompSkipReason* outReason = nullptr,
        float agreementTolerance = 0.12F,
        float stabilityTolerance = 0.08F,
        std::uint32_t stableDilatedStreak = 0,
        std::uint32_t requiredStableDilatedFramesWhenMissingWall = 3) noexcept;
    const char* TimeCompSkipReasonName(TimeCompSkipReason reason) noexcept;
    const char* TimeCompModeName(TimeCompMode mode) noexcept;
    // Absolute wall-clock yaw from look input (after half-rate policy).
    float RewriteWallClockYawDelta(
        float postSensitivityLookX,
        float wallDeltaSeconds) noexcept;
    // Pause FP pitch normalize when dilated+looking yaw was not brought to
    // wall-clock (avoids slow-X / OK-Y when timeComp cannot compensate).
    bool ShouldPausePitchNormalizeForDilatedYaw(
        bool timeDilated,
        bool looking,
        bool yawBroughtToWallClock) noexcept;

    // Address Library IDs for BSTimer::QGlobalTimeMultiplier (Current).
    // CommonLib's BSTimer::GetCurrentGlobalTimeMult incorrectly relocates Target
    // (511883 / 388443). Yaw timeComp must read Current, not the pending target.
    inline constexpr std::uint64_t kBsTimerGlobalTimeMultCurrentSe = 511882;
    inline constexpr std::uint64_t kBsTimerGlobalTimeMultCurrentAeVr = 388442;
    inline constexpr std::uint64_t kBsTimerGlobalTimeMultTargetSe = 511883;
    inline constexpr std::uint64_t kBsTimerGlobalTimeMultTargetAeVr = 388443;
    // BSTimer singleton (has delta + realTimeDelta). Local CommonLib omits GetSingleton.
    inline constexpr std::uint64_t kBsTimerSingletonSe = 523657;
    inline constexpr std::uint64_t kBsTimerSingletonAeVr = 410196;
    // Binary layout (SE/AE/VR / CommonLibVR): realTimeDelta at +0x1C.
    // Local CommonLibSSE-NG omits pad0C, so timer->realTimeDelta aliases delta at +0x18.
    inline constexpr std::size_t kBsTimerDeltaOffset = 0x18;
    inline constexpr std::size_t kBsTimerRealTimeDeltaOffset = 0x1C;

    // Pure camera-person flags from PlayerCamera::IsInFirstPerson / IsInThirdPerson.
    // Mount / furniture / bleedout / dragon leave both false — never infer FP from !TP.
    struct LookCameraPerson
    {
        bool firstPerson{ false };
        bool thirdPerson{ false };
    };
    LookCameraPerson ClassifyLookCameraPerson(
        bool isInFirstPerson,
        bool isInThirdPerson) noexcept;

    // Horizontal look activity used by timeComp (abs(lookX) threshold).
    bool IsLookingForYawCorrection(float lookInputX) noexcept;

    // Cheap polled snapshot for look-correction eligibility. FOV fields are
    // diagnostic only and must never drive multipliers.
    struct LookCorrectionContext
    {
        bool firstPerson{ false };
        bool thirdPerson{ false };
        bool sprinting{ false };
        bool bowAiming{ false };
        bool bowOut{ false };
        bool looking{ false };
        bool casting{ false };
        bool staff{ false };
        float timeMult{ 1.0F };
        bool timeDilated{ false };
        bool lookControlsEnabled{ true };
        bool menuMode{ false };
        float renderedVFovDegrees{ 0.0F };
        float normalVFovDegrees{ 0.0F };
    };

    struct LookCorrectionPolicy
    {
        bool restoreHalfRateYaw{ false };
        bool compensateTimeYaw{ false };
    };

    // Half-rate: exclusive FP && looking (measurement band is the restore gate;
    // sprint/bow are telemetry hints only). TimeComp: timeDilated && exclusive
    // (FP|TP) && looking — not bow-gated; agreement/stability gated at the apply
    // site. Both-true person flags reject half-rate and timeComp. Optional
    // menu/look-control gates apply when enabled.
    LookCorrectionPolicy EvaluateLookCorrectionPolicy(
        const LookCorrectionContext& ctx,
        bool enabled,
        bool enableFirstPersonHook,
        bool enableThirdPersonHook,
        bool disableInMenus = false,
        bool disableWhenLookControlsDisabled = false) noexcept;

    struct PlayerYawCorrectionResult
    {
        float yawDelta{ 0.0F };
        bool halfRateRestored{ false };
        bool timeCompensated{ false };
        TimeCompMode timeCompMode{ TimeCompMode::None };
    };
    // Order: half-rate restore, then ScaleByCurrent (1/timeMult) or RewriteWallClock.
    // Bool overload maps compensateTimeYaw → ScaleByCurrent for legacy tests.
    PlayerYawCorrectionResult ApplyPlayerYawCorrection(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        float globalTimeMult,
        bool restoreHalfRateYaw,
        bool compensateTimeYaw) noexcept;
    PlayerYawCorrectionResult ApplyPlayerYawCorrection(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        float globalTimeMult,
        bool restoreHalfRateYaw,
        TimeCompMode timeCompMode,
        float wallDeltaSeconds) noexcept;
    // Convenience wrappers — require exact FP/TP flags (never infer FP from !TP).
    // Half-rate policy eligibility is FP + looking; sprint/bow are not required.
    bool ShouldRestoreHalfRateFirstPersonYaw(
        bool enabled,
        bool firstPersonHookEnabled,
        bool inFirstPerson,
        bool inThirdPerson,
        bool looking) noexcept;
    // Third-person slow-time yaw compensation. Not bow-gated; requires looking.
    bool ShouldCorrectThirdPersonSlowTimeYaw(
        bool enabled,
        bool thirdPersonHookEnabled,
        bool inFirstPerson,
        bool inThirdPerson,
        bool looking,
        float globalTimeMult) noexcept;
    // Which freelook pixels→look cache to update/read. Neither/both → None.
    enum class FreelookScaleCacheKind : std::uint8_t
    {
        None = 0,
        FirstPerson = 1,
        ThirdPerson = 2
    };
    FreelookScaleCacheKind SelectFreelookScaleCache(
        bool inFirstPerson,
        bool inThirdPerson) noexcept;
    // Freelook aim state never pitch-normalizes (eligible is ignored here).
    // Sprint / !eligible exclude calibration only — must not invert freelook
    // into normalize-eligible when aimState=="freelook".
    bool IsTrueFreelookPitchEnvironment(
        std::string_view aimState,
        bool trueFreelookEligible) noexcept;
    bool IsTrueFreelookPitchBaselineEligible(
        std::string_view aimState,
        bool trueFreelookEligible,
        bool sprinting) noexcept;
    // Reject normalize until aim state has been stable for settleFrames.
    bool IsPitchNormalizeSettled(
        std::uint32_t framesSinceAimStateChange,
        std::uint32_t settleFrames = 3) noexcept;
    // Bow aim mouse reconstruction is first-person only. Third-person camera mods
    // own their own look pipeline once a ranged weapon is out. Requires real FP
    // (mount / furniture / etc. must not use the FP bow path).
    bool ShouldApplyBowAimMousePath(bool inFirstPerson, bool bowAiming) noexcept;
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
    // Seed g_freelookPitchPerLook from true-freelook samples, then freeze.
    // Further samples must not move a nonzero gain (bow-exit interpolation poison).
    bool SeedFrozenFreelookPitchPerLook(
        float postTransformLookY,
        float engineTargetPitchDelta,
        float& currentGain,
        float& pendingGain,
        std::uint32_t& pendingCount) noexcept;
    // Alt-tab focus spike: first event after gapMs > threshold is zeroed.
    // suppressFocusSpike=false never zeros. Missing previous event never zeros.
    bool ShouldSuppressFocusSpikeEvent(
        bool suppressFocusSpike,
        bool havePreviousEvent,
        std::int64_t gapMs,
        int focusSpikeGapMs) noexcept;
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
        // Exact FP/TP only: neither (mount/furniture/etc.) and both → no transform.
        bool ShouldApplyInputTransform(
            const ConfigValues& config,
            bool inFirstPerson,
            bool inThirdPerson,
            bool isGamepad) const;
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
