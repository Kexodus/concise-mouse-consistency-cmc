# Concise Mouse Consistency (CMC)

Release **0.53**.

CMC is a DLL-only SKSE plugin that keeps mouse and gamepad look sensitivity consistent across Skyrim camera states.

The plugin restores full-rate first-person horizontal look while looking in measured 0.5× states (sprint, bow aim, and orphan states such as casting). It normalizes first-person pitch to a frozen true-freelook gain and converts eligible slow-time yaw to wall-clock in first- and third-person. First-person mouse bow reconstructs horizontal look from raw pixels; third-person mouse bow keeps engine/camera-mod deltas. CMC does not apply FOV-based input scaling or normalize third-person pitch.

## Install

Install the ZIP with a mod manager or copy its `Data` directory into Skyrim's root directory.

Requirements:

- SKSE64 matching the installed Skyrim runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework, optional

## Configure

- Settings path: `Concise Mouse Consistency/Settings`.
- INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`.
- The in-game menu exposes common look options (enable, sensitivity, axis multipliers, first-person mouse bow X/Y, smoothing removal, focus-spike suppress, gamepad look) plus one Compatibility checkbox for keeping CMC third-person smoothing removal with camera mods. Gamepad bow multipliers and other advanced knobs stay in the INI `[Advanced]` section.
- Settings apply live. Use **Save to INI** to persist UI changes.
- If SKSE Menu Framework is not installed, edit the INI directly.
- `bVerboseLogging=false` is the release default. Enable it only when troubleshooting; sampled counters and rendered-frustum diagnostics are written to `MouseSensitivityFix.log`.

CMC requires no ESP, ESL, or ESM and does not consume a load-order slot.
