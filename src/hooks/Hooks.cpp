#include "MouseSensitivityFix/Hooks.h"
#include "MouseSensitivityFix/Log.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>

#if MSF_USE_COMMONLIBSSE
#include <RE/A/AIProcess.h>
#include <RE/A/Actor.h>
#include <RE/C/ControlMap.h>
#include <RE/H/HighProcessData.h>
#include <RE/I/INISettingCollection.h>
#include <RE/T/TESObjectWEAP.h>
#include <RE/L/LookHandler.h>
#include <RE/M/MouseMoveEvent.h>
#include <RE/N/NiPoint2.h>
#include <RE/Offsets_VTABLE.h>
#include <RE/P/PlayerCamera.h>
#include <RE/P/PlayerCharacter.h>
#include <RE/P/PlayerControls.h>
#include <RE/P/PlayerControlsData.h>
#include <RE/S/SprintHandler.h>
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
        bool g_mouseHookFiredOnce{ false };
        bool g_runtimeControlSettingsLogged{ false };
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

        struct RuntimeLookState
        {
            bool valid{ false };
            bool sprinting{ false };
            bool movingForward{ false };
            bool movingBack{ false };
            bool movingLeft{ false };
            bool movingRight{ false };
            bool running{ false };
            bool controlsRunning{ false };
            bool sprintHandlerPresent{ false };
            bool sprintHeldActive{ false };
            bool sprintTriggerRelease{ false };
            RE::NiPoint2 moveInput{};
            RE::NiPoint2 previousMoveInput{};
            RE::NiPoint2 previousLookInput{};
            bool hasCameraRoot{ false };
            bool cameraEulerValid{ false };
            float playerPitch{ 0.0F };
            float playerYaw{ 0.0F };
            float cameraYaw{ 0.0F };
            float rotationInputX{ 0.0F };
            float rotationInputY{ 0.0F };
            RE::NiPoint3 cameraRootEuler{};
            float cameraMatrix[3][3]{};
        };

        RuntimeLookTelemetryAccumulator g_runtimeLookInput;
        std::uint64_t g_runtimeLookEventCount{ 0 };
        std::uint64_t g_playerMovementTraceCount{ 0 };
        std::uint64_t g_playerYawCorrectionCount{ 0 };
        bool g_hasPreviousRuntimeLookState{ false };
        RuntimeLookState g_previousRuntimeLookState{};
        std::chrono::steady_clock::time_point g_previousRuntimeLookEventTime{};

        constexpr std::uint64_t kLookLogInterval = 30;
        constexpr std::uint64_t kThirdPersonLogInterval = 180;
        constexpr std::uint64_t kStickLogInterval = 30;

        // Sampled scale: ratio of lookInputVec units per raw mouse pixel at baseline sensitivity.
        // Updated via EMA during normal play only (not during bow aim) to stay uncontaminated.
        float g_sampledScaleX{ 0.0f };
        float g_sampledScaleY{ 0.0f };
        constexpr float kScaleEmaAlpha = 0.15f;

        void LogRuntimeControlSettingsOnce()
        {
            if (g_runtimeControlSettingsLogged) {
                return;
            }
            g_runtimeControlSettingsLogged = true;

            auto* settings = RE::INISettingCollection::GetSingleton();
            if (!settings) {
                LogWarn("RuntimeControlSettings unavailable: INISettingCollection singleton is null.");
                return;
            }

            const auto logBool = [settings](std::string_view name) {
                if (const auto* setting = settings->GetSetting(name)) {
                    LogInfo("RuntimeControlSetting " + std::string(name) + "=" +
                        std::to_string(setting->GetBool()));
                } else {
                    LogWarn("RuntimeControlSetting missing: " + std::string(name));
                }
            };
            const auto logFloat = [settings](std::string_view name) {
                if (const auto* setting = settings->GetSetting(name)) {
                    LogInfo("RuntimeControlSetting " + std::string(name) + "=" +
                        std::to_string(setting->GetFloat()));
                } else {
                    LogWarn("RuntimeControlSetting missing: " + std::string(name));
                }
            };

            logBool("bDampenPlayerControls:Controls");
            logFloat("fControllerDampenTime:Controls");
            logFloat("fControllerBufferDepth:Controls");
            logFloat("fSprintAngleToPathThreshold:Controls");
        }

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

        RuntimeLookState CaptureRuntimeLookState(RE::PlayerCamera* camera) noexcept
        {
            RuntimeLookState state{};
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !camera) {
                return state;
            }

            state.valid = true;
            state.playerPitch = player->GetAngleX();
            state.playerYaw = player->GetAngleZ();
            state.cameraYaw = camera->yaw;
            state.rotationInputX = camera->rotationInput.x;
            state.rotationInputY = camera->rotationInput.y;

            const auto& actorState = REL::RelocateMemberIfNewer<RE::ActorState::ActorState1>(
                SKSE::RUNTIME_SSE_1_6_629, player, 0xC0, 0xC8);
            state.sprinting = static_cast<bool>(actorState.sprinting);
            state.movingForward = static_cast<bool>(actorState.movingForward);
            state.movingBack = static_cast<bool>(actorState.movingBack);
            state.movingLeft = static_cast<bool>(actorState.movingLeft);
            state.movingRight = static_cast<bool>(actorState.movingRight);
            state.running = static_cast<bool>(actorState.running);

            if (const auto* controls = RE::PlayerControls::GetSingleton()) {
                state.controlsRunning = controls->data.running;
                state.moveInput = controls->data.moveInputVec;
                state.previousMoveInput = controls->data.prevMoveVec;
                state.previousLookInput = controls->data.prevLookVec;
                if (const auto* sprintHandler = controls->sprintHandler) {
                    state.sprintHandlerPresent = true;
                    state.sprintHeldActive = sprintHandler->heldStateActive;
                    state.sprintTriggerRelease = sprintHandler->triggerReleaseEvent;
                }
            }

            if (camera->cameraRoot) {
                state.hasCameraRoot = true;
                const auto& rotation = camera->cameraRoot->world.rotate;
                state.cameraEulerValid = rotation.ToEulerAnglesXYZ(state.cameraRootEuler);
                for (std::size_t row = 0; row < 3; ++row) {
                    for (std::size_t column = 0; column < 3; ++column) {
                        state.cameraMatrix[row][column] = rotation.entry[row][column];
                    }
                }
            }
            return state;
        }

        void LogPreviousRuntimeLookEvent(
            const RuntimeLookState& currentState,
            std::chrono::steady_clock::time_point now)
        {
            const auto input = g_runtimeLookInput.Consume();
            if (!input || !g_hasPreviousRuntimeLookState || !currentState.valid) {
                return;
            }

            const auto& previousState = g_previousRuntimeLookState;
            const float playerPitchDelta = WrappedAngleDelta(currentState.playerPitch, previousState.playerPitch);
            const float playerYawDelta = WrappedAngleDelta(currentState.playerYaw, previousState.playerYaw);
            const float cameraYawDelta = WrappedAngleDelta(currentState.cameraYaw, previousState.cameraYaw);
            const bool rootDeltaValid = previousState.hasCameraRoot && currentState.hasCameraRoot &&
                previousState.cameraEulerValid && currentState.cameraEulerValid;
            const float cameraRootXDelta = rootDeltaValid
                ? WrappedAngleDelta(currentState.cameraRootEuler.x, previousState.cameraRootEuler.x) : 0.0F;
            const float cameraRootYDelta = rootDeltaValid
                ? WrappedAngleDelta(currentState.cameraRootEuler.y, previousState.cameraRootEuler.y) : 0.0F;
            const float cameraRootZDelta = rootDeltaValid
                ? WrappedAngleDelta(currentState.cameraRootEuler.z, previousState.cameraRootEuler.z) : 0.0F;
            const auto eventDeltaUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - g_previousRuntimeLookEventTime).count();
            const bool mixedSprintState = input->sprintEventCount != 0 &&
                input->sprintEventCount != input->eventCount;

            ++g_runtimeLookEventCount;
            LogInfo(
                "RuntimeLookEvent"
                " sequence=" + std::to_string(g_runtimeLookEventCount) +
                " dtUs=" + std::to_string(eventDeltaUs) +
                " events=" + std::to_string(input->eventCount) +
                " sprintEvents=" + std::to_string(input->sprintEventCount) +
                " sprintBefore=" + std::to_string(previousState.sprinting) +
                " sprintAfter=" + std::to_string(currentState.sprinting) +
                " mixedSprint=" + std::to_string(mixedSprintState) +
                " movementBefore=(" + std::to_string(previousState.movingForward) + "," +
                    std::to_string(previousState.movingBack) + "," + std::to_string(previousState.movingLeft) + "," +
                    std::to_string(previousState.movingRight) + "," + std::to_string(previousState.running) + ")" +
                " movementAfter=(" + std::to_string(currentState.movingForward) + "," +
                    std::to_string(currentState.movingBack) + "," + std::to_string(currentState.movingLeft) + "," +
                    std::to_string(currentState.movingRight) + "," + std::to_string(currentState.running) + ")" +
                " controlsBefore=(" + std::to_string(previousState.controlsRunning) + "," +
                    std::to_string(previousState.sprintHandlerPresent) + "," +
                    std::to_string(previousState.sprintHeldActive) + "," +
                    std::to_string(previousState.sprintTriggerRelease) + ")" +
                " controlsAfter=(" + std::to_string(currentState.controlsRunning) + "," +
                    std::to_string(currentState.sprintHandlerPresent) + "," +
                    std::to_string(currentState.sprintHeldActive) + "," +
                    std::to_string(currentState.sprintTriggerRelease) + ")" +
                " moveInputBefore=(" + std::to_string(previousState.moveInput.x) + "," +
                    std::to_string(previousState.moveInput.y) + ")" +
                " moveInputAfter=(" + std::to_string(currentState.moveInput.x) + "," +
                    std::to_string(currentState.moveInput.y) + ")" +
                " prevMoveBefore=(" + std::to_string(previousState.previousMoveInput.x) + "," +
                    std::to_string(previousState.previousMoveInput.y) + ")" +
                " prevMoveAfter=(" + std::to_string(currentState.previousMoveInput.x) + "," +
                    std::to_string(currentState.previousMoveInput.y) + ")" +
                " prevLookBefore=(" + std::to_string(previousState.previousLookInput.x) + "," +
                    std::to_string(previousState.previousLookInput.y) + ")" +
                " prevLookAfter=(" + std::to_string(currentState.previousLookInput.x) + "," +
                    std::to_string(currentState.previousLookInput.y) + ")" +
                " rawPx=(" + std::to_string(input->rawPixelX) + "," + std::to_string(input->rawPixelY) + ")" +
                " engine=(" + std::to_string(input->engineX) + "," + std::to_string(input->engineY) + ")" +
                " out=(" + std::to_string(input->outputX) + "," + std::to_string(input->outputY) + ")" +
                " playerBefore=(" + std::to_string(previousState.playerPitch) + "," + std::to_string(previousState.playerYaw) + ")" +
                " playerAfter=(" + std::to_string(currentState.playerPitch) + "," + std::to_string(currentState.playerYaw) + ")" +
                " playerDelta=(" + std::to_string(playerPitchDelta) + "," + std::to_string(playerYawDelta) + ")" +
                " cameraYawBefore=" + std::to_string(previousState.cameraYaw) +
                " cameraYawAfter=" + std::to_string(currentState.cameraYaw) +
                " cameraYawDelta=" + std::to_string(cameraYawDelta) +
                " rotationInputBefore=(" + std::to_string(previousState.rotationInputX) + "," + std::to_string(previousState.rotationInputY) + ")" +
                " rotationInputAfter=(" + std::to_string(currentState.rotationInputX) + "," + std::to_string(currentState.rotationInputY) + ")" +
                " rootDeltaValid=" + std::to_string(rootDeltaValid) +
                " rootBefore=(" + std::to_string(previousState.cameraRootEuler.x) + "," +
                    std::to_string(previousState.cameraRootEuler.y) + "," + std::to_string(previousState.cameraRootEuler.z) + ")" +
                " rootAfter=(" + std::to_string(currentState.cameraRootEuler.x) + "," +
                    std::to_string(currentState.cameraRootEuler.y) + "," + std::to_string(currentState.cameraRootEuler.z) + ")" +
                " rootDelta=(" + std::to_string(cameraRootXDelta) + "," +
                    std::to_string(cameraRootYDelta) + "," + std::to_string(cameraRootZDelta) + ")" +
                " rootMatrixBefore=(" +
                    std::to_string(previousState.cameraMatrix[0][0]) + "," + std::to_string(previousState.cameraMatrix[0][1]) + "," + std::to_string(previousState.cameraMatrix[0][2]) + "," +
                    std::to_string(previousState.cameraMatrix[1][0]) + "," + std::to_string(previousState.cameraMatrix[1][1]) + "," + std::to_string(previousState.cameraMatrix[1][2]) + "," +
                    std::to_string(previousState.cameraMatrix[2][0]) + "," + std::to_string(previousState.cameraMatrix[2][1]) + "," + std::to_string(previousState.cameraMatrix[2][2]) + ")" +
                " rootMatrixAfter=(" +
                    std::to_string(currentState.cameraMatrix[0][0]) + "," + std::to_string(currentState.cameraMatrix[0][1]) + "," + std::to_string(currentState.cameraMatrix[0][2]) + "," +
                    std::to_string(currentState.cameraMatrix[1][0]) + "," + std::to_string(currentState.cameraMatrix[1][1]) + "," + std::to_string(currentState.cameraMatrix[1][2]) + "," +
                    std::to_string(currentState.cameraMatrix[2][0]) + "," + std::to_string(currentState.cameraMatrix[2][1]) + "," + std::to_string(currentState.cameraMatrix[2][2]) + ")");
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
            const RE::NiPoint2 previousLookInput = controls ? controls->data.prevLookVec : RE::NiPoint2{};
            const RE::NiPoint3 movementBefore = movementData;
            const RE::NiPoint3 engineRotation = rotationData;
            rotationData.z = RestoreHalfRateSprintYawDelta(
                lookInput.x,
                delta,
                rotationData.z,
                config.enabled &&
                    !config.hotDisable &&
                    config.enableFirstPersonHook &&
                    !inThirdPerson &&
                    sprinting);
            const bool yawCorrected = rotationData.z != engineRotation.z;
            if (yawCorrected) {
                ++g_playerYawCorrectionCount;
            }
            const RE::NiPoint3 rotationBefore = rotationData;
            const float playerYawBefore = player ? player->GetAngleZ() : 0.0F;

            RE::HighProcessData* highProcess = nullptr;
            RE::NiPoint3 currentRotationBefore{};
            RE::NiPoint3 desiredRotationBefore{};
            if (player) {
                if (auto* process = player->GetActorRuntimeData().currentProcess) {
                    highProcess = process->high;
                    if (highProcess) {
                        currentRotationBefore = highProcess->pathingCurrentRotationSpeed;
                        desiredRotationBefore = highProcess->pathingDesiredRotationSpeed;
                    }
                }
            }

            g_originalPlayerModifyMovementData(player, delta, movementData, rotationData);

            if (!config.verboseLogging || inThirdPerson) {
                return;
            }

            RE::NiPoint3 currentRotationAfter{};
            RE::NiPoint3 desiredRotationAfter{};
            if (highProcess) {
                currentRotationAfter = highProcess->pathingCurrentRotationSpeed;
                desiredRotationAfter = highProcess->pathingDesiredRotationSpeed;
            }

            ++g_playerMovementTraceCount;
            LogInfo(
                "PlayerMovementTrace"
                " sequence=" + std::to_string(g_playerMovementTraceCount) +
                " sprint=" + std::to_string(sprinting) +
                " yawCorrected=" + std::to_string(yawCorrected) +
                " correctionCount=" + std::to_string(g_playerYawCorrectionCount) +
                " delta=" + std::to_string(delta) +
                " lookPostSensitivity=(" + std::to_string(lookInput.x) + "," +
                    std::to_string(lookInput.y) + ")" +
                " prevLook=(" + std::to_string(previousLookInput.x) + "," +
                    std::to_string(previousLookInput.y) + ")" +
                " movementBefore=(" + std::to_string(movementBefore.x) + "," +
                    std::to_string(movementBefore.y) + "," + std::to_string(movementBefore.z) + ")" +
                " movementAfter=(" + std::to_string(movementData.x) + "," +
                    std::to_string(movementData.y) + "," + std::to_string(movementData.z) + ")" +
                " engineRotation=(" + std::to_string(engineRotation.x) + "," +
                    std::to_string(engineRotation.y) + "," + std::to_string(engineRotation.z) + ")" +
                " rotationBefore=(" + std::to_string(rotationBefore.x) + "," +
                    std::to_string(rotationBefore.y) + "," + std::to_string(rotationBefore.z) + ")" +
                " rotationAfter=(" + std::to_string(rotationData.x) + "," +
                    std::to_string(rotationData.y) + "," + std::to_string(rotationData.z) + ")" +
                " currentRotationBefore=(" + std::to_string(currentRotationBefore.x) + "," +
                    std::to_string(currentRotationBefore.y) + "," + std::to_string(currentRotationBefore.z) + ")" +
                " currentRotationAfter=(" + std::to_string(currentRotationAfter.x) + "," +
                    std::to_string(currentRotationAfter.y) + "," + std::to_string(currentRotationAfter.z) + ")" +
                " desiredRotationBefore=(" + std::to_string(desiredRotationBefore.x) + "," +
                    std::to_string(desiredRotationBefore.y) + "," + std::to_string(desiredRotationBefore.z) + ")" +
                " desiredRotationAfter=(" + std::to_string(desiredRotationAfter.x) + "," +
                    std::to_string(desiredRotationAfter.y) + "," + std::to_string(desiredRotationAfter.z) + ")" +
                " playerYawBefore=" + std::to_string(playerYawBefore) +
                " playerYawAfter=" + std::to_string(player ? player->GetAngleZ() : 0.0F));
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
            LogRuntimeControlSettingsOnce();
            auto* camera = RE::PlayerCamera::GetSingleton();
            const bool inThirdPerson = camera && camera->IsInThirdPerson();
            auto* player = RE::PlayerCharacter::GetSingleton();

            if (!g_activeCoordinator->ShouldApplyInputTransform(reloadedConfig, inThirdPerson, false)) {
                g_runtimeLookInput.Consume();
                g_hasPreviousRuntimeLookState = false;
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
            const auto runtimeLookEventTime = std::chrono::steady_clock::now();
            RuntimeLookState runtimeLookStateBeforeInput{};
            if (reloadedConfig.verboseLogging && !inThirdPerson) {
                runtimeLookStateBeforeInput = CaptureRuntimeLookState(camera);
                LogPreviousRuntimeLookEvent(runtimeLookStateBeforeInput, runtimeLookEventTime);
            } else {
                g_runtimeLookInput.Consume();
                g_hasPreviousRuntimeLookState = false;
            }

            g_originalProcessMouseMove(handler, event, data);
            if (!data) {
                return;
            }

            g_lastRawX = data->lookInputVec.x;
            g_lastRawY = data->lookInputVec.y;

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
            if (reloadedConfig.verboseLogging) {
                g_runtimeLookInput.Record(
                    rawPixelX,
                    rawPixelY,
                    g_lastEngineX,
                    g_lastEngineY,
                    outX,
                    outY,
                    IsSprintingRelocated(player));
                if (runtimeLookStateBeforeInput.valid) {
                    g_previousRuntimeLookState = runtimeLookStateBeforeInput;
                    g_previousRuntimeLookEventTime = runtimeLookEventTime;
                    g_hasPreviousRuntimeLookState = true;
                }
            }
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

    void RuntimeLookTelemetryAccumulator::Record(
        float rawPixelX,
        float rawPixelY,
        float engineX,
        float engineY,
        float outputX,
        float outputY,
        bool sprinting) noexcept
    {
        ++_sample.eventCount;
        if (sprinting) {
            ++_sample.sprintEventCount;
        }
        _sample.rawPixelX += rawPixelX;
        _sample.rawPixelY += rawPixelY;
        _sample.engineX += engineX;
        _sample.engineY += engineY;
        _sample.outputX += outputX;
        _sample.outputY += outputY;
    }

    std::optional<RuntimeLookInputSample> RuntimeLookTelemetryAccumulator::Consume() noexcept
    {
        if (_sample.eventCount == 0) {
            return std::nullopt;
        }

        const auto sample = _sample;
        _sample = {};
        return sample;
    }

    float WrappedAngleDelta(float current, float previous) noexcept
    {
        constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
        float delta = std::fmod(current - previous + std::numbers::pi_v<float>, twoPi);
        if (delta < 0.0F) {
            delta += twoPi;
        }
        return delta - std::numbers::pi_v<float>;
    }

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

        LogInfo("Installed relocation-backed LookHandler input hooks with prior-frame look telemetry.");
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
        g_runtimeLookInput = {};
        g_runtimeLookEventCount = 0;
        g_hasPreviousRuntimeLookState = false;
        g_previousRuntimeLookState = {};
        g_previousRuntimeLookEventTime = {};
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

    bool HookCoordinator::InstallPlayerMovementTraceHook()
    {
#if MSF_USE_COMMONLIBSSE
        if (g_originalPlayerModifyMovementData) {
            return true;
        }

        REL::Relocation<std::uintptr_t> playerCharacterVTable{ RE::VTABLE_PlayerCharacter[0] };
        g_originalPlayerModifyMovementData = reinterpret_cast<PlayerModifyMovementDataFn>(
            playerCharacterVTable.write_vfunc(0x11A, PlayerModifyMovementDataHook));
        if (!g_originalPlayerModifyMovementData) {
            LogError("Failed to install PlayerCharacter::ModifyMovementData trace hook.");
            return false;
        }

        g_playerMovementTraceCount = 0;
        g_playerYawCorrectionCount = 0;
        LogInfo("Installed PlayerCharacter::ModifyMovementData final-yaw trace hook.");
        return true;
#else
        LogWarn("CommonLibSSE disabled; cannot install player movement trace hook.");
        return false;
#endif
    }

    void HookCoordinator::RemovePlayerMovementTraceHook()
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
        LogInfo("Removed PlayerCharacter::ModifyMovementData final-yaw trace hook.");
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
        _playerMovementTraceRegistered = false;
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

        _playerMovementTraceRegistered = InstallPlayerMovementTraceHook();
        if (!_playerMovementTraceRegistered) {
            RemoveLookHandlerMouseMoveHook();
            _firstPersonRegistered = false;
#if MSF_USE_COMMONLIBSSE
            g_activeCoordinator = nullptr;
#endif
            return false;
        }

        if (!RegisterHookPoint(HookRegistrationPoint::SmoothingRemoval)) {
            RemovePlayerMovementTraceHook();
            _playerMovementTraceRegistered = false;
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
        RemovePlayerMovementTraceHook();
        RemoveLookHandlerMouseMoveHook();
        RemoveThirdPersonSmoothingHook();
        _firstPersonRegistered = false;
        _playerMovementTraceRegistered = false;
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
