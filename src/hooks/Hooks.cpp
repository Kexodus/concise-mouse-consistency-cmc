#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <tuple>

#if MSF_USE_COMMONLIBSSE
#include <RE/A/Actor.h>
#include <RE/A/ActorMagicCaster.h>
#include <RE/A/ActorState.h>
#include <RE/B/BSTimer.h>
#include <RE/C/ControlMap.h>
#include <RE/F/FirstPersonState.h>
#include <RE/L/LookHandler.h>
#include <RE/M/MagicCaster.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/N/NiCamera.h>
#include <RE/N/NiNode.h>
#include <RE/N/NiPoint2.h>
#include <RE/Offsets_VTABLE.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerControls.h>
#include <RE/P/PlayerControlsData.h>
#include <RE/RTTI.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/T/ThirdPersonState.h>
#include <RE/T/ThumbstickEvent.h>
#include <RE/U/UI.h>
#include <REL/Relocation.h>
#include <SKSE/Version.h>
#endif

namespace msf
{
    namespace
    {
        constexpr std::uint8_t kInputTransformsAllowed = 1U << 0U;
        constexpr std::uint8_t kThirdPersonSmoothingAllowed = 1U << 1U;
    }

#if MSF_USE_COMMONLIBSSE
    namespace
    {
        using ProcessThumbstickFn = void (*)(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
        using ProcessMouseMoveFn = void (*)(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
        using PlayerModifyMovementDataFn = void (*)(
            RE::PlayerCharacter*, float, RE::NiPoint3&, RE::NiPoint3&);
        using FirstPersonStateUpdateFn = void (*)(
            RE::FirstPersonState*, RE::BSTSmartPointer<RE::TESCameraState>&);
        using ThirdPersonHandleLookInputFn = void (*)(RE::ThirdPersonState*, const RE::NiPoint2&);

        // ActorState is a base class of Actor at compile-time offset 0xB8 (SE layout).
        // In AE 1.6.629+, TESObjectREFR grew by 8 bytes, shifting ActorState to 0xC0.
        // The C++ compiler bakes in the SE offset, so direct access reads wrong memory on AE.
        // This helper uses RelocateMemberIfNewer to read from the correct offset.
        // actorState1 (contains meleeAttackState): SE 0xC0, AE 0xC8
        RE::ATTACK_STATE_ENUM GetAttackStateRelocated(const RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return RE::ATTACK_STATE_ENUM::kNone;
            }
            const auto& as1 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState1>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC0, 0xC8);
            return as1.meleeAttackState;
        }

        bool IsSprintingRelocated(const RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return false;
            }
            const auto& as1 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState1>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC0, 0xC8);
            return static_cast<bool>(as1.sprinting);
        }

        RE::WEAPON_STATE GetWeaponStateRelocated(const RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return RE::WEAPON_STATE::kSheathed;
            }
            const auto& as2 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState2>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC4, 0xCC);
            return as2.weaponState;
        }

        // CommonLib's BSTimer::GetCurrentGlobalTimeMult points at Target (511883/388443).
        // Eagle Eye / Slow Time yaw compensation needs the live Current multiplier.
        float ReadCurrentGlobalTimeMultiplier() noexcept
        {
            REL::Relocation<float*> value{
                RELOCATION_ID(kBsTimerGlobalTimeMultCurrentSe, kBsTimerGlobalTimeMultCurrentAeVr) };
            return *value;
        }

        // Local CommonLib omits BSTimer::GetSingleton; Address Library IDs match NG/VR.
        RE::BSTimer* ReadBsTimerSingleton() noexcept
        {
            REL::Relocation<RE::BSTimer*> singleton{
                RELOCATION_ID(kBsTimerSingletonSe, kBsTimerSingletonAeVr) };
            return singleton.get();
        }

        // Binary SE/AE/VR layout: delta@0x18, realTimeDelta@0x1C.
        // Local CommonLibSSE-NG omits pad0C after lastPerformanceCount, so
        // offsetof(RE::BSTimer, realTimeDelta) is 0x18 (aliases binary delta).
        // Always read via explicit offsets; static_assert catches a fixed CommonLib.
        float ReadBsTimerFieldAtOffset(const RE::BSTimer* timer, std::size_t offset) noexcept
        {
            if (!timer) {
                return 0.0F;
            }
            return *reinterpret_cast<const float*>(
                reinterpret_cast<const std::byte*>(timer) + offset);
        }

        float ReadBsTimerRealTimeDelta(const RE::BSTimer* timer) noexcept
        {
            static_assert(
                kBsTimerRealTimeDeltaOffset == 0x1C,
                "BSTimer realTimeDelta binary offset must remain 0x1C (CommonLibVR/SE/AE)");
#if defined(_MSC_VER)
            // When upstream adds pad0C, member offsetof becomes 0x1C and matches binary.
            static_assert(
                offsetof(RE::BSTimer, realTimeDelta) == kBsTimerRealTimeDeltaOffset ||
                    offsetof(RE::BSTimer, realTimeDelta) == kBsTimerDeltaOffset,
                "Unexpected RE::BSTimer::realTimeDelta layout");
#endif
            return ReadBsTimerFieldAtOffset(timer, kBsTimerRealTimeDeltaOffset);
        }

        // Prefer explicit +0x1C realTimeDelta; fall back to steady_clock with hitch rejection.
        // Never trust broken CommonLib member access (aliases game-time delta).
        float g_lastYawWallClockSeconds{ 0.0F };
        bool g_hasYawWallClock{ false };

        float ReadWallClockDeltaSeconds() noexcept
        {
            using clock = std::chrono::steady_clock;
            const float nowSeconds = std::chrono::duration<float>(clock::now().time_since_epoch()).count();
            if (!g_hasYawWallClock) {
                g_hasYawWallClock = true;
                g_lastYawWallClockSeconds = nowSeconds;
                return 0.0F;
            }

            const float wallDelta = nowSeconds - g_lastYawWallClockSeconds;
            g_lastYawWallClockSeconds = nowSeconds;
            if (!std::isfinite(wallDelta) || wallDelta <= 0.0F || wallDelta > 0.25F) {
                return 0.0F;
            }
            return wallDelta;
        }

        float ReadRealTimeDeltaSeconds() noexcept
        {
            if (const auto* timer = ReadBsTimerSingleton()) {
                const float realTimeDelta = ReadBsTimerRealTimeDelta(timer);
                if (std::isfinite(realTimeDelta) &&
                    realTimeDelta > 0.0F &&
                    realTimeDelta <= 0.25F) {
                    // Keep wall-clock baseline fresh so fallback gaps stay small.
                    (void)ReadWallClockDeltaSeconds();
                    return realTimeDelta;
                }
            }

            return ReadWallClockDeltaSeconds();
        }

        // CommonLib's MagicCaster::State enum is empty; values match NG/powerof3.
        enum class MagicCasterState : std::uint32_t
        {
            kNone = 0,
            kUnk01 = 1,
            kUnk02 = 2,
            kReady = 3,
            kUnk04 = 4,
            kCharging = 5,
            kCasting = 6,
            kUnk07 = 7,
            kUnk08 = 8,
            kUnk09 = 9
        };

        bool IsStaffEquipped(RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return false;
            }
            for (const bool leftHand : { true, false }) {
                auto* object = player->GetEquippedObject(leftHand);
                auto* weapon = object ? skyrim_cast<RE::TESObjectWEAP*>(object) : nullptr;
                if (weapon && weapon->GetWeaponType() == RE::WEAPON_TYPE::kStaff) {
                    return true;
                }
            }
            return false;
        }

        // Cheap: magicCaster charging/casting states only (no native IsCasting).
        bool DetectCastingStatesOnly(RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return false;
            }

            const auto& runtime = player->GetActorRuntimeData();
            for (std::size_t slot = 0; slot < RE::Actor::SlotTypes::kTotal; ++slot) {
                auto* caster = runtime.magicCasters[slot];
                if (!caster) {
                    continue;
                }
                const auto state = static_cast<MagicCasterState>(caster->state.underlying());
                if (state == MagicCasterState::kCharging || state == MagicCasterState::kCasting) {
                    return true;
                }
            }
            return false;
        }

        // Verbose telemetry only: states plus up to 4 native IsCasting fallbacks.
        bool DetectCasting(RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return false;
            }

            if (DetectCastingStatesOnly(player)) {
                return true;
            }

            const auto& runtime = player->GetActorRuntimeData();
            for (std::size_t slot = 0; slot < RE::Actor::SlotTypes::kTotal; ++slot) {
                if (runtime.selectedSpells[slot] && player->IsCasting(runtime.selectedSpells[slot])) {
                    return true;
                }
            }
            return false;
        }

        bool IsWeaponDrawnRelocated(const RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return false;
            }
            switch (GetWeaponStateRelocated(player)) {
            case RE::WEAPON_STATE::kDrawn:
            case RE::WEAPON_STATE::kWantToSheathe:
            case RE::WEAPON_STATE::kSheathing:
                return true;
            default:
                return false;
            }
        }

        RE::TESObjectWEAP* GetEquippedRangedWeapon(RE::PlayerCharacter* player) noexcept
        {
            if (!player) {
                return nullptr;
            }
            auto* object = player->GetEquippedObject(false);
            auto* weapon = object ? skyrim_cast<RE::TESObjectWEAP*>(object) : nullptr;
            if (!weapon) {
                return nullptr;
            }
            switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kBow:
            case RE::WEAPON_TYPE::kCrossbow:
                return weapon;
            default:
                return nullptr;
            }
        }

        ProcessThumbstickFn g_originalProcessThumbstick{ nullptr };
        ProcessMouseMoveFn g_originalProcessMouseMove{ nullptr };
        PlayerModifyMovementDataFn g_originalPlayerModifyMovementData{ nullptr };
        FirstPersonStateUpdateFn g_originalFirstPersonStateUpdate{ nullptr };
        ThirdPersonHandleLookInputFn g_originalThirdPersonHandleLookInput{ nullptr };
        HookCoordinator* g_activeCoordinator{ nullptr };
        std::uint64_t g_lookHookCallsTotal{ 0 };
        std::uint64_t g_lookHookCallsFirstPerson{ 0 };
        std::uint64_t g_lookHookCallsThirdPerson{ 0 };
        std::uint64_t g_lookTransformAppliedCount{ 0 };
        float g_lastRawPixelX{ 0.0F };
        float g_lastRawPixelY{ 0.0F };
        float g_lastEngineX{ 0.0F };
        float g_lastEngineY{ 0.0F };
        float g_lastOutX{ 0.0F };
        float g_lastOutY{ 0.0F };
        std::string g_lastCameraState{ "Unknown" };
        std::string g_lastAimState{ "freelook" };
        std::string g_previousAimState{ "freelook" };
        bool g_lastTrueFreelookEligible{ false };
        bool g_lastBowAim{ false };
        bool g_lastBowZoomedIn{ false };
        bool g_lastRenderedZoomedIn{ false };
        bool g_lastBowOut{ false };
        float g_lastBaseAimFov{ 0.0F };
        float g_lastNormalAimFovLogged{ 0.0F };
        float g_lastCurrentAimFov{ 0.0F };
        float g_lastCurrentHFov{ 0.0F };
        float g_lastFrustumLeft{ 0.0F };
        float g_lastFrustumRight{ 0.0F };
        float g_lastFrustumTop{ 0.0F };
        float g_lastFrustumBottom{ 0.0F };
        float g_lastFrustumNear{ 0.0F };
        float g_lastFovControlScale{ 0.0F };
        float g_lastEagleEyeY{ 1.0F };
        float g_lastBowY{ 1.0F };
        std::uint64_t g_bowAimMouseFrames{ 0 };
        std::uint64_t g_eagleEyeMouseFrames{ 0 };
        std::uint64_t g_sensitivityProbeCount{ 0 };
        std::uint64_t g_rotationProbeCount{ 0 };
        bool g_wasBowZoomedIn{ false };
        bool g_loggedEagleEyeFovNarrowed{ false };
        float g_freelookYawPerLook{ 0.0F };
        std::uint32_t g_halfRateBandStreak{ 0 };
        float g_previousTimeMult{ 1.0F };
        bool g_hasPreviousTimeMult{ false };
        std::uint32_t g_stableDilatedTimeMultStreak{ 0 };
        std::uint64_t g_timeCompSkipCount{ 0 };
        std::uint64_t g_timeCompWallRewriteCount{ 0 };
        // Shared with FP pitch normalize: pause Y wall-clock rewrite when dilated
        // looking yaw was left uncompensated (no ScaleByCurrent / wall rewrite).
        std::atomic<bool> g_dilatedLookingYawActive{ false };
        std::atomic<bool> g_dilatedYawBroughtToWallClock{ true };
        constexpr float kLookRatioEmaAlpha = 0.15F;
        constexpr std::uint64_t kSensitivityProbeInterval = 120;
        std::uint64_t g_mouseTelemetryEventId{ 0 };
        std::uint64_t g_firstPersonTelemetryFrame{ 0 };
        float g_freelookPitchPerLook{ 0.0F };
        float g_pendingFreelookPitchPerLook{ 0.0F };
        std::uint32_t g_pendingFreelookPitchSamples{ 0 };
        std::uint64_t g_pitchNormalizationCount{ 0 };
        float g_normalizedPitchTarget{ 0.0F };
        bool g_hasNormalizedPitchTarget{ false };

        struct MouseTelemetryWindow
        {
            std::uint64_t firstEventId{ 0 };
            std::uint64_t lastEventId{ 0 };
            std::uint64_t eventCount{ 0 };
            float rawX{ 0.0F };
            float rawY{ 0.0F };
            float engineX{ 0.0F };
            float engineY{ 0.0F };
            float outX{ 0.0F };
            float outY{ 0.0F };
            float normalFov{ 0.0F };
            float currentVFov{ 0.0F };
            float currentHFov{ 0.0F };
            std::string firstState;
            std::string lastState;
        };

        std::mutex g_mouseTelemetryLock;
        MouseTelemetryWindow g_mouseTelemetryWindow;

        std::uint64_t g_thumbstickHookCallsTotal{ 0 };
        std::uint64_t g_thumbstickTransformAppliedCount{ 0 };
        float g_lastStickRawX{ 0.0F };
        float g_lastStickRawY{ 0.0F };
        float g_lastStickOutX{ 0.0F };
        float g_lastStickOutY{ 0.0F };

        std::uint64_t g_thirdPersonHookCallsTotal{ 0 };
        std::uint64_t g_thirdPersonSmoothingAppliedCount{ 0 };
        std::chrono::steady_clock::time_point g_lastMouseEventTime{};
        std::uint64_t g_playerYawCorrectionCount{ 0 };
        std::uint64_t g_thirdPersonYawCorrectionCount{ 0 };

        constexpr std::uint64_t kLookLogInterval = 600;
        constexpr std::uint64_t kThirdPersonLogInterval = 600;
        constexpr std::uint64_t kStickLogInterval = 600;
        constexpr std::uint64_t kHalfRateYawLogInterval = 120;

        // Sampled scale: ratio of lookInputVec units per raw mouse pixel at baseline sensitivity.
        // Updated via EMA during normal play only (not during bow aim) to stay uncontaminated.
        struct FreelookScaleAxisState
        {
            float value{ 0.0F };
            float pendingValue{ 0.0F };
            std::uint32_t pendingCount{ 0 };
        };

        struct FreelookScaleCache
        {
            FreelookScaleAxisState x;
            FreelookScaleAxisState y;
            const RE::NiNode* cameraRoot{ nullptr };
        };

        FreelookScaleCache g_firstPersonSampledScale;
        FreelookScaleCache g_thirdPersonSampledScale;
        float g_lastSampledScaleX{ 0.0F };
        float g_lastSampledScaleY{ 0.0F };
        float g_normalAimFov{ 0.0f };
        const RE::NiNode* g_lastAimFovCameraRoot{ nullptr };
        bool g_lastAimFovThirdPerson{ false };
        bool g_hasAimFovCameraIdentity{ false };
        std::string g_pitchNormalizeAimState{ "freelook" };
        std::uint32_t g_pitchNormalizeSettleFrames{ 0 };

        // Returns true when the player is actively drawing or aiming with a bow or crossbow.
        // Uses relocated ActorState::GetAttackState() range kBowDraw..kBowNextAttack.
        // OAR-proof, no animation event dependency. Matches IC's proven IsAiming() approach.
        bool DetectBowAim(RE::PlayerCharacter* player) noexcept
        {
            if (!player) return false;
            const auto attackState = GetAttackStateRelocated(player);
            return attackState >= RE::ATTACK_STATE_ENUM::kBowDraw &&
                   attackState <= RE::ATTACK_STATE_ENUM::kBowNextAttack;
        }

        const char* ClassifyAimState(bool bowOut, bool bowAim, bool bowZoomedIn) noexcept
        {
            if (bowAim && bowZoomedIn) {
                return "eagleEye";
            }
            if (bowAim) {
                return "bowPull";
            }
            if (bowOut) {
                return "bowOut";
            }
            return "freelook";
        }

        struct RenderedFovSample
        {
            float vFovDegrees{ 0.0F };
            float hFovDegrees{ 0.0F };
            float left{ 0.0F };
            float right{ 0.0F };
            float top{ 0.0F };
            float bottom{ 0.0F };
            float nearPlane{ 0.0F };
            float fovControlScale{ 0.0F };
        };

        // Eagle Eye narrows NiCamera::viewFrustum; PlayerCamera::firstPersonFOV stays at the
        // configured base (often unchanged under Improved Camera).
        RenderedFovSample ReadRenderedFov(RE::PlayerCamera* camera) noexcept
        {
            RenderedFovSample sample{};
            if (!camera || !camera->cameraRoot) {
                return sample;
            }

            auto* root = camera->cameraRoot.get();
            if (!root) {
                return sample;
            }

            auto& children = root->GetChildren();
            if (children.empty()) {
                return sample;
            }

            RE::NiCamera* niCamera = nullptr;
            for (auto& child : children) {
                if (child) {
                    niCamera = skyrim_cast<RE::NiCamera*>(child.get());
                    if (niCamera) {
                        break;
                    }
                }
            }
            if (!niCamera) {
                return sample;
            }

            const auto& frustum = niCamera->GetRuntimeData2().viewFrustum;
            sample.left = frustum.fLeft;
            sample.right = frustum.fRight;
            sample.top = frustum.fTop;
            sample.bottom = frustum.fBottom;
            sample.nearPlane = frustum.fNear;
            sample.vFovDegrees = FovDegreesFromFrustumEdges(frustum.fTop, frustum.fBottom);
            sample.hFovDegrees = FovDegreesFromFrustumEdges(frustum.fRight, frustum.fLeft);

            if (auto* fps = skyrim_cast<RE::FirstPersonState*>(camera->currentState.get())) {
                if (fps->firstPersonFOVControl) {
                    sample.fovControlScale = fps->firstPersonFOVControl->local.scale;
                }
            }
            return sample;
        }

        void LogSensitivityProbeIfNeeded(const ConfigValues& config, bool force)
        {
            if (!config.verboseLogging) {
                return;
            }
            ++g_sensitivityProbeCount;
            if (!force &&
                !ShouldEmitSampledLog(true, g_sensitivityProbeCount, kSensitivityProbeInterval, true)) {
                return;
            }

            const float outOverEngineY =
                (std::abs(g_lastEngineY) > 0.0001F) ? (g_lastOutY / g_lastEngineY) : 0.0F;
            const float fovRatio =
                (g_lastNormalAimFovLogged > 0.0F) ? (g_lastCurrentAimFov / g_lastNormalAimFovLogged) : 0.0F;

            LogInfo(
                "SensitivityProbe"
                " state=" + g_lastAimState +
                " camera=" + g_lastCameraState +
                " bowOut=" + std::to_string(g_lastBowOut ? 1 : 0) +
                " bowAim=" + std::to_string(g_lastBowAim ? 1 : 0) +
                " bowZoomFlag=" + std::to_string(g_lastBowZoomedIn ? 1 : 0) +
                " renderedZoom=" + std::to_string(g_lastRenderedZoomedIn ? 1 : 0) +
                " rawPx=(" + std::to_string(g_lastRawPixelX) + "," + std::to_string(g_lastRawPixelY) + ")" +
                " engine=(" + std::to_string(g_lastEngineX) + "," + std::to_string(g_lastEngineY) + ")" +
                " out=(" + std::to_string(g_lastOutX) + "," + std::to_string(g_lastOutY) + ")" +
                " outOverEngineY=" + std::to_string(outOverEngineY) +
                " eagleEyeY=" + std::to_string(g_lastEagleEyeY) +
                " bowY=" + std::to_string(g_lastBowY) +
                " baseFov=" + std::to_string(g_lastBaseAimFov) +
                " normalFov=" + std::to_string(g_lastNormalAimFovLogged) +
                " currentFov=" + std::to_string(g_lastCurrentAimFov) +
                " currentHFov=" + std::to_string(g_lastCurrentHFov) +
                " fovRatio=" + std::to_string(fovRatio) +
                " fovCtrlScale=" + std::to_string(g_lastFovControlScale) +
                " frustum=(L=" + std::to_string(g_lastFrustumLeft) +
                ",R=" + std::to_string(g_lastFrustumRight) +
                ",T=" + std::to_string(g_lastFrustumTop) +
                ",B=" + std::to_string(g_lastFrustumBottom) +
                ",N=" + std::to_string(g_lastFrustumNear) + ")" +
                " sampledScale=(" + std::to_string(g_lastSampledScaleX) + "," + std::to_string(g_lastSampledScaleY) + ")" +
                " bowAimFrames=" + std::to_string(g_bowAimMouseFrames) +
                " eagleEyeFrames=" + std::to_string(g_eagleEyeMouseFrames));
        }

        void LogLookHookCountersIfNeeded(const ConfigValues& config, bool force = false)
        {
            if (!force &&
                !ShouldEmitSampledLog(config.verboseLogging, g_lookHookCallsTotal, kLookLogInterval)) {
                return;
            }
            if (force && !config.verboseLogging) {
                return;
            }

            LogInfo(
                "HookCounter[LookHandler::ProcessMouseMove]"
                " total=" + std::to_string(g_lookHookCallsTotal) +
                " firstPerson=" + std::to_string(g_lookHookCallsFirstPerson) +
                " thirdPerson=" + std::to_string(g_lookHookCallsThirdPerson) +
                " transformed=" + std::to_string(g_lookTransformAppliedCount) +
                " state=" + g_lastAimState +
                " bowAimFrames=" + std::to_string(g_bowAimMouseFrames) +
                " eagleEyeFrames=" + std::to_string(g_eagleEyeMouseFrames) +
                " bowAim=" + std::to_string(g_lastBowAim ? 1 : 0) +
                " bowZoomFlag=" + std::to_string(g_lastBowZoomedIn ? 1 : 0) +
                " renderedZoom=" + std::to_string(g_lastRenderedZoomedIn ? 1 : 0) +
                " baseFov=" + std::to_string(g_lastBaseAimFov) +
                " normalFov=" + std::to_string(g_lastNormalAimFovLogged) +
                " currentFov=" + std::to_string(g_lastCurrentAimFov) +
                " frustumTop=" + std::to_string(g_lastFrustumTop) +
                " frustumNear=" + std::to_string(g_lastFrustumNear) +
                " eagleEyeY=" + std::to_string(g_lastEagleEyeY) +
                " bowY=" + std::to_string(g_lastBowY) +
                " rawPx=(" + std::to_string(g_lastRawPixelX) + "," + std::to_string(g_lastRawPixelY) + ")" +
                " engine=(" + std::to_string(g_lastEngineX) + "," + std::to_string(g_lastEngineY) + ")" +
                " out=(" + std::to_string(g_lastOutX) + "," + std::to_string(g_lastOutY) + ")" +
                " sampledScale=(" + std::to_string(g_lastSampledScaleX) + "," + std::to_string(g_lastSampledScaleY) + ")" +
                " camera=" + g_lastCameraState);
        }

        void LogThirdPersonHookCountersIfNeeded(const ConfigValues& config)
        {
            if (!ShouldEmitSampledLog(
                    config.verboseLogging,
                    g_thirdPersonHookCallsTotal,
                    kThirdPersonLogInterval)) {
                return;
            }

            LogInfo(
                "HookCounter[ThirdPersonState::HandleLookInput]"
                " total=" + std::to_string(g_thirdPersonHookCallsTotal) +
                " smoothingRemoved=" + std::to_string(g_thirdPersonSmoothingAppliedCount));
        }

        struct FirstPersonOrientationSample
        {
            float currentPitchOffset{ 0.0F };
            float targetPitchOffset{ 0.0F };
            bool cameraPitchOverride{ false };
            RE::NiQuaternion cameraRotation{};
            RE::NiPoint3 localEuler{};
            RE::NiPoint3 worldEuler{};
            bool hasCameraObject{ false };
            float playerCameraYaw{ 0.0F };
            float rotationInputX{ 0.0F };
            float rotationInputY{ 0.0F };
        };

        FirstPersonOrientationSample CaptureFirstPersonOrientation(
            RE::FirstPersonState* state,
            bool captureDiagnostics)
        {
            FirstPersonOrientationSample sample{};
            if (!state) {
                return sample;
            }

            sample.currentPitchOffset = state->currentPitchOffset;
            sample.targetPitchOffset = state->targetPitchOffset;
            sample.cameraPitchOverride = state->cameraPitchOverride;
            if (!captureDiagnostics) {
                return sample;
            }
            state->GetRotation(sample.cameraRotation);
            if (state->firstPersonCameraObj) {
                sample.hasCameraObject = true;
                state->firstPersonCameraObj->local.rotate.ToEulerAnglesXYZ(sample.localEuler);
                state->firstPersonCameraObj->world.rotate.ToEulerAnglesXYZ(sample.worldEuler);
            }
            if (auto* camera = RE::PlayerCamera::GetSingleton()) {
                sample.playerCameraYaw = camera->yaw;
                sample.rotationInputX = camera->rotationInput.x;
                sample.rotationInputY = camera->rotationInput.y;
            }
            return sample;
        }

        float WrappedAngleDelta(float after, float before) noexcept
        {
            float delta = after - before;
            constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
            while (delta > std::numbers::pi_v<float>) {
                delta -= twoPi;
            }
            while (delta < -std::numbers::pi_v<float>) {
                delta += twoPi;
            }
            return delta;
        }

        void FirstPersonStateUpdateHook(
            RE::FirstPersonState* state,
            RE::BSTSmartPointer<RE::TESCameraState>& nextState)
        {
            if (!g_originalFirstPersonStateUpdate) {
                return;
            }

            const auto config = ConfigManager::Get().GetSnapshot();
            const auto before = CaptureFirstPersonOrientation(state, config.verboseLogging);
            g_originalFirstPersonStateUpdate(state, nextState);
            const auto after = CaptureFirstPersonOrientation(state, config.verboseLogging);

            MouseTelemetryWindow input{};
            {
                std::scoped_lock lock(g_mouseTelemetryLock);
                input = std::move(g_mouseTelemetryWindow);
                g_mouseTelemetryWindow = {};
            }
            ++g_firstPersonTelemetryFrame;
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* camera = RE::PlayerCamera::GetSingleton();
            const bool sprinting = IsSprintingRelocated(player);
            // Idle frames must use live weapon/aim state — not g_lastAimState /
            // g_lastTrueFreelookEligible from the previous mouse event.
            const bool isBowAim = DetectBowAim(player);
            const bool rangedWeaponEquipped = GetEquippedRangedWeapon(player) != nullptr;
            const auto weaponState =
                player ? GetWeaponStateRelocated(player) : RE::WEAPON_STATE::kSheathed;
            const bool weaponFullySheathed = weaponState == RE::WEAPON_STATE::kSheathed;
            const bool bowOut = rangedWeaponEquipped && IsWeaponDrawnRelocated(player);
            const bool bowZoomFlag = camera && camera->bowZoomedIn;
            const bool effectiveBowZoomedIn =
                isBowAim && (bowZoomFlag || g_lastRenderedZoomedIn);
            const std::string liveAimState =
                ClassifyAimState(bowOut, isBowAim, effectiveBowZoomedIn);
            const bool liveTrueFreelookEligible = ShouldUpdateFreelookSampledScale(
                rangedWeaponEquipped,
                weaponFullySheathed,
                isBowAim,
                bowZoomFlag);

            const bool hasInputWindow = input.eventCount > 0;
            const bool stableState =
                !hasInputWindow || input.firstState == input.lastState;
            const std::string activeState =
                hasInputWindow ? input.lastState : liveAimState;
            const bool pitchTargetInRange =
                std::abs(before.targetPitchOffset) < 95.0F &&
                std::abs(after.targetPitchOffset) < 95.0F;
            const float pitchOffsetDelta =
                after.currentPitchOffset - before.currentPitchOffset;
            const float engineTargetPitchDelta =
                after.targetPitchOffset - before.targetPitchOffset;
            bool menuBlocksPitch = false;
            if (config.disableInMenus) {
                if (auto* ui = RE::UI::GetSingleton()) {
                    menuBlocksPitch = ui->GameIsPaused() || ui->IsApplicationMenuOpen();
                }
            }
            bool lookControlsBlockPitch = false;
            if (config.disableWhenLookControlsDisabled) {
                if (const auto* controlMap = RE::ControlMap::GetSingleton()) {
                    lookControlsBlockPitch = !controlMap->IsLookingControlsEnabled();
                }
            }
            // Sprint / casting exclude calibration only — freelook still must not normalize.
            const bool castingBlocksPitchBaseline = DetectCastingStatesOnly(player);
            const bool trueFreelookBaseline =
                hasInputWindow &&
                stableState &&
                IsTrueFreelookPitchBaselineEligible(
                    activeState,
                    liveTrueFreelookEligible,
                    sprinting) &&
                !castingBlocksPitchBaseline &&
                !after.cameraPitchOverride &&
                pitchTargetInRange;
            // The pitch gain is an engine constant relative to post-transform Y.
            // Seed once from three consistent true-freelook samples, then freeze it;
            // bow-exit interpolation can otherwise contaminate an adaptive EMA.
            if (trueFreelookBaseline && g_freelookPitchPerLook == 0.0F) {
                UpdateFreelookScaleSample(
                    input.outY,
                    engineTargetPitchDelta,
                    g_freelookPitchPerLook,
                    g_pendingFreelookPitchPerLook,
                    g_pendingFreelookPitchSamples);
            }

            if (liveAimState != g_pitchNormalizeAimState) {
                g_pitchNormalizeAimState = liveAimState;
                g_pitchNormalizeSettleFrames = 0;
            } else if (g_pitchNormalizeSettleFrames < 255U) {
                ++g_pitchNormalizeSettleFrames;
            }
            constexpr std::uint32_t kPitchNormalizeSettleFrames = 3;
            const bool pitchNormalizeSettled = IsPitchNormalizeSettled(
                g_pitchNormalizeSettleFrames,
                kPitchNormalizeSettleFrames);

            const bool trueFreelookEnvironment = IsTrueFreelookPitchEnvironment(
                liveAimState,
                liveTrueFreelookEligible);
            const bool pausePitchForUncompensatedYaw = ShouldPausePitchNormalizeForDilatedYaw(
                g_dilatedLookingYawActive.load(std::memory_order_relaxed),
                true,
                g_dilatedYawBroughtToWallClock.load(std::memory_order_relaxed));
            const bool normalizationEligible =
                config.enabled &&
                config.enableFirstPersonHook &&
                !menuBlocksPitch &&
                !lookControlsBlockPitch &&
                stableState &&
                pitchNormalizeSettled &&
                !trueFreelookEnvironment &&
                !pausePitchForUncompensatedYaw &&
                !after.cameraPitchOverride &&
                pitchTargetInRange &&
                std::abs(g_freelookPitchPerLook) > 0.0000001F;
            const float requestedNormalizedPitchDelta = NormalizePitchTargetDelta(
                input.outY,
                engineTargetPitchDelta,
                g_freelookPitchPerLook,
                normalizationEligible);

            if (trueFreelookEnvironment ||
                !config.enabled ||
                !config.enableFirstPersonHook ||
                menuBlocksPitch ||
                lookControlsBlockPitch ||
                pausePitchForUncompensatedYaw ||
                after.cameraPitchOverride) {
                g_hasNormalizedPitchTarget = false;
            }
            if (normalizationEligible && !g_hasNormalizedPitchTarget) {
                g_normalizedPitchTarget = before.targetPitchOffset;
                g_hasNormalizedPitchTarget = true;
            }
            if (normalizationEligible && hasInputWindow) {
                g_normalizedPitchTarget = std::clamp(
                    g_normalizedPitchTarget + requestedNormalizedPitchDelta,
                    -100.0F,
                    100.0F);
            }

            const float correctedTargetPitchAfter =
                normalizationEligible && g_hasNormalizedPitchTarget
                ? g_normalizedPitchTarget
                : after.targetPitchOffset;
            const float normalizedTargetPitchDelta =
                correctedTargetPitchAfter - before.targetPitchOffset;
            const bool pitchNormalized =
                normalizationEligible &&
                std::abs(correctedTargetPitchAfter - after.targetPitchOffset) > 0.00001F;
            if (normalizationEligible && g_hasNormalizedPitchTarget) {
                state->targetPitchOffset = correctedTargetPitchAfter;
            }
            if (pitchNormalized) {
                ++g_pitchNormalizationCount;
            }

            float localXDelta = 0.0F;
            float localYDelta = 0.0F;
            float localZDelta = 0.0F;
            float worldXDelta = 0.0F;
            float worldYDelta = 0.0F;
            float worldZDelta = 0.0F;
            float cameraYawDelta = 0.0F;
            if (config.verboseLogging) {
                localXDelta = WrappedAngleDelta(after.localEuler.x, before.localEuler.x);
                localYDelta = WrappedAngleDelta(after.localEuler.y, before.localEuler.y);
                localZDelta = WrappedAngleDelta(after.localEuler.z, before.localEuler.z);
                worldXDelta = WrappedAngleDelta(after.worldEuler.x, before.worldEuler.x);
                worldYDelta = WrappedAngleDelta(after.worldEuler.y, before.worldEuler.y);
                worldZDelta = WrappedAngleDelta(after.worldEuler.z, before.worldEuler.z);
                cameraYawDelta = WrappedAngleDelta(after.playerCameraYaw, before.playerCameraYaw);
            }

            if (ShouldEmitSampledLog(
                    config.verboseLogging,
                    g_firstPersonTelemetryFrame,
                    120,
                    true)) {
                LogInfo(
                    "FinalAxisResponse"
                    " frame=" + std::to_string(g_firstPersonTelemetryFrame) +
                    " events=" + std::to_string(input.eventCount) +
                    " state=" + activeState +
                    " raw=(" + std::to_string(input.rawX) + "," + std::to_string(input.rawY) + ")" +
                    " out=(" + std::to_string(input.outX) + "," + std::to_string(input.outY) + ")" +
                    " pitchOffsetDelta=" + std::to_string(pitchOffsetDelta) +
                    " engineTargetPitchDelta=" + std::to_string(engineTargetPitchDelta) +
                    " requestedNormalizedPitchDelta=" + std::to_string(requestedNormalizedPitchDelta) +
                    " normalizedTargetPitchDelta=" + std::to_string(normalizedTargetPitchDelta) +
                    " freelookPitchPerLook=" + std::to_string(g_freelookPitchPerLook) +
                    " pitchNormalized=" + std::to_string(pitchNormalized ? 1 : 0) +
                    " localEulerDelta=(" + std::to_string(localXDelta) + "," +
                        std::to_string(localYDelta) + "," + std::to_string(localZDelta) + ")" +
                    " worldEulerDelta=(" + std::to_string(worldXDelta) + "," +
                        std::to_string(worldYDelta) + "," + std::to_string(worldZDelta) + ")" +
                    " cameraYawDelta=" + std::to_string(cameraYawDelta) +
                    " fov=(" + std::to_string(input.currentHFov) + "," +
                        std::to_string(input.currentVFov) + ")");
            }
        }
        LookCorrectionContext PollLookCorrectionContext(
            RE::PlayerCharacter* player,
            RE::PlayerCamera* camera,
            const RE::NiPoint2& lookInput,
            bool includeCastingTelemetry) noexcept
        {
            LookCorrectionContext ctx{};
            const auto person = ClassifyLookCameraPerson(
                camera && camera->IsInFirstPerson(),
                camera && camera->IsInThirdPerson());
            ctx.firstPerson = person.firstPerson;
            ctx.thirdPerson = person.thirdPerson;
            ctx.sprinting = IsSprintingRelocated(player);
            ctx.bowAiming = DetectBowAim(player);
            const bool rangedWeaponEquipped = GetEquippedRangedWeapon(player) != nullptr;
            ctx.bowOut = rangedWeaponEquipped && IsWeaponDrawnRelocated(player);
            ctx.looking = IsLookingForYawCorrection(lookInput.x);
            // Casting/staff are verbose telemetry + freelook yaw EMA guards only.
            // Restore stays measurement-driven (orphan band); skip the hot-path scan.
            if (includeCastingTelemetry) {
                ctx.casting = DetectCasting(player);
                ctx.staff = IsStaffEquipped(player);
            }
            ctx.timeMult = ReadCurrentGlobalTimeMultiplier();
            ctx.timeDilated = IsGlobalTimeDilatedForYaw(ctx.timeMult);

            if (const auto* controlMap = RE::ControlMap::GetSingleton()) {
                ctx.lookControlsEnabled = controlMap->IsLookingControlsEnabled();
            }
            if (auto* ui = RE::UI::GetSingleton()) {
                ctx.menuMode = ui->GameIsPaused() || ui->IsApplicationMenuOpen();
            }

            // Diagnostic FOV only — never drive correction multipliers.
            ctx.renderedVFovDegrees = g_lastCurrentAimFov;
            ctx.normalVFovDegrees = g_normalAimFov;
            return ctx;
        }

        void ClearMouseTelemetryWindow() noexcept
        {
            std::scoped_lock lock(g_mouseTelemetryLock);
            g_mouseTelemetryWindow = {};
        }

        void PlayerModifyMovementDataHook(
            RE::PlayerCharacter* player,
            float delta,
            RE::NiPoint3& movementData,
            RE::NiPoint3& rotationData)
        {
            if (!g_originalPlayerModifyMovementData) {
                return;
            }

            auto* controls = RE::PlayerControls::GetSingleton();
            auto* camera = RE::PlayerCamera::GetSingleton();
            const auto config = ConfigManager::Get().GetSnapshot();
            const RE::NiPoint2 lookInput = controls ? controls->data.lookInputVec : RE::NiPoint2{};
            const float engineYawDelta = rotationData.z;
            const LookCorrectionContext lookCtx = PollLookCorrectionContext(
                player,
                camera,
                lookInput,
                config.verboseLogging);
            const LookCorrectionPolicy lookPolicy = EvaluateLookCorrectionPolicy(
                lookCtx,
                config.enabled,
                config.enableFirstPersonHook,
                config.enableThirdPersonHook,
                config.disableInMenus,
                config.disableWhenLookControlsDisabled);

            const float observedScale = ComputeObservedYawScale(
                lookInput.x,
                delta,
                engineYawDelta);
            const bool inHalfRateBand = IsObservedHalfRateScale(observedScale);
            g_halfRateBandStreak = UpdateHalfRateBandStreak(
                lookPolicy.restoreHalfRateYaw,
                inHalfRateBand,
                g_halfRateBandStreak);
            const bool sprintOrBowHint = lookCtx.sprinting || lookCtx.bowAiming;
            const bool applyHalfRate = ShouldApplyHalfRateRestore(
                lookPolicy.restoreHalfRateYaw,
                inHalfRateBand,
                g_halfRateBandStreak,
                sprintOrBowHint);

            const bool timeMultStable = IsGlobalTimeMultStable(
                lookCtx.timeMult,
                g_previousTimeMult,
                g_hasPreviousTimeMult);
            if (lookCtx.timeDilated && timeMultStable) {
                if (g_stableDilatedTimeMultStreak < 255U) {
                    ++g_stableDilatedTimeMultStreak;
                }
            } else {
                g_stableDilatedTimeMultStreak = 0;
            }

            const float realTimeDelta = ReadRealTimeDeltaSeconds();
            TimeCompSkipReason timeCompSkipReason = TimeCompSkipReason::None;
            const TimeCompMode timeCompMode = ResolveTimeCompMode(
                lookPolicy.compensateTimeYaw,
                delta,
                realTimeDelta,
                lookCtx.timeMult,
                g_previousTimeMult,
                g_hasPreviousTimeMult,
                &timeCompSkipReason,
                0.12F,
                0.08F,
                g_stableDilatedTimeMultStreak);
            if (lookPolicy.compensateTimeYaw &&
                timeCompMode == TimeCompMode::None &&
                timeCompSkipReason != TimeCompSkipReason::NotRequested &&
                timeCompSkipReason != TimeCompSkipReason::NotDilated) {
                ++g_timeCompSkipCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_timeCompSkipCount,
                        30,
                        true)) {
                    LogInfo(
                        std::string("YawTimeCompSkip")
                        + " skipReason=" + TimeCompSkipReasonName(timeCompSkipReason)
                        + " mode=" + TimeCompModeName(timeCompMode)
                        + " delta=" + std::to_string(delta)
                        + " realTimeDelta=" + std::to_string(realTimeDelta)
                        + " timeMult=" + std::to_string(lookCtx.timeMult)
                        + " prevTimeMult=" + std::to_string(g_previousTimeMult)
                        + " stableDilatedStreak=" + std::to_string(g_stableDilatedTimeMultStreak)
                        + " agree=" + std::to_string(
                            ComputeTimeDeltaAgreement(delta, realTimeDelta, lookCtx.timeMult))
                        + " casting=" + std::to_string(lookCtx.casting ? 1 : 0));
                }
            }
            if (timeCompMode == TimeCompMode::RewriteWallClock) {
                ++g_timeCompWallRewriteCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_timeCompWallRewriteCount,
                        30,
                        true)) {
                    LogInfo(
                        std::string("YawTimeCompWallRewrite")
                        + " skipReason=" + TimeCompSkipReasonName(timeCompSkipReason)
                        + " delta=" + std::to_string(delta)
                        + " realTimeDelta=" + std::to_string(realTimeDelta)
                        + " timeMult=" + std::to_string(lookCtx.timeMult)
                        + " agree=" + std::to_string(
                            ComputeTimeDeltaAgreement(delta, realTimeDelta, lookCtx.timeMult))
                        + " casting=" + std::to_string(lookCtx.casting ? 1 : 0));
                }
            }
            g_previousTimeMult = lookCtx.timeMult;
            g_hasPreviousTimeMult = true;

            const auto yawCorrection = ApplyPlayerYawCorrection(
                lookInput.x,
                delta,
                rotationData.z,
                lookCtx.timeMult,
                applyHalfRate,
                timeCompMode,
                realTimeDelta);
            rotationData.z = yawCorrection.yawDelta;
            const bool yawCorrected = rotationData.z != engineYawDelta;
            const bool timeDilatedYawCompensated = yawCorrection.timeCompensated;
            // Only treat yaw as wall-clock when Apply actually compensated.
            // RewriteWallClock with a bad rtd leaves yaw uncompensated → pause pitch.
            if (lookPolicy.compensateTimeYaw) {
                g_dilatedLookingYawActive.store(true, std::memory_order_relaxed);
                g_dilatedYawBroughtToWallClock.store(
                    timeDilatedYawCompensated,
                    std::memory_order_relaxed);
            } else {
                g_dilatedLookingYawActive.store(false, std::memory_order_relaxed);
                g_dilatedYawBroughtToWallClock.store(true, std::memory_order_relaxed);
            }
            if (yawCorrected) {
                ++g_playerYawCorrectionCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_playerYawCorrectionCount,
                        kHalfRateYawLogInterval,
                        true)) {
                    LogInfo(
                        "HookCounter[PlayerYawCorrection]"
                        " corrected=" + std::to_string(g_playerYawCorrectionCount) +
                        " thirdPerson=" + std::to_string(lookCtx.thirdPerson ? 1 : 0) +
                        " sprinting=" + std::to_string(lookCtx.sprinting ? 1 : 0) +
                        " bowAiming=" + std::to_string(lookCtx.bowAiming ? 1 : 0) +
                        " casting=" + std::to_string(lookCtx.casting ? 1 : 0) +
                        " staff=" + std::to_string(lookCtx.staff ? 1 : 0) +
                        " halfRate=" + std::to_string(yawCorrection.halfRateRestored ? 1 : 0) +
                        " observedScale=" + std::to_string(observedScale) +
                        " engineYaw=" + std::to_string(engineYawDelta) +
                        " restoredYaw=" + std::to_string(rotationData.z) +
                        " lookX=" + std::to_string(lookInput.x) +
                        " delta=" + std::to_string(delta) +
                        " timeMult=" + std::to_string(lookCtx.timeMult) +
                        " timeComp=" + std::to_string(timeDilatedYawCompensated ? 1 : 0) +
                        " mode=" + TimeCompModeName(timeCompMode));
                }
            }

            if (lookCtx.thirdPerson && timeDilatedYawCompensated && yawCorrected) {
                ++g_thirdPersonYawCorrectionCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_thirdPersonYawCorrectionCount,
                        60,
                        true)) {
                    const float expectedRealtimeYaw =
                        timeCompMode == TimeCompMode::RewriteWallClock
                        ? lookInput.x * realTimeDelta * std::numbers::pi_v<float>
                        : lookInput.x * (delta / lookCtx.timeMult) * std::numbers::pi_v<float>;
                    const float outputRatio = std::abs(expectedRealtimeYaw) > 0.00001F
                        ? rotationData.z / expectedRealtimeYaw
                        : 0.0F;
                    LogInfo(
                        "ThirdPersonYawRotation"
                        " corrected=" + std::to_string(g_thirdPersonYawCorrectionCount) +
                        " state=" + g_lastAimState +
                        " halfRateRestored=" + std::to_string(yawCorrection.halfRateRestored ? 1 : 0) +
                        " timeComp=" + std::to_string(yawCorrection.timeCompensated ? 1 : 0) +
                        " mode=" + TimeCompModeName(timeCompMode) +
                        " casting=" + std::to_string(lookCtx.casting ? 1 : 0) +
                        " lookX=" + std::to_string(lookInput.x) +
                        " rotYawEngine=" + std::to_string(engineYawDelta) +
                        " rotYawOut=" + std::to_string(rotationData.z) +
                        " expectedRealtimeYaw=" + std::to_string(expectedRealtimeYaw) +
                        " outputRatioToExpected=" + std::to_string(outputRatio) +
                        " delta=" + std::to_string(delta) +
                        " realTimeDelta=" + std::to_string(realTimeDelta) +
                        " timeMult=" + std::to_string(lookCtx.timeMult));
                }
            }

            if (config.verboseLogging && lookCtx.firstPerson &&
                (std::abs(lookInput.x) >= 0.01F || std::abs(lookInput.y) >= 0.01F)) {
                const float yawPerLook =
                    (std::abs(lookInput.x) >= 0.01F) ? (rotationData.z / lookInput.x) : 0.0F;
                const std::string state = g_lastAimState;

                if (ShouldUpdateFreelookYawEma(
                        g_lastTrueFreelookEligible,
                        lookCtx.sprinting,
                        lookCtx.casting,
                        observedScale) &&
                    std::abs(lookInput.x) >= 0.01F) {
                    g_freelookYawPerLook = (g_freelookYawPerLook == 0.0F)
                        ? yawPerLook
                        : g_freelookYawPerLook + kLookRatioEmaAlpha * (yawPerLook - g_freelookYawPerLook);
                }

                ++g_rotationProbeCount;
                if (ShouldEmitSampledLog(true, g_rotationProbeCount, kSensitivityProbeInterval, true)) {
                    const float yawRatioToFreelook =
                        (std::abs(g_freelookYawPerLook) > 0.000001F && std::abs(lookInput.x) >= 0.01F)
                        ? (yawPerLook / g_freelookYawPerLook)
                        : 0.0F;
                    LogInfo(
                        "YawRotation"
                        " state=" + state +
                        " sprinting=" + std::to_string(lookCtx.sprinting ? 1 : 0) +
                        " casting=" + std::to_string(lookCtx.casting ? 1 : 0) +
                        " staff=" + std::to_string(lookCtx.staff ? 1 : 0) +
                        " yawCorrected=" + std::to_string(yawCorrected ? 1 : 0) +
                        " halfRate=" + std::to_string(yawCorrection.halfRateRestored ? 1 : 0) +
                        " timeComp=" + std::to_string(timeDilatedYawCompensated ? 1 : 0) +
                        " mode=" + TimeCompModeName(timeCompMode) +
                        " skipReason=" + TimeCompSkipReasonName(timeCompSkipReason) +
                        " look=(" + std::to_string(lookInput.x) + "," + std::to_string(lookInput.y) + ")" +
                        " rotYawEngine=" + std::to_string(engineYawDelta) +
                        " rotYawOut=" + std::to_string(rotationData.z) +
                        " yawPerLook=" + std::to_string(yawPerLook) +
                        " freelookYawPerLook=" + std::to_string(g_freelookYawPerLook) +
                        " yawRatioToFreelook=" + std::to_string(yawRatioToFreelook) +
                        " observedScale=" + std::to_string(observedScale) +
                        " delta=" + std::to_string(delta) +
                        " realTimeDelta=" + std::to_string(realTimeDelta) +
                        " timeMult=" + std::to_string(lookCtx.timeMult));
                }
            }

            g_originalPlayerModifyMovementData(player, delta, movementData, rotationData);
        }

        void ProcessMouseMoveHook(RE::LookHandler* handler, RE::MouseMoveEvent* event, RE::PlayerControlsData* data)
        {
            if (!g_originalProcessMouseMove) {
                return;
            }

            if (!event || !g_activeCoordinator) {
                g_originalProcessMouseMove(handler, event, data);
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto reloadedConfig = ConfigManager::Get().GetSnapshot();
            auto* camera = RE::PlayerCamera::GetSingleton();
            const bool inFirstPerson = camera && camera->IsInFirstPerson();
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            auto* player = RE::PlayerCharacter::GetSingleton();

            if (!g_activeCoordinator->ShouldApplyInputTransform(
                    reloadedConfig,
                    inFirstPerson,
                    inThirdPerson,
                    false)) {
                // Mount/menu/neither/both must not leave stale FP pitch windows.
                ClearMouseTelemetryWindow();
                g_originalProcessMouseMove(handler, event, data);
                return;
            }

            ++g_lookHookCallsTotal;

            if (inThirdPerson) {
                ++g_lookHookCallsThirdPerson;
                g_lastCameraState = "ThirdPerson";
            } else if (inFirstPerson) {
                ++g_lookHookCallsFirstPerson;
                g_lastCameraState = "FirstPerson";
            } else {
                g_lastCameraState = "Other";
            }

            if (reloadedConfig.disableInMenus) {
                auto* ui = RE::UI::GetSingleton();
                if (ui && (ui->GameIsPaused() || ui->IsApplicationMenuOpen())) {
                    g_lastCameraState = "Menu";
                    ClearMouseTelemetryWindow();
                    LogLookHookCountersIfNeeded(reloadedConfig);
                    g_originalProcessMouseMove(handler, event, data);
                    return;
                }
            }

            if (reloadedConfig.disableWhenLookControlsDisabled) {
                const auto* controlMap = RE::ControlMap::GetSingleton();
                if (controlMap && !controlMap->IsLookingControlsEnabled()) {
                    g_lastCameraState = "LookControlsDisabled";
                    ClearMouseTelemetryWindow();
                    LogLookHookCountersIfNeeded(reloadedConfig);
                    g_originalProcessMouseMove(handler, event, data);
                    return;
                }
            }

            if (reloadedConfig.suppressFocusSpike) {
                const auto now = std::chrono::steady_clock::now();
                if (g_lastMouseEventTime.time_since_epoch().count() != 0) {
                    const auto gapMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastMouseEventTime).count();
                    if (gapMs > reloadedConfig.focusSpikeGapMs) {
                        event->mouseInputX = 0;
                        event->mouseInputY = 0;
                        g_lastCameraState = "FocusSpikeSuppressed";
                        g_lastMouseEventTime = now;
                        ClearMouseTelemetryWindow();
                        LogLookHookCountersIfNeeded(reloadedConfig);
                        g_originalProcessMouseMove(handler, event, data);
                        return;
                    }
                }
                g_lastMouseEventTime = now;
            }

            // Capture raw OS mouse pixels before the engine applies any state-based scaling.
            const float rawPixelX = static_cast<float>(event->mouseInputX);
            const float rawPixelY = static_cast<float>(event->mouseInputY);
            g_originalProcessMouseMove(handler, event, data);
            if (!data) {
                return;
            }

            g_lastRawPixelX = rawPixelX;
            g_lastRawPixelY = rawPixelY;
            g_lastEngineX = data->lookInputVec.x;
            g_lastEngineY = data->lookInputVec.y;

            const bool isBowAim = DetectBowAim(player);
            const bool rangedWeaponEquipped = GetEquippedRangedWeapon(player) != nullptr;
            const auto weaponState = player ? GetWeaponStateRelocated(player) : RE::WEAPON_STATE::kSheathed;
            const bool weaponFullySheathed = weaponState == RE::WEAPON_STATE::kSheathed;
            const bool rangedWeaponActive = rangedWeaponEquipped && !weaponFullySheathed;
            const bool bowOut = rangedWeaponEquipped && IsWeaponDrawnRelocated(player);
            const bool bowZoomFlag = camera && camera->bowZoomedIn;

            // Frustum / rendered-zoom classification feeds aim state and eagleEye
            // counters even when verbose logging is off. Heavy log emission stays gated.
            float baseAimFov = camera
                ? (inThirdPerson ? camera->worldFOV : camera->firstPersonFOV)
                : 0.0F;
            RenderedFovSample rendered = ReadRenderedFov(camera);
            float currentAimFov =
                rendered.vFovDegrees > 0.0F ? rendered.vFovDegrees : baseAimFov;

            const auto* cameraRoot = camera ? camera->cameraRoot.get() : nullptr;
            if (!g_hasAimFovCameraIdentity ||
                cameraRoot != g_lastAimFovCameraRoot ||
                inThirdPerson != g_lastAimFovThirdPerson) {
                g_normalAimFov = 0.0F;
                g_lastAimFovCameraRoot = cameraRoot;
                g_lastAimFovThirdPerson = inThirdPerson;
                g_hasAimFovCameraIdentity = true;
            }

            // Only true freelook is a safe baseline. During zoom exit, bowZoomedIn can
            // clear before the rendered frustum expands; bowOut/bowPull frames must not
            // replace the normal denominator with that transitional narrow FOV.
            if (ShouldUpdateNormalAimFov(rangedWeaponActive, isBowAim, bowZoomFlag, currentAimFov)) {
                g_normalAimFov = currentAimFov;
            }
            const bool renderedZoomedIn =
                isBowAim &&
                g_normalAimFov > 0.0F &&
                currentAimFov > 0.0F &&
                (currentAimFov / g_normalAimFov) < 0.98F;
            const bool effectiveBowZoomedIn = isBowAim && (bowZoomFlag || renderedZoomedIn);

            // Update sampled scale only during true freelook on an exact FP or TP camera.
            // Mount/furniture/etc. must not poison either cache (bow X reconstruct uses FP).
            const auto scaleCacheKind = SelectFreelookScaleCache(inFirstPerson, inThirdPerson);
            FreelookScaleCache* sampledScale = nullptr;
            if (scaleCacheKind == FreelookScaleCacheKind::FirstPerson) {
                sampledScale = &g_firstPersonSampledScale;
            } else if (scaleCacheKind == FreelookScaleCacheKind::ThirdPerson) {
                sampledScale = &g_thirdPersonSampledScale;
            }
            const auto* currentCameraRoot = camera ? camera->cameraRoot.get() : nullptr;
            if (sampledScale && sampledScale->cameraRoot != currentCameraRoot) {
                *sampledScale = {};
                sampledScale->cameraRoot = currentCameraRoot;
            }
            const bool trueFreelookEligible = sampledScale && ShouldUpdateFreelookSampledScale(
                    rangedWeaponEquipped,
                    weaponFullySheathed,
                    isBowAim,
                    bowZoomFlag);
            if (trueFreelookEligible && sampledScale) {
                UpdateFreelookScaleSample(
                    rawPixelX,
                    data->lookInputVec.x,
                    sampledScale->x.value,
                    sampledScale->x.pendingValue,
                    sampledScale->x.pendingCount);
                UpdateFreelookScaleSample(
                    rawPixelY,
                    data->lookInputVec.y,
                    sampledScale->y.value,
                    sampledScale->y.pendingValue,
                    sampledScale->y.pendingCount);
            }
            g_lastSampledScaleX = sampledScale ? sampledScale->x.value : 0.0F;
            g_lastSampledScaleY = sampledScale ? sampledScale->y.value : 0.0F;
            g_lastTrueFreelookEligible = trueFreelookEligible;

            float deltaX = data->lookInputVec.x;
            float deltaY = data->lookInputVec.y;

            float eagleEyeY = 1.0F;
            float bowY = 1.0F;
            if (isBowAim) {
                ++g_bowAimMouseFrames;
                if (effectiveBowZoomedIn) {
                    ++g_eagleEyeMouseFrames;
                }
                if (inThirdPerson) {
                    g_lastCameraState =
                        effectiveBowZoomedIn ? "ThirdPerson_EagleEye" : "ThirdPerson_BowAim";
                } else if (inFirstPerson) {
                    g_lastCameraState =
                        effectiveBowZoomedIn ? "FirstPerson_EagleEye" : "FirstPerson_BowAim";
                } else {
                    g_lastCameraState =
                        effectiveBowZoomedIn ? "Other_EagleEye" : "Other_BowAim";
                }
                // FOV is telemetry only while final yaw and pitch gains are measured.
                eagleEyeY = 1.0F;
                // Bow aim X reconstruction + configurable bow multipliers are first-person
                // only. In third person, leave the engine/camera-mod look deltas alone.
                if (ShouldApplyBowAimMousePath(inFirstPerson, true)) {
                    const float bowX = static_cast<float>(reloadedConfig.bowAimMouseXMultiplier);
                    bowY = CalculateBowAimVerticalMultiplier(
                        true,
                        static_cast<float>(reloadedConfig.bowAimMouseYMultiplier));
                    // Reconstruct the normal-sensitivity X delta from raw pixels and the sampled
                    // scale, then apply bowX relative to that baseline. Falls back to the
                    // engine delta if the scale is not yet seeded.
                    // Keep the engine's current Y delta. Rebuilding from the cached normal-play
                    // pixel scale makes bow Y stale and ignores live game sensitivity changes.
                    // Apply the configurable bow adjustment here, then ApplyTransform
                    // applies the current global and mouse Y settings exactly once.
                    const float scaleX = sampledScale ? sampledScale->x.value : 0.0F;
                    std::tie(deltaX, deltaY) = ApplyBowAimMouseDeltas(
                        rawPixelX,
                        deltaX,
                        deltaY,
                        scaleX,
                        bowX,
                        bowY);
                }
            } else if (bowOut) {
                if (inThirdPerson) {
                    g_lastCameraState = "ThirdPerson_BowOut";
                } else if (inFirstPerson) {
                    g_lastCameraState = "FirstPerson_BowOut";
                } else {
                    g_lastCameraState = "Other_BowOut";
                }
            }

            const auto [outX, outY] = g_activeCoordinator->ApplyTransform(
                deltaX, deltaY, reloadedConfig, false);

            ++g_lookTransformAppliedCount;
            g_lastOutX = outX;
            g_lastOutY = outY;
            g_lastBowAim = isBowAim;
            g_lastBowZoomedIn = bowZoomFlag;
            g_lastRenderedZoomedIn = renderedZoomedIn;
            g_lastBowOut = bowOut;
            g_lastBaseAimFov = baseAimFov;
            g_lastNormalAimFovLogged = g_normalAimFov;
            g_lastCurrentAimFov = currentAimFov;
            g_lastCurrentHFov = rendered.hFovDegrees;
            g_lastFrustumLeft = rendered.left;
            g_lastFrustumRight = rendered.right;
            g_lastFrustumTop = rendered.top;
            g_lastFrustumBottom = rendered.bottom;
            g_lastFrustumNear = rendered.nearPlane;
            g_lastFovControlScale = rendered.fovControlScale;
            g_lastEagleEyeY = eagleEyeY;
            g_lastBowY = isBowAim ? bowY : 1.0F;
            g_lastAimState = ClassifyAimState(bowOut, isBowAim, effectiveBowZoomedIn);

            // Only real first-person mouse windows feed FirstPersonState::Update.
            // Mount/furniture/etc. and third person must not leave stale events for the
            // first-person normalizer after a camera transition.
            if (inFirstPerson) {
                std::scoped_lock lock(g_mouseTelemetryLock);
                auto& window = g_mouseTelemetryWindow;
                const auto eventId = ++g_mouseTelemetryEventId;
                if (window.eventCount == 0) {
                    window.firstEventId = eventId;
                    window.firstState = g_lastAimState;
                }
                window.lastEventId = eventId;
                window.lastState = g_lastAimState;
                ++window.eventCount;
                window.rawX += rawPixelX;
                window.rawY += rawPixelY;
                window.engineX += g_lastEngineX;
                window.engineY += g_lastEngineY;
                window.outX += outX;
                window.outY += outY;
                window.normalFov = g_normalAimFov;
                window.currentVFov = currentAimFov;
                window.currentHFov = rendered.hFovDegrees;
            } else {
                ClearMouseTelemetryWindow();
            }

            data->lookInputVec.x = outX;
            data->lookInputVec.y = outY;

            const bool stateChanged = g_lastAimState != g_previousAimState;
            const bool eagleEyeEntered = effectiveBowZoomedIn && !g_wasBowZoomedIn;
            const bool fovIsNarrowed = renderedZoomedIn;
            const bool eagleEyeFovNarrowed = fovIsNarrowed && !g_loggedEagleEyeFovNarrowed;
            if (fovIsNarrowed) {
                g_loggedEagleEyeFovNarrowed = true;
            }
            if (!effectiveBowZoomedIn) {
                g_loggedEagleEyeFovNarrowed = false;
            }
            g_wasBowZoomedIn = effectiveBowZoomedIn;
            g_previousAimState = g_lastAimState;

            const bool forceProbe = stateChanged || eagleEyeEntered || eagleEyeFovNarrowed;
            LogSensitivityProbeIfNeeded(reloadedConfig, forceProbe);
            LogLookHookCountersIfNeeded(reloadedConfig, eagleEyeEntered || stateChanged);
        }

        void ProcessThumbstickHook(RE::LookHandler* handler, RE::ThumbstickEvent* event, RE::PlayerControlsData* data)
        {
            if (!g_originalProcessThumbstick) {
                return;
            }

            if (!event || !g_activeCoordinator) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto reloadedConfig = ConfigManager::Get().GetSnapshot();
            if (!event->IsRight()) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            const auto* camera = RE::PlayerCamera::GetSingleton();
            const bool inFirstPerson = camera && camera->IsInFirstPerson();
            const bool inThirdPerson = camera && camera->IsInThirdPerson();

            if (!g_activeCoordinator->ShouldApplyInputTransform(
                    reloadedConfig,
                    inFirstPerson,
                    inThirdPerson,
                    true)) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            if (reloadedConfig.disableInMenus) {
                auto* ui = RE::UI::GetSingleton();
                if (ui && (ui->GameIsPaused() || ui->IsApplicationMenuOpen())) {
                    g_originalProcessThumbstick(handler, event, data);
                    return;
                }
            }

            if (reloadedConfig.disableWhenLookControlsDisabled) {
                const auto* controlMap = RE::ControlMap::GetSingleton();
                if (controlMap && !controlMap->IsLookingControlsEnabled()) {
                    g_originalProcessThumbstick(handler, event, data);
                    return;
                }
            }

            // Capture raw symmetric joystick values before the engine applies its
            // own per-axis sensitivity scaling, so our transform produces true 1:1
            // X/Y parity regardless of Skyrim's internal gamepad sensitivity settings.
            float rawX = event->xValue;
            float rawY = event->yValue;

            // Gamepad bow multipliers apply in both perspectives. The first-person-only
            // restriction belongs to raw-mouse reconstruction, not thumbstick tuning.
            if (DetectBowAim(RE::PlayerCharacter::GetSingleton())) {
                const float bowX = static_cast<float>(reloadedConfig.bowAimGamepadXMultiplier);
                const float bowY = static_cast<float>(reloadedConfig.bowAimGamepadYMultiplier);
                if (bowX != 1.0f) { rawX *= bowX; }
                if (bowY != 1.0f) { rawY *= bowY; }
            }

            g_originalProcessThumbstick(handler, event, data);
            if (!data) {
                return;
            }

            const auto [outX, outY] = g_activeCoordinator->ApplyTransform(rawX, rawY, reloadedConfig, true);
            ++g_thumbstickTransformAppliedCount;
            g_lastStickRawX = rawX;
            g_lastStickRawY = rawY;
            g_lastStickOutX = std::clamp(outX, -1.0F, 1.0F);
            g_lastStickOutY = std::clamp(outY, -1.0F, 1.0F);
            data->lookInputVec.x = g_lastStickOutX;
            data->lookInputVec.y = g_lastStickOutY;

            ++g_thumbstickHookCallsTotal;
            if (ShouldEmitSampledLog(
                    reloadedConfig.verboseLogging,
                    g_thumbstickHookCallsTotal,
                    kStickLogInterval)) {
                LogInfo(
                    "HookCounter[LookHandler::ProcessThumbstick]"
                    " total=" + std::to_string(g_thumbstickHookCallsTotal) +
                    " transformed=" + std::to_string(g_thumbstickTransformAppliedCount) +
                    " lastRaw=(" + std::to_string(g_lastStickRawX) + "," + std::to_string(g_lastStickRawY) + ")" +
                    " lastOut=(" + std::to_string(g_lastStickOutX) + "," + std::to_string(g_lastStickOutY) + ")");
            }
        }

        void ThirdPersonHandleLookInputHook(RE::ThirdPersonState* state, const RE::NiPoint2& input)
        {
            if (!g_originalThirdPersonHandleLookInput) {
                return;
            }

            const float beforePitch = state ? state->freeRotation.y : 0.0F;
            g_originalThirdPersonHandleLookInput(state, input);

            if (!state || !g_activeCoordinator) {
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto config = ConfigManager::Get().GetSnapshot();
            ++g_thirdPersonHookCallsTotal;

            const float afterPitch = state->freeRotation.y;
            const float engineTargetPitchDelta = afterPitch - beforePitch;
            const std::string activeState = g_lastAimState;

            if (g_activeCoordinator->ShouldRemoveThirdPersonSmoothing(config)) {
                // Collapse camera interpolation in third-person state to remove delayed follow behavior.
                state->currentYaw = state->targetYaw;
                state->currentZoomOffset = state->targetZoomOffset;
                ++g_thirdPersonSmoothingAppliedCount;
            }
            LogThirdPersonHookCountersIfNeeded(config);

            if (ShouldEmitSampledLog(
                    config.verboseLogging,
                    g_thirdPersonHookCallsTotal,
                    120,
                    true)) {
                LogInfo(
                    "ThirdPersonFinalAxisResponse"
                    " total=" + std::to_string(g_thirdPersonHookCallsTotal) +
                    " state=" + activeState +
                    " input=(" + std::to_string(input.x) + "," + std::to_string(input.y) + ")" +
                    " engineTargetPitchDelta=" + std::to_string(engineTargetPitchDelta) +
                    " freeRotationY=" + std::to_string(state->freeRotation.y) +
                    " pitchNormalized=0" +
                    " freeRotationEnabled=" + std::to_string(state->freeRotationEnabled ? 1 : 0));
            }
        }

    }
#endif

    float ComputeObservedYawScale(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta) noexcept
    {
        if (!std::isfinite(postSensitivityLookX) ||
            !std::isfinite(deltaSeconds) ||
            !std::isfinite(engineYawDelta) ||
            deltaSeconds <= 0.0F) {
            return std::numeric_limits<float>::quiet_NaN();
        }

        const float expectedYawDelta =
            postSensitivityLookX * deltaSeconds * std::numbers::pi_v<float>;
        if (std::abs(expectedYawDelta) < 0.00001F) {
            return std::numeric_limits<float>::quiet_NaN();
        }

        return engineYawDelta / expectedYawDelta;
    }

    bool IsObservedHalfRateScale(float observedScale) noexcept
    {
        if (!std::isfinite(observedScale)) {
            return false;
        }
        constexpr float halfRateLowerBound = 0.48F;
        constexpr float halfRateUpperBound = 0.52F;
        return observedScale >= halfRateLowerBound && observedScale <= halfRateUpperBound;
    }

    std::uint32_t UpdateHalfRateBandStreak(
        bool policyEligible,
        bool inBand,
        std::uint32_t previousStreak) noexcept
    {
        if (!policyEligible || !inBand) {
            return 0U;
        }
        return previousStreak + 1U;
    }

    bool ShouldApplyHalfRateRestore(
        bool policyEligible,
        bool inBand,
        std::uint32_t streakAfterUpdate,
        bool sprintOrBowHint,
        std::uint32_t requiredStreak) noexcept
    {
        if (!policyEligible || !inBand || requiredStreak == 0) {
            return false;
        }
        if (sprintOrBowHint) {
            return true;
        }
        return streakAfterUpdate >= requiredStreak;
    }

    bool ShouldUpdateFreelookYawEma(
        bool trueFreelookEligible,
        bool sprinting,
        bool casting,
        float observedScale) noexcept
    {
        if (!trueFreelookEligible || sprinting || casting) {
            return false;
        }
        if (IsObservedHalfRateScale(observedScale)) {
            return false;
        }
        return true;
    }

    float RestoreHalfRateYawDelta(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        bool eligible) noexcept
    {
        if (!eligible) {
            return engineYawDelta;
        }

        const float observedScale = ComputeObservedYawScale(
            postSensitivityLookX,
            deltaSeconds,
            engineYawDelta);
        if (!IsObservedHalfRateScale(observedScale)) {
            return engineYawDelta;
        }

        return postSensitivityLookX * deltaSeconds * std::numbers::pi_v<float>;
    }

    float CompensateTimeDilatedYawDelta(
        float yawDelta,
        float globalTimeMult) noexcept
    {
        if (!std::isfinite(yawDelta) || !IsGlobalTimeDilatedForYaw(globalTimeMult)) {
            return yawDelta;
        }

        return yawDelta / globalTimeMult;
    }

    bool IsGlobalTimeDilatedForYaw(float globalTimeMult) noexcept
    {
        if (!std::isfinite(globalTimeMult) || globalTimeMult <= 0.0F) {
            return false;
        }

        // Near-1.0 means normal play / FPS jitter; only boost when clearly slowed.
        // Eagle Eye's vanilla slow-time factor is ~0.25.
        constexpr float kDilatedUpperBound = 0.90F;
        constexpr float kDilatedLowerBound = 0.05F;
        return globalTimeMult > kDilatedLowerBound && globalTimeMult < kDilatedUpperBound;
    }

    float ComputeTimeDeltaAgreement(
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult) noexcept
    {
        if (!std::isfinite(deltaSeconds) ||
            !std::isfinite(realTimeDelta) ||
            !std::isfinite(globalTimeMult) ||
            deltaSeconds <= 0.0F ||
            realTimeDelta <= 0.0F ||
            globalTimeMult <= 0.0F) {
            return std::numeric_limits<float>::quiet_NaN();
        }

        const float expectedGameDelta = realTimeDelta * globalTimeMult;
        if (expectedGameDelta <= 0.0F) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return deltaSeconds / expectedGameDelta;
    }

    bool IsTimeDeltaAgreementAcceptable(
        float agreement,
        float tolerance) noexcept
    {
        if (!std::isfinite(agreement) || !std::isfinite(tolerance) || tolerance < 0.0F) {
            return false;
        }
        return std::abs(agreement - 1.0F) <= tolerance;
    }

    bool IsGlobalTimeMultStable(
        float currentTimeMult,
        float previousTimeMult,
        bool havePrevious,
        float maxAbsDelta) noexcept
    {
        if (!havePrevious) {
            return true;
        }
        if (!std::isfinite(currentTimeMult) ||
            !std::isfinite(previousTimeMult) ||
            !std::isfinite(maxAbsDelta) ||
            maxAbsDelta < 0.0F) {
            return false;
        }
        return std::abs(currentTimeMult - previousTimeMult) <= maxAbsDelta;
    }

    bool ShouldApplyTimeCompYaw(
        bool policyWantsTimeComp,
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult,
        float previousGlobalTimeMult,
        bool havePreviousTimeMult,
        TimeCompSkipReason* outReason,
        float agreementTolerance,
        float stabilityTolerance,
        std::uint32_t stableDilatedStreak,
        std::uint32_t requiredStableDilatedFramesWhenMissingWall) noexcept
    {
        const auto setReason = [outReason](TimeCompSkipReason reason) noexcept {
            if (outReason) {
                *outReason = reason;
            }
        };

        if (!policyWantsTimeComp) {
            setReason(TimeCompSkipReason::NotRequested);
            return false;
        }
        if (!IsGlobalTimeDilatedForYaw(globalTimeMult)) {
            setReason(TimeCompSkipReason::NotDilated);
            return false;
        }
        if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0F) {
            setReason(TimeCompSkipReason::InvalidInputs);
            return false;
        }
        if (!IsGlobalTimeMultStable(
                globalTimeMult,
                previousGlobalTimeMult,
                havePreviousTimeMult,
                stabilityTolerance)) {
            setReason(TimeCompSkipReason::Unstable);
            return false;
        }
        const bool haveWallClock =
            std::isfinite(realTimeDelta) && realTimeDelta > 0.0F;
        if (haveWallClock) {
            const float agreement = ComputeTimeDeltaAgreement(
                deltaSeconds,
                realTimeDelta,
                globalTimeMult);
            if (!IsTimeDeltaAgreementAcceptable(agreement, agreementTolerance)) {
                setReason(TimeCompSkipReason::Disagree);
                return false;
            }
        } else {
            // No agreement check possible: never blind-apply on transitions.
            // Require Current to have been stable and dilated for several frames.
            const std::uint32_t requiredFrames =
                requiredStableDilatedFramesWhenMissingWall == 0
                ? 3U
                : requiredStableDilatedFramesWhenMissingWall;
            if (stableDilatedStreak < requiredFrames) {
                setReason(TimeCompSkipReason::MissingWallClock);
                return false;
            }
        }

        setReason(TimeCompSkipReason::None);
        return true;
    }

    TimeCompMode ResolveTimeCompMode(
        bool policyWantsTimeComp,
        float deltaSeconds,
        float realTimeDelta,
        float globalTimeMult,
        float previousGlobalTimeMult,
        bool havePreviousTimeMult,
        TimeCompSkipReason* outReason,
        float agreementTolerance,
        float stabilityTolerance,
        std::uint32_t stableDilatedStreak,
        std::uint32_t requiredStableDilatedFramesWhenMissingWall) noexcept
    {
        const auto setReason = [outReason](TimeCompSkipReason reason) noexcept {
            if (outReason) {
                *outReason = reason;
            }
        };

        if (!policyWantsTimeComp) {
            setReason(TimeCompSkipReason::NotRequested);
            return TimeCompMode::None;
        }
        if (!IsGlobalTimeDilatedForYaw(globalTimeMult)) {
            setReason(TimeCompSkipReason::NotDilated);
            return TimeCompMode::None;
        }

        const bool haveWallClock =
            std::isfinite(realTimeDelta) && realTimeDelta > 0.0F;
        const bool deltaValid = std::isfinite(deltaSeconds) && deltaSeconds > 0.0F;
        const bool stable = IsGlobalTimeMultStable(
            globalTimeMult,
            previousGlobalTimeMult,
            havePreviousTimeMult,
            stabilityTolerance);

        if (haveWallClock) {
            // ScaleByCurrent ONLY when agree≈1 and Current is stable.
            // Disagree/Unstable must NEVER use yaw/Current: during EE ramp,
            // delta can already be ~0.25× wall while Current is still ~0.89,
            // so /Current leaves ~0.28× wall. Always rewrite from rtd instead.
            if (deltaValid && stable) {
                const float agreement = ComputeTimeDeltaAgreement(
                    deltaSeconds,
                    realTimeDelta,
                    globalTimeMult);
                if (IsTimeDeltaAgreementAcceptable(agreement, agreementTolerance)) {
                    setReason(TimeCompSkipReason::None);
                    return TimeCompMode::ScaleByCurrent;
                }
                setReason(TimeCompSkipReason::Disagree);
                return TimeCompMode::RewriteWallClock;
            }
            if (!stable) {
                setReason(TimeCompSkipReason::Unstable);
                return TimeCompMode::RewriteWallClock;
            }
            setReason(TimeCompSkipReason::InvalidInputs);
            return TimeCompMode::RewriteWallClock;
        }

        // No usable wall/realTimeDelta: never blind ScaleByCurrent on transitions.
        if (!deltaValid) {
            setReason(TimeCompSkipReason::InvalidInputs);
            return TimeCompMode::None;
        }
        if (!stable) {
            setReason(TimeCompSkipReason::Unstable);
            return TimeCompMode::None;
        }
        const std::uint32_t requiredFrames =
            requiredStableDilatedFramesWhenMissingWall == 0
            ? 3U
            : requiredStableDilatedFramesWhenMissingWall;
        if (stableDilatedStreak < requiredFrames) {
            setReason(TimeCompSkipReason::MissingWallClock);
            return TimeCompMode::None;
        }
        setReason(TimeCompSkipReason::None);
        return TimeCompMode::ScaleByCurrent;
    }

    const char* TimeCompSkipReasonName(TimeCompSkipReason reason) noexcept
    {
        switch (reason) {
        case TimeCompSkipReason::None:
            return "none";
        case TimeCompSkipReason::NotRequested:
            return "notRequested";
        case TimeCompSkipReason::NotDilated:
            return "notDilated";
        case TimeCompSkipReason::Disagree:
            return "disagree";
        case TimeCompSkipReason::Unstable:
            return "unstable";
        case TimeCompSkipReason::InvalidInputs:
            return "invalidInputs";
        case TimeCompSkipReason::MissingWallClock:
            return "missingWallClock";
        }
        return "unknown";
    }

    const char* TimeCompModeName(TimeCompMode mode) noexcept
    {
        switch (mode) {
        case TimeCompMode::None:
            return "none";
        case TimeCompMode::ScaleByCurrent:
            return "scale";
        case TimeCompMode::RewriteWallClock:
            return "wall";
        }
        return "unknown";
    }

    float RewriteWallClockYawDelta(
        float postSensitivityLookX,
        float wallDeltaSeconds) noexcept
    {
        if (!std::isfinite(postSensitivityLookX) ||
            !std::isfinite(wallDeltaSeconds) ||
            wallDeltaSeconds <= 0.0F) {
            return 0.0F;
        }
        return postSensitivityLookX * wallDeltaSeconds * std::numbers::pi_v<float>;
    }

    bool ShouldPausePitchNormalizeForDilatedYaw(
        bool timeDilated,
        bool looking,
        bool yawBroughtToWallClock) noexcept
    {
        return timeDilated && looking && !yawBroughtToWallClock;
    }

    LookCameraPerson ClassifyLookCameraPerson(
        bool isInFirstPerson,
        bool isInThirdPerson) noexcept
    {
        return { isInFirstPerson, isInThirdPerson };
    }

    bool IsLookingForYawCorrection(float lookInputX) noexcept
    {
        return std::abs(lookInputX) >= 0.00001F;
    }

    LookCorrectionPolicy EvaluateLookCorrectionPolicy(
        const LookCorrectionContext& ctx,
        bool enabled,
        bool enableFirstPersonHook,
        bool enableThirdPersonHook,
        bool disableInMenus,
        bool disableWhenLookControlsDisabled) noexcept
    {
        LookCorrectionPolicy policy{};
        if (!enabled) {
            return policy;
        }
        if (disableInMenus && ctx.menuMode) {
            return policy;
        }
        if (disableWhenLookControlsDisabled && !ctx.lookControlsEnabled) {
            return policy;
        }

        // Measurement-driven: exclusive FP + looking. Sprint/bow/casting are telemetry only.
        // Both-true person flags must not unlock half-rate or timeComp.
        const bool exclusiveFirstPerson = ctx.firstPerson && !ctx.thirdPerson;
        const bool exclusiveThirdPerson = ctx.thirdPerson && !ctx.firstPerson;
        policy.restoreHalfRateYaw =
            exclusiveFirstPerson &&
            enableFirstPersonHook &&
            ctx.looking;

        const bool cameraHookEnabled =
            (exclusiveFirstPerson && enableFirstPersonHook) ||
            (exclusiveThirdPerson && enableThirdPersonHook);
        policy.compensateTimeYaw =
            cameraHookEnabled &&
            ctx.timeDilated &&
            ctx.looking;

        return policy;
    }

    PlayerYawCorrectionResult ApplyPlayerYawCorrection(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        float globalTimeMult,
        bool restoreHalfRateYaw,
        bool compensateTimeYaw) noexcept
    {
        return ApplyPlayerYawCorrection(
            postSensitivityLookX,
            deltaSeconds,
            engineYawDelta,
            globalTimeMult,
            restoreHalfRateYaw,
            compensateTimeYaw ? TimeCompMode::ScaleByCurrent : TimeCompMode::None,
            0.0F);
    }

    PlayerYawCorrectionResult ApplyPlayerYawCorrection(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        float globalTimeMult,
        bool restoreHalfRateYaw,
        TimeCompMode timeCompMode,
        float wallDeltaSeconds) noexcept
    {
        const float halfRateCorrected = RestoreHalfRateYawDelta(
            postSensitivityLookX,
            deltaSeconds,
            engineYawDelta,
            restoreHalfRateYaw);
        const bool halfRateRestored = halfRateCorrected != engineYawDelta;

        if (timeCompMode == TimeCompMode::RewriteWallClock) {
            if (std::isfinite(wallDeltaSeconds) && wallDeltaSeconds > 0.0F) {
                const float wallYaw = RewriteWallClockYawDelta(
                    postSensitivityLookX,
                    wallDeltaSeconds);
                return {
                    wallYaw,
                    halfRateRestored,
                    true,
                    TimeCompMode::RewriteWallClock
                };
            }
            return {
                halfRateCorrected,
                halfRateRestored,
                false,
                TimeCompMode::None
            };
        }

        if (timeCompMode == TimeCompMode::ScaleByCurrent) {
            const float timeCorrected = CompensateTimeDilatedYawDelta(
                halfRateCorrected,
                globalTimeMult);
            return {
                timeCorrected,
                halfRateRestored,
                timeCorrected != halfRateCorrected,
                timeCorrected != halfRateCorrected
                    ? TimeCompMode::ScaleByCurrent
                    : TimeCompMode::None
            };
        }

        return {
            halfRateCorrected,
            halfRateRestored,
            false,
            TimeCompMode::None
        };
    }

    std::pair<float, float> ApplyBowAimMouseDeltas(
        float rawPixelX,
        float engineDeltaX,
        float engineDeltaY,
        float sampledScaleX,
        float bowXMultiplier,
        float bowYMultiplier) noexcept
    {
        const float outputX = sampledScaleX != 0.0F && std::abs(rawPixelX) >= 1.0F
            ? rawPixelX * sampledScaleX * bowXMultiplier
            : engineDeltaX * bowXMultiplier;
        return { outputX, engineDeltaY * bowYMultiplier };
    }

    float CalculateBowAimVerticalMultiplier(
        bool bowAiming,
        float configuredBowYMultiplier) noexcept
    {
        return bowAiming ? configuredBowYMultiplier : 1.0F;
    }

    float NormalizePitchTargetDelta(
        float postTransformLookY,
        float engineTargetPitchDelta,
        float freelookPitchPerLook,
        bool eligible) noexcept
    {
        if (!eligible ||
            !std::isfinite(postTransformLookY) ||
            !std::isfinite(freelookPitchPerLook) ||
            std::abs(freelookPitchPerLook) <= 0.0000001F) {
            return engineTargetPitchDelta;
        }
        return postTransformLookY * freelookPitchPerLook;
    }

    bool ShouldUpdateFreelookSampledScale(
        bool rangedWeaponEquipped,
        bool weaponFullySheathed,
        bool bowAiming,
        bool bowZoomFlag) noexcept
    {
        return !bowAiming && !bowZoomFlag &&
               (!rangedWeaponEquipped || weaponFullySheathed);
    }

    bool UpdateFreelookScaleSample(
        float rawPixelDelta,
        float engineDelta,
        float& currentScale,
        float& pendingScale,
        std::uint32_t& pendingCount) noexcept
    {
        if (!std::isfinite(rawPixelDelta) || !std::isfinite(engineDelta) ||
            std::abs(rawPixelDelta) < 1.0F || std::abs(engineDelta) < 0.00001F) {
            return false;
        }

        const float candidate = engineDelta / rawPixelDelta;
        constexpr float kMinimumMagnitude = 0.00001F;
        constexpr float kMaximumMagnitude = 16.0F;
        constexpr float kSampleEmaAlpha = 0.15F;
        if (!std::isfinite(candidate) ||
            std::abs(candidate) < kMinimumMagnitude ||
            std::abs(candidate) > kMaximumMagnitude) {
            return false;
        }

        const auto samplesAreConsistent = [](float lhs, float rhs) noexcept {
            if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
                std::abs(lhs) < kMinimumMagnitude ||
                std::abs(rhs) < kMinimumMagnitude ||
                lhs * rhs <= 0.0F) {
                return false;
            }
            const float relativeMagnitude = std::abs(lhs / rhs);
            return relativeMagnitude >= 0.5F && relativeMagnitude <= 2.0F;
        };

        if (!std::isfinite(currentScale)) {
            currentScale = 0.0F;
        }
        if (currentScale != 0.0F && samplesAreConsistent(candidate, currentScale)) {
            currentScale += kSampleEmaAlpha * (candidate - currentScale);
            pendingScale = 0.0F;
            pendingCount = 0;
            return true;
        }

        if (pendingCount == 0 || !samplesAreConsistent(candidate, pendingScale)) {
            pendingScale = candidate;
            pendingCount = 1;
            return false;
        }

        pendingScale += kSampleEmaAlpha * (candidate - pendingScale);
        ++pendingCount;
        constexpr std::uint32_t kSamplesRequiredToSeed = 3;
        if (pendingCount < kSamplesRequiredToSeed) {
            return false;
        }

        currentScale = pendingScale;
        pendingScale = 0.0F;
        pendingCount = 0;
        return true;
    }

    bool ShouldUpdateNormalAimFov(
        bool rangedWeaponActive,
        bool bowAiming,
        bool bowZoomFlag,
        float renderedFovDegrees) noexcept
    {
        return renderedFovDegrees > 0.0F &&
               !rangedWeaponActive && !bowAiming && !bowZoomFlag;
    }

    float FovDegreesFromFrustumEdges(float positiveEdge, float negativeEdge) noexcept
    {
        if (!std::isfinite(positiveEdge) || !std::isfinite(negativeEdge) ||
            !(positiveEdge > negativeEdge)) {
            return 0.0F;
        }
        constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
        return (std::atan(positiveEdge) - std::atan(negativeEdge)) * kRadiansToDegrees;
    }

    bool ShouldRestoreHalfRateFirstPersonYaw(
        bool enabled,
        bool firstPersonHookEnabled,
        bool inFirstPerson,
        bool inThirdPerson,
        bool looking) noexcept
    {
        const auto person = ClassifyLookCameraPerson(inFirstPerson, inThirdPerson);
        LookCorrectionContext ctx{};
        ctx.firstPerson = person.firstPerson;
        ctx.thirdPerson = person.thirdPerson;
        ctx.looking = looking;
        return EvaluateLookCorrectionPolicy(
            ctx,
            enabled,
            firstPersonHookEnabled,
            true).restoreHalfRateYaw;
    }

    bool ShouldCorrectThirdPersonSlowTimeYaw(
        bool enabled,
        bool thirdPersonHookEnabled,
        bool inFirstPerson,
        bool inThirdPerson,
        bool looking,
        float globalTimeMult) noexcept
    {
        const auto person = ClassifyLookCameraPerson(inFirstPerson, inThirdPerson);
        LookCorrectionContext ctx{};
        ctx.firstPerson = person.firstPerson;
        ctx.thirdPerson = person.thirdPerson;
        ctx.looking = looking;
        ctx.timeMult = globalTimeMult;
        ctx.timeDilated = IsGlobalTimeDilatedForYaw(globalTimeMult);
        return EvaluateLookCorrectionPolicy(
            ctx,
            enabled,
            false,
            thirdPersonHookEnabled).compensateTimeYaw;
    }

    FreelookScaleCacheKind SelectFreelookScaleCache(
        bool inFirstPerson,
        bool inThirdPerson) noexcept
    {
        if (inFirstPerson && !inThirdPerson) {
            return FreelookScaleCacheKind::FirstPerson;
        }
        if (inThirdPerson && !inFirstPerson) {
            return FreelookScaleCacheKind::ThirdPerson;
        }
        return FreelookScaleCacheKind::None;
    }

    bool IsTrueFreelookPitchEnvironment(
        std::string_view aimState,
        bool /*trueFreelookEligible*/) noexcept
    {
        // Freelook aim state must never enable normalize — even when !eligible
        // (ranged WantToDraw/Drawing). Eligible gates calibration only.
        return aimState == "freelook";
    }

    bool IsTrueFreelookPitchBaselineEligible(
        std::string_view aimState,
        bool trueFreelookEligible,
        bool sprinting) noexcept
    {
        return IsTrueFreelookPitchEnvironment(aimState, trueFreelookEligible) &&
               trueFreelookEligible &&
               !sprinting;
    }

    bool IsPitchNormalizeSettled(
        std::uint32_t framesSinceAimStateChange,
        std::uint32_t settleFrames) noexcept
    {
        return framesSinceAimStateChange >= settleFrames;
    }

    bool ShouldApplyBowAimMousePath(bool inFirstPerson, bool bowAiming) noexcept
    {
        return bowAiming && inFirstPerson;
    }

    bool ShouldEmitSampledLog(
        bool enabled,
        std::uint64_t count,
        std::uint64_t interval,
        bool includeFirst) noexcept
    {
        if (!enabled || count == 0 || interval == 0) {
            return false;
        }
        return (includeFirst && count == 1) || (count % interval) == 0;
    }

    bool HookCoordinator::InstallLookHandlerMouseMoveHook()
    {
#if MSF_USE_COMMONLIBSSE
        REL::Relocation<std::uintptr_t> lookHandlerVTable{ RE::VTABLE_LookHandler[0] };
        if (!g_originalProcessThumbstick) {
            g_originalProcessThumbstick = reinterpret_cast<ProcessThumbstickFn>(
                lookHandlerVTable.write_vfunc(2, ProcessThumbstickHook));
        }
        if (!g_originalProcessMouseMove) {
            g_originalProcessMouseMove = reinterpret_cast<ProcessMouseMoveFn>(
                lookHandlerVTable.write_vfunc(3, ProcessMouseMoveHook));
        }

        if (!g_originalProcessThumbstick || !g_originalProcessMouseMove) {
            LogError("Failed to install LookHandler input vtable hooks.");
            RemoveLookHandlerMouseMoveHook();
            return false;
        }

        LogInfo("Installed relocation-backed LookHandler input hooks.");
        g_lookHookCallsTotal = 0;
        g_lookHookCallsFirstPerson = 0;
        g_lookHookCallsThirdPerson = 0;
        g_lookTransformAppliedCount = 0;
        g_lastRawPixelX = 0.0F;
        g_lastRawPixelY = 0.0F;
        g_lastEngineX = 0.0F;
        g_lastEngineY = 0.0F;
        g_lastOutX = 0.0F;
        g_lastOutY = 0.0F;
        g_lastCameraState = "Unknown";
        g_lastAimState = "freelook";
        g_previousAimState = "freelook";
        g_lastTrueFreelookEligible = false;
        g_pitchNormalizeAimState = "freelook";
        g_pitchNormalizeSettleFrames = 0;
        g_lastBowAim = false;
        g_lastBowZoomedIn = false;
        g_lastRenderedZoomedIn = false;
        g_lastBowOut = false;
        g_lastBaseAimFov = 0.0F;
        g_lastNormalAimFovLogged = 0.0F;
        g_lastCurrentAimFov = 0.0F;
        g_lastCurrentHFov = 0.0F;
        g_lastFrustumLeft = 0.0F;
        g_lastFrustumRight = 0.0F;
        g_lastFrustumTop = 0.0F;
        g_lastFrustumBottom = 0.0F;
        g_lastFrustumNear = 0.0F;
        g_lastFovControlScale = 0.0F;
        g_lastEagleEyeY = 1.0F;
        g_lastBowY = 1.0F;
        g_bowAimMouseFrames = 0;
        g_eagleEyeMouseFrames = 0;
        g_sensitivityProbeCount = 0;
        g_rotationProbeCount = 0;
        g_wasBowZoomedIn = false;
        g_loggedEagleEyeFovNarrowed = false;
        g_freelookYawPerLook = 0.0F;
        g_halfRateBandStreak = 0;
        g_previousTimeMult = 1.0F;
        g_hasPreviousTimeMult = false;
        g_stableDilatedTimeMultStreak = 0;
        g_timeCompSkipCount = 0;
        g_timeCompWallRewriteCount = 0;
        g_dilatedLookingYawActive.store(false, std::memory_order_relaxed);
        g_dilatedYawBroughtToWallClock.store(true, std::memory_order_relaxed);
        g_hasYawWallClock = false;
        g_lastYawWallClockSeconds = 0.0F;
        g_firstPersonSampledScale = {};
        g_thirdPersonSampledScale = {};
        g_lastSampledScaleX = 0.0F;
        g_lastSampledScaleY = 0.0F;
        g_normalAimFov = 0.0F;
        g_lastAimFovCameraRoot = nullptr;
        g_lastAimFovThirdPerson = false;
        g_hasAimFovCameraIdentity = false;
        g_lastMouseEventTime = {};
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install relocation-backed mouse hook.");
        return false;
#endif
    }

    void HookCoordinator::RemoveLookHandlerMouseMoveHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (!g_originalProcessThumbstick && !g_originalProcessMouseMove) {
            return;
        }

        REL::Relocation<std::uintptr_t> lookHandlerVTable{ RE::VTABLE_LookHandler[0] };
        if (g_originalProcessThumbstick) {
            lookHandlerVTable.write_vfunc(2, reinterpret_cast<std::uintptr_t>(g_originalProcessThumbstick));
        }
        if (g_originalProcessMouseMove) {
            lookHandlerVTable.write_vfunc(3, reinterpret_cast<std::uintptr_t>(g_originalProcessMouseMove));
        }
        g_originalProcessThumbstick = nullptr;
        g_originalProcessMouseMove = nullptr;
        LogInfo("Removed LookHandler input hooks.");
#endif
    }

    bool HookCoordinator::InstallPlayerYawHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (g_originalPlayerModifyMovementData) {
            return true;
        }

        REL::Relocation<std::uintptr_t> playerCharacterVTable{ RE::VTABLE_PlayerCharacter[0] };
        g_originalPlayerModifyMovementData = reinterpret_cast<PlayerModifyMovementDataFn>(
            playerCharacterVTable.write_vfunc(0x11A, PlayerModifyMovementDataHook));
        if (!g_originalPlayerModifyMovementData) {
            LogError("Failed to install PlayerCharacter::ModifyMovementData half-rate yaw hook.");
            return false;
        }

        g_playerYawCorrectionCount = 0;
        g_thirdPersonYawCorrectionCount = 0;
        g_halfRateBandStreak = 0;
        g_previousTimeMult = 1.0F;
        g_hasPreviousTimeMult = false;
        g_stableDilatedTimeMultStreak = 0;
        g_timeCompSkipCount = 0;
        g_timeCompWallRewriteCount = 0;
        g_dilatedLookingYawActive.store(false, std::memory_order_relaxed);
        g_dilatedYawBroughtToWallClock.store(true, std::memory_order_relaxed);
        g_hasYawWallClock = false;
        g_lastYawWallClockSeconds = 0.0F;
        LogInfo("Installed PlayerCharacter::ModifyMovementData first-person half-rate yaw correction hook.");
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install first-person half-rate yaw correction hook.");
        return false;
#endif
    }

    void HookCoordinator::RemovePlayerYawHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (!g_originalPlayerModifyMovementData) {
            return;
        }

        REL::Relocation<std::uintptr_t> playerCharacterVTable{ RE::VTABLE_PlayerCharacter[0] };
        playerCharacterVTable.write_vfunc(
            0x11A,
            reinterpret_cast<std::uintptr_t>(g_originalPlayerModifyMovementData));
        g_originalPlayerModifyMovementData = nullptr;
        LogInfo("Removed PlayerCharacter::ModifyMovementData first-person half-rate yaw correction hook.");
#endif
    }

    bool HookCoordinator::InstallFirstPersonTelemetryHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (g_originalFirstPersonStateUpdate) {
            return true;
        }

        REL::Relocation<std::uintptr_t> firstPersonStateVTable{ RE::VTABLE_FirstPersonState[0] };
        g_originalFirstPersonStateUpdate = reinterpret_cast<FirstPersonStateUpdateFn>(
            firstPersonStateVTable.write_vfunc(3, FirstPersonStateUpdateHook));
        if (!g_originalFirstPersonStateUpdate) {
            LogError("Failed to install FirstPersonState::Update final-axis telemetry hook.");
            return false;
        }

        g_firstPersonTelemetryFrame = 0;
        g_mouseTelemetryEventId = 0;
        g_freelookPitchPerLook = 0.0F;
        g_pendingFreelookPitchPerLook = 0.0F;
        g_pendingFreelookPitchSamples = 0;
        g_pitchNormalizationCount = 0;
        g_normalizedPitchTarget = 0.0F;
        g_hasNormalizedPitchTarget = false;
        g_pitchNormalizeAimState = "freelook";
        g_pitchNormalizeSettleFrames = 0;
        {
            std::scoped_lock lock(g_mouseTelemetryLock);
            g_mouseTelemetryWindow = {};
        }
        LogInfo("Installed FirstPersonState::Update final-axis telemetry hook.");
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install final-axis telemetry hook.");
        return false;
#endif
    }

    void HookCoordinator::RemoveFirstPersonTelemetryHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (!g_originalFirstPersonStateUpdate) {
            return;
        }

        REL::Relocation<std::uintptr_t> firstPersonStateVTable{ RE::VTABLE_FirstPersonState[0] };
        firstPersonStateVTable.write_vfunc(
            3,
            reinterpret_cast<std::uintptr_t>(g_originalFirstPersonStateUpdate));
        g_originalFirstPersonStateUpdate = nullptr;
        LogInfo("Removed FirstPersonState::Update final-axis telemetry hook.");
#endif
    }


    bool HookCoordinator::InstallThirdPersonSmoothingHook()
    {
#if MSF_USE_COMMONLIBSSE
        REL::Relocation<std::uintptr_t> thirdPersonStateVTable{ RE::VTABLE_ThirdPersonState[0] };
        if (!g_originalThirdPersonHandleLookInput) {
            g_originalThirdPersonHandleLookInput = reinterpret_cast<ThirdPersonHandleLookInputFn>(
                thirdPersonStateVTable.write_vfunc(0x0F, ThirdPersonHandleLookInputHook));
        }

        if (!g_originalThirdPersonHandleLookInput) {
            LogError("Failed to install ThirdPersonState::HandleLookInput vtable hook.");
            return false;
        }

        LogInfo("Installed relocation-backed ThirdPersonState::HandleLookInput hook.");
        g_thirdPersonHookCallsTotal = 0;
        g_thirdPersonSmoothingAppliedCount = 0;
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install third-person smoothing hook.");
        return false;
#endif
    }

    void HookCoordinator::RemoveThirdPersonSmoothingHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (!g_originalThirdPersonHandleLookInput) {
            return;
        }

        REL::Relocation<std::uintptr_t> thirdPersonStateVTable{ RE::VTABLE_ThirdPersonState[0] };
        thirdPersonStateVTable.write_vfunc(0x0F, reinterpret_cast<std::uintptr_t>(g_originalThirdPersonHandleLookInput));
        g_originalThirdPersonHandleLookInput = nullptr;
        LogInfo("Removed ThirdPersonState::HandleLookInput vtable hook.");
#endif
    }

    bool HookCoordinator::RegisterHookPoint(HookRegistrationPoint point)
    {
        switch (point) {
        case HookRegistrationPoint::InputLook:
            _firstPersonRegistered = InstallLookHandlerMouseMoveHook();
            return _firstPersonRegistered;
        case HookRegistrationPoint::SmoothingRemoval:
            _smoothingRemovalRegistered = InstallThirdPersonSmoothingHook();
            return _smoothingRemovalRegistered;
        default:
            return false;
        }
    }

    bool HookCoordinator::Install()
    {
        if (_installed) {
            return true;
        }

        _firstPersonRegistered = false;
        _playerYawRegistered = false;
        _firstPersonTelemetryRegistered = false;
        _smoothingRemovalRegistered = false;
#if MSF_USE_COMMONLIBSSE
        g_activeCoordinator = this;
#endif

        if (!RegisterHookPoint(HookRegistrationPoint::InputLook)) {
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        _playerYawRegistered = InstallPlayerYawHook();
        if (!_playerYawRegistered) {
            RemoveLookHandlerMouseMoveHook();
            _firstPersonRegistered = false;
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        _firstPersonTelemetryRegistered = InstallFirstPersonTelemetryHook();
        if (!_firstPersonTelemetryRegistered) {
            RemovePlayerYawHook();
            _playerYawRegistered = false;
            RemoveLookHandlerMouseMoveHook();
            _firstPersonRegistered = false;
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        if (!RegisterHookPoint(HookRegistrationPoint::SmoothingRemoval)) {
            RemoveFirstPersonTelemetryHook();
            _firstPersonTelemetryRegistered = false;
            RemovePlayerYawHook();
            _playerYawRegistered = false;
            RemoveLookHandlerMouseMoveHook();
            _firstPersonRegistered = false;
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        _installed = true;
        LogInfo("All input hooks installed; runtime configuration controls pass-through behavior.");
        return true;
    }

    void HookCoordinator::Remove()
    {
        RemoveFirstPersonTelemetryHook();
        RemovePlayerYawHook();
        RemoveLookHandlerMouseMoveHook();
        RemoveThirdPersonSmoothingHook();
        _firstPersonRegistered = false;
        _playerYawRegistered = false;
        _firstPersonTelemetryRegistered = false;
        _smoothingRemovalRegistered = false;
        _installed = false;
#if MSF_USE_COMMONLIBSSE
        g_activeCoordinator = nullptr;
#endif
        LogInfo("Mouse hook registrations removed.");
    }

    bool HookCoordinator::UpdatePolicy(const CompatibilityPolicy& policy)
    {
        bool changed = false;
        {
            std::scoped_lock guard(_policyLock);
            changed = _activePolicy.mode != policy.mode ||
                      _activePolicy.installInputHooks != policy.installInputHooks ||
                      _activePolicy.allowThirdPersonSmoothingIntervention != policy.allowThirdPersonSmoothingIntervention ||
                      _activePolicy.reason != policy.reason;
            _activePolicy = policy;
        }
        std::uint8_t flags = 0;
        if (policy.installInputHooks) {
            flags |= kInputTransformsAllowed;
        }
        if (policy.allowThirdPersonSmoothingIntervention) {
            flags |= kThirdPersonSmoothingAllowed;
        }
        _policyFlags.store(flags, std::memory_order_release);
        return changed;
    }

    CompatibilityPolicy HookCoordinator::GetPolicySnapshot() const
    {
        std::scoped_lock guard(_policyLock);
        return _activePolicy;
    }

    bool HookCoordinator::ShouldApplyInputTransform(
        const ConfigValues& config,
        bool inFirstPerson,
        bool inThirdPerson,
        bool isGamepad) const
    {
        const auto policyFlags = _policyFlags.load(std::memory_order_acquire);
        if (!config.enabled ||
            (policyFlags & kInputTransformsAllowed) == 0) {
            return false;
        }
        if (isGamepad && !config.affectGamepadLook) {
            return false;
        }
        if (inFirstPerson && !inThirdPerson) {
            return config.enableFirstPersonHook;
        }
        if (inThirdPerson && !inFirstPerson) {
            return config.enableThirdPersonHook;
        }
        // Neither (mount/furniture/bleedout/dragon/null) and both → no transform.
        return false;
    }

    bool HookCoordinator::ShouldRemoveThirdPersonSmoothing(const ConfigValues& config) const
    {
        const auto policyFlags = _policyFlags.load(std::memory_order_acquire);
        return config.enabled &&
               config.enableSmoothingRemovalHook &&
               (policyFlags & kThirdPersonSmoothingAllowed) != 0;
    }

    std::pair<float, float> HookCoordinator::ApplyTransform(float deltaX, float deltaY, const ConfigValues& config, bool isGamepad) const
    {
        if (!config.enabled) {
            return { deltaX, deltaY };
        }

        const auto scale = static_cast<float>(config.globalSensitivity);
        const auto xMultiplier = static_cast<float>(isGamepad ? config.gamepadXAxisMultiplier : config.mouseXAxisMultiplier);
        const auto yMultiplier = static_cast<float>(isGamepad ? config.gamepadYAxisMultiplier : config.mouseYAxisMultiplier);

        const float outX = deltaX * scale * xMultiplier;
        const float outY = deltaY * scale * yMultiplier;

        return { outX, outY };
    }
}
