# Test Plan

Project: **Concise Mouse Consistency (CMC)**.

## 0) Automated checks

- Run dependency-free checks with `cmake --preset unit-tests`, `cmake --build --preset unit-tests`, and `ctest --preset unit-tests`.
- Run the same tests against the full dependency graph with `ctest --preset plugin-release`.
- Confirm tests cover transforms, runtime gates, live compatibility-policy changes, INI parsing/clamping/save/reload, case-insensitive DLL detection, and serialized config callbacks.
- Confirm sprint-yaw tests cover the `0.48..0.52` correction window, both yaw directions, boundary values, nearby non-matching scales, disabled eligibility, zero input, and zero frame delta.
- Confirm sampled-log tests cover disabled logging, zero counts/intervals, first-correction emission, and before/exact/after interval boundaries.

## 1) Startup

- Launch through SKSE loader.
- Confirm clean init in `MouseSensitivityFix.log`.

## 2) Sensitivity behavior

Validate in first-person and third-person:

- global sensitivity changes are obvious at low/high values
- X and Y feel consistent at parity defaults
- horizontal sensitivity remains matched while actively sprinting in first person
- entering and leaving sprint does not produce a doubled transition frame
- behavior is stable after save + reload from UI

## 3) Smoothing checks

- do fast stop/start flicks
- verify no delayed glide introduced by plugin

## 4) UI and INI

- SKSE Menu Framework panel appears when installed
- INI fallback works when framework is missing
- changed values apply live and persist correctly
- start with `bEnabled=false`, enable it in the UI, and verify transforms begin without restarting
- change smoothing delegation and force-override settings live, then verify policy-update log entries
- edit the INI rapidly while moving the mouse and verify the final loaded settings win without duplicate hook installation

## 5) Compatibility

- Smoke-test with SmoothCam and Improved Camera enabled
- Verify auto-detection delegates third-person smoothing while keeping sensitivity transforms active
- Verify `bForceOverrideSmoothCam` / `bForceOverrideImprovedCamera` restore full intervention

## 6) Edge cases

- alt-tab out/in: no spikes or stuck deltas
- menu transitions: transforms gate correctly on `GameIsPaused() || IsApplicationMenuOpen()`
- gamepad transform only when `bAffectGamepadLook=true`
- with heavily modded setups where `ControlMap::IsLookingControlsEnabled()` returns false: confirm `bDisableWhenLookControlsDisabled=false` default keeps transforms active

## 7) FPS and device matrix

- 30 / 60 / 120 / 240 FPS
- high polling mice (1000Hz+)
- confirm focus-spike suppression is stable after alt-tab at high framerates

## 8) Release Readiness

- package has only required runtime files + docs
- clean-profile install works
- known issues are documented
- update `docs/RUNTIME_VALIDATION.md` with runtime version, build commit, result, and evidence
- return `bVerboseLogging` to `false` before packaging or normal play
