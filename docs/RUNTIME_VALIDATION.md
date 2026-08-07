# Runtime Validation

Project: **Concise Mouse Consistency (CMC)**.

CommonLibSSE-NG produces one multi-runtime DLL, but a successful build is not an in-game compatibility result. A row is **Passed** only after the listed build is launched through the matching SKSE version and the manual checks in `TEST_PLAN.md` succeed.

## Current matrix

| Runtime | Distribution | Status | Evidence |
|---|---|---|---|
| `1.5.97` | Steam SE | Pending | Runtime is not installed in the current test environment. |
| `1.6.640` | Steam AE | Pending | Runtime is not installed in the current test environment. |
| `1.6.1170` | Steam AE | Pending new-build playtest | Earlier build loaded all three hooks and the SKSE Menu Framework page on 2026-08-06. The release-hardening build compiles, packages, and is deployed, but has not yet been launched in Skyrim. |
| Latest supported GOG | GOG | Pending | No GOG runtime is installed in the current test environment. |
| VR | Steam VR | Pending | No VR runtime is installed in the current test environment. Do not advertise validated VR support until this row passes. |

## Build evidence

Recorded on 2026-08-06 from branch `codex/release-hardening`:

- pinned vcpkg baseline `61d43ee6f0cc440dc97983d36e0c85e80fe429d4` bootstrapped successfully
- CommonLibSSE-NG `3.5.3` plugin compiled with MSVC warnings treated as errors
- deployed DLL SHA-256: `514E96EC3206CB2AF7ECF6DB79F3FAC8BABBE1D3A9F5757C8AFC7F64F8E7330E`
- dependency-free and full-build unit-test runs passed
- CPack generated `Concise-Mouse-Consistency-0.1.0.zip`
- archive contents were limited to `MouseSensitivityFix.dll`, `MouseSensitivityFix.ini`, and `README.md` under the intended install layout

## Evidence required for a pass

For each runtime, record:

1. Exact `SkyrimSE.exe` or VR executable version and distribution.
2. Matching SKSE version.
3. Git commit and DLL SHA-256.
4. Clean startup log with mouse, thumbstick, and third-person hook installation.
5. First/third-person mouse behavior, gamepad behavior, smoothing, focus regain, menu gating, UI save/reload, and external INI reload results.
6. SmoothCam and Improved Camera results when those mods support the runtime.

Do not replace a pending result with an inference from CommonLibSSE support. Keep the row pending until the game was actually run.
