# Technical Design

Project: **Concise Mouse Consistency (CMC)**.

## Architecture

CMC has four parts:

1. **Bootstrap**
   - SKSE entry and runtime init
   - logging startup
2. **Hooks**
   - mouse/gamepad look interception
   - sensitivity transform and smoothing-related handling
3. **Config**
   - INI source of truth
   - live reload and clamped values
4. **UI bridge**
   - SKSE Menu Framework panel
   - in-memory apply + INI save/reload

## Transform model

- Input deltas: `dx`, `dy`
- Output:
  - `outX = dx * global * xMultiplier`
  - `outY = dy * global * yMultiplier`
- Strict mode clamps to `maxDeltaPerFrame`

Default values keep parity (`xMultiplier = yMultiplier = 1.0`).

## Runtime strategy

- Built with `add_commonlibsse_plugin(...)`
- CommonLibSSE-NG multi-runtime path for SE/AE/GOG support
- Relocation-based hooks isolated in hook module

## Config model

INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`

- `[General]` core runtime toggles and sensitivity
- `[Advanced]` multipliers, clamp, verbose logging
- `[Compatibility]` SmoothCam / Improved Camera policy toggles

## Compatibility behavior

Current compatibility policy is intentionally narrow:

- targets SmoothCam + Improved Camera
- may reduce third-person intervention / smoothing-removal hooks
- keeps core sensitivity transform active

## Known risks

- runtime updates can invalidate relocation assumptions
- camera stacks from third-party mods can alter input order
- behavior must be regression-tested after Skyrim updates
