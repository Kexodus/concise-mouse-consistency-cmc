# Concise Mouse Consistency (CMC)

CMC is a DLL-only SKSE plugin that keeps mouse and gamepad look sensitivity consistent across Skyrim camera states.

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

CMC requires no ESP, ESL, or ESM and does not consume a load-order slot.
