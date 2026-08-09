# Changelog

## [0.1.3] - 2026-08-09

### Fixed

- Kept Eagle Eye horizontal and vertical mouse response freelook-equivalent instead of scaling vertical input by the rendered zoom ratio.
- Isolated first- and third-person sensitivity baselines and rejected bow transitions, consumed input, and outliers that could contaminate horizontal reconstruction.

### Changed

- Added always-on build identity and verbose rendered-frustum diagnostics so playtest logs can prove which DLL and input behavior loaded.
- Hardened camera diagnostics with checked camera traversal, correct frustum edge-angle conversion, and camera-lifecycle baseline resets.

## [0.1.2] - 2026-08-07

### Fixed

- Restored full horizontal sensitivity while drawing or aiming bows and crossbows, including Eagle Eye zoom.
- Kept bow vertical input tied to Skyrim's current engine delta so live sensitivity settings remain effective.
- Preserved normal look and full-rate transition frames by correcting only measured half-rate yaw.

## [0.1.1] - 2026-08-07

### Changed

- Aligned the SKSE Menu Framework page with the current General, Advanced, and Compatibility configuration surfaces.
- Removed obsolete menu grouping and unused UI bridge state.
