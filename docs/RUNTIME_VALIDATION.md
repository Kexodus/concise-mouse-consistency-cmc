# Runtime Validation

Project: **Concise Mouse Consistency (CMC)**.

CommonLibSSE-NG produces one multi-runtime DLL, but a successful build is not an in-game compatibility result. A row is **Passed** only after the listed build is launched through the matching SKSE version and the manual checks in `TEST_PLAN.md` succeed.

## Current matrix

| Runtime | Distribution | Status | Evidence |
|---|---|---|---|
| `1.5.97` | Steam SE | Pending | Runtime is not installed in the current test environment. |
| `1.6.640` | Steam AE | Pending | Runtime is not installed in the current test environment. |
| `1.6.1170` | Steam AE | Passed `0.53b` playtest; NG v7 static-md loads then 0.54b boot-CTD'd | 2026-08-14: SKSE 2.2.6, IC+SmoothCam, settled Eagle Eye `timeMult=0.250 timeComp=1`. Main/beta NG v7 `x64-windows` failed SKSE 126 (spdlog/fmt). Main `0.53.2` static-md loaded (handle 139, no 126). 2026-09-02: 0.54b static-md `bytes=715776` inits cleanly on SKSE 2.2.8 then CTDs ~17s after D3D with `ucrtbase` `_purecall` abort; CMC not on the dump stack. PostLoad UI deferral is the candidate fix; re-validate. |
| `1.7.99` | Steam AE | Pending | Requires SKSE 2.3.1 and Address Library format 5 (`versionlib-1-7-99-0.bin`). Not playtested. |
| `1.7.104` | Steam AE | Pending | Latest Steam AE. Requires SKSE 2.3.1 and Address Library format 5 (`versionlib-1-7-104-0.bin`). Not playtested. |
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

### 2026-08-12 `0.1.4` pre-runtime evidence

The release build composes the guarded half-rate yaw restoration with `1/globalTimeMult` compensation only for finite multipliers in the open interval `(0.05, 0.90)`. Dependency-free and CommonLibSSE-NG release tests pass, including exact boundary/adjacent values and the complete Eagle Eye composition. CPack generated `Concise-Mouse-Consistency-0.1.4.zip` with the intended four files.

- DLL: 523,776 bytes, SHA-256 `42C2EF9D2CA804656CEC416BE9384B56FB0C10922518B373E4418937B9EC8AF6`
- ZIP: 215,187 bytes, SHA-256 `359A0CCB01AD7F1682CF2C2F81E42410AF1E9EB261821B6E1FA59B41EB13F5FC`
- Runtime status: pending. The latest available CMC log records no Eagle Eye frames, so it cannot validate the new slow-time branch.

### 2026-08-14 `0.53b` playtest (Steam `1.6.1170`)

Session `14:34:43`–`14:39:16` in `MouseSensitivityFix.log`. SKSE 2.2.6 (`01064920` = 1.6.1170). `ImprovedCamera=yes SmoothCam=yes`. CrashLogger loaded; no crash dump from this session. SKSE loaded `MouseSensitivityFix.dll` correctly (handle 136). Playtest binary still identified as `version=0.1.4` `bytes=543744` (pre-bump); markers included `eagleEyeFovBoth=0 axisParity=1 timeCompWallRewrite=1 bowAimMouseFirstPersonOnly=1 thirdPersonPitchNormalize=0`. All four hooks installed; UI bridge registered; `Initialization complete`. No CMC errors or warnings.

Covered: first- and third-person freelook, bow out/pull, Eagle Eye enter/exit, UI Save to INI with live FP bow multipliers (`bowY=0.35` then restored), dual-cast freelook. No `ProcessThumbstick` samples (gamepad unused).

- Settled Eagle Eye: `timeMult=0.250 timeComp=1 mode=scale yawRatioToFreelook=0.997227`
- EE transitions: `YawTimeCompWallRewrite skipReason=disagree` (ramp `agree≈0.28`, collapsed `agree≈3.35`); no `YawTimeCompSkip`
- FP pitch after EE settle: `pitchNormalized=1` with `normalizedTargetPitchDelta/outY = freelookPitchPerLook=0.079554` while `engineTargetPitchDelta` diverged (e.g. `11.50` vs requested `-1.35` at ~44° VFOV)
- TP: every `ThirdPersonFinalAxisResponse` had `pitchNormalized=0`; smoothing `smoothingRemoved=600/600`
- FP-only reconstruct: after UI bow `0.35`, FP `bowPull` `out=engine*0.35`; TP `bowPull` stayed `out=engine` (`bowY=1`)
- Casting: `casting=1 halfRate=1 observedScale=0.500` restored yaw; `freelookYawPerLook` stayed ≈`0.052`

CPack generated `Concise-Mouse-Consistency-0.53b.zip` with only `Data/SKSE/Plugins/MouseSensitivityFix.dll`, `Data/SKSE/Plugins/MouseSensitivityFix.ini`, `README.md`, and `CHANGELOG.md`. Extracted DLL hash matches the build output. Packaged and dist INIs both have `bVerboseLogging=false`. The DLL was copied to the MO2 mod folder; the existing playtest INI was left in place.

- DLL: 543,744 bytes, SHA-256 `910B83FBB2AAFB524B839D688DE07938A2155B942712697832E2DF5DB3EA0A63`
- ZIP: 224,799 bytes, SHA-256 `F2B89279A5815EECA1E08F7954BCA5F98748FED0BAFBC58283A81B40CB812402`

### 2026-08-31 NG v7 1.6 load (Steam `1.6.1170` / SKSE 2.2.8)

The NG v7 `x64-windows` dynamic-triplet DLL failed SKSE `LoadLibrary` with Windows 126 (`spdlog.dll` / `fmt.dll` PE imports). Main `0.53.2` switched the plugin preset to `x64-windows-static-md` and loaded cleanly:

- SKSE 2.2.8 runtime `01064920` (1.6.1170). `plugin MouseSensitivityFix.dll ... loaded correctly (handle 139)`. No `couldn't load plugin 126` for CMC (`plugin 126` in the log is SKSE messaging another plugin's handle).
- `dumpbin /dependents` listed CRT/system DLLs only — no `spdlog.dll` / `fmt.dll`.
- That was a load/smoke confirmation of the static-md fix, not a replay of the 2026-08-14 gameplay matrix. Beta now uses the same preset; this 0.54b DLL still needs a 1.6.1170 reload to confirm.

## Evidence required for a pass

For each runtime, record:

1. Exact `SkyrimSE.exe` or VR executable version and distribution.
2. Matching SKSE version.
3. Git commit and DLL SHA-256.
4. Clean startup log with mouse, thumbstick, and third-person hook installation.
5. First/third-person mouse behavior, gamepad behavior, smoothing, focus regain, menu gating, UI save/reload, and external INI reload results.
6. SmoothCam and Improved Camera results when those mods support the runtime.

Do not replace a pending result with an inference from CommonLibSSE support. Keep the row pending until the game was actually run.
