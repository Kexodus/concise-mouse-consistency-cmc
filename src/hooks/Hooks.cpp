#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>

#if MSF_USE_COMMONLIBSSE
#include <RE/A/Actor.h>
#include <RE/C/ControlMap.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/L/LookHandler.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/N/NiPoint2.h>
#include <RE/Offsets_VTABLE.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerControlsData.h>
#include <RE/T/ThirdPersonState.h>
#include <RE/T/ThumbstickEvent.h>
#include <RE/U/UI.h>
#include <REL/Relocation.h>
#include <SKSE/Version.h>
#endif

namespace msf
{
#if MSF_USE_COMMONLIBSSE
    namespace
    {
        using ProcessThumbstickFn = void (*)(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
        using ProcessMouseMoveFn = void (*)(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
        using ThirdPersonHandleLookInputFn = void (*)(RE::ThirdPersonState*, const RE::NiPoint2&);

        // ActorState is a base class of Actor at compile-time offset 0xB8 (SE layout).
        // In AE 1.6.629+, TESObjectREFR grew by 8 bytes, shifting ActorState to 0xC0.
        // The C++ compiler bakes in the SE offset, so direct access reads wrong memory on AE.
        // These helpers use RelocateMemberIfNewer to read from the correct offset.
        // actorState1 (contains meleeAttackState): SE 0xC0, AE 0xC8
        // actorState2 (contains weaponState):      SE 0xC4, AE 0xCC
        RE::ATTACK_STATE_ENUM GetAttackStateRelocated(const RE::PlayerCharacter* player) noexcept
        {
            const auto& as1 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState1>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC0, 0xC8);
            return as1.meleeAttackState;
        }

        bool IsWeaponDrawnRelocated(const RE::PlayerCharacter* player) noexcept
        {
            const auto& as2 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState2>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC4, 0xCC);
            switch (as2.weaponState) {
            case RE::WEAPON_STATE::kDrawn:
            case RE::WEAPON_STATE::kWantToSheathe:
            case RE::WEAPON_STATE::kSheathing:
                return true;
            default:
                return false;
            }
        }

        ProcessThumbstickFn g_originalProcessThumbstick{ nullptr };
        ProcessMouseMoveFn g_originalProcessMouseMove{ nullptr };
        ThirdPersonHandleLookInputFn g_originalThirdPersonHandleLookInput{ nullptr };
        HookCoordinator* g_activeCoordinator{ nullptr };
        bool g_allowThirdPersonIntervention{ true };
        bool g_mouseHookFiredOnce{ false };
        bool g_thumbstickHookFiredOnce{ false };

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
        std::string   g_lastBowDiag{ "." };
        std::uint32_t g_lastAttackState{ 0 };
        bool          g_lastWeaponDrawn{ false };
        float g_lastRawPixelX{ 0.0F };
        float g_lastRawPixelY{ 0.0F };
        float g_lastEngineX{ 0.0F };
        float g_lastEngineY{ 0.0F };
        float g_lastBowMulX{ 1.0F };
        float g_lastBowMulY{ 1.0F };
        std::uint64_t g_thirdPersonSmoothingAppliedCount{ 0 };
        std::chrono::steady_clock::time_point g_lastMouseEventTime{};

        constexpr std::uint64_t kLookLogInterval = 30;
        constexpr std::uint64_t kThirdPersonLogInterval = 180;
        constexpr std::uint64_t kStickLogInterval = 30;

        // Sampled scale: ratio of lookInputVec units per raw mouse pixel at baseline sensitivity.
        // Updated via EMA during normal play only (not during bow aim) to stay uncontaminated.
        float g_sampledScaleX{ 0.0f };
        float g_sampledScaleY{ 0.0f };
        constexpr float kScaleEmaAlpha = 0.15f;

        // Returns true when the player is actively drawing or aiming with a bow or crossbow.
        // Uses ActorState::GetAttackState() range kBowDraw..kBowNextAttack.
        // OAR-proof, no animation event dependency. Matches IC's proven IsAiming() approach.
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

        // Diagnostic character for bow aim state logging.
        // Returns attack state value as char (8-D hex) when in bow range, '.' otherwise.
        char BowAimDiagChar(RE::PlayerCharacter* player) noexcept
        {
            if (!player) return '.';
            const auto attackState = static_cast<std::uint32_t>(GetAttackStateRelocated(player));
            if (attackState >= 8 && attackState <= 13) {
                return "89ABCD"[attackState - 8];
            }
            return '.';
        }

        void LogLookHookCountersIfNeeded(const ConfigValues& config)
        {
            if (!config.verboseLogging || (g_lookHookCallsTotal % kLookLogInterval) != 0) {
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
                " camera=" + g_lastCameraState +
                " bowDiag=" + g_lastBowDiag +
                " atkState=" + std::to_string(g_lastAttackState) +
                " wpnDrawn=" + std::to_string(g_lastWeaponDrawn) +
                " rawPx=(" + std::to_string(g_lastRawPixelX) + "," + std::to_string(g_lastRawPixelY) + ")" +
                " engine=(" + std::to_string(g_lastEngineX) + "," + std::to_string(g_lastEngineY) + ")" +
                " bowMul=(" + std::to_string(g_lastBowMulX) + "," + std::to_string(g_lastBowMulY) + ")");
        }

        void LogThirdPersonHookCountersIfNeeded(const ConfigValues& config)
        {
            if (!config.verboseLogging || (g_thirdPersonHookCallsTotal % kThirdPersonLogInterval) != 0) {
                return;
            }

            LogInfo(
                "HookCounter[ThirdPersonState::HandleLookInput]"
                " total=" + std::to_string(g_thirdPersonHookCallsTotal) +
                " smoothingRemoved=" + std::to_string(g_thirdPersonSmoothingAppliedCount));
        }

        void ProcessMouseMoveHook(RE::LookHandler* handler, RE::MouseMoveEvent* event, RE::PlayerControlsData* data)
        {
            if (!g_mouseHookFiredOnce) {
                g_mouseHookFiredOnce = true;
                LogInfo("Diag: ProcessMouseMoveHook first call. coordinator=" +
                    std::string(g_activeCoordinator ? "set" : "null") +
                    " event=" + std::string(event ? "valid" : "null"));
            }

            if (!g_originalProcessMouseMove) {
                return;
            }

            if (!event || !g_activeCoordinator) {
                g_originalProcessMouseMove(handler, event, data);
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto reloadedConfig = ConfigManager::Get().GetSnapshot();
            if (!reloadedConfig.enabled) {
                g_originalProcessMouseMove(handler, event, data);
                return;
            }

            ++g_lookHookCallsTotal;

            const auto* camera = RE::PlayerCamera::GetSingleton();
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            if (inThirdPerson) {
                ++g_lookHookCallsThirdPerson;
                g_lastCameraState = "ThirdPerson";
            } else {
                ++g_lookHookCallsFirstPerson;
                g_lastCameraState = "FirstPerson";
            }

            if (!inThirdPerson && !reloadedConfig.enableFirstPersonHook) {
                LogLookHookCountersIfNeeded(reloadedConfig);
                g_originalProcessMouseMove(handler, event, data);
                return;
            }
            if (inThirdPerson && !reloadedConfig.enableThirdPersonHook) {
                LogLookHookCountersIfNeeded(reloadedConfig);
                g_originalProcessMouseMove(handler, event, data);
                return;
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

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player) {
                g_lastAttackState = static_cast<std::uint32_t>(GetAttackStateRelocated(player));
                g_lastWeaponDrawn = IsWeaponDrawnRelocated(player);
            }
            const char bowDiag = BowAimDiagChar(player);
            g_lastBowDiag = std::string(1, bowDiag);
            const bool isBowAim = (bowDiag != '.');

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

            // Always capture raw pixels and engine-processed values for diagnostics.
            g_lastRawPixelX = rawPixelX;
            g_lastRawPixelY = rawPixelY;
            g_lastEngineX = data->lookInputVec.x;
            g_lastEngineY = data->lookInputVec.y;

            if (isBowAim) {
                g_lastCameraState = inThirdPerson ? "ThirdPerson_BowAim" : "FirstPerson_BowAim";
                const float bowX = static_cast<float>(reloadedConfig.bowAimMouseXMultiplier);
                const float bowY = static_cast<float>(reloadedConfig.bowAimMouseYMultiplier);
                g_lastBowMulX = bowX;
                g_lastBowMulY = bowY;
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
            if (!g_thumbstickHookFiredOnce) {
                g_thumbstickHookFiredOnce = true;
                LogInfo("Diag: ProcessThumbstickHook first call. coordinator=" +
                    std::string(g_activeCoordinator ? "set" : "null") +
                    " event=" + std::string(event ? "valid" : "null") +
                    (event ? " isRight=" + std::string(event->IsRight() ? "true" : "false") : ""));
            }

            if (!g_originalProcessThumbstick) {
                return;
            }

            if (!event || !g_activeCoordinator) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto reloadedConfig = ConfigManager::Get().GetSnapshot();
            if (!reloadedConfig.enabled || !reloadedConfig.affectGamepadLook) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            if (!event->IsRight()) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            const auto* camera = RE::PlayerCamera::GetSingleton();
            const bool inThirdPerson = camera && camera->IsInThirdPerson();

            if (!inThirdPerson && !reloadedConfig.enableFirstPersonHook) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }
            if (inThirdPerson && !reloadedConfig.enableThirdPersonHook) {
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
            if (reloadedConfig.verboseLogging && (g_thumbstickHookCallsTotal % kStickLogInterval) == 0) {
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

            if (!state || !g_allowThirdPersonIntervention) {
                return;
            }

            ConfigManager::Get().ReloadIfChanged();
            const auto config = ConfigManager::Get().GetSnapshot();
            ++g_thirdPersonHookCallsTotal;

            if (!config.enabled || !config.enableSmoothingRemovalHook) {
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
            LogError("Failed to install LookHandler thumbstick/mouse vtable hooks.");
            return false;
        }

        LogInfo("Installed relocation-backed LookHandler::ProcessThumbstick and ProcessMouseMove hooks.");
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
        LogInfo("Removed LookHandler::ProcessThumbstick and ProcessMouseMove vtable hooks.");
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
        case HookRegistrationPoint::FirstPersonMouseLook:
            _firstPersonRegistered = InstallLookHandlerMouseMoveHook();
            return _firstPersonRegistered;
        case HookRegistrationPoint::SmoothingRemoval:
            _smoothingRemovalRegistered = InstallThirdPersonSmoothingHook();
            return _smoothingRemovalRegistered;
        default:
            return false;
        }
    }

    bool HookCoordinator::Install(const CompatibilityPolicy& policy, const ConfigValues& config)
    {
        if (!config.enabled || !policy.installInputHooks) {
            _installed = false;
            _activePolicy = policy;
            LogWarn("Hooks not installed because plugin is disabled or policy blocks input hooks.");
            return true;
        }

        bool ok = true;
        _firstPersonRegistered = false;
        _smoothingRemovalRegistered = false;
        _activePolicy = policy;
#if MSF_USE_COMMONLIBSSE
        g_activeCoordinator = this;
        g_allowThirdPersonIntervention = policy.allowThirdPersonIntervention;
#endif

        if (config.enableFirstPersonHook || config.enableThirdPersonHook) {
            ok = ok && RegisterHookPoint(HookRegistrationPoint::FirstPersonMouseLook);
        }

        if (config.enableThirdPersonHook && !policy.allowThirdPersonIntervention) {
            LogWarn("Third-person hook skipped due to compatibility policy.");
        }

        if (config.enableSmoothingRemovalHook && policy.installSmoothingRemovalHooks) {
            ok = ok && RegisterHookPoint(HookRegistrationPoint::SmoothingRemoval);
        } else if (config.enableSmoothingRemovalHook) {
            LogWarn("Smoothing-removal hook skipped due to compatibility policy.");
        }

        _installed = ok;
        return ok;
    }

    void HookCoordinator::Remove()
    {
        if (!_installed) {
            return;
        }

        RemoveLookHandlerMouseMoveHook();
        RemoveThirdPersonSmoothingHook();
        _firstPersonRegistered = false;
        _smoothingRemovalRegistered = false;
        _installed = false;
#if MSF_USE_COMMONLIBSSE
        g_activeCoordinator = nullptr;
#endif
        LogInfo("Mouse hook registrations removed.");
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
