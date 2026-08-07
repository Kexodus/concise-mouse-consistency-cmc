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
        config.hotDisable = true;
        CHECK(!coordinator.ShouldApplyInputTransform(config, false, false));
        CHECK(!coordinator.ShouldRemoveThirdPersonSmoothing(config));
    }

    void TestLiveCompatibilityPolicyUpdates()
    {
        TemporaryDirectory directory;
        WriteText(directory.Path() / "SmoothCam.DLL", "");

        msf::CompatibilityManager compatibility;
        compatibility.ScanInstalledCameraMods(directory.Path());

        msf::ConfigValues config;
        auto policy = compatibility.EvaluatePolicy(config);
        CHECK(policy.mode == msf::CompatibilityMode::ReducedIntervention);
        CHECK(!policy.installSmoothingRemovalHooks);
        CHECK(!policy.allowThirdPersonSmoothingIntervention);

        msf::HookCoordinator coordinator;
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(!coordinator.UpdatePolicy(policy));
        CHECK(coordinator.ShouldApplyInputTransform(config, true, false));
        CHECK(!coordinator.ShouldRemoveThirdPersonSmoothing(config));

        config.forceOverrideSmoothCam = true;
        policy = compatibility.EvaluatePolicy(config);
        CHECK(coordinator.UpdatePolicy(policy));
        CHECK(policy.installSmoothingRemovalHooks);
        CHECK(policy.allowThirdPersonSmoothingIntervention);
        CHECK(coordinator.ShouldRemoveThirdPersonSmoothing(config));
    }

    void TestRuntimeLookTelemetryAggregation()
    {
        msf::RuntimeLookTelemetryAccumulator telemetry;
        CHECK(!telemetry.Consume().has_value());

        telemetry.Record(3.0F, -2.0F, 3.0F, 2.0F, 3.0F, 2.0F, false);
        telemetry.Record(5.0F, 1.0F, 5.0F, -1.0F, 5.0F, -1.0F, true);

        const auto sample = telemetry.Consume();
        CHECK(sample.has_value());
        CHECK(sample->eventCount == 2);
        CHECK(sample->sprintEventCount == 1);
        CHECK(Near(sample->rawPixelX, 8.0F));
        CHECK(Near(sample->rawPixelY, -1.0F));
        CHECK(Near(sample->engineX, 8.0F));
        CHECK(Near(sample->engineY, 1.0F));
        CHECK(Near(sample->outputX, 8.0F));
        CHECK(Near(sample->outputY, 1.0F));
        CHECK(!telemetry.Consume().has_value());
    }

    void TestWrappedAngleDelta()
    {
        constexpr float pi = 3.14159265358979323846F;
        CHECK(Near(msf::WrappedAngleDelta(0.75F, 0.25F), 0.5F));
        CHECK(Near(msf::WrappedAngleDelta(-pi + 0.1F, pi - 0.1F), 0.2F));
        CHECK(Near(msf::WrappedAngleDelta(pi - 0.1F, -pi + 0.1F), -0.2F));
    }

    void TestHalfRateSprintYawRestoration()
    {
        constexpr float pi = 3.14159265358979323846F;
        constexpr float lookX = 0.6F;
        constexpr float delta = 1.0F / 60.0F;
        constexpr float expected = lookX * delta * pi;

        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, delta, expected * 0.5F, true), expected));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(-lookX, delta, -expected * 0.5F, true), -expected));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, delta, expected * 0.49F, true), expected));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, delta, expected, true), expected));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, delta, expected * 0.75F, true), expected * 0.75F));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, delta, expected * 0.5F, false), expected * 0.5F));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(0.0F, delta, 0.25F, true), 0.25F));
        CHECK(Near(msf::RestoreHalfRateSprintYawDelta(lookX, 0.0F, 0.25F, true), 0.25F));
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
            "iFocusSpikeGapMs=9000\n"
            "[Advanced]\n"
            "fGlobalSensitivity=0.001\n"
            "fMouseXAxisMultiplier=99\n");

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
        manager.ApplyUiUpdate(values);
        values = manager.GetSnapshot();
        CHECK(callbackCount == 2);
        CHECK(values.focusSpikeGapMs == 50);
        CHECK(values.mouseYAxisMultiplier == 20.0);
        CHECK(manager.SaveToIni(iniPath));

        const auto knownWriteTime = std::filesystem::last_write_time(iniPath);
        WriteText(iniPath, "[General]\nbEnabled=false\niFocusSpikeGapMs=500\n");
        std::filesystem::last_write_time(iniPath, knownWriteTime + std::chrono::seconds(2));
        CHECK(manager.ReloadIfChanged());
        values = manager.GetSnapshot();
        CHECK(callbackCount == 3);
        CHECK(!values.enabled);
        CHECK(values.focusSpikeGapMs == 500);
        CHECK(!manager.ReloadIfChanged());

        manager.SetChangeCallback({});
    }

    void TestCompatibilityPresetCanBeDisabled()
    {
        TemporaryDirectory directory;
        WriteText(directory.Path() / "ImprovedCameraSE-NG.dll", "");

        msf::CompatibilityManager compatibility;
        compatibility.ScanInstalledCameraMods(directory.Path());
        msf::ConfigValues config;
        config.useCompatibilityPresets = false;

        const auto policy = compatibility.EvaluatePolicy(config);
        CHECK(policy.mode == msf::CompatibilityMode::Safe);
        CHECK(policy.installInputHooks);
        CHECK(policy.installSmoothingRemovalHooks);
        CHECK(policy.allowThirdPersonSmoothingIntervention);
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
        { "live compatibility policy updates", TestLiveCompatibilityPolicyUpdates },
        { "runtime look telemetry aggregation", TestRuntimeLookTelemetryAggregation },
        { "wrapped angle delta", TestWrappedAngleDelta },
        { "half-rate sprint yaw restoration", TestHalfRateSprintYawRestoration },
        { "config load, clamp, save, and reload", TestConfigLoadClampSaveAndReload },
        { "compatibility presets can be disabled", TestCompatibilityPresetCanBeDisabled },
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
