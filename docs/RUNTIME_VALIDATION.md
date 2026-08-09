# Runtime Validation

Project: **Concise Mouse Consistency (CMC)**.

CommonLibSSE-NG produces one multi-runtime DLL, but a successful build is not an in-game compatibility result. A row is **Passed** only after the listed build is launched through the matching SKSE version and the manual checks in `TEST_PLAN.md` succeed.

## Current matrix

| Runtime | Distribution | Status | Evidence |
|---|---|---|---|
| `1.5.97` | Steam SE | Pending | Runtime is not installed in the current test environment. |
| `1.6.640` | Steam AE | Pending | Runtime is not installed in the current test environment. |
| `1.6.1170` | Steam AE | Passed behavior | Sprint yaw was validated on 2026-08-07. On 2026-08-09, the latest playtested hardening DLL `96A622F8...C0F1` loaded cleanly, rejected bow-transition samples, and retained the seeded freelook baseline through rendered zoom. Logs proved CMC applied no extra Y scaling and preserved its X correction; the user confirmed the resulting axis response felt matched. The final `0.1.3` package retains that correction and awaits a separate startup smoke test. |
| Latest supported GOG | GOG | Pending | No GOG runtime is installed in the current test environment. |
| VR | Steam VR | Pending | No VR runtime is installed in the current test environment. Do not advertise validated VR support until this row passes. |

## Build evidence

Recorded on 2026-08-07 from branch `codex/release-hardening`:

- pinned vcpkg baseline `61d43ee6f0cc440dc97983d36e0c85e80fe429d4` bootstrapped successfully
- CommonLibSSE-NG `3.5.3` plugin compiled with MSVC warnings treated as errors
- runtime-validated sprint-fix DLL SHA-256: `AC64ED6BABBE642022863A35CAA202BBFB2BA2D1F299EF1E6BB6A9F191D145BD`
- packaged and deployed quiet-log DLL SHA-256: `990ABD5A25AACFFD4631CD955AB888CF3BB8BD8CF8FC0695ED6149079BCA7998`
- runtime-validated bow-fix DLL SHA-256: `B837B29E54A2C1FAC8F24370B3A467DAFEB6FEEDD819BFA6D8C3855400D8C111`
- final `0.1.2` package DLL SHA-256: `25AFC8C009EB5FCD8C6F3335C36CE8090B231B9B4CDBAA2C1E84EBC2CC4BF102`
- dependency-free and full-build unit-test runs passed
- CPack generated `Concise-Mouse-Consistency-0.1.2.zip`
- archive contents were limited to `MouseSensitivityFix.dll`, `MouseSensitivityFix.ini`, `README.md`, and `CHANGELOG.md` under the intended install layout
- extracted archive DLL hash matched the build output and the packaged INI contained `bVerboseLogging=false`

## First-person sprint evidence

The 2026-08-07 playtest on Steam `1.6.1170` recorded 560 active-sprint half-rate yaw frames. CMC restored all 560 from Skyrim's `0.5` scale to the full expected yaw. It left all 66 sprint-tagged full-rate transition frames and all 278 normal frames unchanged. Every corrected result matched final player yaw within `0.000001` radians, and the user confirmed normal and sprinting horizontal sensitivity felt matched.

The diagnostic build used to establish those counts emitted per-frame movement and camera state. That path has been removed. Release logging defaults to off; opt-in verbose mode now keeps only low-frequency input counters and compact half-rate yaw correction samples.

## Bow and Eagle Eye evidence

The 2026-08-07 playtest on Steam `1.6.1170` covered normal bow draw and Eagle Eye. A forced `fBowAimMouseYMultiplier=0.1` made vertical input dramatically slower, confirming the setting reaches the camera. After returning bow X/Y multipliers to `1.0`, the user confirmed both aim states felt correct.

Verbose runtime logs recorded 2,040 corrected bow frames. All 18 sampled records had `bowAiming=1` and `sprinting=0`; each restored yaw was exactly twice Skyrim's measured half-rate value within `0.000001` radians. The released correction remains guarded by the `0.48..0.52` observed-scale window, so normal and already-full transition frames pass through unchanged.

### 2026-08-09 Eagle Eye revalidation

The diagnostic `0.1.2` DLL (`E2C487B7...F8644`, 507,904 bytes) loaded on Steam `1.6.1170` with `ImprovedCamera=no SmoothCam=no`. The user reproduced the outstanding issue: regular bow aim felt correct, but Eagle Eye vertical sensitivity remained higher than horizontal.

The log captured 517 Eagle Eye mouse frames and a real rendered-frustum contraction. The legacy diagnostic conversion reported `3.604575` to `2.003606` (`0.555851`); those values are retained here as historical log evidence, not degrees. It also captured six non-Eagle-Eye `bowPull` samples while the frustum was still narrowed to `0.56..0.57`, plus a zoom-exit `bowOut` sample that replaced the normal baseline with transitional value `3.340328`.

Follow-up DLL `AC8E3157...14FAE` (514,560 bytes) limited baseline sampling to true freelook and added rendered-FOV gating. In the fresh playtest, the user confirmed Eagle Eye felt correct: Y was no longer more sensitive and X was no longer lowered. The run began with the bow already out, so `normalFov` remained `0.0`; as a result the attempted FOV correction stayed inactive and both `eagleEyeY` and `bowY` remained `1.0` throughout the narrowed frustum. This is a clean behavioral A/B result against the prior `0.555851` Y multiplier. The production target is therefore configured/freelook-equivalent axis parity, with rendered FOV retained for diagnostics only.

The same run showed that active `NiCamera` world rotation, like `PlayerCharacter::ModifyMovementData::rotationData.x`, reports no usable first-person pitch delta. The `RenderedRotation` probe was removed rather than presenting zero pitch as parity evidence.

Final parity DLL `5253A729...76AAA` (507,392 bytes) makes the validated no-FOV-scaling behavior unconditional and identifies itself with `eagleEyeFovY=0 axisParity=1 renderedFovDiag=1`. Dependency-free and full-plugin tests passed.

The exact DLL loaded cleanly on Steam `1.6.1170` on 2026-08-09. All hooks installed with no CMC warnings or errors. Freelook seeded the legacy diagnostic `normalFov=3.604575`; two Eagle Eye holds narrowed its ratio to `0.555851` while every sampled Eagle Eye record retained `bowY=1.0`, `eagleEyeY=1.0`, and `outOverEngineY=1.0`. The run recorded 1,037 bow-aim mouse frames, 774 Eagle Eye frames, and 960 guarded first-person bow yaw corrections. This exact run validates that CMC applied no extra Y scaling and preserved its X correction while the user-confirmed parity behavior was active.

Pre-landing review then produced diagnostic-hardening DLL `96A622F8...C0F1` (508,416 bytes): rendered-FOV traversal now runs only with verbose logging, camera children use checked RTTI, camera-mode/root changes reset the diagnostic FOV baseline, and sampled X updates only in true freelook. Both test presets pass.

The exact follow-up DLL loaded cleanly on Steam `1.6.1170`. During the initial bow-out and Eagle Eye sequence, sampled scale correctly remained unseeded instead of accepting transition frames. After the player sheathed the bow, true freelook seeded `sampledScale=(1.0,-1.0)` and `normalFov=3.604575`; those values then stayed unchanged through later bow-out, bow-pull, Eagle Eye, and a narrowed `currentFov=2.003953` exit frame. All sampled Eagle Eye records retained `bowY=1.0`, `eagleEyeY=1.0`, and `outOverEngineY=1.0`. CMC emitted no warnings or errors, and no crash artifact was generated.

Release packaging then produced version `0.1.3`. The final DLL is 508,928 bytes with SHA-256 `473549BA3451B1CC31EFCDD9B998D4E3273246299E6FF662902ABE36C65A0605`; the generated `Concise-Mouse-Consistency-0.1.3.zip` has SHA-256 `8A65E4B3C9FE1681E9A96B01331909740F8626F7F50ACE096FA7934A03E43015`. The archive contains only `MouseSensitivityFix.dll`, `MouseSensitivityFix.ini`, `README.md`, and `CHANGELOG.md`; its DLL matches the build output, and its README/CHANGELOG match the committed release sources. The deployed DLL also matches byte-for-byte, and the active INI was preserved.

This final package adds post-playtest defensive hardening: separate first/third-person caches, camera-root resets, ranged-transition exclusion, three-sample seed/reseed validation, corrected diagnostic frustum angles, null-safe player access, and long-path build identity. Both test presets and the final independent source reviews pass. The exact final package has not received a separate in-game smoke test; the user-confirmed Eagle Eye parity evidence above applies to the preceding `96A622F8...C0F1` implementation of the sensitivity correction.

## Evidence required for a pass

For each runtime, record:

1. Exact `SkyrimSE.exe` or VR executable version and distribution.
2. Matching SKSE version.
3. Git commit and DLL SHA-256.
4. Clean startup log with mouse, thumbstick, and third-person hook installation.
5. First/third-person mouse behavior, gamepad behavior, smoothing, focus regain, menu gating, UI save/reload, and external INI reload results.
6. SmoothCam and Improved Camera results when those mods support the runtime.

Do not replace a pending result with an inference from CommonLibSSE support. Keep the row pending until the game was actually run.
