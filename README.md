# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin for Skyrim.  
It keeps mouse and gamepad look sensitivity consistent across camera states and can remove third-person camera interpolation.

## What it does

- No ESP/ESL/ESM required
- Global sensitivity scaling
- Independent mouse and gamepad X/Y scaling
- First-person, third-person, and bow/crossbow aim controls
- Optional gamepad look support with right-stick filtering
- Alt-tab focus-spike suppression
- Live INI reload and runtime-safe enable/disable behavior
- In-game settings via SKSE Menu Framework (INI fallback if missing)
- Compatibility controls for SmoothCam and Improved Camera

## Runtime requirements

- SKSE64 matching your game runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework (optional)

## Build

From a clean clone:

```powershell
./scripts/bootstrap-vcpkg.ps1
cmake --preset plugin-release
cmake --build --preset plugin-release
ctest --preset plugin-release
```

Run dependency-free unit tests with `cmake --preset unit-tests`, `cmake --build --preset unit-tests`, and `ctest --preset unit-tests`.
Full setup, packaging, and troubleshooting steps are in `docs/SETUP_AND_BUILD.md`.

## Runtime config

- INI: `Data/SKSE/Plugins/MouseSensitivityFix.ini`
- UI path: `Concise Mouse Consistency/Settings`
- UI and external INI edits apply without reinstalling hooks or restarting Skyrim

## Validation targets

The codebase produces one CommonLibSSE-NG multi-runtime DLL. Runtime validation is tracked separately for:

- `1.5.97` (SE)
- `1.6.640` (AE)
- latest `1.6.x` Steam
- latest supported GOG build
- VR

See `docs/RUNTIME_VALIDATION.md` for current evidence and pending playtests.

## Docs

- `docs/OBJECTIVES.md`
- `docs/IMPLEMENTATION_PLAN.md`
- `docs/TECHNICAL_DESIGN.md`
- `docs/SETUP_AND_BUILD.md`
- `docs/TEST_PLAN.md`
- `docs/RUNTIME_VALIDATION.md`
- `docs/PUBLISHING.md`
- `docs/research.md`
