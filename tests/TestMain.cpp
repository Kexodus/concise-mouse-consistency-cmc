#include "MouseSensitivityFix/Compatibility.h"
#include "MouseSensitivityFix/Config.h"
#include "MouseSensitivityFix/Hooks.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    void Check(bool condition, const char* expression, int line)
    {
        if (!condition) {
            throw std::runtime_error("check failed at line " + std::to_string(line) + ": " + expression);
        }
    }

#define CHECK(expression) Check((expression), #expression, __LINE__)

    bool Near(float left, float right)
    {
        return std::abs(left - right) < 0.0001F;
    }

    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
        {
            const auto suffix = std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            _path = std::filesystem::temp_directory_path() / ("cmc-tests-" + suffix);
            std::filesystem::create_directories(_path);
        }

        ~TemporaryDirectory()
        {
            std::error_code ec;
            std::filesystem::remove_all(_path, ec);
        }

        const std::filesystem::path& Path() const
        {
            return _path;
        }

    private:
        std::filesystem::path _path;
    };

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::trunc);
        output << text;
        output.close();
        CHECK(output.good());
    }

    std::string ReadText(const std::filesystem::path& path)
    {
        std::ifstream input(path);
        return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    }

    void TestTransformAndRuntimeGates()
    {
        msf::HookCoordinator coordinator;
        msf::ConfigValues config;
        config.globalSensitivity = 2.0;
        config.mouseXAxisMultiplier = 0.5;
        config.mouseYAxisMultiplier = 1.5;

        const auto [mouseX, mouseY] = coordinator.ApplyTransform(4.0F, -2.0F, config, false);
        CHECK(Near(mouseX, 4.0F));
        CHECK(Near(mouseY, -6.0F));
        // Exact FP / TP only — never infer FP from !TP.
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false, false));
        CHECK(coordinator.ShouldApplyInputTransform(config, false, true, false));
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, false, false));
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, true, false));

        config.enableThirdPersonHook = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, true, false));
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false, false));
        config.enableThirdPersonHook = true;
        config.enableFirstPersonHook = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, false, false));
        CHECK(coordinator.ShouldApplyInputTransform(config, false, true, false));
        config.enableFirstPersonHook = true;
        config.affectGamepadLook = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, false, true));
        config.enabled = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, false, false));
        CHECK(!coordinator.ShouldRemoveThirdPersonSmoothing(config));
    }

    void TestGamepadTransformDefaultsToAxisParity()
    {
        msf::HookCoordinator coordinator;
        msf::ConfigValues config;

        const auto [defaultX, defaultY] = coordinator.ApplyTransform(0.5F, -0.5F, config, true);
        CHECK(Near(defaultX, 0.5F));
        CHECK(Near(defaultY, -0.5F));

        config.globalSensitivity = 2.0;
        config.gamepadXAxisMultiplier = 0.5;
        config.gamepadYAxisMultiplier = 0.25;
        const auto [scaledX, scaledY] = coordinator.ApplyTransform(0.5F, -0.5F, config, true);
        CHECK(Near(scaledX, 0.5F));
        CHECK(Near(scaledY, -0.25F));
    }

    void TestBowAimMouseDeltasUseSampledXAndCurrentEngineY()
    {
        msf::ConfigValues config;
        config.globalSensitivity = 2.0;
        config.mouseYAxisMultiplier = 0.75;

        const auto [bowAdjustedX, bowAdjustedY] = msf::ApplyBowAimMouseDeltas(
            3.0F,
            1.0F,
            -4.0F,
            2.0F,
            0.5F,
            0.5F);
        CHECK(Near(bowAdjustedX, 3.0F));
        CHECK(Near(bowAdjustedY, -2.0F));

        msf::HookCoordinator coordinator;
        const auto [outputX, outputY] = coordinator.ApplyTransform(bowAdjustedX, bowAdjustedY, config, false);
        CHECK(Near(outputX, 6.0F));
        CHECK(Near(outputY, -3.0F));

        const auto [fallbackX, currentY] = msf::ApplyBowAimMouseDeltas(
            0.5F,
            4.0F,
            -6.0F,
            2.0F,
            0.25F,
            0.5F);
        CHECK(Near(fallbackX, 1.0F));
        CHECK(Near(currentY, -3.0F));
    }

    void TestBowAimVerticalMultiplierPreservesConfiguredAxisParity()
    {
        CHECK(Near(msf::CalculateBowAimVerticalMultiplier(false, 0.75F), 1.0F));
        CHECK(Near(msf::CalculateBowAimVerticalMultiplier(true, 0.75F), 0.75F));
        CHECK(Near(msf::CalculateBowAimVerticalMultiplier(true, 1.0F), 1.0F));
    }

    void TestPitchTargetNormalizationUsesFreelookGain()
    {
        CHECK(Near(msf::NormalizePitchTargetDelta(-20.0F, -2.1F, 0.08F, true), -1.6F));
        CHECK(Near(msf::NormalizePitchTargetDelta(15.0F, 1.0F, 0.08F, true), 1.2F));
        CHECK(Near(msf::NormalizePitchTargetDelta(-20.0F, -2.1F, 0.08F, false), -2.1F));
        CHECK(Near(msf::NormalizePitchTargetDelta(0.0F, 0.5F, 0.08F, true), 0.0F));
        CHECK(Near(msf::NormalizePitchTargetDelta(10.0F, 0.5F, 0.0F, true), 0.5F));
        // Third-person freeRotation.y can seed a negative gain; preserve signed scale.
        CHECK(Near(msf::NormalizePitchTargetDelta(-20.0F, 1.6F, -0.08F, true), 1.6F));
        CHECK(Near(msf::NormalizePitchTargetDelta(15.0F, -1.0F, -0.08F, true), -1.2F));
    }

    void TestSampledScaleUpdatesOnlyFromTrueFreelook()
    {
        CHECK(msf::ShouldUpdateFreelookSampledScale(false, true, false, false));
        CHECK(msf::ShouldUpdateFreelookSampledScale(true, true, false, false));
        CHECK(!msf::ShouldUpdateFreelookSampledScale(true, false, false, false));
        CHECK(!msf::ShouldUpdateFreelookSampledScale(true, false, true, false));
        CHECK(!msf::ShouldUpdateFreelookSampledScale(true, false, true, true));
        CHECK(!msf::ShouldUpdateFreelookSampledScale(false, true, false, true));
    }

    void TestFreelookScaleSamplesRejectConsumedAndOutlierInput()
    {
        float scale = 0.0F;
        float pending = 0.0F;
        std::uint32_t pendingCount = 0;

        CHECK(!msf::UpdateFreelookScaleSample(1.0F, 10.0F, scale, pending, pendingCount));
        CHECK(Near(scale, 0.0F));
        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 4.0F, scale, pending, pendingCount));
        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 4.0F, scale, pending, pendingCount));
        CHECK(msf::UpdateFreelookScaleSample(4.0F, 4.0F, scale, pending, pendingCount));
        CHECK(Near(scale, 1.0F));

        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 0.0F, scale, pending, pendingCount));
        CHECK(!msf::UpdateFreelookScaleSample(4.0F, -4.0F, scale, pending, pendingCount));
        CHECK(Near(scale, 1.0F));
        CHECK(pendingCount == 1);
        CHECK(msf::UpdateFreelookScaleSample(4.0F, 4.0F, scale, pending, pendingCount));
        CHECK(Near(scale, 1.0F));
        CHECK(pendingCount == 0);

        CHECK(!msf::UpdateFreelookScaleSample(4.0F, -4.0F, scale, pending, pendingCount));
        CHECK(!msf::UpdateFreelookScaleSample(4.0F, -4.0F, scale, pending, pendingCount));
        CHECK(msf::UpdateFreelookScaleSample(4.0F, -4.0F, scale, pending, pendingCount));
        CHECK(Near(scale, -1.0F));

        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 16.0F, scale, pending, pendingCount));
        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 16.0F, scale, pending, pendingCount));
        CHECK(msf::UpdateFreelookScaleSample(4.0F, 16.0F, scale, pending, pendingCount));
        CHECK(Near(scale, 4.0F));

        CHECK(!msf::UpdateFreelookScaleSample(4.0F, 80.0F, scale, pending, pendingCount));
        CHECK(!msf::UpdateFreelookScaleSample(0.5F, 0.5F, scale, pending, pendingCount));
        CHECK(Near(scale, 4.0F));
    }

    void TestNormalAimFovUpdatesOnlyFromFreelook()
    {
        CHECK(msf::ShouldUpdateNormalAimFov(false, false, false, 3.604575F));
        CHECK(!msf::ShouldUpdateNormalAimFov(true, false, false, 3.340328F));
        CHECK(!msf::ShouldUpdateNormalAimFov(true, true, false, 2.003606F));
        CHECK(!msf::ShouldUpdateNormalAimFov(true, true, true, 2.003606F));
        CHECK(!msf::ShouldUpdateNormalAimFov(false, false, false, 0.0F));
    }

    void TestFovDegreesFromFrustumEdges()
    {
        // Symmetric tangent slopes of +1 and -1 represent ±45°, or a 90° field of view.
        CHECK(Near(msf::FovDegreesFromFrustumEdges(1.0F, -1.0F), 90.0F));
        CHECK(Near(msf::FovDegreesFromFrustumEdges(0.5F, -0.5F), 53.1301F));
        CHECK(Near(msf::FovDegreesFromFrustumEdges(0.0F, 0.0F), 0.0F));
        CHECK(Near(msf::FovDegreesFromFrustumEdges(-1.0F, 1.0F), 0.0F));
    }

    void TestLiveCompatibilityPolicyUpdates()
    {
        TemporaryDirectory directory;
        WriteText(directory.Path() / "SmoothCam.DLL", "");

        msf::CompatibilityManager compatibility;
        compatibility.ScanInstalledCameraMods(directory.Path());

        msf::ConfigValues config;
        // Default: keep CMC third-person smoothing removal with camera mods.
        auto policy = compatibility.EvaluatePolicy(config);
        CHECK(policy.mode == msf::CompatibilityMode::Safe);
        CHECK(policy.installInputHooks);
        CHECK(policy.allowThirdPersonSmoothingIntervention);

        msf::HookCoordinator coordinator;
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(!coordinator.UpdatePolicy(policy));
        CHECK(coordinator.ShouldApplyInputTransform(config, false, true, false));
        CHECK(coordinator.ShouldRemoveThirdPersonSmoothing(config));

        // Legacy reduced-intervention: skip CMC smoothing removal when camera mods are present.
        config.keepThirdPersonSmoothingRemovalWithCameraMods = false;
        policy = compatibility.EvaluatePolicy(config);
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
        CHECK(policy.installInputHooks);
        CHECK(!policy.allowThirdPersonSmoothingIntervention);
        CHECK(!coordinator.ShouldRemoveThirdPersonSmoothing(config));
        CHECK(coordinator.ShouldApplyInputTransform(config, false, true, false));

        policy.installInputHooks = false;
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, false, false));
    }

    void TestCompatibilityPolicyCoversKnownCameraStacks()
    {
        msf::ConfigValues config;
        config.keepThirdPersonSmoothingRemovalWithCameraMods = false;

        {
            TemporaryDirectory directory;
            WriteText(directory.Path() / "ImprovedCameraSE.dll", "");
            msf::CompatibilityManager compatibility;
            compatibility.ScanInstalledCameraMods(directory.Path());
            const auto policy = compatibility.EvaluatePolicy(config);
            CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
            CHECK(policy.reason == "Improved Camera detected; third-person smoothing delegated.");
        }

        {
            TemporaryDirectory directory;
            WriteText(directory.Path() / "ImprovedCameraSE.dll", "");
            WriteText(directory.Path() / "SmoothCam.dll", "");
            msf::CompatibilityManager compatibility;
            compatibility.ScanInstalledCameraMods(directory.Path());
            const auto policy = compatibility.EvaluatePolicy(config);
            CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
            CHECK(policy.reason == "SmoothCam and Improved Camera detected; third-person smoothing delegated.");
        }

        {
            TemporaryDirectory directory;
            WriteText(directory.Path() / "SmoothCamSSE.dll", "");
            msf::CompatibilityManager compatibility;
            compatibility.ScanInstalledCameraMods(directory.Path());
            const auto policy = compatibility.EvaluatePolicy(config);
            CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
            CHECK(policy.reason == "SmoothCam detected; third-person smoothing delegated.");
        }

        {
            TemporaryDirectory directory;
            WriteText(directory.Path() / "SmoothCamAEPre629.dll", "");
            msf::CompatibilityManager compatibility;
            compatibility.ScanInstalledCameraMods(directory.Path());
            const auto policy = compatibility.EvaluatePolicy(config);
            CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
            CHECK(policy.reason == "SmoothCam detected; third-person smoothing delegated.");
        }

        {
            // Bogus legacy name must not count as SmoothCam.
            TemporaryDirectory directory;
            WriteText(directory.Path() / "SmoothCamSE.dll", "");
            msf::CompatibilityManager compatibility;
            compatibility.ScanInstalledCameraMods(directory.Path());
            const auto policy = compatibility.EvaluatePolicy(config);
            CHECK(policy.mode == msf::CompatibilityMode::Safe);
            CHECK(policy.allowThirdPersonSmoothingIntervention);
        }
    }

    void TestHalfRateYawRestoration()
    {
        constexpr float pi = 3.14159265358979323846F;
        constexpr float lookX = 0.6F;
        constexpr float delta = 1.0F / 60.0F;
        constexpr float expected = lookX * delta * pi;

        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.5F, true), expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(-lookX, delta, -expected * 0.5F, true), -expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.48F, true), expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.52F, true), expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.49F, true), expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.47F, true), expected * 0.47F));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.53F, true), expected * 0.53F));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected, true), expected));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.75F, true), expected * 0.75F));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, delta, expected * 0.5F, false), expected * 0.5F));
        CHECK(Near(msf::RestoreHalfRateYawDelta(0.0F, delta, 0.25F, true), 0.25F));
        CHECK(Near(msf::RestoreHalfRateYawDelta(lookX, 0.0F, 0.25F, true), 0.25F));
    }

    void TestTimeDilatedYawCompensation()
    {
        constexpr float halfRateRestored = 0.001739F;
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 1.0F), halfRateRestored));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.95F), halfRateRestored));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.25F), halfRateRestored / 0.25F));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.5F), halfRateRestored / 0.5F));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.05F), halfRateRestored));
        CHECK(Near(
            msf::CompensateTimeDilatedYawDelta(
                halfRateRestored,
                std::nextafter(0.05F, 1.0F)),
            halfRateRestored / std::nextafter(0.05F, 1.0F)));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.04F), halfRateRestored));
        CHECK(Near(
            msf::CompensateTimeDilatedYawDelta(
                halfRateRestored,
                std::nextafter(0.90F, 0.0F)),
            halfRateRestored / std::nextafter(0.90F, 0.0F)));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.90F), halfRateRestored));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, 0.0F), halfRateRestored));
        CHECK(Near(msf::CompensateTimeDilatedYawDelta(halfRateRestored, -1.0F), halfRateRestored));
        CHECK(Near(
            msf::CompensateTimeDilatedYawDelta(
                halfRateRestored,
                std::numeric_limits<float>::infinity()),
            halfRateRestored));
        CHECK(Near(
            msf::CompensateTimeDilatedYawDelta(
                halfRateRestored,
                std::numeric_limits<float>::quiet_NaN()),
            halfRateRestored));
        CHECK(std::isnan(msf::CompensateTimeDilatedYawDelta(
            std::numeric_limits<float>::quiet_NaN(),
            0.25F)));

        constexpr float lookX = 0.156279F;
        constexpr float gameDelta = 0.003542F;
        constexpr float timeMult = 0.25F;
        constexpr float pi = 3.14159265358979323846F;
        const float engineHalfRate = lookX * gameDelta * pi * 0.5F;
        const float afterHalfRate = msf::RestoreHalfRateYawDelta(lookX, gameDelta, engineHalfRate, true);
        const float wallClock = msf::CompensateTimeDilatedYawDelta(afterHalfRate, timeMult);
        const float expectedWallClock = lookX * (gameDelta / timeMult) * pi;
        CHECK(Near(afterHalfRate, lookX * gameDelta * pi));
        CHECK(Near(wallClock, expectedWallClock));
    }

    void TestFirstPersonYawCorrectionComposition()
    {
        constexpr float pi = 3.14159265358979323846F;
        constexpr float lookX = 0.156279F;
        constexpr float gameDelta = 0.003542F;
        constexpr float timeMult = 0.25F;
        const float engineHalfRate = lookX * gameDelta * pi * 0.5F;

        const auto eagleEye = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            true,
            true);
        CHECK(Near(eagleEye.yawDelta, lookX * (gameDelta / timeMult) * pi));
        CHECK(eagleEye.halfRateRestored);
        CHECK(eagleEye.timeCompensated);

        const auto normalTime = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            1.0F,
            true,
            true);
        CHECK(Near(normalTime.yawDelta, lookX * gameDelta * pi));
        CHECK(normalTime.halfRateRestored);
        CHECK(!normalTime.timeCompensated);

        const float engineFullRate = lookX * gameDelta * pi;
        const auto slowTimeFullRate = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineFullRate,
            timeMult,
            false,
            true);
        CHECK(Near(slowTimeFullRate.yawDelta, engineFullRate / timeMult));
        CHECK(!slowTimeFullRate.halfRateRestored);
        CHECK(slowTimeFullRate.timeCompensated);

        // Split flags: timeComp alone must not restore half-rate.
        const auto timeOnlyHalfEngine = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            false,
            true);
        CHECK(Near(timeOnlyHalfEngine.yawDelta, engineHalfRate / timeMult));
        CHECK(!timeOnlyHalfEngine.halfRateRestored);
        CHECK(timeOnlyHalfEngine.timeCompensated);

        // Split flags: half-rate alone must not apply timeComp.
        const auto halfOnly = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            true,
            false);
        CHECK(Near(halfOnly.yawDelta, lookX * gameDelta * pi));
        CHECK(halfOnly.halfRateRestored);
        CHECK(!halfOnly.timeCompensated);

        const auto ineligible = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            false,
            false);
        CHECK(Near(ineligible.yawDelta, engineHalfRate));
        CHECK(!ineligible.halfRateRestored);
        CHECK(!ineligible.timeCompensated);
    }

    void TestHalfRateYawEligibilityIncludesBowAim()
    {
        // (enabled, fpHook, inFP, inTP, looking) — sprint/bow are telemetry only.
        CHECK(msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, true, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, true, false, false));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, false, true, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(false, true, true, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, false, true, false, true));
        // Neither camera must not unlock half-rate via !TP → FP inference.
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, false, false, true));
        // Both-true must not unlock half-rate.
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, true, true, true));
    }

    void TestThirdPersonSlowTimeYawEligibilityIsExact()
    {
        // looking=true; bow is irrelevant for TP slow-time compensation.
        // (enabled, tpHook, inFP, inTP, looking, timeMult)
        CHECK(msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, true, true, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, true, true, 1.0F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, true, true, 0.05F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, true, true, 0.90F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, true, false, true, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, true, false, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, false, false, true, true, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(false, true, false, true, true, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, false, false, true, 0.25F));
        // Both-true must not unlock timeComp.
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(true, true, true, true, true, 0.25F));
        CHECK(!msf::ShouldCorrectThirdPersonSlowTimeYaw(
            true,
            true,
            false,
            true,
            true,
            std::numeric_limits<float>::quiet_NaN()));
    }

    msf::LookCorrectionContext MakeLookContext(
        bool firstPerson,
        bool thirdPerson,
        bool sprinting,
        bool bowAiming,
        bool looking,
        float timeMult) noexcept
    {
        msf::LookCorrectionContext ctx{};
        ctx.firstPerson = firstPerson;
        ctx.thirdPerson = thirdPerson;
        ctx.sprinting = sprinting;
        ctx.bowAiming = bowAiming;
        ctx.bowOut = bowAiming;
        ctx.looking = looking;
        ctx.timeMult = timeMult;
        ctx.timeDilated = msf::IsGlobalTimeDilatedForYaw(timeMult);
        ctx.lookControlsEnabled = true;
        ctx.menuMode = false;
        return ctx;
    }

    void TestOrphanHalfRateAndFreelookEmaGuards()
    {
        constexpr float pi = 3.14159265358979323846F;
        constexpr float lookX = 0.6F;
        constexpr float delta = 1.0F / 60.0F;
        constexpr float expected = lookX * delta * pi;
        const float halfEngine = expected * 0.5F;

        CHECK(msf::IsObservedHalfRateScale(0.5F));
        CHECK(msf::IsObservedHalfRateScale(0.48F));
        CHECK(msf::IsObservedHalfRateScale(0.52F));
        CHECK(!msf::IsObservedHalfRateScale(0.47F));
        CHECK(!msf::IsObservedHalfRateScale(1.0F));
        CHECK(!msf::IsObservedHalfRateScale(std::numeric_limits<float>::quiet_NaN()));

        CHECK(Near(msf::ComputeObservedYawScale(lookX, delta, halfEngine), 0.5F));

        // Orphan FP freelook looking is policy-eligible; band + streak gate apply.
        const auto orphanPolicy = msf::EvaluateLookCorrectionPolicy(
            MakeLookContext(true, false, false, false, true, 1.0F),
            true,
            true,
            true);
        CHECK(orphanPolicy.restoreHalfRateYaw);

        std::uint32_t streak = 0;
        streak = msf::UpdateHalfRateBandStreak(true, true, streak);
        CHECK(streak == 1);
        CHECK(!msf::ShouldApplyHalfRateRestore(true, true, streak, false, 2));
        streak = msf::UpdateHalfRateBandStreak(true, true, streak);
        CHECK(streak == 2);
        CHECK(msf::ShouldApplyHalfRateRestore(true, true, streak, false, 2));
        // Sprint/bow hints restore on the first in-band frame.
        CHECK(msf::ShouldApplyHalfRateRestore(true, true, 1, true, 2));
        streak = msf::UpdateHalfRateBandStreak(true, false, streak);
        CHECK(streak == 0);
        // In-band while policy-ineligible must not accumulate a streak for re-enable.
        streak = msf::UpdateHalfRateBandStreak(false, true, 5);
        CHECK(streak == 0);
        CHECK(!msf::ShouldApplyHalfRateRestore(false, true, 2, false, 2));

        // EMA must reject half-scale freelook samples and casting.
        CHECK(msf::ShouldUpdateFreelookYawEma(true, false, false, 1.0F));
        CHECK(!msf::ShouldUpdateFreelookYawEma(true, false, false, 0.5F));
        CHECK(!msf::ShouldUpdateFreelookYawEma(true, false, true, 1.0F));
        CHECK(!msf::ShouldUpdateFreelookYawEma(true, true, false, 1.0F));
        CHECK(!msf::ShouldUpdateFreelookYawEma(false, false, false, 1.0F));
    }

    void TestTimeCompAgreementGate()
    {
        constexpr float pi = 3.14159265358979323846F;

        // Settled EE: delta ≈ real * Current → agree≈1 → ScaleByCurrent.
        CHECK(Near(msf::ComputeTimeDeltaAgreement(0.004167F, 0.016668F, 0.25F), 1.0F));
        CHECK(msf::IsTimeDeltaAgreementAcceptable(1.0F, 0.12F));
        CHECK(msf::IsTimeDeltaAgreementAcceptable(1.10F, 0.12F));
        CHECK(!msf::IsTimeDeltaAgreementAcceptable(0.80F, 0.12F));

        msf::TimeCompSkipReason reason = msf::TimeCompSkipReason::None;
        CHECK(msf::ShouldApplyTimeCompYaw(
            true,
            0.004167F,
            0.016668F,
            0.25F,
            0.25F,
            true,
            &reason));
        CHECK(reason == msf::TimeCompSkipReason::None);
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.004167F,
                0.016668F,
                0.25F,
                0.25F,
                true,
                &reason) == msf::TimeCompMode::ScaleByCurrent);
        CHECK(reason == msf::TimeCompSkipReason::None);

        // Ramp disagree (playtest): delta already ~0.25× wall, Current still ~0.89.
        // yaw/Current would leave ~0.28× wall — MUST RewriteWallClock, never ScaleByCurrent.
        CHECK(!msf::ShouldApplyTimeCompYaw(
            true,
            0.004167F,
            0.016667F,
            0.885843F,
            0.885843F,
            true,
            &reason));
        CHECK(reason == msf::TimeCompSkipReason::Disagree);
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.004167F,
                0.016667F,
                0.885843F,
                0.885843F,
                true,
                &reason) == msf::TimeCompMode::RewriteWallClock);
        CHECK(reason == msf::TimeCompSkipReason::Disagree);

        // Collapsed disagree: delta==rtd while Current dilated → wall rewrite (~identity).
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.016667F,
                0.016667F,
                0.304749F,
                0.304749F,
                true,
                &reason) == msf::TimeCompMode::RewriteWallClock);
        CHECK(reason == msf::TimeCompSkipReason::Disagree);

        // Unstable Current with valid rtd → RewriteWallClock (not ScaleByCurrent).
        CHECK(!msf::ShouldApplyTimeCompYaw(
            true,
            0.004167F,
            0.016668F,
            0.25F,
            0.50F,
            true,
            &reason,
            0.12F,
            0.08F));
        CHECK(reason == msf::TimeCompSkipReason::Unstable);
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.004167F,
                0.016668F,
                0.25F,
                0.50F,
                true,
                &reason,
                0.12F,
                0.08F) == msf::TimeCompMode::RewriteWallClock);
        CHECK(reason == msf::TimeCompSkipReason::Unstable);
        CHECK(std::string_view{ msf::TimeCompSkipReasonName(reason) } == "unstable");
        CHECK(std::string_view{ msf::TimeCompModeName(msf::TimeCompMode::RewriteWallClock) } == "wall");

        // Missing realTimeDelta must not ScaleByCurrent during transitions.
        CHECK(!msf::ShouldApplyTimeCompYaw(
            true,
            0.004167F,
            0.0F,
            0.25F,
            0.25F,
            true,
            &reason,
            0.12F,
            0.08F,
            1,
            3));
        CHECK(reason == msf::TimeCompSkipReason::MissingWallClock);
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.004167F,
                0.0F,
                0.25F,
                0.25F,
                true,
                &reason,
                0.12F,
                0.08F,
                1,
                3) == msf::TimeCompMode::None);
        CHECK(reason == msf::TimeCompSkipReason::MissingWallClock);

        // Settled dilation without wall may ScaleByCurrent after stable streak.
        CHECK(msf::ShouldApplyTimeCompYaw(
            true,
            0.004167F,
            0.0F,
            0.25F,
            0.25F,
            true,
            &reason,
            0.12F,
            0.08F,
            3,
            3));
        CHECK(reason == msf::TimeCompSkipReason::None);
        CHECK(
            msf::ResolveTimeCompMode(
                true,
                0.004167F,
                0.0F,
                0.25F,
                0.25F,
                true,
                &reason,
                0.12F,
                0.08F,
                3,
                3) == msf::TimeCompMode::ScaleByCurrent);

        // Wall rewrite composition: after half-rate, yaw = lookX * rtd * π.
        constexpr float lookX = 2.0F;
        constexpr float gameDelta = 0.004167F;
        constexpr float realTimeDelta = 0.016667F;
        constexpr float rampTimeMult = 0.885843F;
        const float engineYaw = lookX * gameDelta * pi;
        const auto wallRewrite = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineYaw,
            rampTimeMult,
            false,
            msf::TimeCompMode::RewriteWallClock,
            realTimeDelta);
        CHECK(Near(wallRewrite.yawDelta, lookX * realTimeDelta * pi));
        CHECK(wallRewrite.timeCompensated);
        CHECK(wallRewrite.timeCompMode == msf::TimeCompMode::RewriteWallClock);
        // Contrast: ScaleByCurrent on the same ramp frame leaves ~0.28× wall.
        const auto badScale = msf::ApplyPlayerYawCorrection(
            lookX,
            gameDelta,
            engineYaw,
            rampTimeMult,
            false,
            msf::TimeCompMode::ScaleByCurrent,
            realTimeDelta);
        CHECK(Near(badScale.yawDelta, engineYaw / rampTimeMult));
        CHECK(std::abs(badScale.yawDelta) < std::abs(wallRewrite.yawDelta) * 0.5F);

        // Pitch pause when dilated+looking yaw was left uncompensated (no rtd).
        CHECK(msf::ShouldPausePitchNormalizeForDilatedYaw(true, true, false));
        CHECK(!msf::ShouldPausePitchNormalizeForDilatedYaw(true, true, true));
        CHECK(!msf::ShouldPausePitchNormalizeForDilatedYaw(true, false, false));
        CHECK(!msf::ShouldPausePitchNormalizeForDilatedYaw(false, true, false));

        CHECK(Near(msf::RewriteWallClockYawDelta(lookX, realTimeDelta), lookX * realTimeDelta * pi));
        CHECK(Near(msf::RewriteWallClockYawDelta(lookX, 0.0F), 0.0F));

        CHECK(msf::IsGlobalTimeMultStable(0.25F, 0.25F, true, 0.08F));
        CHECK(!msf::IsGlobalTimeMultStable(0.25F, 0.50F, true, 0.08F));
        CHECK(msf::IsGlobalTimeMultStable(0.25F, 1.0F, false, 0.08F));
    }

    void TestLookCorrectionPolicyMatrix()
    {
        constexpr float timeMults[] = { 1.0F, 0.25F, 0.05F, 0.90F };

        for (const float timeMult : timeMults) {
            const bool dilated = msf::IsGlobalTimeDilatedForYaw(timeMult);
            CHECK(dilated == (timeMult > 0.05F && timeMult < 0.90F));

            // FP freelook looking — orphan half-rate eligible; band still gates restore
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(true, false, false, false, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // FP sprint looking
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(true, false, true, false, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // FP bow aim looking
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(true, false, false, true, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // FP bow aim idle (not looking): half-rate and timeComp both need looking
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(true, false, false, true, false, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(!policy.compensateTimeYaw);
            }

            // TP freelook looking — timeComp not bow-gated; never half-rate
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(false, true, false, false, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // TP bow aim looking — still no half-rate; timeComp when dilated
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(false, true, false, true, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // TP sprint looking — sprint does not unlock half-rate outside FP
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(false, true, true, false, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(policy.compensateTimeYaw == dilated);
            }

            // Neither camera — no half-rate, no timeComp
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(false, false, true, true, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(!policy.compensateTimeYaw);
            }

            // Both-true person flags — reject half-rate and timeComp (exclusive FP|TP).
            {
                const auto policy = msf::EvaluateLookCorrectionPolicy(
                    MakeLookContext(true, true, true, true, true, timeMult),
                    true,
                    true,
                    true);
                CHECK(!policy.restoreHalfRateYaw);
                CHECK(!policy.compensateTimeYaw);
            }
        }

        // Camera hook gates
        {
            const auto fpDisabled = msf::EvaluateLookCorrectionPolicy(
                MakeLookContext(true, false, true, true, true, 0.25F),
                true,
                false,
                true);
            CHECK(!fpDisabled.restoreHalfRateYaw);
            CHECK(!fpDisabled.compensateTimeYaw);

            const auto tpDisabled = msf::EvaluateLookCorrectionPolicy(
                MakeLookContext(false, true, false, false, true, 0.25F),
                true,
                true,
                false);
            CHECK(!tpDisabled.restoreHalfRateYaw);
            CHECK(!tpDisabled.compensateTimeYaw);
        }

        // Optional menu / look-control gates
        {
            auto menuCtx = MakeLookContext(true, false, true, true, true, 0.25F);
            menuCtx.menuMode = true;
            const auto menuBlocked = msf::EvaluateLookCorrectionPolicy(
                menuCtx,
                true,
                true,
                true,
                true,
                false);
            CHECK(!menuBlocked.restoreHalfRateYaw);
            CHECK(!menuBlocked.compensateTimeYaw);

            auto lookDisabledCtx = MakeLookContext(false, true, false, false, true, 0.25F);
            lookDisabledCtx.lookControlsEnabled = false;
            const auto lookBlocked = msf::EvaluateLookCorrectionPolicy(
                lookDisabledCtx,
                true,
                true,
                true,
                false,
                true);
            CHECK(!lookBlocked.restoreHalfRateYaw);
            CHECK(!lookBlocked.compensateTimeYaw);
        }

        // Composition under split policy: TP freelook slow-time applies timeComp only
        {
            constexpr float pi = 3.14159265358979323846F;
            constexpr float lookX = 0.2F;
            constexpr float gameDelta = 1.0F / 60.0F;
            constexpr float timeMult = 0.25F;
            const float engineFullRate = lookX * gameDelta * pi;
            const auto policy = msf::EvaluateLookCorrectionPolicy(
                MakeLookContext(false, true, false, false, true, timeMult),
                true,
                true,
                true);
            CHECK(!policy.restoreHalfRateYaw);
            CHECK(policy.compensateTimeYaw);
            const auto corrected = msf::ApplyPlayerYawCorrection(
                lookX,
                gameDelta,
                engineFullRate,
                timeMult,
                policy.restoreHalfRateYaw,
                policy.compensateTimeYaw);
            CHECK(Near(corrected.yawDelta, engineFullRate / timeMult));
            CHECK(!corrected.halfRateRestored);
            CHECK(corrected.timeCompensated);
        }
    }

    void TestBowAimMousePathIsFirstPersonOnly()
    {
        CHECK(msf::ShouldApplyBowAimMousePath(true, true));
        CHECK(!msf::ShouldApplyBowAimMousePath(false, true));
        CHECK(!msf::ShouldApplyBowAimMousePath(true, false));
        CHECK(!msf::ShouldApplyBowAimMousePath(false, false));
    }

    void TestLookCameraClassificationDoesNotInferFirstPerson()
    {
        const auto neither = msf::ClassifyLookCameraPerson(false, false);
        CHECK(!neither.firstPerson);
        CHECK(!neither.thirdPerson);

        const auto first = msf::ClassifyLookCameraPerson(true, false);
        CHECK(first.firstPerson);
        CHECK(!first.thirdPerson);

        const auto third = msf::ClassifyLookCameraPerson(false, true);
        CHECK(!third.firstPerson);
        CHECK(third.thirdPerson);

        // Null / non-FP/TP camera must not unlock half-rate or timeComp.
        msf::LookCorrectionContext neitherCtx{};
        neitherCtx.firstPerson = neither.firstPerson;
        neitherCtx.thirdPerson = neither.thirdPerson;
        neitherCtx.sprinting = true;
        neitherCtx.bowAiming = true;
        neitherCtx.looking = true;
        neitherCtx.timeMult = 0.25F;
        neitherCtx.timeDilated = msf::IsGlobalTimeDilatedForYaw(0.25F);
        const auto neitherPolicy = msf::EvaluateLookCorrectionPolicy(
            neitherCtx,
            true,
            true,
            true);
        CHECK(!neitherPolicy.restoreHalfRateYaw);
        CHECK(!neitherPolicy.compensateTimeYaw);
    }

    void TestLookingDefinitionAndTimeMultRelocationIds()
    {
        CHECK(!msf::IsLookingForYawCorrection(0.0F));
        CHECK(!msf::IsLookingForYawCorrection(0.000009F));
        CHECK(msf::IsLookingForYawCorrection(0.00001F));
        CHECK(msf::IsLookingForYawCorrection(-0.00001F));
        CHECK(msf::IsLookingForYawCorrection(0.5F));

        // Guard against regressing to QGlobalTimeMultiplierTarget.
        CHECK(msf::kBsTimerGlobalTimeMultCurrentSe == 511882ULL);
        CHECK(msf::kBsTimerGlobalTimeMultCurrentAeVr == 388442ULL);
        CHECK(msf::kBsTimerGlobalTimeMultTargetSe == 511883ULL);
        CHECK(msf::kBsTimerGlobalTimeMultTargetAeVr == 388443ULL);
        CHECK(msf::kBsTimerGlobalTimeMultCurrentSe != msf::kBsTimerGlobalTimeMultTargetSe);
        CHECK(msf::kBsTimerGlobalTimeMultCurrentAeVr != msf::kBsTimerGlobalTimeMultTargetAeVr);
        CHECK(msf::kBsTimerDeltaOffset == 0x18U);
        CHECK(msf::kBsTimerRealTimeDeltaOffset == 0x1CU);
    }

    void TestFreelookScaleCacheRequiresExactPerson()
    {
        CHECK(msf::SelectFreelookScaleCache(true, false) ==
              msf::FreelookScaleCacheKind::FirstPerson);
        CHECK(msf::SelectFreelookScaleCache(false, true) ==
              msf::FreelookScaleCacheKind::ThirdPerson);
        CHECK(msf::SelectFreelookScaleCache(false, false) ==
              msf::FreelookScaleCacheKind::None);
        CHECK(msf::SelectFreelookScaleCache(true, true) ==
              msf::FreelookScaleCacheKind::None);
    }

    void TestPitchNormalizeFreelookAndSprintGates()
    {
        // Freelook aim state never normalizes — even when !eligible (WantToDraw/Drawing).
        CHECK(msf::IsTrueFreelookPitchEnvironment("freelook", true));
        CHECK(msf::IsTrueFreelookPitchEnvironment("freelook", false));
        CHECK(!msf::IsTrueFreelookPitchEnvironment("bowPull", true));
        CHECK(!msf::IsTrueFreelookPitchEnvironment("eagleEye", true));
        CHECK(!msf::IsTrueFreelookPitchEnvironment("bowOut", true));

        // Calibration still requires eligible + !sprint.
        CHECK(msf::IsTrueFreelookPitchBaselineEligible("freelook", true, false));
        CHECK(!msf::IsTrueFreelookPitchBaselineEligible("freelook", true, true));
        CHECK(!msf::IsTrueFreelookPitchBaselineEligible("freelook", false, false));
        CHECK(!msf::IsTrueFreelookPitchBaselineEligible("bowPull", true, false));

        CHECK(!msf::IsPitchNormalizeSettled(0, 3));
        CHECK(!msf::IsPitchNormalizeSettled(2, 3));
        CHECK(msf::IsPitchNormalizeSettled(3, 3));
        CHECK(msf::IsPitchNormalizeSettled(10, 3));
    }

    void TestSampledLoggingPolicy()
    {
        CHECK(!msf::ShouldEmitSampledLog(false, 600, 600));
        CHECK(!msf::ShouldEmitSampledLog(true, 0, 600));
        CHECK(!msf::ShouldEmitSampledLog(true, 1, 600));
        CHECK(msf::ShouldEmitSampledLog(true, 1, 120, true));
        CHECK(!msf::ShouldEmitSampledLog(true, 119, 120, true));
        CHECK(msf::ShouldEmitSampledLog(true, 120, 120, true));
        CHECK(!msf::ShouldEmitSampledLog(true, 121, 120, true));
        CHECK(msf::ShouldEmitSampledLog(true, 600, 600));
        CHECK(!msf::ShouldEmitSampledLog(true, 600, 0));
    }

    void TestConfigLoadClampSaveAndReload()
    {
        TemporaryDirectory directory;
        const auto iniPath = directory.Path() / "MouseSensitivityFix.ini";
        WriteText(
            iniPath,
            "[General]\n"
            "bEnabled=true\n"
            "bAffectGamepadLook=false\n"
            "fGlobalSensitivity=0.001\n"
            "fMouseXAxisMultiplier=99\n"
            "[Advanced]\n"
            "iFocusSpikeGapMs=9000\n");

        auto& manager = msf::ConfigManager::Get();
        int callbackCount = 0;
        manager.SetChangeCallback([&callbackCount](const msf::ConfigValues&) {
            ++callbackCount;
        });

        CHECK(manager.LoadFromIni(iniPath));
        auto values = manager.GetSnapshot();
        CHECK(callbackCount == 1);
        CHECK(!values.affectGamepadLook);
        CHECK(values.focusSpikeGapMs == 5000);
        CHECK(values.globalSensitivity == 0.01);
        CHECK(values.mouseXAxisMultiplier == 20.0);

        values.focusSpikeGapMs = 1;
        values.mouseYAxisMultiplier = 30.0;
        values.keepThirdPersonSmoothingRemovalWithCameraMods = false;
        manager.ApplyUiUpdate(values);
        values = manager.GetSnapshot();
        CHECK(callbackCount == 2);
        CHECK(values.focusSpikeGapMs == 50);
        CHECK(values.mouseYAxisMultiplier == 20.0);

        // NaN / Inf must keep previous finite values (not clamp into garbage).
        values.globalSensitivity = std::numeric_limits<double>::quiet_NaN();
        values.mouseXAxisMultiplier = std::numeric_limits<double>::infinity();
        manager.ApplyUiUpdate(values);
        values = manager.GetSnapshot();
        CHECK(callbackCount == 3);
        CHECK(values.globalSensitivity == 0.01);
        CHECK(values.mouseXAxisMultiplier == 20.0);

        WriteText(
            iniPath,
            "[General]\n"
            "fGlobalSensitivity=nan\n"
            "fMouseYAxisMultiplier=inf\n"
            "fGamepadXAxisMultiplier=1.5\n");
        CHECK(manager.LoadFromIni(iniPath));
        values = manager.GetSnapshot();
        CHECK(callbackCount == 4);
        CHECK(values.globalSensitivity == 0.01);
        CHECK(values.mouseYAxisMultiplier == 20.0);
        CHECK(values.gamepadXAxisMultiplier == 1.5);

        values.keepThirdPersonSmoothingRemovalWithCameraMods = false;
        manager.ApplyUiUpdate(values);
        CHECK(callbackCount == 5);
        CHECK(manager.SaveToIni(iniPath));

        const auto savedText = ReadText(iniPath);
        CHECK(savedText.find("bKeepThirdPersonSmoothingRemovalWithCameraMods=false") != std::string::npos);
        CHECK(savedText.find("bHotDisable") == std::string::npos);
        CHECK(savedText.find("bUseCompatibilityPresets") == std::string::npos);

        values.keepThirdPersonSmoothingRemovalWithCameraMods = true;
        manager.ApplyUiUpdate(values);
        CHECK(callbackCount == 6);
        CHECK(manager.LoadFromIni(iniPath));
        values = manager.GetSnapshot();
        CHECK(callbackCount == 7);
        CHECK(!values.keepThirdPersonSmoothingRemovalWithCameraMods);

        const auto knownWriteTime = std::filesystem::last_write_time(iniPath);
        WriteText(iniPath, "[General]\nbEnabled=false\niFocusSpikeGapMs=500\n");
        std::filesystem::last_write_time(iniPath, knownWriteTime + std::chrono::seconds(2));
        CHECK(manager.ReloadIfChanged());
        values = manager.GetSnapshot();
        CHECK(callbackCount == 8);
        CHECK(!values.enabled);
        CHECK(values.focusSpikeGapMs == 500);
        CHECK(!manager.ReloadIfChanged());

        manager.SetChangeCallback({});
    }

    void TestCompatibilityKeepsSmoothingWithoutCameraMods()
    {
        TemporaryDirectory directory;
        // Empty plugins directory: no camera mods detected.
        msf::CompatibilityManager compatibility;
        compatibility.ScanInstalledCameraMods(directory.Path());

        msf::ConfigValues config;
        config.keepThirdPersonSmoothingRemovalWithCameraMods = false;

        const auto policy = compatibility.EvaluatePolicy(config);
        CHECK(policy.mode == msf::CompatibilityMode::Safe);
        CHECK(policy.installInputHooks);
        CHECK(policy.allowThirdPersonSmoothingIntervention);
    }

    void TestLegacyConfigKeysRemainSafe()
    {
        TemporaryDirectory directory;
        const auto iniPath = directory.Path() / "MouseSensitivityFix.ini";
        WriteText(
            iniPath,
            "[General]\n"
            "bEnabled=true\n"
            "bHotDisable=true\n"
            "[Compatibility]\n"
            "bUseCompatibilityPresets=true\n"
            "bPresetImprovedCamera=true\n"
            "bPresetSmoothCam=true\n"
            "bDelegateThirdPersonWhenSmoothCam=true\n"
            "bDelegateThirdPersonWhenImprovedCamera=true\n"
            "bForceOverrideSmoothCam=false\n"
            "bForceOverrideImprovedCamera=false\n");

        auto& manager = msf::ConfigManager::Get();
        manager.SetChangeCallback({});
        CHECK(manager.LoadFromIni(iniPath));
        const auto values = manager.GetSnapshot();
        CHECK(!values.enabled);
        CHECK(!values.keepThirdPersonSmoothingRemovalWithCameraMods);

        WriteText(
            iniPath,
            "[Compatibility]\n"
            "bUseCompatibilityPresets=false\n");
        CHECK(manager.LoadFromIni(iniPath));
        CHECK(manager.GetSnapshot().keepThirdPersonSmoothingRemovalWithCameraMods);
    }

    void TestReplacementCompatibilityKeyOverridesLegacyMigration()
    {
        TemporaryDirectory directory;
        const auto iniPath = directory.Path() / "MouseSensitivityFix.ini";
        WriteText(
            iniPath,
            "[Compatibility]\n"
            "bUseCompatibilityPresets=true\n"
            "bDelegateThirdPersonWhenSmoothCam=true\n"
            "bKeepThirdPersonSmoothingRemovalWithCameraMods=true\n");

        auto& manager = msf::ConfigManager::Get();
        manager.SetChangeCallback({});
        CHECK(manager.LoadFromIni(iniPath));
        CHECK(manager.GetSnapshot().keepThirdPersonSmoothingRemovalWithCameraMods);
    }

    void TestConfigCallbacksAreSerialized()
    {
        auto& manager = msf::ConfigManager::Get();
        std::atomic_int activeCallbacks{ 0 };
        std::atomic_int maximumActiveCallbacks{ 0 };
        std::atomic_int callbackCount{ 0 };

        manager.SetChangeCallback([&](const msf::ConfigValues&) {
            const int active = activeCallbacks.fetch_add(1) + 1;
            int observedMaximum = maximumActiveCallbacks.load();
            while (active > observedMaximum &&
                   !maximumActiveCallbacks.compare_exchange_weak(observedMaximum, active)) {
            }
            std::this_thread::yield();
            ++callbackCount;
            --activeCallbacks;
        });

        auto update = [&manager](double base) {
            for (int index = 0; index < 25; ++index) {
                auto values = manager.GetSnapshot();
                values.globalSensitivity = base + static_cast<double>(index) / 100.0;
                manager.ApplyUiUpdate(values);
            }
        };

        std::thread first(update, 1.0);
        std::thread second(update, 2.0);
        first.join();
        second.join();

        CHECK(callbackCount == 50);
        CHECK(maximumActiveCallbacks == 1);
        manager.SetChangeCallback({});
    }
}

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        { "transform and runtime gates", TestTransformAndRuntimeGates },
        { "gamepad transform defaults to axis parity", TestGamepadTransformDefaultsToAxisParity },
        { "bow aim mouse deltas use sampled X and current engine Y", TestBowAimMouseDeltasUseSampledXAndCurrentEngineY },
        { "bow aim Y preserves configured axis parity", TestBowAimVerticalMultiplierPreservesConfiguredAxisParity },
        { "pitch target normalization uses freelook gain", TestPitchTargetNormalizationUsesFreelookGain },
        { "sampled scale updates only from true freelook", TestSampledScaleUpdatesOnlyFromTrueFreelook },
        { "freelook scale samples reject consumed and outlier input", TestFreelookScaleSamplesRejectConsumedAndOutlierInput },
        { "normal aim FOV updates only from freelook", TestNormalAimFovUpdatesOnlyFromFreelook },
        { "FOV degrees from NiFrustum tangent edges", TestFovDegreesFromFrustumEdges },
        { "live compatibility policy updates", TestLiveCompatibilityPolicyUpdates },
        { "compatibility policy covers known camera stacks", TestCompatibilityPolicyCoversKnownCameraStacks },
        { "half-rate yaw restoration", TestHalfRateYawRestoration },
        { "time-dilated yaw compensation", TestTimeDilatedYawCompensation },
        { "first-person yaw correction composition", TestFirstPersonYawCorrectionComposition },
        { "half-rate yaw eligibility includes bow aim", TestHalfRateYawEligibilityIncludesBowAim },
        { "third-person slow-time yaw eligibility is exact", TestThirdPersonSlowTimeYawEligibilityIsExact },
        { "orphan half-rate and freelook EMA guards", TestOrphanHalfRateAndFreelookEmaGuards },
        { "timeComp agreement gate", TestTimeCompAgreementGate },
        { "look correction policy matrix", TestLookCorrectionPolicyMatrix },
        { "bow aim mouse path is first-person only", TestBowAimMousePathIsFirstPersonOnly },
        { "look camera classification does not infer first person", TestLookCameraClassificationDoesNotInferFirstPerson },
        { "looking definition and timeMult relocation ids", TestLookingDefinitionAndTimeMultRelocationIds },
        { "freelook scale cache requires exact person", TestFreelookScaleCacheRequiresExactPerson },
        { "pitch normalize freelook and sprint gates", TestPitchNormalizeFreelookAndSprintGates },
        { "sampled logging policy", TestSampledLoggingPolicy },
        { "config load, clamp, save, and reload", TestConfigLoadClampSaveAndReload },
        { "compatibility keeps smoothing without camera mods", TestCompatibilityKeepsSmoothingWithoutCameraMods },
        { "legacy config keys remain safe", TestLegacyConfigKeysRemainSafe },
        { "replacement compatibility key overrides legacy migration", TestReplacementCompatibilityKeyOverridesLegacyMigration },
        { "config callbacks are serialized", TestConfigCallbacksAreSerialized },
    };

    int failures = 0;
    for (const auto& [name, test] : tests) {
        try {
            test();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    std::cout << tests.size() - static_cast<std::size_t>(failures) << "/"
              << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
