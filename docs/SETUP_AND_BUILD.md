# Setup and Build

Project: **Concise Mouse Consistency (CMC)**.

## Toolchain

- Visual Studio 2022 (MSVC v143)
- CMake 3.24+
- Git and PowerShell 5.1+
- SKSE64 development headers/runtime for target Skyrim version
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG), installed automatically through the pinned vcpkg manifest

## Runtime Dependencies

- SKSE64 runtime component
- Address Library for SKSE Plugins
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
- ZIP: `build-commonlib/Concise-Mouse-Consistency-0.1.0.zip`

The bootstrap script clones vcpkg into the ignored `.vcpkg/` directory, checks out the pinned 40-character baseline, and disables vcpkg metrics. Re-running it verifies the same baseline instead of silently upgrading dependencies.

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

The startup log reports four installed behaviors: the combined mouse/gamepad look hooks, first-person sprint-yaw correction, third-person smoothing, and the optional UI bridge. Normal release installs keep `bVerboseLogging=false`. Enable it temporarily only when sampled hook counters are needed.
