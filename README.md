# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin for Skyrim.  
It keeps look sensitivity consistent and removes plugin-side smoothing behavior.

## What it does

- No ESP/ESL/ESM required
- Global sensitivity scaling
- Mouse look support
- Optional gamepad look support
- In-game settings via SKSE Menu Framework (INI fallback if missing)
- Compatibility controls for SmoothCam and Improved Camera

## Runtime requirements

- SKSE64 matching your game runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework (optional)

## Build

Use `build-commonlib` as the canonical build directory.  
Full steps: `docs/SETUP_AND_BUILD.md`

## Runtime config

- INI: `Data/SKSE/Plugins/MouseSensitivityFix.ini`
- UI path: `Mouse Sensitivity Fix/Settings`

## Validation targets

Validate before release on:

- `1.5.97` (SE)
- `1.6.640` (AE)
- latest `1.6.x` Steam
- latest supported GOG build

## Docs

- `docs/OBJECTIVES.md`
- `docs/TECHNICAL_DESIGN.md`
- `docs/SETUP_AND_BUILD.md`
- `docs/TEST_PLAN.md`
- `docs/PUBLISHING.md`
