# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

**Concise Mouse Consistency (CMC)** — a DLL-only SKSE64 plugin (C++23) for Skyrim SE/AE/GOG/VR. No ESP required. Built on CommonLibSSE-NG for multi-runtime support.

## Build

```bash
# Bootstrap pinned dependencies, configure, build, and test
./scripts/bootstrap-vcpkg.ps1
cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release

# Create the mod-manager-ready ZIP
cmake --build --preset plugin-release --target package
```

Output: `build-commonlib/Release/MouseSensitivityFix.dll`

**Deploy for testing:** After every successful build, replace the DLL in the MO2 mod folder. Seed the INI only when the test install does not already have one, so local settings survive rebuilds:

```bash
DEPLOY="E:/modding/Kexodus Skyrim/mods/Concise Mouse Consistency (CMC)/SKSE/Plugins"

cp "build-commonlib/Release/MouseSensitivityFix.dll" "$DEPLOY/"
[ -f "$DEPLOY/MouseSensitivityFix.ini" ] || cp "dist/Data/SKSE/Plugins/MouseSensitivityFix.ini" "$DEPLOY/"

```

The source `dist/` INI and normal deployed copy keep `bVerboseLogging=false`. Turn it on temporarily only when sampled hook counters or rendered-frustum diagnostics are needed, then turn it off before packaging or normal play. Check `MouseSensitivityFix.log` after launch for clean startup and the expected `BuildIdentity` line.

Dependency-free tests run with the `unit-tests` configure/build/test presets. CI runs those tests on Windows and Linux, then performs a full Windows plugin build, test, package, and artifact upload. In-game validation remains manual per `docs/TEST_PLAN.md` and is recorded in `docs/RUNTIME_VALIDATION.md`.

## Architecture

Initialization order (`Plugin::Initialize` in `src/Plugin.cpp`):
1. `ConfigManager` loads the INI
2. `CompatibilityManager` scans `Data/SKSE/Plugins/` for SmoothCam/Improved Camera DLLs and produces a `CompatibilityPolicy`
3. `ConfigManager` registers a serialized callback that recomputes and atomically publishes compatibility policy
4. `HookCoordinator::Install` transactionally installs all vtable hooks; disabled features remain pass-through
5. `MenuFrameworkBridge::Initialize` registers the in-game ImGui panel (optional; falls back to INI-only)

### Hook system (`src/hooks/Hooks.cpp`)

All hooks use `REL::Relocation` (CommonLibSSE-NG) to patch vtables — no hardcoded offsets:

| Hook | vtable slot | Purpose |
|---|---|---|
| `LookHandler::ProcessThumbstick` | +2 | Gamepad right-stick look |
| `LookHandler::ProcessMouseMove` | +3 | Mouse look |
| `PlayerCharacter::ModifyMovementData` | +0x11A | Selective first-person sprint yaw restoration |
| `ThirdPersonState::HandleLookInput` | +0x0F | Smoothing removal |

Each mouse/gamepad hook calls the original function first, then reads `data->lookInputVec` back out, applies `HookCoordinator::ApplyTransform`, and writes it back. During bow aim, the mouse path first reconstructs X from raw pixels and the camera-specific freelook sample while preserving the current engine Y delta; rendered FOV remains diagnostic only. The smoothing hook collapses `currentYaw → targetYaw` and `currentZoomOffset → targetZoomOffset` after the original call.

Transform: `out = delta * globalSensitivity * axisMultiplier`. `ApplyTransform` takes an `isGamepad` bool to select mouse or gamepad multipliers (`mouseX/Y` vs `gamepadX/Y`).

Hook callbacks call `ReloadIfChanged()`, which throttles filesystem polling to once per 250 ms. INI and UI changes are serialized, then compatibility behavior is updated through atomic gates. Hooks are never removed or installed from an input callback.

### Config (`src/config/Config.cpp`, `include/MouseSensitivityFix/Config.h`)

`ConfigManager` is a singleton. `GetSnapshot()` returns a copy of `ConfigValues` for thread-safe reads. `ApplyUiUpdate()` clamps and publishes a new in-memory snapshot; the UI's **Save to INI** button persists it. The full field list with defaults is in `Config.h`.

### Compatibility (`src/compat/Compatibility.cpp`)

`CompatibilityManager::ScanInstalledCameraMods` looks for known DLL filenames in `Data/SKSE/Plugins/`. `EvaluatePolicy` may delegate third-person smoothing removal while keeping core sensitivity transforms active. Live changes to `bForceOverrideSmoothCam` and `bForceOverrideImprovedCamera` recompute that policy without reinstalling hooks.

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
