# Test Plan

Project: **Concise Mouse Consistency (CMC)**.

## 0) Automated checks

- Run dependency-free checks with `cmake --preset unit-tests`, `cmake --build --preset unit-tests`, and `ctest --preset unit-tests`.
- Run the same tests against the full dependency graph with `ctest --preset plugin-release`.
- Confirm tests cover transforms, runtime gates, live compatibility-policy changes, INI parsing/clamping/save/reload, case-insensitive DLL detection, and serialized config callbacks.
- Confirm half-rate yaw tests cover the `0.48..0.52` correction window, both yaw directions, boundary values, nearby non-matching scales, sprint and bow eligibility, disabled eligibility, zero input, and zero frame delta.
- Confirm time-dilated yaw compensation boosts only when `globalTimeMult` is in `(0.05, 0.90)`, leaves near-1.0 untouched, and restores Eagle Eye wall-clock yaw after half-rate restore.
- Confirm bow Y tests preserve Skyrim's current engine delta and apply only the configured bow and mouse Y multipliers.
- Confirm first-person pitch normalization uses the frozen true-freelook gain, holds its persistent target on zero-Y updates, and remains disabled until the three-sample baseline is seeded.
- Confirm third-person pitch telemetry remains passive and `ThirdPersonState::HandleLookInput` does not rewrite `freeRotation.y` beyond smoothing removal's yaw/zoom updates.
- Confirm rendered FOV remains telemetry-only while final X and Y camera response is measured.
- Confirm first- and third-person sampled X baselines are isolated, update only in true freelook, reject ranged/zoom transitions and isolated invalid samples, and require three consistent candidates before a legitimate sign or scale change can reseed the cache.
- Confirm normal-FOV baseline tests reject bow-out, bow-aim, zoomed, and invalid-FOV samples.
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
- horizontal and vertical sensitivity remain matched while drawing a bow and during Eagle Eye zoom
- during fully zoomed Eagle Eye, confirm `timeMult≈0.25`, `timeComp=1`, and `yawRatioToFreelook≈1.0` (wall-clock match after slow-time compensation)
- make separate one-direction X and Y sweeps in freelook, bow pull, and fully zoomed Eagle Eye
- confirm `FinalAxisResponse` correlates each raw/input window with final pitch and yaw orientation changes
- confirm `normalizedTargetPitchDelta / outY` stays equal to the frozen freelook baseline across bow pull, Eagle Eye transitions, and fully settled Eagle Eye
- in third person, confirm `ThirdPersonFinalAxisResponse` reports `pitchNormalized=0` and no `ThirdPersonPitchHold` hook is installed
- hold Eagle Eye still and move horizontally only; verify the persistent pitch target does not drift
- entering and leaving bow aim does not produce a doubled transition frame
- behavior is stable after save + reload from UI

## 3) Smoothing checks

- do fast stop/start flicks
- verify no delayed glide introduced by plugin

## 4) UI and INI

- SKSE Menu Framework panel appears when installed
- INI fallback works when framework is missing
- changed values apply live and persist correctly
- start with `bEnabled=false`, enable it in the UI, and verify transforms begin without restarting
- toggle `bKeepThirdPersonSmoothingRemovalWithCameraMods` live, then verify policy-update log entries
- edit the INI rapidly while moving the mouse and verify the final loaded settings win without duplicate hook installation

## 5) Compatibility

- Smoke-test with SmoothCam and Improved Camera enabled
- Default (`bKeepThirdPersonSmoothingRemovalWithCameraMods=true`): verify CMC still removes third-person smoothing while keeping sensitivity transforms active
- Set the option false and verify CMC skips third-person smoothing intervention when those camera mods are detected (sensitivity transforms remain active)

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
