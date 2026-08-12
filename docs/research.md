# CMC Research Log

Cross-session findings. Read before re-investigating hooks, compatibility, or sensitivity math.

---

## Works

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

### Relocated ActorState access for AE 1.6.629+ (2026-04-04)
Using `REL::RelocateMemberIfNewer` to read `ActorState1` (SE offset 0xC0, AE offset 0xC8) and `ActorState2` (SE 0xC4, AE 0xCC) directly from the `PlayerCharacter` pointer fixes bow detection on AE runtimes. Confirmed working on 1.6.1170.0: `atkState` cycles through real values (8=kBowDraw, 9=kBowAttached, 10=kBowDrawn, 12=kBowReleased), `wpnDrawn` toggles correctly, `bowDiag` fires on every frame during aim. This is the correct pattern for any `ActorState` field access in a multi-runtime NG build.

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

### Third-person freelook-parity pitch normalize with bow out + IC/SmoothCam (2026-08-10)
Rewriting `ThirdPersonState::freeRotation.y` to freelook pitch-per-look whenever `aimState != freelook` (including `bowOut` with weapon drawn but not aiming) produced goofy asymmetric look under Improved Camera + SmoothCam on the default profile. Do not treat FP pitch-normalize success as proof that the same policy is safe on TP `freeRotation.y`. Keep CMC 3P to input transforms + optional smoothing removal until measured otherwise.

### Eagle Eye slow-time leaves yaw at ~0.25× freelook while pitch normalize stays realtime (2026-08-10)
End-user `0.1.3` verbose log (`eagleEyeFovBoth=0`, `finalPitchNormalize=3`, IC+SmoothCam, compat presets off). Input stage stayed 1:1 in every state (`outOverEngineY=1`, `eagleEyeY=1`, `bowY=1`). Half-rate bow yaw restore still fired during Eagle Eye (`restoredYaw=2×engineYaw`, observed scale in `0.48..0.52`).

Unzoomed `bowPull` kept `yawRatioToFreelook≈1.00` with `delta≈0.0145`. Active `eagleEye` dropped `delta` to `≈0.003625` (exactly `0.25×`) and `yawRatioToFreelook` locked at `≈0.250` after the half-rate restore — matching vanilla Eagle Eye slow-time, not a failed 0.5→1.0 correction. Final pitch normalize still rewrote `targetPitchOffset` from realtime look Y (`pitchNormalized=1`, `requested=outY×freelookPitchPerLook`), so Y tracks freelook wall-clock gain while X tracks game-time delta.

Net feel: Eagle Eye X ≈ `0.25×` freelook, Y ≈ freelook → strong axis mismatch. Settled EE frustum here was ~`43°` VFOV / `fovRatio≈0.64` with base FOV 100 under IC+SmoothCam.

**Fix (2026-08-10, pending playtest):** After half-rate restore, multiply yaw by `1/BSTimer::GetCurrentGlobalTimeMult()` when the mult is in `(0.05, 0.90)`. That converts game-time yaw to wall-clock freelook equivalence. Build marker: `eagleEyeYawTimeComp=1`. Verbose logs add `timeMult` / `timeComp`. Do not treat pre-fix `yawRatioToFreelook≈0.25` during Eagle Eye as a half-rate-hook failure.

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
