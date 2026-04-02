# Implementation Plan

Project: **Concise Mouse Consistency (CMC)**.

## Workstreams

1. **Foundation**
   - SKSE bootstrap
   - logging
   - INI config load/save/reload
2. **Input behavior**
   - mouse/gamepad look hooks
   - sensitivity transform
   - smoothing-related handling
3. **UI + runtime sync**
   - SKSE Menu Framework panel
   - in-memory apply + INI persistence
4. **Compatibility**
   - SmoothCam / Improved Camera policy rules
5. **Release hardening**
   - multi-runtime validation
   - packaging + docs

## Acceptance Checklist

- [ ] No ESP/ESL/ESM required for core functionality
- [ ] DLL loads via SKSE across targeted runtimes
- [ ] X/Y sensitivity parity verified
- [ ] Mouse smoothing removed
- [ ] SKSE Menu Framework entry present and functional
- [ ] Save/reload applies immediately and persists to INI
