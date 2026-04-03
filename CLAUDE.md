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

**Deploy for testing:** copy `MouseSensitivityFix.dll` and `dist/Data/SKSE/Plugins/MouseSensitivityFix.ini` to `<Skyrim>/Data/SKSE/Plugins/`. Check `MouseSensitivityFix.log` after launch for clean startup.

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

### Crash guard

On startup, if `MouseSensitivityFix.runtime.lock` already exists (previous session didn't clean up = crash), the counter in `.crashguard.state` increments. At the internal threshold (default 3) the plugin self-disables for the session. The lock file is deleted on clean `Plugin::Shutdown`. (`crashDisableThreshold` and `crashWindowSeconds` are internal-only and not exposed in the INI.)

## Local-only directories

Never commit: `.vcpkg/`, `.skse-menu-framework-2/`, `.knockout-ext/`, `build/`, `build-commonlib/`, `build-vs/`, `release-staging/`.

## Release packaging

See `docs/PUBLISHING.md`. Release artifact is the contents of `dist/` with the freshly built DLL dropped in.
