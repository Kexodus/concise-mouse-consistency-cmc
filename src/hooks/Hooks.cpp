#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>

#if MSF_USE_COMMONLIBSSE
#include <RE/C/ControlMap.h>
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
#endif

namespace msf
{
#if MSF_USE_COMMONLIBSSE
    namespace
    {
        using ProcessThumbstickFn = void (*)(RE::LookHandler*, RE::ThumbstickEvent*, RE::PlayerControlsData*);
        using ProcessMouseMoveFn = void (*)(RE::LookHandler*, RE::MouseMoveEvent*, RE::PlayerControlsData*);
        using ThirdPersonHandleLookInputFn = void (*)(RE::ThirdPersonState*, const RE::NiPoint2&);

        ProcessThumbstickFn g_originalProcessThumbstick{ nullptr };
        ProcessMouseMoveFn g_originalProcessMouseMove{ nullptr };
        ThirdPersonHandleLookInputFn g_originalThirdPersonHandleLookInput{ nullptr };
        HookCoordinator* g_activeCoordinator{ nullptr };
        bool g_allowThirdPersonIntervention{ true };

        std::uint64_t g_lookHookCallsTotal{ 0 };
        std::uint64_t g_lookHookCallsFirstPerson{ 0 };
        std::uint64_t g_lookHookCallsThirdPerson{ 0 };
        std::uint64_t g_lookTransformAppliedCount{ 0 };
        float g_lastRawX{ 0.0F };
        float g_lastRawY{ 0.0F };
        float g_lastOutX{ 0.0F };
        float g_lastOutY{ 0.0F };
        std::string g_lastCameraState{ "Unknown" };

        std::uint64_t g_thirdPersonHookCallsTotal{ 0 };
        std::uint64_t g_thirdPersonSmoothingAppliedCount{ 0 };
        std::chrono::steady_clock::time_point g_lastMouseEventTime{};

        constexpr std::uint64_t kLookLogInterval = 240;
        constexpr std::uint64_t kThirdPersonLogInterval = 180;

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
                " camera=" + g_lastCameraState);
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
            if (!reloadedConfig.allowInFirstPerson && !inThirdPerson) {
                LogLookHookCountersIfNeeded(reloadedConfig);
                g_originalProcessMouseMove(handler, event, data);
                return;
            }
            if (!reloadedConfig.allowInThirdPerson && inThirdPerson) {
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

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && !reloadedConfig.allowInCombat) {
                if (player->IsInCombat()) {
                    g_lastCameraState = "CombatBlocked";
                    LogLookHookCountersIfNeeded(reloadedConfig);
                    g_originalProcessMouseMove(handler, event, data);
                    return;
                }
            }

            if (!reloadedConfig.allowInIronSights && camera) {
                const auto current = camera->currentState.get();
                const auto iron = camera->cameraStates[RE::CameraState::kIronSights].get();
                if (current != nullptr && current == iron) {
                    g_lastCameraState = "IronSightsBlocked";
                    LogLookHookCountersIfNeeded(reloadedConfig);
                    g_originalProcessMouseMove(handler, event, data);
                    return;
                }
            }

            if (!reloadedConfig.allowInBowZoom && camera) {
                if (camera->bowZoomedIn) {
                    g_lastCameraState = "BowZoomBlocked";
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

            g_originalProcessMouseMove(handler, event, data);
            if (!data) {
                return;
            }

            g_lastRawX = data->lookInputVec.x;
            g_lastRawY = data->lookInputVec.y;
            const auto [outX, outY] = g_activeCoordinator->ApplyTransform(
                data->lookInputVec.x,
                data->lookInputVec.y,
                reloadedConfig);

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
            if (!reloadedConfig.allowInFirstPerson && !inThirdPerson) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }
            if (!reloadedConfig.allowInThirdPerson && inThirdPerson) {
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

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && !reloadedConfig.allowInCombat && player->IsInCombat()) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            if (!reloadedConfig.allowInIronSights && camera) {
                const auto current = camera->currentState.get();
                const auto iron = camera->cameraStates[RE::CameraState::kIronSights].get();
                if (current != nullptr && current == iron) {
                    g_originalProcessThumbstick(handler, event, data);
                    return;
                }
            }

            if (!reloadedConfig.allowInBowZoom && camera && camera->bowZoomedIn) {
                g_originalProcessThumbstick(handler, event, data);
                return;
            }

            // Capture raw symmetric joystick values before the engine applies its
            // own per-axis sensitivity scaling, so our transform produces true 1:1
            // X/Y parity regardless of Skyrim's internal gamepad sensitivity settings.
            const float rawX = event->xValue;
            const float rawY = event->yValue;

            g_originalProcessThumbstick(handler, event, data);
            if (!data) {
                return;
            }

            const auto [outX, outY] = g_activeCoordinator->ApplyTransform(rawX, rawY, reloadedConfig);
            data->lookInputVec.x = std::clamp(outX, -1.0F, 1.0F);
            data->lookInputVec.y = std::clamp(outY, -1.0F, 1.0F);
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

            if (!config.enabled || !config.enableSmoothingRemovalHook || !config.strictRawInputMode) {
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

    std::pair<float, float> HookCoordinator::ApplyTransform(float deltaX, float deltaY, const ConfigValues& config) const
    {
        if (!config.enabled || config.hotDisable) {
            return { deltaX, deltaY };
        }

        const auto scale = static_cast<float>(config.globalSensitivity);
        const auto xMultiplier = static_cast<float>(config.xAxisMultiplier);
        const auto yMultiplier = static_cast<float>(config.yAxisMultiplier);

        float outX = deltaX * scale * xMultiplier;
        float outY = deltaY * scale * yMultiplier;

        if (config.strictRawInputMode) {
            // Keep transform deterministic and avoid frame-dependent smoothing.
            const auto maxDelta = static_cast<float>(config.maxDeltaPerFrame);
            outX = std::clamp(outX, -maxDelta, maxDelta);
            outY = std::clamp(outY, -maxDelta, maxDelta);
        }

        return { outX, outY };
    }
}
