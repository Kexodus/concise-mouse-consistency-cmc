#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <tuple>

#if MSF_USE_COMMONLIBSSE
#include <RE/A/Actor.h>
#include <RE/A/ActorState.h>
#include <RE/C/ControlMap.h>
#include <RE/F/FirstPersonState.h>
#include <RE/L/LookHandler.h>
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

        RE::WEAPON_STATE GetWeaponStateRelocated(const RE::PlayerCharacter* player) noexcept
        {
            const auto& as2 = REL::RelocateMemberIfNewer<RE::ActorState::ActorState2>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC4, 0xCC);
            return as2.weaponState;
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

        bool IsRangedWeaponDrawn(RE::PlayerCharacter* player) noexcept
        {
            if (!IsWeaponDrawnRelocated(player)) {
                return false;
            }
            auto* object = player->GetEquippedObject(false);
            auto* weapon = object ? skyrim_cast<RE::TESObjectWEAP*>(object) : nullptr;
            if (!weapon) {
                return false;
            }
            switch (weapon->GetWeaponType()) {
            case RE::WEAPON_TYPE::kBow:
            case RE::WEAPON_TYPE::kCrossbow:
                return true;
            default:
                return false;
            }
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
        float g_lastRawPixelX{ 0.0F };
        float g_lastRawPixelY{ 0.0F };
        float g_lastEngineX{ 0.0F };
        float g_lastEngineY{ 0.0F };
        float g_lastOutX{ 0.0F };
        float g_lastOutY{ 0.0F };
        std::string g_lastCameraState{ "Unknown" };
        std::string g_lastAimState{ "freelook" };
        std::string g_previousAimState{ "freelook" };
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
        constexpr float kLookRatioEmaAlpha = 0.15F;
        constexpr std::uint64_t kSensitivityProbeInterval = 120;

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
        constexpr std::uint64_t kHalfRateYawLogInterval = 120;

        // Sampled scale: ratio of lookInputVec units per raw mouse pixel at baseline sensitivity.
        // Updated via EMA during normal play only (not during bow aim) to stay uncontaminated.
        float g_sampledScaleX{ 0.0f };
        float g_sampledScaleY{ 0.0f };
        constexpr float kScaleEmaAlpha = 0.15f;
        float g_normalAimFov{ 0.0f };
        const RE::NiNode* g_lastAimFovCameraRoot{ nullptr };
        bool g_lastAimFovThirdPerson{ false };
        bool g_hasAimFovCameraIdentity{ false };

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
            sample.vFovDegrees = VerticalFovDegreesFromFrustum(frustum.fTop, frustum.fNear);
            sample.hFovDegrees = VerticalFovDegreesFromFrustum(frustum.fRight, frustum.fNear);

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
                " sampledScale=(" + std::to_string(g_sampledScaleX) + "," + std::to_string(g_sampledScaleY) + ")" +
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
            const bool bowAiming = DetectBowAim(player);
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            const RE::NiPoint2 lookInput = controls ? controls->data.lookInputVec : RE::NiPoint2{};
            const float engineYawDelta = rotationData.z;
            rotationData.z = RestoreHalfRateYawDelta(
                lookInput.x,
                delta,
                rotationData.z,
                ShouldRestoreHalfRateFirstPersonYaw(
                    config.enabled,
                    config.hotDisable,
                    config.enableFirstPersonHook,
                    inThirdPerson,
                    sprinting,
                    bowAiming));
            const bool yawCorrected = rotationData.z != engineYawDelta;
            if (yawCorrected) {
                ++g_playerYawCorrectionCount;
                if (ShouldEmitSampledLog(
                        config.verboseLogging,
                        g_playerYawCorrectionCount,
                        kHalfRateYawLogInterval,
                        true)) {
                    LogInfo(
                        "HookCounter[FirstPersonHalfRateYaw]"
                        " corrected=" + std::to_string(g_playerYawCorrectionCount) +
                        " sprinting=" + std::to_string(sprinting) +
                        " bowAiming=" + std::to_string(bowAiming) +
                        " engineYaw=" + std::to_string(engineYawDelta) +
                        " restoredYaw=" + std::to_string(rotationData.z) +
                        " lookX=" + std::to_string(lookInput.x) +
                        " delta=" + std::to_string(delta));
                }
            }

            if (config.verboseLogging && !inThirdPerson &&
                (std::abs(lookInput.x) >= 0.01F || std::abs(lookInput.y) >= 0.01F)) {
                const float yawPerLook =
                    (std::abs(lookInput.x) >= 0.01F) ? (rotationData.z / lookInput.x) : 0.0F;
                const std::string state = g_lastAimState;

                if (state == "freelook" && !sprinting) {
                    if (std::abs(lookInput.x) >= 0.01F) {
                        g_freelookYawPerLook = (g_freelookYawPerLook == 0.0F)
                            ? yawPerLook
                            : g_freelookYawPerLook + kLookRatioEmaAlpha * (yawPerLook - g_freelookYawPerLook);
                    }
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
                        " sprinting=" + std::to_string(sprinting ? 1 : 0) +
                        " yawCorrected=" + std::to_string(yawCorrected ? 1 : 0) +
                        " look=(" + std::to_string(lookInput.x) + "," + std::to_string(lookInput.y) + ")" +
                        " rotYawEngine=" + std::to_string(engineYawDelta) +
                        " rotYawOut=" + std::to_string(rotationData.z) +
                        " yawPerLook=" + std::to_string(yawPerLook) +
                        " freelookYawPerLook=" + std::to_string(g_freelookYawPerLook) +
                        " yawRatioToFreelook=" + std::to_string(yawRatioToFreelook) +
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

            g_lastRawPixelX = rawPixelX;
            g_lastRawPixelY = rawPixelY;
            g_lastEngineX = data->lookInputVec.x;
            g_lastEngineY = data->lookInputVec.y;

            const bool isBowAim = DetectBowAim(player);
            const bool bowOut = IsRangedWeaponDrawn(player);
            const bool bowZoomFlag = camera && camera->bowZoomedIn;

            float baseAimFov = 0.0F;
            float currentAimFov = 0.0F;
            RenderedFovSample rendered{};
            bool renderedZoomedIn = false;
            if (reloadedConfig.verboseLogging) {
                // Eagle Eye narrows NiCamera::viewFrustum. PlayerCamera::firstPersonFOV /
                // worldFOV are configured base values and often stay fixed (esp. with IC).
                baseAimFov = camera
                    ? (inThirdPerson ? camera->worldFOV : camera->firstPersonFOV)
                    : 0.0F;
                rendered = ReadRenderedFov(camera);
                currentAimFov =
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
                if (ShouldUpdateNormalAimFov(bowOut, isBowAim, bowZoomFlag, currentAimFov)) {
                    g_normalAimFov = currentAimFov;
                }
                renderedZoomedIn =
                    isBowAim &&
                    g_normalAimFov > 0.0F &&
                    currentAimFov > 0.0F &&
                    (currentAimFov / g_normalAimFov) < 0.98F;
            }
            const bool effectiveBowZoomedIn = isBowAim && (bowZoomFlag || renderedZoomedIn);

            // Update sampled scale only during true freelook.
            // Tracks the engine's pixels-to-lookInputVec ratio at baseline sensitivity.
            // Bow-out and zoom-transition frames cannot replace the normal baseline.
            if (ShouldUpdateFreelookSampledScale(bowOut, isBowAim, bowZoomFlag)) {
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

            float eagleEyeY = 1.0F;
            float bowY = 1.0F;
            if (isBowAim) {
                ++g_bowAimMouseFrames;
                if (effectiveBowZoomedIn) {
                    ++g_eagleEyeMouseFrames;
                }
                g_lastCameraState = inThirdPerson
                    ? (effectiveBowZoomedIn ? "ThirdPerson_EagleEye" : "ThirdPerson_BowAim")
                    : (effectiveBowZoomedIn ? "FirstPerson_EagleEye" : "FirstPerson_BowAim");
                const float bowX = static_cast<float>(reloadedConfig.bowAimMouseXMultiplier);
                bowY = CalculateBowAimVerticalMultiplier(
                    true,
                    static_cast<float>(reloadedConfig.bowAimMouseYMultiplier));
                // Rendered FOV is diagnostic only. Eagle Eye must preserve the configured
                // freelook-equivalent Y response instead of scaling it by the zoom ratio.
                eagleEyeY = 1.0F;
                // Reconstruct the normal-sensitivity X delta from raw pixels and the sampled
                // scale, then apply bowX relative to that baseline. Falls back to the
                // engine delta if the scale is not yet seeded.
                // Keep the engine's current Y delta. Eagle Eye changes the zoom/FOV
                // response, and rebuilding from the cached normal-play pixel scale
                // makes bow Y stale and ignores live game sensitivity changes.
                // Apply the configurable bow adjustment here, then ApplyTransform
                // applies the current global and mouse Y settings exactly once.
                std::tie(deltaX, deltaY) = ApplyBowAimMouseDeltas(
                    rawPixelX,
                    deltaX,
                    deltaY,
                    g_sampledScaleX,
                    bowX,
                    bowY);
            } else if (bowOut) {
                g_lastCameraState = inThirdPerson ? "ThirdPerson_BowOut" : "FirstPerson_BowOut";
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

    float RestoreHalfRateYawDelta(
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

    bool ShouldUpdateFreelookSampledScale(
        bool bowOut,
        bool bowAiming,
        bool bowZoomFlag) noexcept
    {
        return !bowOut && !bowAiming && !bowZoomFlag;
    }

    bool ShouldUpdateNormalAimFov(
        bool bowOut,
        bool bowAiming,
        bool bowZoomFlag,
        float renderedFovDegrees) noexcept
    {
        return renderedFovDegrees > 0.0F && !bowOut && !bowAiming && !bowZoomFlag;
    }

    float VerticalFovDegreesFromFrustum(float fTop, float fNear) noexcept
    {
        if (!(fTop > 0.0F) || !(fNear > 0.0F)) {
            return 0.0F;
        }
        constexpr float kRadiansToDegrees = 180.0F / std::numbers::pi_v<float>;
        return 2.0F * std::atan(fTop / fNear) * kRadiansToDegrees;
    }

    bool ShouldRestoreHalfRateFirstPersonYaw(
        bool enabled,
        bool hotDisabled,
        bool firstPersonHookEnabled,
        bool inThirdPerson,
        bool sprinting,
        bool bowAiming) noexcept
    {
        return enabled &&
               !hotDisabled &&
               firstPersonHookEnabled &&
               !inThirdPerson &&
               (sprinting || bowAiming);
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

        if (!RegisterHookPoint(HookRegistrationPoint::SmoothingRemoval)) {
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
        RemovePlayerYawHook();
        RemoveLookHandlerMouseMoveHook();
        RemoveThirdPersonSmoothingHook();
        _firstPersonRegistered = false;
        _playerYawRegistered = false;
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
