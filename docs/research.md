# CMC Research Log

Cross-session findings. Read before re-investigating hooks, compatibility, or sensitivity math.

---

## Works

### Third-person hooks work alongside IC + SmoothCam (2026-04-04)
With both ImprovedCameraSE v1.1.2.4228 and SmoothCam active (compatibility presets disabled), all three CMC hooks installed and fired correctly on Skyrim 1.6.1170.0:
- `LookHandler::ProcessMouseMove` — transforms applied in both first- and third-person
- `LookHandler::ProcessThumbstick` — installed successfully
- `ThirdPersonState::HandleLookInput` — smoothing removal at 100% (`smoothingRemoved=total` at every checkpoint)

No judder, no conflict. IC's MinHook on `NiCamera` and `PlayerCamera` does not interfere with CMC's vtable hooks.

### Focus spike suppression works (2026-04-04)
Alt-tab out, wait >350ms, alt-tab back — first event zeroed (`camera=FocusSpikeSuppressed`), next event resumes normally. Single-frame suppression is sufficient; no visible camera jerk observed. Tested on 1.6.1170.0 with IC + SmoothCam active.

### EMA sampled scale converges correctly (2026-04-04)
`sampledScaleX` → 1.0, `sampledScaleY` → -1.0. The -1.0 on Y is correct — the engine inverts Y axis (positive raw pixel → negative `lookInputVec.y`). The scale encodes both magnitude and sign, so bow aim reconstruction via `rawPixelY * g_sampledScaleY * bowY` produces the correct sign convention.

### Relocated ActorState access for AE 1.6.629+ (2026-04-04)
Using `REL::RelocateMemberIfNewer` to read `ActorState1` (SE offset 0xC0, AE offset 0xC8) and `ActorState2` (SE 0xC4, AE 0xCC) directly from the `PlayerCharacter` pointer fixes bow detection on AE runtimes. Confirmed working on 1.6.1170.0: `atkState` cycles through real values (8=kBowDraw, 9=kBowAttached, 10=kBowDrawn, 12=kBowReleased), `wpnDrawn` toggles correctly, `bowDiag` fires on every frame during aim. This is the correct pattern for any `ActorState` field access in a multi-runtime NG build.

### Engine applies 1:1 pixel-to-lookInputVec ratio on both axes during bow aim (2026-04-04)
Added diagnostic logging of raw OS pixels (`rawPx`), engine output (`engine`), and bow multipliers (`bowMul`). Result: `|rawPx.X| == |engine.X|` and `|rawPx.Y| == |engine.Y|` on every frame — engine only inverts Y sign, no per-axis attenuation. This is identical between freelook and bow aim. If Y feels more sensitive during bow aim, it's FOV asymmetry (vertical FOV narrower than horizontal), not engine scaling. Use `fBowAimMouseYMultiplier < 1.0` to compensate.

### IC does not hook weapon/actor state queries (2026-04-04, verified via source)
Reviewed [ImprovedCameraSE-NG](https://github.com/ArranzCNL/ImprovedCameraSE-NG). IC does NOT hook or intercept:
- `Actor::IsWeaponDrawn()`
- `Actor::GetEquippedObject()`
- `ActorState::GetAttackState()`

IC reads these passively via its own `IsAiming()` helper. Any bow detection failures in CMC are CMC bugs, not IC interference.

---

## Doesn't Work

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
