# Product Objectives

Project: **Concise Mouse Consistency (CMC)**.

## Goal

Ship a DLL-only SKSE plugin that:

- keeps X/Y look response consistent
- removes plugin-side smoothing behavior
- supports in-game config without an ESP

## Required

- Use [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG)
- No ESP/ESL/ESM requirement
- Support SE/AE/GOG runtimes handled by CommonLibSSE-NG + Address Library
- Offer SKSE Menu Framework UI with INI fallback
- Keep compatibility surface focused on camera/input behavior only

## Non-Goals

- Gameplay record edits through CK plugins
- Replacing full camera systems
- Maintaining multiple UI frameworks beyond SKSE Menu Framework + INI

## Definition of Done

- Plugin loads cleanly on targeted runtimes
- X/Y behavior validated in repeatable tests
- Smoothing-removal behavior verified
- UI and INI settings both apply correctly
