# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin that keeps mouse and gamepad look sensitivity consistent across Skyrim camera states.

The plugin also restores full-rate first-person horizontal look while actively sprinting or aiming a bow, including Eagle Eye zoom. It only corrects Skyrim's measured half-rate yaw frames, leaving normal look and full-rate transitions unchanged.

## Install

Install the ZIP with a mod manager or copy its `Data` directory into Skyrim's root directory.

Requirements:

- SKSE64 matching the installed Skyrim runtime
- Address Library for SKSE Plugins
- SKSE Menu Framework, optional

## Configure

- Settings path: `Concise Mouse Consistency/Settings`.
- INI path: `Data/SKSE/Plugins/MouseSensitivityFix.ini`.
- Settings apply live. Use **Save to INI** to persist UI changes.
- If SKSE Menu Framework is not installed, edit the INI directly.
- `bVerboseLogging=false` is the release default. Enable it only when troubleshooting; sampled counters are written to `MouseSensitivityFix.log`.

CMC requires no ESP, ESL, or ESM and does not consume a load-order slot.
