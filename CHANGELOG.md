# Changelog

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
