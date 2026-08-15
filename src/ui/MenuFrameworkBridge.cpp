#include "MouseSensitivityFix/MenuFrameworkBridge.h"

#include "MouseSensitivityFix/Config.h"
#include "MouseSensitivityFix/Log.h"

#include <Windows.h>

#include <filesystem>

namespace msf
{
    namespace
    {
        struct ImVec2
        {
            float x;
            float y;
        };

        using RenderFunction = void(__stdcall*)();
        using AddSectionItemFn = void(__cdecl*)(const char* path, RenderFunction rendererFunction);
        using ImGuiTextUnformattedFn = void(__cdecl*)(const char* text, const char* textEnd);
        using ImGuiSeparatorTextFn = void(__cdecl*)(const char* label);
        using ImGuiSeparatorFn = void(__cdecl*)();
        using ImGuiCheckboxFn = bool(__cdecl*)(const char* label, bool* value);
        using ImGuiSliderFloatFn = bool(__cdecl*)(const char* label, float* value, float minValue, float maxValue, const char* format, int flags);
        using ImGuiButtonFn = bool(__cdecl*)(const char* label, ImVec2 size);
        using ImGuiSameLineFn = void(__cdecl*)(float offsetFromStartX, float spacing);

        struct FrameworkApi
        {
            AddSectionItemFn AddSectionItem{ nullptr };
            ImGuiTextUnformattedFn TextUnformatted{ nullptr };
            ImGuiSeparatorTextFn SeparatorText{ nullptr };
            ImGuiSeparatorFn Separator{ nullptr };
            ImGuiCheckboxFn Checkbox{ nullptr };
            ImGuiSliderFloatFn SliderFloat{ nullptr };
            ImGuiButtonFn Button{ nullptr };
            ImGuiSameLineFn SameLine{ nullptr };
        };

        FrameworkApi g_api{};
        bool g_registered{ false };
        bool g_lastSaveSucceeded{ false };
        bool g_lastSaveAttempted{ false };

        const char* kMenuPath = "Concise Mouse Consistency/Settings";
        const auto kConfigPath = std::filesystem::path("Data/SKSE/Plugins/MouseSensitivityFix.ini");

        template <class T>
        T ResolveProc(HMODULE module, const char* procName)
        {
            return module ? reinterpret_cast<T>(::GetProcAddress(module, procName)) : nullptr;
        }

        void UiText(const char* text)
        {
            if (g_api.TextUnformatted) {
                g_api.TextUnformatted(text, nullptr);
            }
        }

        void UiSeparatorText(const char* text)
        {
            if (g_api.SeparatorText) {
                g_api.SeparatorText(text);
            } else {
                UiText(text);
            }
        }

        void UiSeparator()
        {
            if (g_api.Separator) {
                g_api.Separator();
            }
        }

        void __stdcall RenderSettingsPage()
        {
            auto values = ConfigManager::Get().GetSnapshot();
            bool changed = false;

            // Front-page controls. Verbose logging and gamepad bow stay INI-only.
            // First-person mouse bow X/Y are on this page (custom bow sensitivity).
            UiSeparatorText("General");
            changed |= g_api.Checkbox("Enabled", &values.enabled);
            float globalSens = static_cast<float>(values.globalSensitivity);
            if (g_api.SliderFloat("Global sensitivity", &globalSens, 0.01F, 20.0F, "%.2f", 0)) {
                values.globalSensitivity = static_cast<double>(globalSens);
                changed = true;
            }
            float mouseX = static_cast<float>(values.mouseXAxisMultiplier);
            float mouseY = static_cast<float>(values.mouseYAxisMultiplier);
            float gamepadX = static_cast<float>(values.gamepadXAxisMultiplier);
            float gamepadY = static_cast<float>(values.gamepadYAxisMultiplier);
            if (g_api.SliderFloat("Mouse X multiplier", &mouseX, 0.01F, 20.0F, "%.2f", 0)) {
                values.mouseXAxisMultiplier = static_cast<double>(mouseX);
                changed = true;
            }
            if (g_api.SliderFloat("Mouse Y multiplier", &mouseY, 0.01F, 20.0F, "%.2f", 0)) {
                values.mouseYAxisMultiplier = static_cast<double>(mouseY);
                changed = true;
            }
            if (g_api.SliderFloat("Gamepad X multiplier", &gamepadX, 0.01F, 20.0F, "%.2f", 0)) {
                values.gamepadXAxisMultiplier = static_cast<double>(gamepadX);
                changed = true;
            }
            if (g_api.SliderFloat("Gamepad Y multiplier", &gamepadY, 0.01F, 20.0F, "%.2f", 0)) {
                values.gamepadYAxisMultiplier = static_cast<double>(gamepadY);
                changed = true;
            }
            changed |= g_api.Checkbox("Remove third-person smoothing", &values.enableSmoothingRemovalHook);
            changed |= g_api.Checkbox("Suppress focus spike (alt-tab)", &values.suppressFocusSpike);
            changed |= g_api.Checkbox("Apply to gamepad look", &values.affectGamepadLook);

            UiSeparatorText("Bow aim (first-person mouse)");
            UiText(
                "X reconstructs from raw pixels * sampled freelook scale * this multiplier. "
                "1.0 = freelook-equivalent reconstructed X, not zoom/FOV compensation. "
                "Y multiplies the live engine delta. Third-person mouse bow multipliers do not apply.");
            float bowMouseX = static_cast<float>(values.bowAimMouseXMultiplier);
            float bowMouseY = static_cast<float>(values.bowAimMouseYMultiplier);
            if (g_api.SliderFloat("Mouse bow X", &bowMouseX, 0.01F, 20.0F, "%.2f", 0)) {
                values.bowAimMouseXMultiplier = static_cast<double>(bowMouseX);
                changed = true;
            }
            if (g_api.SliderFloat("Mouse bow Y", &bowMouseY, 0.01F, 20.0F, "%.2f", 0)) {
                values.bowAimMouseYMultiplier = static_cast<double>(bowMouseY);
                changed = true;
            }
            UiText("Gamepad bow X/Y stay in the INI.");

            // ── Compatibility ─────────────────────────────────────────────────
            UiSeparatorText("Compatibility");
            changed |= g_api.Checkbox(
                "Keep CMC 3rd-person smoothing removal with camera mods",
                &values.keepThirdPersonSmoothingRemovalWithCameraMods);

            // ─────────────────────────────────────────────────────────────────
            if (changed) {
                ConfigManager::Get().ApplyUiUpdate(values);
                g_lastSaveAttempted = false;
            }

            UiSeparator();
            if (g_api.Button("Save to INI", ImVec2{ 0.0F, 0.0F })) {
                g_lastSaveAttempted = true;
                g_lastSaveSucceeded = ConfigManager::Get().SaveToIni(kConfigPath);
                LogInfo(g_lastSaveSucceeded ? "UI: saved configuration to INI." : "UI: failed to save configuration to INI.");
            }
            if (g_api.SameLine) {
                g_api.SameLine(0.0F, -1.0F);
            }
            if (g_api.Button("Reload from INI", ImVec2{ 0.0F, 0.0F })) {
                const bool loaded = ConfigManager::Get().LoadFromIni(kConfigPath);
                g_lastSaveAttempted = true;
                g_lastSaveSucceeded = loaded;
                LogInfo(loaded ? "UI: reloaded configuration from INI." : "UI: failed to reload configuration from INI.");
            }

            const bool unsaved = ConfigManager::Get().HasUnsavedChanges();
            if (unsaved || !g_lastSaveAttempted) {
                UiText("Changes apply immediately. Use Save to persist.");
            } else if (g_lastSaveSucceeded) {
                UiText("Last operation succeeded.");
            } else {
                UiText("Last operation failed. Check log for details.");
            }
        }

        bool ResolveFrameworkApi()
        {
            HMODULE frameworkModule = ::GetModuleHandleA("SKSEMenuFramework.dll");
            if (!frameworkModule) {
                frameworkModule = ::LoadLibraryA("Data\\SKSE\\Plugins\\SKSEMenuFramework.dll");
            }
            if (!frameworkModule) {
                return false;
            }

            g_api.AddSectionItem = ResolveProc<AddSectionItemFn>(frameworkModule, "AddSectionItem");
            g_api.TextUnformatted = ResolveProc<ImGuiTextUnformattedFn>(frameworkModule, "igTextUnformatted");
            g_api.SeparatorText = ResolveProc<ImGuiSeparatorTextFn>(frameworkModule, "igSeparatorText");
            g_api.Separator = ResolveProc<ImGuiSeparatorFn>(frameworkModule, "igSeparator");
            g_api.Checkbox = ResolveProc<ImGuiCheckboxFn>(frameworkModule, "igCheckbox");
            g_api.SliderFloat = ResolveProc<ImGuiSliderFloatFn>(frameworkModule, "igSliderFloat");
            g_api.Button = ResolveProc<ImGuiButtonFn>(frameworkModule, "igButton");
            g_api.SameLine = ResolveProc<ImGuiSameLineFn>(frameworkModule, "igSameLine");

            return g_api.AddSectionItem != nullptr &&
                   g_api.TextUnformatted != nullptr &&
                   g_api.Checkbox != nullptr &&
                   g_api.SliderFloat != nullptr &&
                   g_api.Button != nullptr;
        }
    }

    bool MenuFrameworkBridge::Initialize()
    {
        const auto menuFrameworkDll = std::filesystem::path("Data/SKSE/Plugins/SKSEMenuFramework.dll");
        if (!std::filesystem::exists(menuFrameworkDll)) {
            LogWarn("SKSE Menu Framework not found. INI-based config remains available.");
            return true;
        }

        if (!ResolveFrameworkApi()) {
            LogWarn("SKSE Menu Framework detected, but required exports were unavailable. INI-based config remains available.");
            return true;
        }

        g_api.AddSectionItem(kMenuPath, RenderSettingsPage);
        g_registered = true;
        LogInfo("UI Bridge initialized: registered SKSE Menu Framework settings page.");

        return true;
    }

    void MenuFrameworkBridge::Shutdown()
    {
        if (g_registered) {
            LogInfo("UI Bridge shutdown.");
            g_registered = false;
        }
    }

}
