# Technical Design

Project: **Concise Mouse Consistency (CMC)**.

## Architecture

CMC has four parts:

1. **Bootstrap**
   - SKSE entry and runtime init
   - logging startup
2. **Hooks**
   - permanently installed mouse/gamepad look interception
   - sensitivity transform and smoothing-related handling
   - selective first-person sprint yaw restoration at final player rotation
3. **Config**
   - INI source of truth
   - throttled live reload, clamped values, and serialized change notifications
4. **UI bridge**
   - SKSE Menu Framework panel
   - in-memory apply + INI save/reload

## Transform model

- Input deltas: `dx`, `dy`
- Output:
  - `outX = dx * global * mouseXAxisMultiplier` (or `gamepadXAxisMultiplier`)
  - `outY = dy * global * mouseYAxisMultiplier` (or `gamepadYAxisMultiplier`)
- `ApplyTransform` takes an `isGamepad` bool to select the correct multiplier pair

Default mouse multipliers are `1.0`. Default `gamepadYAxisMultiplier` is `0.55` to compensate for Skyrim's FoV asymmetry on ultrawide displays.

## Runtime strategy

- Built with `add_commonlibsse_plugin(...)`
- CommonLibSSE-NG multi-runtime path for SE/AE/GOG/VR support
- Relocation-based hooks isolated in hook module
- all vtable hooks install transactionally at startup and remain installed
- disabled features pass input through unchanged instead of removing/reinstalling hooks
- config callbacks recompute compatibility policy and publish atomic runtime gates

This keeps live enable/disable and compatibility override changes safe. Vtables are never patched from inside an input callback, and a partial installation is rolled back before initialization fails.

`PlayerCharacter::ModifyMovementData` receives Skyrim's final first-person yaw delta. During active sprint frames, Skyrim can apply an exact `0.5` movement scale after normal mouse sensitivity processing. CMC restores the expected `lookX * deltaSeconds * pi` result only when the observed scale is within `0.48..0.52`. Full-rate transition frames, third-person look, disabled states, and zero-input frames pass through unchanged.

## Config model

INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`

- `[General]` core runtime toggles and sensitivity
- `[Advanced]` per-device axis multipliers and verbose logging
- `iFocusSpikeGapMs` controls focus-regain suppression from 50 to 5000 ms
- `[Compatibility]` SmoothCam / Improved Camera policy toggles

## Compatibility behavior

Current compatibility policy is intentionally narrow:

- targets SmoothCam + Improved Camera
- auto-detection drives policy via `_improvedCameraDetected` / `_smoothCamDetected` flags directly; no override fields
- may delegate third-person smoothing removal to the detected camera mod
- keeps core sensitivity transform active

Compatibility and hook toggles apply immediately. Settings change behavior through atomic gates; they do not mutate hook registrations at runtime.

Verbose logging is off in the release INI. When enabled, it emits low-frequency hook counters and sprint-correction samples, not per-frame camera, matrix, or movement dumps.

## Known risks

- runtime updates can invalidate relocation assumptions
- camera stacks from third-party mods can alter input order
- behavior must be regression-tested after Skyrim updates
