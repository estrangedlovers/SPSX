# PROGRESS

## Phase 2: Hardware UI (status: done, awaiting review)

### Done
- **PanelModel** (`src/core/ui_model`): the hardware's button logic as a GUI-free state machine. Every SPEC flow with an engine behind it works from the panel: banks A..J, pads, SAMPLE MODE, START/END/LEVEL (CTRL 1/2 fine adjust, CTRL 3 level), TIME/BPM (pattern tempo on the stepped table, sample BPM halve/double, Time Modify), TAP TEMPO, MARK (start+end, start only, hold MARK + pad for end only, clear), DEL with multi-select, truncate (DEL, MARK, DEL), CANCEL + DEL, CANCEL + REMAIN format, FUNC 1..12, FUNC + REMAIN / CANCEL protect, FUNC + TAP pause, CANCEL x4, REMAIN (remaining time, current pad, effect targets, parameter presets), SLP sleep.
- Effects panel state is complete (exclusive buttons, MFX type by MFX + pad, MFX + DJFX + pad and MFX + CTRL 3, EFFECT GRAB, last-effect blink, per-effect knob values, hidden FLANGER/PHASER balance). Phase 3 connects it to DSP.
- **Panel UI** (`src/ui`): vector-drawn panel laid out from the manual's diagram, resizable and sharp at any scale, 7-segment display with the manual's glyphs, illumination ring, lit and blinking buttons and pads, knobs with drag, wheel, Shift fine and double-click to centre.
- **Keyboard**: the full shortcut table; held keys hold buttons; Option = FUNC; Shift + Z..B = banks F..J; arrows select and turn knobs; `?` shows key labels on the panel. Shift-click latches a button for mouse-only combinations.
- **Extended side panel**: pad info, waveform editor with draggable start/end and a live playhead, pattern view placeholder. Authentic/Extended switch in the top bar (saved with the plugin state).
- Drag audio files onto a pad to import from there onwards (ASCII order, protected banks skipped), same as FUNC + 3.
- `tools/snapshot` renders the panel headlessly; screenshots in `docs/images/`.
- Tests: 86 test cases (30 new for the panel), all passing.

### Known issues
- Not yet seen on a real screen: this session has no display, so the UI was reviewed from headless renders. Please check it on your Mac (CI builds it).
- Keyboard input inside a DAW depends on the host passing keys to the plugin window; the standalone app always gets them.
- Buttons for later phases show `---` (REC sampling, RESAMPLE, EXT SOURCE) or `ENP` (backup load, pattern copy/exchange/delete).
- New working decisions U28 to U34 in SPEC section 14 (idle display, value flash time, fine-adjust units, MFX toggle timing, LED details, effect knob memory).

### Next
Phase 3: effects (all 29, Effect Limit, Effect Grab, REMAIN targets, seamless switching).

---

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
