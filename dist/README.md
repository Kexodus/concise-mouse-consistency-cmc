# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin that keeps mouse and gamepad look sensitivity consistent across Skyrim camera states.

The plugin restores full-rate first-person horizontal look while actively sprinting or aiming a bow. It normalizes bow pitch to the measured freelook gain and compensates eligible Eagle Eye yaw for Skyrim's active slow-time multiplier. CMC selectively restores measured half-rate yaw; normal-time full-rate transitions remain unchanged.

## Install

Install the ZIP with a mod manager or copy its `Data` directory into Skyrim's root directory.

Requirements:

- SKSE64 matching the installed Skyrim runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework, optional

## Configure

- Settings path: `Concise Mouse Consistency/Settings`.
- INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`.
- The in-game menu exposes common look options (enable, sensitivity, axis multipliers, smoothing removal, focus-spike suppress, gamepad look) plus one Compatibility checkbox for keeping CMC third-person smoothing removal with camera mods. Advanced knobs stay in the INI `[Advanced]` section.
- Settings apply live. Use **Save to INI** to persist UI changes.
- If SKSE Menu Framework is not installed, edit the INI directly.
- `bVerboseLogging=false` is the release default. Enable it only when troubleshooting; sampled counters and rendered-frustum diagnostics are written to `MouseSensitivityFix.log`.

CMC requires no ESP, ESL, or ESM and does not consume a load-order slot.
