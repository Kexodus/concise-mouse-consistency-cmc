# Publishing Checklist

Project: **Concise Mouse Consistency (CMC)**.

Use this before cutting a public release.

## 1) Repo check

- Ensure `.gitignore` excludes local toolchains/build artifacts.
- Confirm no local-only folders are tracked (`.vcpkg`, build dirs, external test clones).
- Verify docs match current behavior.

## 2) Build

- Configure and build from `build-commonlib` in `Release`.
- Confirm output DLL exists:
  - `build-commonlib/Release/MouseSensitivityFix.dll`

## 3) Runtime validation

Validate in-game startup and input behavior on targeted runtimes:

- `1.5.97` (SE)
- `1.6.640` (AE)
- latest `1.6.x` Steam
- latest supported GOG build

Minimum checks:

- Plugin loads cleanly in `MouseSensitivityFix.log`
- Menu entry appears when SKSE Menu Framework is installed
- Global sensitivity changes have immediate effect
- Save/reload in UI persists values to INI
- Gamepad toggle behaves correctly

## 4) Packaging

Include only:

- `Data/SKSE/Plugins/MouseSensitivityFix.dll`
- `Data/SKSE/Plugins/MouseSensitivityFix.ini`
- `README.md` (and changelog if used)

No build directories or local dependency clones in release artifacts.

## 5) Manual Staging

Create a clean release folder and copy:

- `Data/SKSE/Plugins/MouseSensitivityFix.dll`
- `Data/SKSE/Plugins/MouseSensitivityFix.ini`
- `README.md`
- `docs/PUBLISHING.md` (optional)
