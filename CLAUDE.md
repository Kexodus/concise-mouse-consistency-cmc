# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**Concise Mouse Consistency (CMC)** — a DLL-only SKSE64 plugin (C++23) for Skyrim SE/AE/GOG/VR. No ESP required. Built on CommonLibSSE-NG for multi-runtime support.

## Build

```bash
# Configure (run once or after CMakeLists changes)
cmake -S . -B build-commonlib -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=".vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_TARGET_TRIPLET=x64-windows

# Build
cmake --build build-commonlib --config Release
```

Output: `build-commonlib/Release/MouseSensitivityFix.dll`

**Deploy for testing:** After every successful build, automatically copy both files to the MO2 mod folder:

```bash
DEPLOY="E:/modding/Kexodus Skyrim/mods/Concise Mouse Consistency (CMC)/SKSE/Plugins"

cp "build-commonlib/Release/MouseSensitivityFix.dll" "$DEPLOY/"
cp "dist/Data/SKSE/Plugins/MouseSensitivityFix.ini"  "$DEPLOY/"

# Enable debug settings in the deployed INI for playtesting
sed -i 's/bVerboseLogging=false/bVerboseLogging=true/' "$DEPLOY/MouseSensitivityFix.ini"
```

The source `dist/` INI always keeps `bVerboseLogging=false` (release default). Only the deployed copy gets it flipped to `true`. Check `MouseSensitivityFix.log` after launch for clean startup.

There are no automated tests — validation is manual per `docs/TEST_PLAN.md`. Validate on runtimes `1.5.97`, `1.6.640`, latest Steam `1.6.x`, and GOG before release.

## Architecture

Initialization order (`Plugin::Initialize` in `src/Plugin.cpp`):
1. `ConfigManager` loads INI; applies `hotDisable` or crash-guard early-out if triggered
2. `CompatibilityManager` scans `Data/SKSE/Plugins/` for SmoothCam/Improved Camera DLLs and produces a `CompatibilityPolicy`
3. `HookCoordinator::Install` installs vtable hooks gated by that policy
4. `MenuFrameworkBridge::Initialize` registers the in-game ImGui panel (optional; falls back to INI-only)

### Hook system (`src/hooks/Hooks.cpp`)

All hooks use `REL::Relocation` (CommonLibSSE-NG) to patch vtables — no hardcoded offsets:

| Hook | vtable slot | Purpose |
|---|---|---|
| `LookHandler::ProcessThumbstick` | +2 | Gamepad right-stick look |
| `LookHandler::ProcessMouseMove` | +3 | Mouse look |
| `ThirdPersonState::HandleLookInput` | +0x0F | Smoothing removal |

Each mouse/gamepad hook calls the original function first, then reads `data->lookInputVec` back out, applies `HookCoordinator::ApplyTransform`, and writes it back. The smoothing hook collapses `currentYaw → targetYaw` and `currentZoomOffset → targetZoomOffset` after the original call.

Transform: `out = delta * globalSensitivity * axisMultiplier`. `ApplyTransform` takes an `isGamepad` bool to select mouse or gamepad multipliers (`mouseX/Y` vs `gamepadX/Y`).

Both hooks reload `ConfigManager` on every call via `ReloadIfChanged()` (file-watch with mutex), so INI edits apply without restart.

### Config (`src/config/Config.cpp`, `include/MouseSensitivityFix/Config.h`)

`ConfigManager` is a singleton. `GetSnapshot()` returns a copy of `ConfigValues` for thread-safe reads. `ApplyUiUpdate()` writes a new snapshot and saves to INI. The full field list with defaults is in `Config.h`.

### Compatibility (`src/compat/Compatibility.cpp`)

`CompatibilityManager::ScanInstalledCameraMods` looks for known DLL filenames in `Data/SKSE/Plugins/`. `EvaluatePolicy` returns a `CompatibilityPolicy` that may set `allowThirdPersonIntervention = false` or `installSmoothingRemovalHooks = false` to avoid conflicts. Policy is driven directly from `_improvedCameraDetected` / `_smoothCamDetected` flags; `bForceOverrideSmoothCam` / `bForceOverrideImprovedCamera` in INI can bypass auto-detection.

## Local-only directories

Never commit: `.vcpkg/`, `.skse-menu-framework-2/`, `.knockout-ext/`, `build/`, `build-commonlib/`, `build-vs/`, `release-staging/`.

## Playtesting

When the user says **"I am playtesting"**, immediately read all `.log` files in:

`%USERPROFILE%\Documents\My Games\Skyrim Special Edition\SKSE\`

Read `MouseSensitivityFix.log` first, then any other SKSE logs present. Look for errors, warnings, hook diagnostics, and sampled scale values. Summarise findings without waiting to be asked.

## Research log

All findings across sessions — what works, what doesn't, and hard limits not worth revisiting — are tracked in `docs/research.md`.

**When to update it:**
- An approach is confirmed working in-game → add to **Works**
- An approach is confirmed broken or has been tried and abandoned → add to **Doesn't work**
- A hard engine/API limit is hit with no viable workaround → add to **Hard limits**

Read `docs/research.md` at the start of any session involving hooks, compatibility, or sensitivity math to avoid re-investigating settled questions. Update it whenever a session produces a new confirmed finding.

## Release packaging

See `docs/PUBLISHING.md`. Release artifact is the contents of `dist/` with the freshly built DLL dropped in.
