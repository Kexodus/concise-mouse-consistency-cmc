# Changelog

## [Unreleased]

## [0.54b] - 2026-08-31

### Added

- Opt-in per-state look overlays (Walking, Running, Sprinting, Bow pullback/aiming, Magic use, One Hand, Two Handed, Dual Wielding). Each ships Disabled so 0.53b feel is unchanged until a page is unchecked.
- SKSE Menu pages under Concise Mouse Consistency / State Overrides, plus matching INI sections. One X/Y pair per state; exact first- or third-person gates.
- When the Bow overlay is enabled it replaces `fBowAim*` for that event. Disabled leaves `fBowAim*` as 0.53b. Reconstruct X and engine Y still run. FOV is never a multiplier.

### Changed

- One SKSE DLL now declares Address Library v5 so SKSE 2.3.1 can load CMC on Skyrim 1.7.99 and 1.7.104, while keeping SE / 1.6.x AE / GOG / VR compatibility.
- CommonLibSSE-NG is consumed from an overlay port of [alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) `v7.0.0` (`8b032fa`) instead of the stale colorglass / CharmedBaryon 3.5.3 pin.

### Fixed

- `ParseBool` now accepts `TRUE`/`FALSE` (any case) so unchecking Disabled in the INI actually enables an overlay.

### Tests / Docs

- Resolver, composition, person-gate, priority, bow replace-not-stack, `FALSE` opt-in, and dist-INI Disabled defaults.
- Documented 1.7.99 / 1.7.104 as validation targets (in-game playtest still pending). SKSE 2.3.1 and Address Library `versionlib-1-7-*.bin` are required on those runtimes.

## [0.53b] - 2026-08-14

### Changed

- Restored first-person mouse bow X/Y controls on the SKSE Menu Framework settings page. Labels match FP-only reconstruct (1.0 = freelook-equivalent X; Y multiplies live engine delta; no zoom/FOV scaling). Gamepad bow multipliers stay INI-only.
- Save to INI writes a temp file then replaces the destination, and regenerates comments so the first UI save cannot leave an empty or comment-stripped INI.
- Unsaved live UI values are not overwritten by throttled disk reload; further slider edits after Save return to the “Changes apply immediately…” prompt.

### Fixed

- Camera-mod DLL scan no longer skips later plugins after one unreadable directory entry.
- Menu Framework initialize failure no longer unloads the plugin (INI-only fallback).

### Tests / Docs

- Added hook-faithful yaw, pitch-freeze, FOV-independence, focus-spike, config completeness, reload-throttle, and unsaved-UI regression tests.
- Playtest docs now expect `YawTimeCompWallRewrite` / `timeComp=1` `mode=wall` on Eagle Eye disagree, not `YawTimeCompSkip` / `timeComp=0`.

## [0.1.4] - 2026-08-12

### Fixed

- Restored Eagle Eye horizontal wall-clock sensitivity by compensating the final first-person yaw for Skyrim's active slow-time multiplier after the guarded half-rate correction.
- Normalized first-person pitch targets against a frozen true-freelook baseline while preserving zero-input holds and camera-override resets.
- Kept third-person camera behavior passive after rejecting the experimental third-person pitch normalizer, including preserving bow gamepad multipliers in both perspectives.

### Changed

- Simplified camera-mod compatibility to one live option controlling whether CMC keeps third-person smoothing removal, with safe migration of legacy settings.
- Expanded release tests for exact slow-time boundaries, composed Eagle Eye yaw correction, gamepad parity, camera-stack policy, and INI persistence.
- Removed development-only per-frame file telemetry and kept rendered-frustum/orientation diagnostics behind sampled verbose logging.

## [0.1.3] - 2026-08-09

### Fixed

- Kept Eagle Eye horizontal and vertical mouse response freelook-equivalent instead of scaling vertical input by the rendered zoom ratio.
- Isolated first- and third-person sensitivity baselines and rejected bow transitions, consumed input, and outliers that could contaminate horizontal reconstruction.

### Changed

- Playtest logs now identify the exact DLL and enabled input behavior, with opt-in rendered-frustum diagnostics for Eagle Eye validation.
- Made camera diagnostics safer and more accurate with checked traversal, correct frustum edge-angle conversion, and baseline resets when the active camera changes.

## [0.1.2] - 2026-08-07

### Fixed

- Restored full horizontal sensitivity while drawing or aiming bows and crossbows, including Eagle Eye zoom.
- Kept bow vertical input tied to Skyrim's current engine delta so live sensitivity settings remain effective.
- Preserved normal look and full-rate transition frames by correcting only measured half-rate yaw.

## [0.1.1] - 2026-08-07

### Changed

- Aligned the SKSE Menu Framework page with the current General, Advanced, and Compatibility configuration surfaces.
- Removed obsolete menu grouping and unused UI bridge state.
