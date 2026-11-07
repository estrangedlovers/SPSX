# CLAUDE.md

Recreation of a 2009 12-pad phrase sampler (7-segment display model) as a JUCE app: Standalone + AU + VST3 from one codebase, with an Authentic mode (hardware limits) and an Extended mode (song-finishing features).

## Start here
1. `docs/PROGRESS.md`: current phase, open decisions, next step.
2. `docs/SPEC.md`: hardware behaviour (manual is the source of truth for Authentic mode). Section 0 lists prompt-vs-manual conflicts; section 14 lists undocumented behaviours and working decisions.
3. `docs/ARCHITECTURE.md`: modules, threading, state, Authentic/Extended via `Limits`.
4. `docs/PLAN.md`: phase tasks. `docs/FIDELITY.md`: per-DSP-block confidence.

Manual: `docs/SP-404SX_Owners_Manual.pdf` (git-ignored, fetch with `tools/fetch_manual.sh`). English is PDF pages 3 to 54; printed page = PDF page - 2.

## Process rules (from the owner)
- Work phase by phase. At the end of each phase: clean build, tests pass, update `docs/PROGRESS.md`, commit, **stop and wait for review**.
- Unknown hardware behaviour goes into `docs/FIDELITY.md` / SPEC section 14, never guessed silently.
- No vendor logos, wordmarks or the product name in UI, bundle ID or assets. Function labels are fine.
- Real-time safety: no allocation, locks, file I/O or logging on the audio thread. Lock-free FIFOs only.
- Limits are parameters (`Limits` struct), not separate code paths.
- Licenses: check before adding any dependency; prefer permissive. Time-stretch: signalsmith-stretch (MIT). Ableton Link / Rubber Band need owner approval.
- Writing style for docs: no em dashes.

## Build (Phase 1 onward; not yet created)
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```
Xcode: `cmake -S . -B build-xcode -G Xcode`. Universal: add `-DUNIVERSAL=ON`.

## Tools
- `tools/slicer_patterns.py`: Slicer+FLG P01..P16 from the manual; run to verify and print gate strings.
- `tools/fetch_manual.sh`: download the manual into `docs/`.
