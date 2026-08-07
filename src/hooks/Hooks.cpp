#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#if MSF_USE_COMMONLIBSSE
#include <RE/A/Actor.h>
#include <RE/C/ControlMap.h>
#include <RE/L/LookHandler.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/N/NiPoint2.h>
#include <RE/Offsets_VTABLE.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerControls.h>
#include <RE/P/PlayerControlsData.h>
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
        constexpr std::uint8_t kSmoothingRemovalAllowed = 1U << 1U;
        constexpr std::uint8_t kThirdPersonSmoothingAllowed = 1U << 2U;
    }

#if MSF_USE_COMMONLIBSSE
    namespace
    {
        using ProcessThumbstickFn = void (*)(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
        using ProcessMouseMoveFn = void (*)(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
        using PlayerModifyMovementDataFn = void (*)(
            RE::PlayerCharacter*, float, RE::NiPoint3&, RE::NiPoint3&);
        using ThirdPersonHandleLookInputFn = void (*)(RE::ThirdPersonState*, const RE::NiPoint2&);

        // ActorState is a base class of Actor at compile-time offset 0xB8 (SE layout).
        // In AE 1.6.629+, TESObjectREFR grew by 8 bytes, shifting ActorState to 0xC0.
        // The C++ compiler bakes in the SE offset, so direct access reads wrong memory on AE.
        // This helper uses RelocateMemberIfNewer to read from the correct offset.
        // actorState1 (contains meleeAttackState): SE 0xC0, AE 0xC8
        RE::ATTACK_STATE_ENUM GetAttackStateRelocated(const RE::PlayerCharacter* player) noexcept
        {
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

        ProcessThumbstickFn g_originalProcessThumbstick{ nullptr };
        ProcessMouseMoveFn g_originalProcessMouseMove{ nullptr };
        PlayerModifyMovementDataFn g_originalPlayerModifyMovementData{ nullptr };
        ThirdPersonHandleLookInputFn g_originalThirdPersonHandleLookInput{ nullptr };
        HookCoordinator* g_activeCoordinator{ nullptr };
        std::uint64_t g_lookHookCallsTotal{ 0 };
        std::uint64_t g_lookHookCallsFirstPerson{ 0 };
        std::uint64_t g_lookHookCallsThirdPerson{ 0 };
        std::uint64_t g_lookTransformAppliedCount{ 0 };
        float g_lastRawX{ 0.0F };
        float g_lastRawY{ 0.0F };
        float g_lastOutX{ 0.0F };
        float g_lastOutY{ 0.0F };
        std::string g_lastCameraState{ "Unknown" };

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

        constexpr std::uint64_t kLookLogInterval = 600;
        constexpr std::uint64_t kThirdPersonLogInterval = 600;
        constexpr std::uint64_t kStickLogInterval = 600;
        constexpr std::uint64_t kSprintYawLogInterval = 120;

        // Sampled scale: ratio of lookInputVec units per raw mouse pixel at baseline sensitivity.
        // Updated via EMA during normal play only (not during bow aim) to stay uncontaminated.
        float g_sampledScaleX{ 0.0f };
        float g_sampledScaleY{ 0.0f };
        constexpr float kScaleEmaAlpha = 0.15f;

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

        void LogLookHookCountersIfNeeded(const ConfigValues& config)
        {
            if (!ShouldEmitSampledLog(config.verboseLogging, g_lookHookCallsTotal, kLookLogInterval)) {
                return;
            }

            LogInfo(
                "HookCounter[LookHandler::ProcessMouseMove]"
                " total=" + std::to_string(g_lookHookCallsTotal) +
                " firstPerson=" + std::to_string(g_lookHookCallsFirstPerson) +
                " thirdPerson=" + std::to_string(g_lookHookCallsThirdPerson) +
                " transformed=" + std::to_string(g_lookTransformAppliedCount) +
                " lastRaw=(" + std::to_string(g_lastRawX) + "," + std::to_string(g_lastRawY) + ")" +
                " lastOut=(" + std::to_string(g_lastOutX) + "," + std::to_string(g_lastOutY) + ")" +
                " sampledScale=(" + std::to_string(g_sampledScaleX) + "," + std::to_string(g_sampledScaleY) + ")" +
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
            const bool sprinting = IsSprintingRelocated(player);
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            const RE::NiPoint2 lookInput = controls ? controls->data.lookInputVec : RE::NiPoint2{};
            const float engineYawDelta = rotationData.z;
            rotationData.z = RestoreHalfRateSprintYawDelta(
                lookInput.x,
                delta,
                rotationData.z,
                config.enabled &&
                    !config.hotDisable &&
                    config.enableFirstPersonHook &&
                    !inThirdPerson &&
                    sprinting);
            const bool yawCorrected = rotationData.z != engineYawDelta;
            if (yawCorrected) {
                ++g_playerYawCorrectionCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_playerYawCorrectionCount,
                        kSprintYawLogInterval,
                        true)) {
                    LogInfo(
                        "HookCounter[FirstPersonSprintYaw]"
                        " corrected=" + std::to_string(g_playerYawCorrectionCount) +
                        " engineYaw=" + std::to_string(engineYawDelta) +
                        " restoredYaw=" + std::to_string(rotationData.z) +
                        " lookX=" + std::to_string(lookInput.x) +
                        " delta=" + std::to_string(delta));
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
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            auto* player = RE::PlayerCharacter::GetSingleton();

            if (!g_activeCoordinator->ShouldApplyInputTransform(reloadedConfig, inThirdPerson, false)) {
                g_originalProcessMouseMove(handler, event, data);
                return;
            }

            ++g_lookHookCallsTotal;

            if (inThirdPerson) {
                ++g_lookHookCallsThirdPerson;
                g_lastCameraState = "ThirdPerson";
            } else {
                ++g_lookHookCallsFirstPerson;
                g_lastCameraState = "FirstPerson";
            }

            if (reloadedConfig.disableInMenus) {
                auto* ui = RE::UI::GetSingleton();
                if (ui && (ui->GameIsPaused() || ui->IsApplicationMenuOpen())) {
                    g_lastCameraState = "Menu";
                    LogLookHookCountersIfNeeded(reloadedConfig);
                    g_originalProcessMouseMove(handler, event, data);
                    return;
                }
            }

            if (reloadedConfig.disableWhenLookControlsDisabled) {
                const auto* controlMap = RE::ControlMap::GetSingleton();
                if (controlMap && !controlMap->IsLookingControlsEnabled()) {
                    g_lastCameraState = "LookControlsDisabled";
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

            g_lastRawX = data->lookInputVec.x;
            g_lastRawY = data->lookInputVec.y;

            const bool isBowAim = DetectBowAim(player);

            // Update sampled scale only during normal (non-bow-aim) play.
            // Tracks the engine's pixels-to-lookInputVec ratio at baseline sensitivity.
            // Excluded during bow aim to prevent contamination by the engine's bow-zoom
            // attenuation, which produces a smaller ratio for the same raw pixel delta.
            if (!isBowAim) {
                if (std::abs(rawPixelX) >= 1.0f) {
                    const float kX = data->lookInputVec.x / rawPixelX;
                    g_sampledScaleX = (g_sampledScaleX == 0.0f)
                        ? kX : g_sampledScaleX + kScaleEmaAlpha * (kX - g_sampledScaleX);
                }
                if (std::abs(rawPixelY) >= 1.0f) {
                    const float kY = data->lookInputVec.y / rawPixelY;
                    g_sampledScaleY = (g_sampledScaleY == 0.0f)
                        ? kY : g_sampledScaleY + kScaleEmaAlpha * (kY - g_sampledScaleY);
                }
            }

            float deltaX = data->lookInputVec.x;
            float deltaY = data->lookInputVec.y;

            if (isBowAim) {
                g_lastCameraState = inThirdPerson ? "ThirdPerson_BowAim" : "FirstPerson_BowAim";
                const float bowX = static_cast<float>(reloadedConfig.bowAimMouseXMultiplier);
                const float bowY = static_cast<float>(reloadedConfig.bowAimMouseYMultiplier);
                // Reconstruct the normal-sensitivity delta from raw pixels and the sampled
                // scale, then apply bowX/Y relative to that baseline.
                // bowX/Y = 1.0 produces exactly the same feel as normal first-person look.
                // Falls back to the engine-attenuated delta if the scale is not yet seeded.
                if (g_sampledScaleX != 0.0f && std::abs(rawPixelX) >= 1.0f) {
                    deltaX = rawPixelX * g_sampledScaleX * bowX;
                } else {
                    deltaX *= bowX;
                }
                if (g_sampledScaleY != 0.0f && std::abs(rawPixelY) >= 1.0f) {
                    deltaY = rawPixelY * g_sampledScaleY * bowY;
                } else {
                    deltaY *= bowY;
                }
            }

            const auto [outX, outY] = g_activeCoordinator->ApplyTransform(
                deltaX, deltaY, reloadedConfig, false);

            ++g_lookTransformAppliedCount;
            g_lastOutX = outX;
            g_lastOutY = outY;
            data->lookInputVec.x = outX;
            data->lookInputVec.y = outY;
            LogLookHookCountersIfNeeded(reloadedConfig);
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
            const bool inThirdPerson = camera && camera->IsInThirdPerson();

            if (!g_activeCoordinator->ShouldApplyInputTransform(reloadedConfig, inThirdPerson, true)) {
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

            g_originalThirdPersonHandleLookInput(state, input);

            if (!state || !g_activeCoordinator) {
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto config = ConfigManager::Get().GetSnapshot();
            ++g_thirdPersonHookCallsTotal;

            if (!g_activeCoordinator->ShouldRemoveThirdPersonSmoothing(config)) {
                LogThirdPersonHookCountersIfNeeded(config);
                return;
            }

            // Collapse camera interpolation in third-person state to remove delayed follow behavior.
            state->currentYaw = state->targetYaw;
            state->currentZoomOffset = state->targetZoomOffset;
            ++g_thirdPersonSmoothingAppliedCount;
            LogThirdPersonHookCountersIfNeeded(config);
        }

    }
#endif

    float RestoreHalfRateSprintYawDelta(
        float postSensitivityLookX,
        float deltaSeconds,
        float engineYawDelta,
        bool eligible) noexcept
    {
        if (!eligible || deltaSeconds <= 0.0F) {
            return engineYawDelta;
        }

        const float expectedYawDelta =
            postSensitivityLookX * deltaSeconds * std::numbers::pi_v<float>;
        if (std::abs(expectedYawDelta) < 0.00001F) {
            return engineYawDelta;
        }

        const float observedScale = engineYawDelta / expectedYawDelta;
        constexpr float halfRateLowerBound = 0.48F;
        constexpr float halfRateUpperBound = 0.52F;
        if (observedScale >= halfRateLowerBound && observedScale <= halfRateUpperBound) {
            return expectedYawDelta;
        }

        return engineYawDelta;
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
        g_lastRawX = 0.0F;
        g_lastRawY = 0.0F;
        g_lastOutX = 0.0F;
        g_lastOutY = 0.0F;
        g_lastCameraState = "Unknown";
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

    bool HookCoordinator::InstallPlayerSprintYawHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (g_originalPlayerModifyMovementData) {
            return true;
        }

        REL::Relocation<std::uintptr_t> playerCharacterVTable{ RE::VTABLE_PlayerCharacter[0] };
        g_originalPlayerModifyMovementData = reinterpret_cast<PlayerModifyMovementDataFn>(
            playerCharacterVTable.write_vfunc(0x11A, PlayerModifyMovementDataHook));
        if (!g_originalPlayerModifyMovementData) {
            LogError("Failed to install PlayerCharacter::ModifyMovementData sprint-yaw hook.");
            return false;
        }

        g_playerYawCorrectionCount = 0;
        LogInfo("Installed PlayerCharacter::ModifyMovementData first-person sprint-yaw correction hook.");
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install first-person sprint-yaw correction hook.");
        return false;
#endif
    }

    void HookCoordinator::RemovePlayerSprintYawHook()
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
        LogInfo("Removed PlayerCharacter::ModifyMovementData first-person sprint-yaw correction hook.");
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
        _playerSprintYawRegistered = false;
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

        _playerSprintYawRegistered = InstallPlayerSprintYawHook();
        if (!_playerSprintYawRegistered) {
            RemoveLookHandlerMouseMoveHook();
            _firstPersonRegistered = false;
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        if (!RegisterHookPoint(HookRegistrationPoint::SmoothingRemoval)) {
            RemovePlayerSprintYawHook();
            _playerSprintYawRegistered = false;
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
        RemovePlayerSprintYawHook();
        RemoveLookHandlerMouseMoveHook();
        RemoveThirdPersonSmoothingHook();
        _firstPersonRegistered = false;
        _playerSprintYawRegistered = false;
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
                      _activePolicy.installSmoothingRemovalHooks != policy.installSmoothingRemovalHooks ||
                      _activePolicy.allowThirdPersonSmoothingIntervention != policy.allowThirdPersonSmoothingIntervention ||
                      _activePolicy.reason != policy.reason;
            _activePolicy = policy;
        }
        std::uint8_t flags = 0;
        if (policy.installInputHooks) {
            flags |= kInputTransformsAllowed;
        }
        if (policy.installSmoothingRemovalHooks) {
            flags |= kSmoothingRemovalAllowed;
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
        bool inThirdPerson,
        bool isGamepad) const
    {
        const auto policyFlags = _policyFlags.load(std::memory_order_acquire);
        if (!config.enabled || config.hotDisable ||
            (policyFlags & kInputTransformsAllowed) == 0) {
            return false;
        }
        if (isGamepad && !config.affectGamepadLook) {
            return false;
        }
        return inThirdPerson ? config.enableThirdPersonHook : config.enableFirstPersonHook;
    }

    bool HookCoordinator::ShouldRemoveThirdPersonSmoothing(const ConfigValues& config) const
    {
        const auto policyFlags = _policyFlags.load(std::memory_order_acquire);
        return config.enabled &&
               !config.hotDisable &&
               config.enableSmoothingRemovalHook &&
               (policyFlags & kSmoothingRemovalAllowed) != 0 &&
               (policyFlags & kThirdPersonSmoothingAllowed) != 0;
    }

    std::pair<float, float> HookCoordinator::ApplyTransform(float deltaX, float deltaY, const ConfigValues& config, bool isGamepad) const
    {
        if (!config.enabled || config.hotDisable) {
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
