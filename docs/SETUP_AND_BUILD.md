# Setup and Build

Project: **Concise Mouse Consistency (CMC)**.

## Toolchain

- Visual Studio 2022 (MSVC v143)
- CMake
- SKSE64 development headers/runtime for target Skyrim version
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)

## Runtime Dependencies

- SKSE64 runtime component
- Address Library for SKSE Plugins
- SKSE Menu Framework (optional for in-game UI)

## Local Test Install

1. Configure:
   - `cmake -S . -B build-commonlib -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=".vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows`
2. Build:
   - `cmake --build build-commonlib --config Release`
3. Copy runtime files to Skyrim:
   - `Data/SKSE/Plugins/MouseSensitivityFix.dll`
   - `Data/SKSE/Plugins/MouseSensitivityFix.ini`
4. Launch through SKSE.
5. Check `MouseSensitivityFix.log` for clean startup.
