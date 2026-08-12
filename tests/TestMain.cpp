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
        CHECK(coordinator.ShouldApplyInputTransform(config, false, false));
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false));

        config.enableThirdPersonHook = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, true, false));
        config.enableThirdPersonHook = true;
        config.affectGamepadLook = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, true));
        config.enabled = false;
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, false));
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
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false));
        CHECK(coordinator.ShouldRemoveThirdPersonSmoothing(config));

        // Legacy reduced-intervention: skip CMC smoothing removal when camera mods are present.
        config.keepThirdPersonSmoothingRemovalWithCameraMods = false;
        policy = compatibility.EvaluatePolicy(config);
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
        CHECK(policy.installInputHooks);
        CHECK(!policy.allowThirdPersonSmoothingIntervention);
        CHECK(!coordinator.ShouldRemoveThirdPersonSmoothing(config));
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false));

        policy.installInputHooks = false;
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, false));
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

        const auto eagleEye = msf::ApplyFirstPersonYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            true);
        CHECK(Near(eagleEye.yawDelta, lookX * (gameDelta / timeMult) * pi));
        CHECK(eagleEye.halfRateRestored);
        CHECK(eagleEye.timeCompensated);

        const auto normalTime = msf::ApplyFirstPersonYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            1.0F,
            true);
        CHECK(Near(normalTime.yawDelta, lookX * gameDelta * pi));
        CHECK(normalTime.halfRateRestored);
        CHECK(!normalTime.timeCompensated);

        const float engineFullRate = lookX * gameDelta * pi;
        const auto slowTimeFullRate = msf::ApplyFirstPersonYawCorrection(
            lookX,
            gameDelta,
            engineFullRate,
            timeMult,
            true);
        CHECK(Near(slowTimeFullRate.yawDelta, engineFullRate / timeMult));
        CHECK(!slowTimeFullRate.halfRateRestored);
        CHECK(slowTimeFullRate.timeCompensated);

        const auto ineligible = msf::ApplyFirstPersonYawCorrection(
            lookX,
            gameDelta,
            engineHalfRate,
            timeMult,
            false);
        CHECK(Near(ineligible.yawDelta, engineHalfRate));
        CHECK(!ineligible.halfRateRestored);
        CHECK(!ineligible.timeCompensated);
    }

    void TestHalfRateYawEligibilityIncludesBowAim()
    {
        CHECK(msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, false, true, false));
        CHECK(msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, false, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, false, false, false));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, true, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(false, true, false, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, false, false, false, true));
        CHECK(!msf::ShouldRestoreHalfRateFirstPersonYaw(true, true, true, true, true));
    }

    void TestBowAimMousePathIsFirstPersonOnly()
    {
        CHECK(msf::ShouldApplyBowAimMousePath(false, true));
        CHECK(!msf::ShouldApplyBowAimMousePath(true, true));
        CHECK(!msf::ShouldApplyBowAimMousePath(false, false));
        CHECK(!msf::ShouldApplyBowAimMousePath(true, false));
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
        CHECK(manager.SaveToIni(iniPath));

        const auto savedText = ReadText(iniPath);
        CHECK(savedText.find("bKeepThirdPersonSmoothingRemovalWithCameraMods=false") != std::string::npos);
        CHECK(savedText.find("bHotDisable") == std::string::npos);
        CHECK(savedText.find("bUseCompatibilityPresets") == std::string::npos);

        values.keepThirdPersonSmoothingRemovalWithCameraMods = true;
        manager.ApplyUiUpdate(values);
        CHECK(callbackCount == 3);
        CHECK(manager.LoadFromIni(iniPath));
        values = manager.GetSnapshot();
        CHECK(callbackCount == 4);
        CHECK(!values.keepThirdPersonSmoothingRemovalWithCameraMods);

        const auto knownWriteTime = std::filesystem::last_write_time(iniPath);
        WriteText(iniPath, "[General]\nbEnabled=false\niFocusSpikeGapMs=500\n");
        std::filesystem::last_write_time(iniPath, knownWriteTime + std::chrono::seconds(2));
        CHECK(manager.ReloadIfChanged());
        values = manager.GetSnapshot();
        CHECK(callbackCount == 5);
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
        { "bow aim mouse path is first-person only", TestBowAimMousePathIsFirstPersonOnly },
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
