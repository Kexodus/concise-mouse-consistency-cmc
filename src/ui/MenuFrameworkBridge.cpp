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
        const char* kWalkingPath = "Concise Mouse Consistency/State Overrides/Walking";
        const char* kRunningPath = "Concise Mouse Consistency/State Overrides/Running";
        const char* kSprintingPath = "Concise Mouse Consistency/State Overrides/Sprinting";
        const char* kBowAimPath = "Concise Mouse Consistency/State Overrides/Bow pullback aiming";
        const char* kMagicUsePath = "Concise Mouse Consistency/State Overrides/Magic use";
        const char* kOneHandPath = "Concise Mouse Consistency/State Overrides/One Hand";
        const char* kTwoHandedPath = "Concise Mouse Consistency/State Overrides/Two Handed";
        const char* kDualWieldingPath = "Concise Mouse Consistency/State Overrides/Dual Wielding";
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
            UiText("Optional per-state look overlays are under Concise Mouse Consistency / State Overrides. Leave Disabled checked to keep 0.53b feel.");

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

        void RenderStateOverrideEditor(StateLookOverride& overlay, const char* help, bool& changed)
        {
            changed |= g_api.Checkbox("Disabled", &overlay.disabled);
            UiText(help);
            float x = static_cast<float>(overlay.xSensitivity);
            float y = static_cast<float>(overlay.ySensitivity);
            if (g_api.SliderFloat("X sensitivity", &x, 0.01F, 20.0F, "%.2f", 0)) {
                overlay.xSensitivity = static_cast<double>(x);
                changed = true;
            }
            if (g_api.SliderFloat("Y sensitivity", &y, 0.01F, 20.0F, "%.2f", 0)) {
                overlay.ySensitivity = static_cast<double>(y);
                changed = true;
            }
            changed |= g_api.Checkbox("Apply in first person", &overlay.applyFirstPerson);
            changed |= g_api.Checkbox("Apply in third person", &overlay.applyThirdPerson);
        }

        void RenderStateOverridePage(StateLookOverride ConfigValues::* member, const char* help)
        {
            auto values = ConfigManager::Get().GetSnapshot();
            bool changed = false;
            RenderStateOverrideEditor(values.*member, help, changed);
            if (changed) {
                ConfigManager::Get().ApplyUiUpdate(values);
                g_lastSaveAttempted = false;
            }
        }

        void __stdcall RenderWalkingPage()
        {
            RenderStateOverridePage(
                &ConfigValues::walking,
                "When Disabled is checked, walking look is unchanged from 0.53b. Uncheck to apply X/Y while moving at walk (not run/sprint, not standing still).");
        }

        void __stdcall RenderRunningPage()
        {
            RenderStateOverridePage(
                &ConfigValues::running,
                "When Disabled is checked, running look is unchanged from 0.53b. Uncheck to apply X/Y while running (not sprint, not standing still).");
        }

        void __stdcall RenderSprintingPage()
        {
            RenderStateOverridePage(
                &ConfigValues::sprinting,
                "When Disabled is checked, sprint look is unchanged from 0.53b. Uncheck to apply X/Y while sprinting. Sprint wins over weapon style and walk/run.");
        }

        void __stdcall RenderBowAimPage()
        {
            RenderStateOverridePage(
                &ConfigValues::bowAim,
                "Bow pullback/aiming, including Eagle Eye frames that are still bowAim. Disabled leaves fBowAim* as 0.53b. Enabled replaces fBowAim* (does not multiply). Reconstruct X and engine Y still run. FOV is never a multiplier.");
        }

        void __stdcall RenderMagicUsePage()
        {
            RenderStateOverridePage(
                &ConfigValues::magicUse,
                "When Disabled is checked, magic look is unchanged from 0.53b. Uncheck to apply X/Y while charging or casting. A staff or spell merely equipped does not count.");
        }

        void __stdcall RenderOneHandPage()
        {
            RenderStateOverridePage(
                &ConfigValues::oneHand,
                "When Disabled is checked, one-hand look is unchanged from 0.53b. Uncheck to apply X/Y with a one-handed weapon drawn and the other hand empty or a shield. Not bow, staff, or two-hander.");
        }

        void __stdcall RenderTwoHandedPage()
        {
            RenderStateOverridePage(
                &ConfigValues::twoHanded,
                "When Disabled is checked, two-handed look is unchanged from 0.53b. Uncheck to apply X/Y with a drawn greatsword, battleaxe, or warhammer. Bows and staves do not count.");
        }

        void __stdcall RenderDualWieldingPage()
        {
            RenderStateOverridePage(
                &ConfigValues::dualWielding,
                "When Disabled is checked, dual-wield look is unchanged from 0.53b. Uncheck to apply X/Y with one-handed weapons drawn in both hands.");
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
        g_api.AddSectionItem(kWalkingPath, RenderWalkingPage);
        g_api.AddSectionItem(kRunningPath, RenderRunningPage);
        g_api.AddSectionItem(kSprintingPath, RenderSprintingPage);
        g_api.AddSectionItem(kBowAimPath, RenderBowAimPage);
        g_api.AddSectionItem(kMagicUsePath, RenderMagicUsePage);
        g_api.AddSectionItem(kOneHandPath, RenderOneHandPage);
        g_api.AddSectionItem(kTwoHandedPath, RenderTwoHandedPage);
        g_api.AddSectionItem(kDualWieldingPath, RenderDualWieldingPage);
        g_registered = true;
        LogInfo("UI Bridge initialized: registered SKSE Menu Framework settings and state-override pages.");

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
