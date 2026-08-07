# Runtime Validation

Project: **Concise Mouse Consistency (CMC)**.

CommonLibSSE-NG produces one multi-runtime DLL, but a successful build is not an in-game compatibility result. A row is **Passed** only after the listed build is launched through the matching SKSE version and the manual checks in `TEST_PLAN.md` succeed.

## Current matrix

| Runtime | Distribution | Status | Evidence |
|---|---|---|---|
| `1.5.97` | Steam SE | Pending | Runtime is not installed in the current test environment. |
| `1.6.640` | Steam AE | Pending | Runtime is not installed in the current test environment. |
| `1.6.1170` | Steam AE | Passed behavior; quiet-log build pending startup smoke test | On 2026-08-07, DLL `AC64ED6B...D145BD` loaded with Improved Camera and SmoothCam, restored first-person sprint yaw, and passed the user feel test. The logging-only follow-up DLL `990ABD5A...A7998` is built, packaged, and deployed but has not yet been launched. |
| Latest supported GOG | GOG | Pending | No GOG runtime is installed in the current test environment. |
| VR | Steam VR | Pending | No VR runtime is installed in the current test environment. Do not advertise validated VR support until this row passes. |

## Build evidence

Recorded on 2026-08-07 from branch `codex/release-hardening`:

- pinned vcpkg baseline `61d43ee6f0cc440dc97983d36e0c85e80fe429d4` bootstrapped successfully
- CommonLibSSE-NG `3.5.3` plugin compiled with MSVC warnings treated as errors
- runtime-validated sprint-fix DLL SHA-256: `AC64ED6BABBE642022863A35CAA202BBFB2BA2D1F299EF1E6BB6A9F191D145BD`
- packaged and deployed quiet-log DLL SHA-256: `990ABD5A25AACFFD4631CD955AB888CF3BB8BD8CF8FC0695ED6149079BCA7998`
- dependency-free and full-build unit-test runs passed
- CPack generated `Concise-Mouse-Consistency-0.1.0.zip`
- archive contents were limited to `MouseSensitivityFix.dll`, `MouseSensitivityFix.ini`, and `README.md` under the intended install layout
- extracted archive DLL hash matched the build output and the packaged INI contained `bVerboseLogging=false`

## First-person sprint evidence

The 2026-08-07 playtest on Steam `1.6.1170` recorded 560 active-sprint half-rate yaw frames. CMC restored all 560 from Skyrim's `0.5` scale to the full expected yaw. It left all 66 sprint-tagged full-rate transition frames and all 278 normal frames unchanged. Every corrected result matched final player yaw within `0.000001` radians, and the user confirmed normal and sprinting horizontal sensitivity felt matched.

The diagnostic build used to establish those counts emitted per-frame movement and camera state. That path has been removed. Release logging defaults to off; opt-in verbose mode now keeps only low-frequency input counters and compact sprint-correction samples.

## Evidence required for a pass

For each runtime, record:

1. Exact `SkyrimSE.exe` or VR executable version and distribution.
2. Matching SKSE version.
3. Git commit and DLL SHA-256.
4. Clean startup log with mouse, thumbstick, and third-person hook installation.
5. First/third-person mouse behavior, gamepad behavior, smoothing, focus regain, menu gating, UI save/reload, and external INI reload results.
6. SmoothCam and Improved Camera results when those mods support the runtime.

Do not replace a pending result with an inference from CommonLibSSE support. Keep the row pending until the game was actually run.
