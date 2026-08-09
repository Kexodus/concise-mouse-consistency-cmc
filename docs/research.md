# CMC Research Log

Cross-session findings. Read before re-investigating hooks, compatibility, or sensitivity math.

---

## Works

### Rendered Eagle Eye frustum read without camera mods (2026-08-09)
The `0.1.2` diagnostic DLL loaded with `ImprovedCamera=no SmoothCam=no` and captured a stable first-person Eagle Eye contraction from vertical FOV `3.604575` to `2.003606` (`fovRatio=0.555851`). This confirms `cameraRoot` child 0 points at the rendered `NiCamera` in the current vanilla camera stack. The frustum is useful for state and transition diagnostics, but its ratio must not scale mouse input.

### Final first-person bow yaw correction and live Y delta (2026-08-07)
Bow draw and Eagle Eye use the same final-stage `0.5` yaw scale previously confirmed during sprinting. Extending the guarded `PlayerCharacter::ModifyMovementData` correction to relocated bow attack states restored full horizontal sensitivity only when the observed scale was within `0.48..0.52`.

The bow Y path now preserves Skyrim's current engine delta instead of rebuilding it from the cached normal-play pixel scale. A forced `fBowAimMouseYMultiplier=0.1` playtest made Y dramatically slower, proving the configured Y multiplier reaches the camera. With both bow multipliers returned to `1.0`, the user confirmed normal bow draw and Eagle Eye felt correct. Runtime logs recorded 2,040 corrected bow frames; all sampled entries had `bowAiming=1`, `sprinting=0`, and restored yaw equal to twice the measured half-rate yaw within `0.000001` radians.

### Final first-person sprint yaw correction (2026-08-07)
`MovementHandlerAgentPlayerControls` computes final yaw as `postSensitivityX * deltaSeconds * pi * movementScale`. During active first-person sprinting, `movementScale` is exactly `0.5`; `PlayerCharacter::ModifyMovementData` otherwise passes that value through. Correcting at the `ModifyMovementData` vtable hook restores full yaw without changing normal input transforms.

The selective `0.48..0.52` scale check corrected 560/560 active sprint half-rate frames, left 66/66 sprint-tagged full-rate transition frames untouched, and left 278/278 normal frames untouched on Skyrim `1.6.1170`. Corrected output matched final player yaw within `0.000001` radians. The user confirmed the sprinting X-axis feel matched normal first-person look.

The proof-only per-frame movement, camera matrix, and prior-frame input telemetry was removed after validation. Production verbose logging retains low-frequency counters only.

### Third-person hooks work alongside IC + SmoothCam (2026-04-04)
With both ImprovedCameraSE v1.1.2.4228 and SmoothCam active (compatibility presets disabled), the original three CMC input/camera hooks installed and fired correctly on Skyrim 1.6.1170.0:
- `LookHandler::ProcessMouseMove` — transforms applied in both first- and third-person
- `LookHandler::ProcessThumbstick` — installed successfully
- `ThirdPersonState::HandleLookInput` — smoothing removal at 100% (`smoothingRemoved=total` at every checkpoint)

No judder, no conflict. IC's MinHook on `NiCamera` and `PlayerCamera` does not interfere with CMC's vtable hooks.

### Focus spike suppression works (2026-04-04)
Alt-tab out, wait >350ms, alt-tab back — first event zeroed (`camera=FocusSpikeSuppressed`), next event resumes normally. Single-frame suppression is sufficient; no visible camera jerk observed. Tested on 1.6.1170.0 with IC + SmoothCam active.

### EMA sampled scale converges correctly (2026-04-04)
`sampledScaleX` → 1.0, `sampledScaleY` → -1.0. The -1.0 on Y is correct — the engine inverts Y axis (positive raw pixel → negative `lookInputVec.y`). CMC still uses the sampled X scale to reconstruct the normal horizontal baseline during bow aim. The original sampled-Y reconstruction was later removed because it could become stale and bypass live sensitivity changes; bow Y now preserves the current engine delta.

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

### Gating Eagle Eye correction only on `PlayerCamera::bowZoomedIn` (2026-08-09)
The playtest captured six sampled `bowPull` events with a still-zoomed rendered frustum (`fovRatio=0.56..0.57`) after `bowZoomedIn` had cleared. This proves the flag does not describe the full visual transition. Use the live frustum when diagnosing zoom entry/exit; neither the flag nor the frustum should gate an input multiplier.

### Caching normal FOV while a ranged weapon remains out (2026-08-09)
On zoom exit, `bowZoomedIn` and bow aim cleared before the frustum fully expanded. The old cache condition accepted a `bowOut` sample at `3.340328` as the new normal, replacing the true `3.604575` baseline. Any diagnostic normal rendered FOV may only be sampled in true freelook: no ranged weapon out, no bow aim, and no zoom flag.

### Automatic Eagle Eye vertical FOV multiplier (2026-08-09)
Scaling bow Y by the rendered FOV ratio (`2.003606 / 3.604575 = 0.555851`) targets screen-space angular motion, not the requested freelook-equivalent mouse response, and makes X/Y use different targets. In the follow-up playtest, the normal-FOV baseline was deliberately absent because the bow began already drawn, so the multiplier stayed at `1.0`; the user confirmed both axes then felt correct. Preserve the configured bow Y multiplier across Eagle Eye and retain FOV only as diagnostics.

### First-person pitch from `PlayerCharacter::ModifyMovementData::rotationData.x` (2026-08-09)
Every freelook, bow, and Eagle Eye `SensRotation` record reported `rotPitch=0` even during deliberate vertical sweeps. The follow-up `RenderedRotation` probe also reported zero pitch from the active `NiCamera` world matrix while yaw changed. Neither source carries rendered first-person pitch, so the misleading pitch-ratio telemetry was removed.

### Eagle Eye Y via `PlayerCamera::firstPersonFOV` alone (2026-08-08)
Diagnostic build confirmed `bowZoomedIn=1` / `FirstPerson_EagleEye` while the user saw a real RMB zoom. Logged `normalFov`/`currentFov` both stayed `110` because those values came from `PlayerCamera::firstPersonFOV`, which does **not** change during Eagle Eye (Improved Camera keeps writing the configured base FOV). Live zoom is on `NiCamera::viewFrustum` (`2*atan(fTop/fNear)`). Read the frustum for diagnostics and keep `firstPersonFOV` only as `baseFov` diagnostics; the later A/B test rejected applying either value as an automatic input multiplier.

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
