# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin for Skyrim Special Edition.
One CommonLibSSE-NG build targets SE, AE, GOG, and VR. Release **0.53.1** adds Address Library v5 so SKSE 2.3.1 can load the same DLL on Skyrim 1.7.99 / 1.7.104 without changing 0.53 gameplay. Playtest-validated on Steam AE `1.6.1170`; 1.7 and other runtimes compile into the same DLL but are not yet in-game validated — see `docs/RUNTIME_VALIDATION.md`.

It keeps mouse and gamepad look sensitivity consistent across camera states, restores first-person yaw that Skyrim halves or slows, and can remove third-person camera interpolation.

## What it does

- No ESP/ESL/ESM required
- Global sensitivity scaling with independent mouse and gamepad X/Y multipliers
- Separate bow/crossbow aim multipliers for mouse and gamepad
- First-person and third-person look transforms; first-person bow/crossbow aim reconstructs horizontal look from raw pixels
- First-person pitch normalization to a frozen true-freelook baseline (bow, Eagle Eye, and other non-freelook states)
- Selective first-person half-rate yaw restoration while looking (sprint, bow aim, and other measured 0.5× states such as casting)
- Wall-clock yaw compensation during slow-time (Eagle Eye and similar) in first- and third-person
- Optional third-person smoothing removal
- Optional gamepad look (right-stick) transforms
- Alt-tab focus-spike suppression
- Live INI reload and runtime-safe enable/disable (hooks stay installed; disabled features pass through)
- In-game settings via SKSE Menu Framework (INI fallback if missing)
- One SmoothCam / Improved Camera option: keep or skip CMC third-person smoothing removal

CMC does not replace camera mods, apply automatic FOV-based input scaling, or normalize third-person pitch. Third-person with a ranged weapon out uses input transforms and optional smoothing removal only.

## Runtime requirements

- SKSE64 matching your game runtime (SKSE 2.3.1 on Skyrim 1.7.99 / 1.7.104; SKSE 2.2.6 remains the 1.6.1170 target)
- Address Library for SKSE Plugins (1.7.99+ needs the format-5 `versionlib-1-7-*.bin` files)
- SKSE Menu Framework (optional)

## Build

Plugin builds need Visual Studio 2022 (MSVC v143) and CMake 3.24+. From a clean clone:

```powershell
./scripts/bootstrap-vcpkg.ps1
cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release
cmake --build --preset plugin-release --target package
```

Outputs:

- DLL: `build-commonlib/Release/MouseSensitivityFix.dll`
- ZIP: `build-commonlib/Concise-Mouse-Consistency-0.53.1.zip`

Run dependency-free unit tests with `cmake --preset unit-tests`, `cmake --build --preset unit-tests`, and `ctest --preset unit-tests`.
Full setup, packaging, and troubleshooting steps are in `docs/SETUP_AND_BUILD.md`.

## Runtime config

- INI: `Data/SKSE/Plugins/MouseSensitivityFix.ini`
- UI path: `Concise Mouse Consistency/Settings`
- The in-game menu exposes enable, sensitivity, axis multipliers, first-person mouse bow X/Y, smoothing removal, focus-spike suppress, gamepad look, and the camera-mod smoothing option. Gamepad bow multipliers and other advanced knobs stay in the INI `[Advanced]` section.
- UI and external INI edits apply without reinstalling hooks or restarting Skyrim. Use **Save to INI** to persist UI changes.
- Release logging is quiet by default. Set `bVerboseLogging=true` only when collecting sampled hook counters and rendered-frustum diagnostics.

## Validation targets

The codebase produces one CommonLibSSE-NG multi-runtime DLL. Runtime validation is tracked separately for:

- `1.5.97` (SE)
- `1.6.640` (AE)
- `1.6.1170` (Steam AE)
- `1.7.99` / `1.7.104` (Steam AE 1.7; SKSE 2.3.1 + Address Library v5)
- latest supported GOG build
- VR

See `docs/RUNTIME_VALIDATION.md` for current evidence and pending playtests. Do not treat a successful build as in-game proof for an untested runtime.

## Docs

- `CHANGELOG.md`
- `CLAUDE.md` (contributor and automation guidance)
- `dist/README.md` (release-package README)
- `docs/OBJECTIVES.md`
- `docs/IMPLEMENTATION_PLAN.md`
- `docs/TECHNICAL_DESIGN.md`
- `docs/SETUP_AND_BUILD.md`
- `docs/TEST_PLAN.md`
- `docs/RUNTIME_VALIDATION.md`
- `docs/PUBLISHING.md`
- `docs/research.md`
