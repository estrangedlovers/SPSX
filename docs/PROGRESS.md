# PROGRESS

## Phase 1: Engine core (status: done, awaiting review)

### Done
- Build: CMake + Ninja, JUCE 8.0.17-security, signalsmith-stretch 1.4.0, Catch2 3.8.1, all pinned (`cmake/Dependencies.cmake`). Targets: `SPSX_Standalone`, `SPSX_VST3`, `SPSX_AU` (Apple only), `SPSXTests`, `render_cli`.
- Engine (`src/core/engine`): voice pool with channel-voice budget and documented stealing; gate/trigger, loop/one-shot, reverse, start/end, LEVEL, LO-FI, HOLD, SUB PAD, stop all, PAUSE; sample-accurate timed commands; lock-free UI to audio FIFO; retired-sample handoff so the audio thread never frees memory.
- EngineHost: runs the engine at 44.1 kHz behind a 128-tap sinc converter with constant, reported latency (107 samples at 48 kHz); bypassed at 44.1 kHz; Extended native-rate mode.
- Sample editing (`src/core/sampler`): MARK flows (start+end, end-only, start-only, BPM snap), START/END fine adjust (±130 ms, 100 ms minimum, centre cancels), destructive truncate, BPM derivation and halve/double, Time Modify (Authentic real-time granular, Extended signalsmith render on a worker), tempo table (127 values).
- I/O: WAV/AIFF load with conversion to 44.1 kHz and 16-bit requantize (no dither); bit-exact 16/24-bit writer.
- `SamplerController`: message-thread facade (load sync/async, copy, exchange, delete, truncate, protect checks, settings edits, Extended stretch jobs).
- Plugin/standalone shell with a temporary test editor (pad grid, drop files onto pads, GATE/LOOP/REVERSE/LO-FI, HOLD, SUB PAD, stop all, VOLUME).
- `render_cli`: headless scripted renderer through the same EngineHost path as the plugin.
- Tests: 56 test cases, all passing in Debug and Release on Linux, including a zero-allocation check on the audio path.

### Bug found and fixed along the way
JUCE's float-to-16-bit WAV writer scales by 2^31-1 and truncates, losing one LSB on positive samples. `io::writeFile` now writes integer PCM itself so 16-bit samples round-trip bit for bit.

### Known issues
- The Xcode generator is untested (CI uses Ninja). Builds are ad-hoc signed, not notarized.
- Plugin state stores sample file paths, not audio. Self-contained sessions arrive in Phase 9.
- MIDI input is ignored until Phase 6. The test editor is a placeholder until Phase 2.
- The MARK, fine-adjust and CANCEL x4 logic is tested but not reachable from the UI yet; the PanelModel state machine (Phase 2) wires them to buttons.
- New working decisions U23 to U27 in SPEC section 14 (SUB PAD retrigger, new-sample defaults, LEVEL curve, fades, BPM resolution).

### Release
- CI (`.github/workflows/build.yml`) builds on macOS (universal AU/VST3/Standalone, ad-hoc signed) and Linux on every push, runs the unit tests, `auval -strict` and pluginval at strictness 10 on AU and VST3. All pass.
- Released v0.1.0 as a pre-release: https://github.com/estrangedlovers/SPSX/releases/tag/v0.1.0
- Tag pushes are refused by the cloud session's git proxy, so releases from a cloud session use a manual workflow run with `release_tag` (GitHub creates the tag). From your own machine, pushing a `v*` tag works too.

### Next
Phase 2: hardware UI (PanelModel state machine, vector panel, 7-segment display, keyboard map).

---

## Phase 0: Spec (status: done, reviewed)

- Owner's manual read in full; SPEC, ARCHITECTURE, PLAN, FIDELITY written; Slicer P01..P16 transcribed.
- Owner decisions: app name SPSX; JUCE under personal-use terms; manual committed to the repo.
- Findings vs the original prompt are listed in SPEC section 0 (polyphony is 12 mono / 6 stereo, pattern length steps, Effect Grab uses TAP TEMPO, MFX + pad selection, hidden Flanger/Phaser balance, Note Off not recognized, Slicer patterns defined in the manual).
