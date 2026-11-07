# PROGRESS

## Phase 0: Spec (status: done, awaiting review)

### Done
- Fetched the owner's manual from Roland's support page and read the English section (manual pp. 2 to 52) in full, including figures: panel diagram, effect tables with Effect Limit stars, Slicer pattern notation.
- `docs/SPEC.md`: controls, connectors, every FUNC / CANCEL / REMAIN combination, power-on combos, all 29 effects with CTRL 1/2/3 and display codes, Effect Limit (17 effects verified), sequencer, sampling, resample, EXT SOURCE, display codes, error messages, full MIDI Implementation Chart, note map, a list of 10 prompt-vs-manual conflicts and 22 undocumented behaviours with working decisions.
- `docs/ARCHITECTURE.md`, `docs/PLAN.md`, `docs/FIDELITY.md`.
- `tools/slicer_patterns.py`: P01..P16 transcribed from notation and verified to sum to one bar each.
- `CLAUDE.md` for session hand-off.

### Findings that change the prompt's assumptions
1. Polyphony is 12 **mono** voices; a stereo sample uses 2 (so 6 stereo). Resample: 4 mono / 2 stereo.
2. Pattern length is 1..20 in 1-bar steps, then 4-bar steps up to 99.
3. Slicer patterns P01..P16 are defined in the manual; no design needed.
4. Effect Grab uses TAP TEMPO as the modifier.
5. MFX can be picked with MFX + pad (1..12) and MFX + DJFX LOOPER + pad (13..24).
6. FLANGER and PHASER have a hidden balance control (MFX + CTRL 1).
7. MIDI Note Off is not recognized by the hardware; velocity is marked "recognized" but its effect is undocumented.
8. Garbled parameters in your sources, now verified: NOISE GEN = white / disc / hum; C.CANCELER = L-R balance / low boost / high boost; SUBSONIC = pitch / threshold / balance; BPM LOOPER = length★ / timing (oFF, 1..8) / loop sw.

### Known issues / open decisions (need owner input)
- **App name**: `[APP NAME]` was not filled in. Needed before Phase 1 for bundle ID, product name and plugin codes.
- **JUCE license**: JUCE 8 is AGPLv3 or commercial. Confirm which you hold before the first build commit.
- **Manual in repo**: the PDF (70 MB, 372 pages, Roland copyright) is downloaded to `docs/` but git-ignored. `tools/fetch_manual.sh` re-downloads it. Say if you want it committed anyway (Git LFS recommended).
- **macOS builds**: this cloud session runs on Linux. Phase 1+ can build and test `core` + VST3/Standalone on Linux here, but AU, Xcode, universal binaries and pluginval on AU need a Mac (your machine or a macOS CI runner).
- **Working decisions** for U1..U22 in SPEC section 14: please skim and veto any you disagree with.

### Next
Phase 1: engine core (see PLAN.md).
