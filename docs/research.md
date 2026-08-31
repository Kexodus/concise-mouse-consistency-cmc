# CMC Research Log

Cross-session findings. Read before re-investigating hooks, compatibility, or sensitivity math.

---

## Works

### Orphan half-rate restore + freelook yaw EMA poison reject (2026-08-12)
Half-rate policy is measurement-driven: exclusive `FP && looking` (no sprint/bow requirement; both-true person flags rejected). Restore still needs `observedScale∈[0.48,0.52]`; orphan cast/etc. also needs two consecutive in-band hits while policy-eligible, while sprint/bow hints may restore on the first hit. In-band streak resets when policy-ineligible. Freelook yaw EMA rejects half-scale samples and casting frames so `freelookYawPerLook` is not poisoned to ~0.5×. Casting/staff scans run only for verbose telemetry + EMA — not on the quiet restore hot path; restore stays measurement-driven (orphan band), not casting-triggered.

**Playtest 2026-08-14 (IC+SmoothCam):** dual-cast first-person freelook restored at `casting=1 halfRate=1 observedScale=0.500`; `freelookYawPerLook` stayed ≈`0.052` (not poisoned to ~0.5×).

### timeComp ScaleByCurrent + Disagree wall rewrite (2026-08-12)
Settled Eagle Eye with `agree≈1` uses `yaw/Current` (`TimeCompMode::ScaleByCurrent`). During EE transitions, `ModifyMovementData` `delta` disagrees with `Current` in two playtest-proven modes: **ramp** (`delta≈0.004167`, `rtd≈0.016667`, `timeMult≈0.89`, `agree≈0.28`) and **collapsed** (`delta==rtd`, `timeMult≈0.30`, `agree≈3.3`). Applying `1/Current` on ramp only multiplies by ~1.13 and leaves yaw at ~0.28× wall — does **not** fix feel. Production therefore **never ScaleByCurrent on Disagree/Unstable**; with a valid `realTimeDelta` it applies `RewriteWallClock`: after half-rate, `yaw = lookX * realTimeDelta * π`. Hard skip (`None`) only when wall/rtd is missing. FP pitch normalize pauses while dilated+looking yaw was left uncompensated (`pitchPauseOnYawSkip=1`). Build markers: `timeCompWallRewrite=1 pitchPauseOnYawSkip=1`. Prefers `BSTimer` wall-clock at binary `+0x1C`; steady_clock fallback rejects >250ms gaps. Half-rate restore remains independent.

**Playtest 2026-08-14 (IC+SmoothCam, Steam 1.6.1170):** wall rewrite confirmed live. Ramp/collapsed disagree logged `YawTimeCompWallRewrite` (no `YawTimeCompSkip`); settled EE `timeMult=0.250 timeComp=1 mode=scale yawRatioToFreelook=0.997`. FP `pitchNormalized=1` held frozen `freelookPitchPerLook=0.079554` through settled EE (~44° VFOV) while `engineTargetPitchDelta` diverged. TP `pitchNormalized=0`. After UI bow multipliers `0.35`, FP `bowPull` reconstructed and TP bow stayed engine 1:1 (`bowAimMouseFirstPersonOnly`).

### BSTimer `realTimeDelta` at +0x1C (pad0C) (2026-08-12)
Older CommonLibSSE-NG `BSTimer` omitted `pad0C` after `lastPerformanceCount`, so `timer->realTimeDelta` aliased binary `delta` at `0x18`. Agreement then became `~1/Current` (~4 under EE) and settled timeComp was always skipped (`Disagree`). Production reads realTimeDelta via explicit `+0x1C` (CommonLibVR/SE/AE layout) with `static_assert` on the constant; wall-clock fallback when the singleton value is unusable. Build marker: `bstimerRealtimeOffset=1`. alandtse NG v7.0.0 matches the binary layout (`realTimeDelta` at `0x1C`); keep the explicit offset anyway. Confirmed live in 2026-08-12 playtest (`rtd≈0.016667` while `delta≈0.004167`); the remaining axis bug was disagree-skip, not the offset.

### Third-person pitch normalization on `freeRotation.y` — disabled (2026-08-10)
Third person previously only had input transforms plus smoothing removal. A mirror of the first-person pitch normalizer against `freeRotation.y` was tried (`thirdPersonPitchNormalize=1`): calibrate freelook pitch-per-look, then rewrite non-freelook deltas including `bowOut`. **Playtest with IC+SmoothCam + default profile rejected it:** third person with a bow merely equipped/out felt extreme X-low / Y-high. Logs had no verbose samples (`bVerboseLogging=false`) but BuildIdentity confirmed the TP pitch-normalize binary; half-rate yaw restore was already first-person-only; bow multipliers were 1.0. Root cause: TP pitch normalize activated for `bowOut`/`bowPull`/`eagleEye` and forced freelook-equivalent Y while X stayed at the camera-mod/engine rate. Production removes the normalizer and its extra `ThirdPersonState::Update` hook; passive `ThirdPersonFinalAxisResponse` sampling remains in `HandleLookInput`. Bow aim mouse reconstruction is also gated first-person-only (`bowAimMouseFirstPersonOnly=1`). Do not re-enable TP pitch normalize for ranged states without a dedicated IC+SmoothCam A/B.

### Persistent first-person pitch normalization (2026-08-10)
`FirstPersonState::targetPitchOffset` is normalized against a runtime true-freelook pitch-per-output baseline after three consistent samples. The normalized target persists across vertical, horizontal, and idle updates, so Skyrim's interpolation/clamping cannot overwrite it. In the final verbose playtest, all 1,456 eligible updates were corrected; 204 zero-Y normalized frames held the target exactly (`normalizedTargetPitchDelta=0`) even though Skyrim attempted downstream target changes as large as `12.0906`. Unzoomed bow-pull yaw remained within approximately `0.997..1.007×` the freelook baseline, and the user confirmed those tested states felt correct. Later end-user logs showed that claim does not extend to active Eagle Eye slow-time (see below).

The initial v2 run also showed that continuously adapting the pitch baseline can ingest bow-exit interpolation and temporarily move far outside the true `~0.08` gain. Production v3 therefore freezes the baseline after its initial three-sample true-freelook calibration. This does not remove telemetry; normal deployments keep `bVerboseLogging=false` while the same probes remain available when needed.

### Final first-person pitch target telemetry proves state/FOV-dependent Y gain (2026-08-10)
Hooking `FirstPersonState::Update` and correlating each mouse event with `targetPitchOffset` finally exposes the downstream vertical response. Across 774 telemetry records, input remained exactly `engineX/rawX=1`, `engineY/rawY=-1`, and CMC output remained 1:1 in every state. Clean one-event vertical samples produced these target-pitch gains per raw pixel: freelook `~0.0808`, bow-out `~0.0794`, unzoomed bow-pull `~0.1034`, and fully settled Eagle Eye (`VFOV < 31°`) `~0.0666`. Therefore unzoomed bow pitch is about `1.28×` freelook, while fully zoomed Eagle Eye pitch is about `0.824×` freelook and `0.645×` unzoomed bow-pull.

Within Eagle Eye, target-pitch gain tracks live rendered VFOV almost perfectly (`correlation=0.999787`): approximately `0.0666` at `29.5°`, `0.0842` at `38.3°`, `0.0915` at `42.2°`, and `0.1004` at `47.2°`. This proves the uneven Y behavior occurs downstream of `LookHandler::ProcessMouseMove` and varies continuously through zoom transitions; it is not caused by CMC applying unequal input multipliers. `currentPitchOffset` follows the target with a per-frame smoothing/step limit, while the `FirstPersonState::GetRotation` quaternion changes consistently with pitch.

### Rendered Eagle Eye frustum read without camera mods (2026-08-09)
The `0.1.2` diagnostic DLL loaded with `ImprovedCamera=no SmoothCam=no` and captured a stable first-person Eagle Eye contraction in the active `NiCamera` frustum. The original diagnostic incorrectly divided the tangent-plane edges by `fNear`, producing values `3.604575` and `2.003606` with ratio `0.555851`. Correct edge-angle conversion yields approximately `50.53°` and `29.40°`, with angular ratio `0.582`. The camera scan now uses checked RTTI across the root's children.

### Final first-person bow yaw correction and live Y delta (2026-08-07)
Bow draw and Eagle Eye use the same final-stage `0.5` yaw scale previously confirmed during sprinting. Extending the guarded `PlayerCharacter::ModifyMovementData` correction to relocated bow attack states restored full horizontal sensitivity only when the observed scale was within `0.48..0.52`.

The bow Y path now preserves Skyrim's current engine delta instead of rebuilding it from the cached normal-play pixel scale. A forced `fBowAimMouseYMultiplier=0.1` playtest made Y dramatically slower, proving the configured Y multiplier reaches the camera. With both bow multipliers returned to `1.0`, the user confirmed normal bow draw and Eagle Eye felt correct. Runtime logs recorded 2,040 corrected bow frames; all sampled entries had `bowAiming=1`, `sprinting=0`, and restored yaw equal to twice the measured half-rate yaw within `0.000001` radians.

### Final first-person sprint yaw correction (2026-08-07)
`MovementHandlerAgentPlayerControls` computes final yaw as `postSensitivityX * deltaSeconds * pi * movementScale`. During active first-person sprinting, `movementScale` is exactly `0.5`; `PlayerCharacter::ModifyMovementData` otherwise passes that value through. Correcting at the `ModifyMovementData` vtable hook restores full yaw without changing normal input transforms.

The selective `0.48..0.52` scale check corrected 560/560 active sprint half-rate frames, left 66/66 sprint-tagged full-rate transition frames untouched, and left 278/278 normal frames untouched on Skyrim `1.6.1170`. Corrected output matched final player yaw within `0.000001` radians. The user confirmed the sprinting X-axis feel matched normal first-person look.

The proof-only per-frame movement, camera matrix, and prior-frame input telemetry was removed after validation. Production verbose logging retains low-frequency counters plus sampled sensitivity, yaw, and rendered-frustum diagnostics.

### Third-person hooks work alongside IC + SmoothCam (2026-04-04)
With both ImprovedCameraSE v1.1.2.4228 and SmoothCam active, the original three CMC input/camera hooks installed and fired correctly on Skyrim 1.6.1170.0:
- `LookHandler::ProcessMouseMove` — transforms applied in both first- and third-person
- `LookHandler::ProcessThumbstick` — installed successfully
- `ThirdPersonState::HandleLookInput` — smoothing removal at 100% (`smoothingRemoved=total` at every checkpoint)

No judder, no conflict. IC's MinHook on `NiCamera` and `PlayerCamera` does not interfere with CMC's vtable hooks. Product default keeps CMC third-person smoothing removal with camera mods (`bKeepThirdPersonSmoothingRemovalWithCameraMods=true`); set false to restore legacy reduced-intervention (skip CMC 3P smoothing).

### Focus spike suppression works (2026-04-04)
Alt-tab out, wait >350ms, alt-tab back — first event zeroed (`camera=FocusSpikeSuppressed`), next event resumes normally. Single-frame suppression is sufficient; no visible camera jerk observed. Tested on 1.6.1170.0 with IC + SmoothCam active.

### EMA sampled scale converges correctly (2026-04-04)
`sampledScaleX` → 1.0, `sampledScaleY` → -1.0. The -1.0 on Y is correct — the engine inverts Y axis (positive raw pixel → negative `lookInputVec.y`). CMC still uses the sampled X scale to reconstruct the normal horizontal baseline during bow aim. The original sampled-Y reconstruction was later removed because it could become stale and bypass live sensitivity changes; bow Y now preserves the current engine delta.

First- and third-person caches are isolated because camera mods can use different pixels-to-look scales. Sampling rejects ranged draw/zoom transitions, consumed zero-output events, and implausible outliers. An isolated sign or scale anomaly cannot poison the EMA; three mutually consistent candidates are required to seed or deliberately reseed after a legitimate input-scale change.

### `x64-windows-static-md` loads on 1.6.1170 without spdlog/fmt DLLs (2026-08-31)
After the NG v7 dynamic-triplet 126 failure, the plugin-release preset `x64-windows-static-md` build loaded on Steam 1.6.1170 / SKSE 2.2.8: handle 139, `BuildIdentity` + `Initialization complete`, all four hooks installed, no CMC errors. Deployed DLL matched the build hash; `dumpbin /dependents` had no `spdlog.dll` / `fmt.dll`. Do not revert to `x64-windows` for the plugin.

### CommonLibSSE-NG v7.0.0 Address Library format 5 (2026-08-31)
One multi-runtime DLL stays on [alandtse/CommonLibSSE-NG](https://github.com/alandtse/CommonLibSSE-NG) `v7.0.0` (`8b032fa`), not [powerof3/CommonLibSSE](https://github.com/powerof3/CommonLibSSE). po3 is AE-only and would drop SE/VR. NG v7 parses Address Library format 5 (`IDDB::load_v5` / `Format::SSEv5`), defines `RUNTIME_SSE_1_7_99`, and still enables SE+AE+VR. `RUNTIME_SSE_1_7_104` is not named in this tag; REL loads `versionlib-1-7-104-0.bin` from the exe version. CMC exports `SKSEPlugin_Version` as `PluginVersionData` with Post-AE Address Library, Address Library v5, and NoStructUse. Generated `PluginDeclaration` Independent is only NoStructUse (value 1) and would miss v5. SKSE 2.3.1 requires v5 on 1.7.99+; SKSE 2.2.6 and 2.2.8 ignore unknown `versionIndependenceEx` bits and still honor NoStructUse (v5 is not a 1.6 load failure). NG v7 `PlayerCamera` FOV/yaw/`bowZoomedIn` live in `RUNTIME_DATA2` (`GetRuntimeData2()`, SE `0x13C` / VR `0x158`); direct members do not compile in a multi-runtime build. ActorState remains the 1.6.629 breakpoint (SE 0xC0/AE 0xC8); NG v7 does not add a 1.7 ActorState layout shift. BSTimer in v7 has `realTimeDelta` at `0x1C`; CMC still reads that explicit offset. In-game 1.7.99/1.7.104 playtest is still required.

### Relocated ActorState access for AE 1.6.629+ (2026-04-04)
Using `REL::RelocateMemberIfNewer` to read `ActorState1` (SE offset 0xC0, AE offset 0xC8) and `ActorState2` (SE 0xC4, AE 0xCC) directly from the `PlayerCharacter` pointer fixes bow detection on AE runtimes. Confirmed working on 1.6.1170.0: `atkState` cycles through real values (8=kBowDraw, 9=kBowAttached, 10=kBowDrawn, 12=kBowReleased), `wpnDrawn` toggles correctly, `bowDiag` fires on every frame during aim. This is the correct pattern for any `ActorState` field access in a multi-runtime NG build.

### Exact FP/TP camera classification for look correction (2026-08-12)
`PollLookCorrectionContext` must set `firstPerson` / `thirdPerson` from `PlayerCamera::IsInFirstPerson()` / `IsInThirdPerson()` independently. Inferring `firstPerson = !IsInThirdPerson()` falsely treats mount / furniture / bleedout / dragon (and null camera) as first-person, which could unlock half-rate restore or timeComp outside real FP/TP. Bow-aim mouse reconstruction and FP pitch telemetry windows are likewise gated on real first-person.

### Exact FP/TP for input transforms + freelook scale cache (2026-08-12)
`ShouldApplyInputTransform` and freelook scale sampling must also use exact person flags. Treating `!inThirdPerson` as first-person poisoned FP transforms and `g_firstPersonSampledScale` (bow X reconstruct) on mount/furniture/bleedout/dragon/null camera. Production: FP hook only when real FP; TP hook only when real TP; neither → no transform and no scale-cache update/select.

### Eagle Eye / rendered-zoom classification independent of verbose logging (2026-08-12)
`ReadRenderedFov` / `renderedZoomedIn` feed `effectiveBowZoomedIn` → aim state + `g_eagleEyeMouseFrames`. Computing them only under `bVerboseLogging` left quiet playtests at `eagleEyeFrames=0` and wrong aim tags. Frustum classification always runs; only sampled log emission stays verbose-gated. Telemetry must not change aim behavior based on the verbose flag.

### FP pitch normalize idle eligibility uses live aim state (2026-08-12)
Idle `FirstPersonState::Update` frames must not use stale `g_lastAimState` / `g_lastTrueFreelookEligible` from the last mouse event (stale bow kept normalize on in freelook). Live bow/drawn/sheathed drive eligibility. Freelook aim state = do not normalize even when `!trueFreelookEligible` (ranged WantToDraw/Drawing); sprint/casting exclude calibration only (must not invert freelook into normalize-eligible). Small settle window after aim transitions; TP pitch normalize stays disabled.

### Engine input-stage deltas are 1:1 during bow aim (2026-04-04)
Added diagnostic logging of raw OS pixels (`rawPx`), engine output (`engine`), and bow multipliers (`bowMul`). Result: `|rawPx.X| == |engine.X|` and `|rawPx.Y| == |engine.Y|` on every frame — engine only inverts Y sign, no per-axis attenuation. This is identical between freelook and bow aim. The later 2026-08-09 A/B test established that the requested freelook-equivalent target keeps `fBowAimMouseYMultiplier=1.0`; scaling Y by the visual FOV ratio was the wrong response target.

This finding describes the input hook only. Skyrim later applies a separate `0.5` horizontal scale at final player rotation during bow aim; CMC corrects that downstream stage without reconstructing Y.

### IC does not hook weapon/actor state queries (2026-04-04, verified via source)
Reviewed [ImprovedCameraSE-NG](https://github.com/ArranzCNL/ImprovedCameraSE-NG). IC does NOT hook or intercept:
- `Actor::IsWeaponDrawn()`
- `Actor::GetEquippedObject()`
- `ActorState::GetAttackState()`

IC reads these passively via its own `IsAiming()` helper. Any bow detection failures in CMC are CMC bugs, not IC interference.

---

## Doesn't Work

### NG v7 + default `x64-windows` dynamic vcpkg triplet (2026-08-31)
alandtse CommonLibSSE-NG `v7.0.0` PUBLIC-links vcpkg spdlog/fmt. Building the plugin with `VCPKG_TARGET_TRIPLET=x64-windows` produces PE imports of `spdlog.dll` and `fmt.dll`. Those DLLs are not packaged or deployed, so SKSE `LoadLibrary` fails with Windows 126 (`ERROR_MOD_NOT_FOUND`) — logged as `couldn't load plugin 126`. Confirmed on Skyrim 1.6.1170 / SKSE 2.2.8: plugin alone → 126; plugin plus those two DLLs beside it → success. Address Library v5 metadata is not the 1.6 failure (SKSE 2.2.6/2.2.8 ignore unknown `versionIndependenceEx` bits). 0.1.5-beta had no spdlog/fmt PE imports. **Fix:** plugin preset `x64-windows-static-md` (static libs, dynamic CRT). Do not ship spdlog/fmt next to the plugin, and do not use fully static `x64-windows-static` (that static-links the CRT, which is wrong for SKSE plugins).

### powerof3/CommonLibSSE as the sole CMC dependency (2026-08-31)
po3 CommonLibSSE `dev` has `RUNTIME_SSE_1_7_99` / `RUNTIME_SSE_1_7_104` and defaults `versionIndependenceEx` to Address Library v5, but it is AE-only. Switching CMC onto it would drop SE/VR (and the NG multi-runtime RelocateMemberIfNewer path). Stay on alandtse CommonLibSSE-NG and declare v5 in CMC's own `SKSEPlugin_Version`.

### Generated `PluginDeclaration` Independent without Address Library v5 (2026-08-31)
`add_commonlibsse_plugin` emits `SKSE::PluginDeclaration` whose `StructCompatibility::Independent` is `1` (`NoStructUse` only). SKSE 2.3.1 on 1.7.99+ requires `kVersionIndependentEx_AddressLibraryV5`. Do not restore generated plugin metadata without ORing that bit. CMC writes `PluginVersionData` in `src/main.cpp` instead.

### Third-person freelook-parity pitch normalize with bow out + IC/SmoothCam (2026-08-10)
Rewriting `ThirdPersonState::freeRotation.y` to freelook pitch-per-look whenever `aimState != freelook` (including `bowOut` with weapon drawn but not aiming) produced goofy asymmetric look under Improved Camera + SmoothCam on the default profile. Do not treat FP pitch-normalize success as proof that the same policy is safe on TP `freeRotation.y`. Keep CMC 3P to input transforms + optional smoothing removal until measured otherwise.

### Eagle Eye slow-time leaves yaw at ~0.25× freelook while pitch normalize stays realtime (2026-08-10)
End-user `0.1.3` verbose log (`eagleEyeFovBoth=0`, `finalPitchNormalize=3`, IC+SmoothCam, compat presets off). Input stage stayed 1:1 in every state (`outOverEngineY=1`, `eagleEyeY=1`, `bowY=1`). Half-rate bow yaw restore still fired during Eagle Eye (`restoredYaw=2×engineYaw`, observed scale in `0.48..0.52`).

Unzoomed `bowPull` kept `yawRatioToFreelook≈1.00` with `delta≈0.0145`. Active `eagleEye` dropped `delta` to `≈0.003625` (exactly `0.25×`) and `yawRatioToFreelook` locked at `≈0.250` after the half-rate restore — matching vanilla Eagle Eye slow-time, not a failed 0.5→1.0 correction. Final pitch normalize still rewrote `targetPitchOffset` from realtime look Y (`pitchNormalized=1`, `requested=outY×freelookPitchPerLook`), so Y tracks freelook wall-clock gain while X tracks game-time delta.

Net feel: Eagle Eye X ≈ `0.25×` freelook, Y ≈ freelook → strong axis mismatch. Settled EE frustum here was ~`43°` VFOV / `fovRatio≈0.64` with base FOV 100 under IC+SmoothCam.

**Fix (2026-08-10, later superseded):** After half-rate restore, multiplying yaw by `1/timeMult` when the mult is in `(0.05, 0.90)` was the first timeComp attempt. Settled Eagle Eye with `agree≈1` still uses that as `ScaleByCurrent`. Disagree/Unstable with valid `rtd` now uses `RewriteWallClock` instead of skip — see Works “timeComp ScaleByCurrent + Disagree wall rewrite”. Do not treat pre-fix `yawRatioToFreelook≈0.25` during Eagle Eye as a half-rate-hook failure.

**Architecture note (2026-08-12):** Half-rate restore and time compensation are separate policy flags. `compensateTimeYaw` is `timeDilated && exclusive(FP|TP) && looking` — not bow-gated — so third-person Slow Time / similar dilations get wall-clock yaw without requiring bow aim. Half-rate is exclusive FP && looking with the `0.48..0.52` observed-scale band (sprint/bow are hints only). EE transition disagree with valid `rtd` uses `RewriteWallClock`, not skip — see Works.

**BSTimer Current vs Target (2026-08-12):** Local CommonLib's `BSTimer::GetCurrentGlobalTimeMult()` relocates `RELOCATION_ID(511883, 388443)` = `QGlobalTimeMultiplierTarget`, not Current. CMC now reads Current via `RELOCATION_ID(511882, 388442)` in its own helper. Using Target can leave `timeMult≈1` while Eagle Eye is already dilated (or show a stale pending value), so `timeComp` never fires. Do not call the CommonLib helper for yaw timeComp until upstream is fixed.

**Playtest 2026-08-12 (`0.1.4`, `eagleEyeYawTimeComp=1 timeMultCurrent=1`, IC+SmoothCam):** Fix is live and fires. Settled Eagle Eye with `timeMult=0.250` + `timeComp=1` reached `yawRatioToFreelook≈0.997` (`delta≈0.004167` ≈ wall/4). **Not a Current-vs-Target miss.** Perceived regression comes from **`ModifyMovementData` `delta` disagreeing with Current during EE zoom transitions:** e.g. `delta≈0.0042` with `timeMult≈0.769` → after `1/timeMult` still `yawRatio≈0.327`; and `delta≈0.0168` with `timeMult≈0.282` → `yawRatio≈3.58`. Pitch normalize stays wall-clock (`pitchNormalized=1`), so transitions recreate the old X/Y mismatch even though settled EE is fixed. Zero `halfRate=1` samples this run (bow/EE observedScale vs game-time delta stayed ~1.0).

**Playtest 2026-08-12 (`bstimerRealtimeOffset=1`, bytes=537600, 16:22–16:25):** Offset fix confirmed (`rtd` correct). Root cause of remaining slow X: `timeCompAgreeGate` hard-skipped most dilated EE frames (`YawTimeCompSkip skipReason=disagree`); when timeComp did apply, `outputRatioToExpected=1`. Ramp disagree math: `delta=0.004167 rtd=0.016667 timeMult=0.885843 agree=0.282` → `/Current` only ×1.13 → still ~0.28× wall. Collapsed: `delta==rtd` with dilated Current → `agree≈3.3`. **Ship fix:** Disagree/Unstable + valid rtd → `RewriteWallClock` (`lookX * rtd * π`); ScaleByCurrent only when `agree≈1`; pause FP pitch normalize if yaw left uncompensated. Markers: `timeCompWallRewrite=1 pitchPauseOnYawSkip=1`.

### Magic / casting applies half-rate yaw while tagged freelook (2026-08-12)
Same session: after sheathing, `ControlledCasting` `RequestCast` spam coincides with freelook `YawRotation` samples at `yawPerLook≈0.026` (exactly `0.5×` the `≈0.052` freelook baseline) while `sprinting=0`, `bowAim=0`, `timeMult=1`, `yawCorrected=0`. Half-rate policy was only `FP && (sprint || bowAim)`, so casting never became eligible. Freelook yaw EMA also ingested those half-rate frames and poisoned `freelookYawPerLook` down to `≈0.026`. **Ship fix is in production** (orphan half-rate + EMA poison reject + casting telemetry) — see Works. This entry keeps the original failure mode; it is not a pending playtest.

### Both-axes Eagle Eye FOV scaling without final pitch proof (2026-08-10)
A diagnostic build multiplied both input axes by live `currentVFov/normalVFov`, reaching `0.581687` at full Eagle Eye. NDJSON proved identical input-stage ratios (`outOverEngineX == outOverEngineY`), but the user reported Y then felt less sensitive than X. The same run proved X still receives its downstream half-rate yaw restoration, while no equivalent final pitch measurement existed. The change was removed. Do not apply FOV-based input scaling to either axis until final rendered pitch and yaw response per raw pixel is measured in every state.

### Gating Eagle Eye correction only on `PlayerCamera::bowZoomedIn` (2026-08-09)
The playtest captured six sampled `bowPull` events with a still-zoomed rendered frustum (`fovRatio=0.56..0.57`) after `bowZoomedIn` had cleared. This proves the flag does not describe the full visual transition. Use the live frustum when diagnosing zoom entry/exit; neither the flag nor the frustum should gate an input multiplier.

### Caching normal FOV while a ranged weapon remains out (2026-08-09)
On zoom exit, `bowZoomedIn` and bow aim cleared before the frustum fully expanded. The old cache condition accepted a `bowOut` sample at `3.340328` as the new normal, replacing the true `3.604575` baseline. Any diagnostic normal rendered FOV may only be sampled in true freelook: no ranged weapon out, no bow aim, and no zoom flag.

### Automatic Eagle Eye vertical FOV multiplier (2026-08-09)
Scaling **only** bow Y by the rendered FOV ratio makes X/Y use different targets and was rejected. The discarded build used the legacy frustum proxy ratio (`2.003606 / 3.604575 = 0.555851`). A follow-up A/B with inactive FOV correction (`normalFov=0`) felt correct and temporarily locked freelook-equivalent parity as the production target. Do not revive Y-only FOV scaling.

### First-person pitch from `PlayerCharacter::ModifyMovementData::rotationData.x` (2026-08-09)
Every freelook, bow, and Eagle Eye `SensRotation` record reported `rotPitch=0` even during deliberate vertical sweeps. The follow-up `RenderedRotation` probe also reported zero pitch from the active `NiCamera` world matrix while yaw changed. A 2026-08-10 `FirstPersonState::Update` run further rejected `firstPersonCameraObj` local/world Euler fields, `PlayerCamera::yaw`, and `rotationInput` for per-update deltas (all zero in 774/774 records). Use `FirstPersonState::targetPitchOffset` for input-to-final-pitch target gain and its `GetRotation` quaternion/current offset for convergence.

### Eagle Eye Y via `PlayerCamera::firstPersonFOV` alone (2026-08-08)
Diagnostic build confirmed `bowZoomedIn=1` / `FirstPerson_EagleEye` while the user saw a real RMB zoom. Logged `normalFov`/`currentFov` both stayed `110` because those values came from `PlayerCamera::firstPersonFOV`, which remained at the configured base value in the tested vanilla camera stack. Live zoom is on `NiCamera::viewFrustum`; convert its tangent edges with `atan(top) - atan(bottom)` vertically and `atan(right) - atan(left)` horizontally. Read the frustum for diagnostics and keep `firstPersonFOV` only as `baseFov` diagnostics; the later A/B test rejected applying either value as an automatic input multiplier.

### Eagle Eye multiplier degrees/radians heuristic (2026-08-08)
During the discarded automatic-scaling experiment, playtest logged `normalFov=5.117778` → `currentFov=3.008229` but `eagleEyeY=1.0`. `CalculateEagleEyeVerticalMultiplier` treated values `> pi` as degrees and `<= pi` as radians, so the pair was mixed and clamped to 1. Interpreting both as degrees fixed that experiment, but the entire FOV multiplier was later removed after the behavioral A/B test.

### Why prior logs could not prove bow/Eagle Eye 1:1 (2026-08-08)
Missing evidence, not proof of success:
1. No freelook baseline (`yawPerLook` / `pitchPerLook`) to compare against bow-out / bow-pull / eagle-eye.
2. No distinct `bowOut` vs `bowPull` state tags.
3. `lastRaw==lastOut` only proves CMC passthrough, not equal world angle per mouse move.
4. Eagle Eye samples often logged `bowZoomedIn=1` while `currentFov≈normalFov` (zoom flag before frustum settles, or zoom never applied that frame), so `eagleEyeY` stayed 1.0 with no FOV-narrowed capture.

Added `SensitivityProbe` (rawPx/engine/out/state/fov/frustum) and `YawRotation` (look vs final yaw and ratio to the freelook EMA), forced on state changes and the first FOV-narrowed Eagle Eye frame. The attempted pitch fields were later removed because neither tested rotation source carried first-person pitch.

### `kIronSights` camera state fallback for bow aim detection (2026-04-04)
CMC's `DetectBowAim()` falls back to checking `camera->currentState->id == RE::CameraStates::kIronSights`. This never fires — bow aiming in first-person stays in `kFirstPerson` camera state (with or without IC). The IronSights state is not used for bow aim. This fallback should be removed.

### `IsBowDrawn()` comment claims `GetAttackState()` but code doesn't use it (2026-04-04)
The function comment says "Uses ActorState::GetAttackState() — OAR-proof" but the implementation only checks `IsWeaponDrawn()` + `GetEquippedObject(false)` + weapon type. It never calls `GetAttackState()`. IC's working `IsAiming()` approach checks `GetAttackState()` is in range `kBowDraw..kBowNextAttack`, which is the correct detection method.

### `bowDiag=.` throughout entire playtest despite bow being drawn (2026-04-04)
3870 mouse events, zero bow aim detections with the original `IsBowDrawn()` approach (`IsWeaponDrawn()` + `GetEquippedObject(false)` + weapon type).

### Direct `player->GetAttackState()` / `IsWeaponDrawn()` broken on AE 1.6.629+ (2026-04-04)
These methods read from compile-time base class offsets (SE layout). On AE 1.6.629+, `TESObjectREFR` grew by 8 bytes (`REFERENCE_RUNTIME_DATA` shifted 0x88→0x90), pushing `ActorState` from offset 0xB8 to 0xC0 within `Actor`. Since `ActorState` is a C++ base class, the compiler bakes in the SE offset — so `player->GetAttackState()` reads 8 bytes before the real data, returning frozen garbage (`atkState=1 wpnDrawn=0` permanently). **Do not call these methods directly.** Use `RelocateMemberIfNewer` helpers instead (see "Relocated ActorState access" in Works section).

---

## Hard Limits

### IC suppresses `kIronSights` camera transition during bow aim (2026-04-04, verified via source)
IC keeps the camera in `kFirstPerson` state throughout bow drawing/aiming — it manages bow state internally via its own camera system (`FirstPerson.cpp` state machine). Do not rely on `kIronSights` camera state for bow detection when IC is active. Use `GetAttackState()` range checks instead.
