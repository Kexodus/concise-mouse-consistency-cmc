# Publishing Checklist

Project: **Concise Mouse Consistency (CMC)**.

Use this before cutting a public release.

## 1) Repo check

- Ensure `.gitignore` excludes local toolchains/build artifacts.
- Confirm no local-only folders are tracked (`.vcpkg`, build dirs, external test clones).
- Verify docs match current behavior.

## 2) Build

- Run `./scripts/bootstrap-vcpkg.ps1`.
- Configure with `cmake --preset plugin-release`.
- Build with `cmake --build --preset plugin-release`.
- Test with `ctest --preset plugin-release`.
- Package with `cmake --build --preset plugin-release --target package`.
- Confirm output DLL exists:
   - `build-commonlib/Release/MouseSensitivityFix.dll`
- Confirm release archive exists:
   - `build-commonlib/Concise-Mouse-Consistency-<version>.zip`
- Inspect the archive and confirm it contains only the DLL, INI, runtime README, and changelog layout.
- Extract the archive into a fresh empty directory and verify the DLL hash matches `build-commonlib/Release/MouseSensitivityFix.dll`.

## 3) Runtime validation

Validate in-game startup and input behavior on targeted runtimes:

- `1.5.97` (SE)
- `1.6.640` (AE)
- latest `1.6.x` Steam
- latest supported GOG build
- VR if the release advertises VR support

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
- `README.md`
- `CHANGELOG.md`

The packaged INI must keep `bVerboseLogging=false`. Diagnostic playtest settings belong only in a local test install.

No build directories or local dependency clones in release artifacts.

## 5) CI artifact

The GitHub Actions `plugin-build` job repeats the pinned bootstrap, warning-clean build, unit tests, CPack archive creation, and artifact upload. Download the `Concise-Mouse-Consistency` artifact from the successful workflow run and compare it with the locally generated archive when cutting a release.

Do not publish unless the required rows in `docs/RUNTIME_VALIDATION.md` are marked passed with evidence.
