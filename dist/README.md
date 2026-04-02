# Runtime Packaging Layout

Project: **Concise Mouse Consistency (CMC)**.

This folder mirrors the final mod install layout.

## Files

- `Data/SKSE/Plugins/MouseSensitivityFix.dll`
- `Data/SKSE/Plugins/MouseSensitivityFix.ini`

## Notes

- DLL-only runtime; no ESP/ESL/ESM required.
- Uses SKSE Menu Framework for in-game settings.
- Falls back to INI-only config if the framework is missing.
