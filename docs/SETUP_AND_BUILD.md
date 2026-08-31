# Setup and Build

Project: **Concise Mouse Consistency (CMC)**.

## Toolchain

- Visual Studio 2022 (MSVC v143)
- CMake 3.24+
- Git and PowerShell 5.1+
- SKSE64 development headers/runtime for target Skyrim version
- [CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) `v7.0.0`, installed from the overlay port in `cmake/vcpkg-overlay-ports/commonlibsse-ng` (pins commit `8b032fa992750d654d6d38a33731714d8b86be1f`). This fork still builds SE+AE+VR and parses Address Library format 5. Do not substitute [powerof3/CommonLibSSE](https://github.com/powerof3/CommonLibSSE); that tree is AE-only.

## Runtime Dependencies

- SKSE64 runtime component matching the game (SKSE 2.3.1 on 1.7.99 / 1.7.104)
- Address Library for SKSE Plugins (`versionlib-1-7-*.bin` on 1.7.99+)
- SKSE Menu Framework (optional for in-game UI)

## Clean-clone build

1. Bootstrap the exact vcpkg baseline from `vcpkg-configuration.json`:
   - `./scripts/bootstrap-vcpkg.ps1`
2. Configure:
   - `cmake --preset plugin-release`
3. Build the DLL and unit tests:
   - `cmake --build --preset plugin-release`
4. Run unit tests:
   - `ctest --preset plugin-release`
5. Create the mod-manager-ready ZIP:
   - `cmake --build --preset plugin-release --target package`

Outputs:

- DLL: `build-commonlib/Release/MouseSensitivityFix.dll`
- ZIP: `build-commonlib/Concise-Mouse-Consistency-<version>.zip` (DLL + INI only; README/CHANGELOG are not packed)

The bootstrap script clones vcpkg into the ignored `.vcpkg/` directory, checks out the pinned 40-character baseline, and disables vcpkg metrics. Re-running it verifies the same baseline instead of silently upgrading dependencies. `commonlibsse-ng` is resolved from `cmake/vcpkg-overlay-ports`, not the old colorglass registry. The first plugin configure compiles CommonLibSSE-NG from source and is slow.

## Dependency-free tests

The config, transform, and compatibility-policy tests do not require vcpkg, CommonLibSSE, or Skyrim:

```powershell
cmake --preset unit-tests
cmake --build --preset unit-tests
ctest --preset unit-tests
```

## Local test install

1. Copy runtime files to Skyrim or an isolated mod-manager mod:
   - `Data/SKSE/Plugins/MouseSensitivityFix.dll`
   - `Data/SKSE/Plugins/MouseSensitivityFix.ini`
2. Launch through SKSE.
3. Check `MouseSensitivityFix.log` for clean startup and all expected hook-install messages.
4. Follow `docs/TEST_PLAN.md` and record evidence in `docs/RUNTIME_VALIDATION.md`.

The startup log reports the exact DLL version and feature markers in `BuildIdentity`, followed by four installed behaviors: the combined mouse/gamepad look hooks, first-person half-rate yaw correction, third-person smoothing, and the optional UI bridge. Normal release installs keep `bVerboseLogging=false`. Enable it temporarily only when sampled hook counters or rendered-frustum diagnostics are needed.
