# Implementation Plan

Project: **Concise Mouse Consistency (CMC)**.

## Workstreams

1. **Foundation**
   - [x] SKSE bootstrap
   - [x] logging
   - [x] INI config load/save/throttled reload
2. **Input behavior**
   - [x] mouse/gamepad look hooks
   - [x] sensitivity transform
   - [x] smoothing-related handling
3. **UI + runtime sync**
   - [x] SKSE Menu Framework panel
   - [x] in-memory apply + INI persistence
   - [x] permanent hooks with live atomic behavior gates
4. **Compatibility**
   - [x] SmoothCam / Improved Camera policy rules
   - [x] live policy reconciliation
5. **Release hardening**
   - [ ] complete multi-runtime in-game validation
   - [x] automated tests and CI
   - [x] reproducible clean-clone builds
   - [x] generated release packaging + synchronized docs

## Acceptance Checklist

- [x] No ESP/ESL/ESM required for core functionality
- [ ] DLL loads via SKSE across targeted runtimes
- [x] X/Y transform math covered by automated tests
- [x] Third-person smoothing removal verified on Steam `1.6.1170`
- [x] SKSE Menu Framework entry verified on Steam `1.6.1170`
- [x] Save/reload behavior covered by automated tests
- [ ] Repeat the in-game behavior checks for every runtime in `docs/RUNTIME_VALIDATION.md`
