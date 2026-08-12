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
   - selective final-player-rotation yaw correction (FP half-rate restore; FP/TP slow-time compensation)
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

Default mouse and gamepad axis multipliers are `1.0`. Users can lower `gamepadYAxisMultiplier` if vertical look feels too fast.

During bow aim, CMC reconstructs X from raw pixels and a camera-specific freelook sample before applying the configured bow and mouse multipliers. First- and third-person samples are isolated, reset when the camera root changes, update only during true freelook, and require three consistent candidates before an unseeded or changed scale is accepted. Y preserves Skyrim's current engine delta and applies the configured bow and mouse Y multipliers exactly once.

Eagle Eye keeps the same freelook-equivalent input target on both axes. The rendered `NiCamera` frustum is sampled only for verbose diagnostics and never adds an automatic zoom-ratio multiplier.

## Runtime strategy

- Built with `add_commonlibsse_plugin(...)`
- CommonLibSSE-NG multi-runtime path for SE/AE/GOG/VR support
- Relocation-based hooks isolated in hook module
- all vtable hooks install transactionally at startup and remain installed
- disabled features pass input through unchanged instead of removing/reinstalling hooks
- config callbacks recompute compatibility policy and publish atomic runtime gates

This keeps live enable/disable and compatibility override changes safe. Vtables are never patched from inside an input callback, and a partial installation is rolled back before initialization fails.

`PlayerCharacter::ModifyMovementData` receives Skyrim's final yaw delta. Look-correction eligibility is split:

- **Half-rate restore** (`restoreHalfRateYaw`): first-person while looking. CMC restores `lookX * deltaSeconds * pi` only when the observed scale is within `0.48..0.52` (orphan/cast needs two consecutive band hits; sprint/bow may restore on the first). Sprint/bow/casting are telemetry hints, not sole eligibility gates. Freelook yaw EMA rejects half-scale and casting samples.
- **Slow-time compensation** (`compensateTimeYaw`): when the live `BSTimer::QGlobalTimeMultiplier` **Current** value (`RELOCATION_ID(511882, 388442)`) is in `(0.05, 0.90)` and the player is looking, multiply yaw by `1/timeMult` in first- or third-person — only when Current is stable and `delta` agrees with `realTimeDelta*Current` (else skip this frame). Do not use CommonLib's `GetCurrentGlobalTimeMult()` helper (it currently points at Target). This is not bow-gated (covers Eagle Eye and other slow-time). Order is always half-rate restore, then time compensation.

Disabled hooks, optional menu/look-control gates, and idle (non-looking) frames leave rotation untouched. Rendered FOV remains diagnostic only.

## Config model

INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`

- `[General]` core runtime toggles, global sensitivity, per-device axis multipliers, smoothing, focus-spike suppression, and gamepad enablement
- `[Advanced]` per-camera gates, menu/look-control guards, focus gap, bow multipliers, and verbose logging
- `iFocusSpikeGapMs` controls focus-regain suppression from 50 to 5000 ms
- `[Compatibility]` single `bKeepThirdPersonSmoothingRemovalWithCameraMods` toggle

## Compatibility behavior

Current compatibility policy is intentionally narrow:

- targets SmoothCam + Improved Camera detection
- default (`bKeepThirdPersonSmoothingRemovalWithCameraMods=true`): CMC keeps third-person smoothing removal even when those mods are present
- optional legacy mode (`false`): skip CMC third-person smoothing intervention when SmoothCam or Improved Camera is detected
- keeps core sensitivity transform active regardless

Compatibility and hook toggles apply immediately. Settings change behavior through atomic gates; they do not mutate hook registrations at runtime.

Verbose logging is off in the release INI. When enabled, it emits low-frequency hook counters, sensitivity and yaw samples, rendered-frustum diagnostics, and half-rate yaw correction samples, not per-frame camera, matrix, or movement dumps.

## Known risks

- runtime updates can invalidate relocation assumptions
- camera stacks from third-party mods can alter input order
- behavior must be regression-tested after Skyrim updates
