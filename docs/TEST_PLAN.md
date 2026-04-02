# Test Plan

Project: **Concise Mouse Consistency (CMC)**.

## 1) Startup

- Launch through SKSE loader.
- Confirm clean init in `MouseSensitivityFix.log`.

## 2) Sensitivity behavior

Validate in first-person and third-person:

- global sensitivity changes are obvious at low/high values
- X and Y feel consistent at parity defaults
- behavior is stable after save + reload from UI

## 3) Smoothing checks

- do fast stop/start flicks
- verify no delayed glide introduced by plugin

## 4) UI and INI

- SKSE Menu Framework panel appears when installed
- INI fallback works when framework is missing
- changed values apply live and persist correctly

## 5) Compatibility

- Smoke-test with SmoothCam and Improved Camera enabled
- Verify compatibility toggles affect behavior as documented

## 6) Edge cases

- alt-tab out/in: no spikes or stuck deltas
- menu transitions: gating works when menu filtering enabled
- gamepad transform only when explicitly enabled

## 7) FPS and device matrix

- 30 / 60 / 120 / 240 FPS
- high polling mice (1000Hz+)
- confirm clamp behavior is stable at extremes

## 8) Release Readiness

- package has only required runtime files + docs
- clean-profile install works
- known issues are documented
