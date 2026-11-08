# PROGRESS

## Phase 6: MIDI, plus output safety (status: done, released as v0.6.0)

### Done
- **MIDI IN** (`src/core/midi/MidiInput.h`): the manual's note map (46 EXT SOURCE, 47..106 pads on Pad Base Ch for A..E and Base Ch + 1 for F..J), FUNC + 9 channel, All Sound Off on any channel, Active Sensing timeout. Authentic follows the implementation chart: velocity and Note Off are ignored, so a Note On is a one-shot hit (a GATE sample plays out). Notes land on their exact sample.
- **MIDI sync** (FUNC + 10): `AUt` follows clock and Start / Continue / Stop / Song Position (HOLD + pattern pad waits for Start), `tNp` follows tempo only, `oFF` ignores both. The block is split at transport messages so patterns start on the right sample. In a DAW the host's transport works the same way and obeys the same setting; incoming MIDI clock takes priority.
- **Extended**: velocity (fixed / linear / soft curve), Note Off releases, MIDI learn for every knob and button (MIDI button in the top bar, then click a control and move a controller; saved with the project), export of all patterns as standard MIDI files on the note map.
- Patterns now store velocity and one-shot hits (saved in the project JSON).
- **Output safety** (both modes): nothing above 0 dBFS and nothing non-finite ever leaves the app. Final brickwall after sample-rate conversion (bit-transparent below full scale), NaN / Inf mute with a fade back in, effect bus reset if it runs away, fade-in after a re-prepare, imported files and plugin input sanitised. A fuzz test (random pads, effects, extreme parameters, +12 dB output gain, full-scale noise input, both modes) checks every output sample.
- New working decisions U52 to U56 in SPEC section 14; U50 updated.
- Tests: 156 test cases, all passing.

### Known issues
- The UI is still reviewed only from headless renders; please check it on your Mac.
- EXT SOURCE from MIDI goes through the panel and lands up to one UI frame late (U53).
- MIDI out, clock out and learn profiles are deferred to Phase 9; Ableton Link needs your approval (license).

### Next
Phase 7: Extended song features (song mode, second effect slot, mixdown and stems, projects with autosave).

---

## Phase 5: Pattern sequencer, plus a review and optimisation pass (status: done)

### Done
- **Sequencer** (`src/core/engine/Sequencer`, `src/core/state/PatternData.h`): 96 PPQN patterns with their own tempo and note lengths; events go to the engine's timed queue at exact sample times; loop, reserve at pattern end, SUB PAD immediate switch, stop, HOLD + pad arm.
- **Pattern recording**: standby metronome, one-bar count-in, loop overdub, 10 quantize grids with shuffle applied at record time, rehearsal, erase (pad or HOLD = all), length rules, 16,000-note limit, CANCEL commits.
- **Panel**: PATTERN SELECT play / reserve / stop, REC flow, LENGTH, QUANTIZE, metronome level, TIME/BPM per pattern, pattern delete / copy / exchange, displays (Ptn, -4..-1, M.b, rEH, ErS), tempo-blinking ring.
- **Plugin**: host tempo drives the sequencer; HOLD + pattern pad starts with the host transport (SPEC U50).
- Patterns saved with the project (JSON).
- New working decisions U44 to U51 in SPEC section 14.

### Review and optimisation pass
- **Stability**: PAUSE used to drop timed events that fell due while paused, which could leave sequencer notes stuck on. Timed events are now held and the sequencer is frozen until resume. PATTERN SELECT is ignored while sampling or resampling, so REC cannot change meaning mid-take.
- **Speed**: the timed queue is a binary heap (O(log n) insert instead of a sorted shift; ties keep insertion order). The sample-rate converter interpolates each coefficient row once and shares it across channels. The panel repaints only the controls, knobs, PEAK LED and display whose pixels changed, instead of the whole panel on every blink.
- **Benchmark** (`tools/bench`, Release, `spsx_bench 3`): each effect alone stays under about 1.1 % of one core at 44.1 kHz / 256. Worst case (12 voices, effect on all, dense pattern): 1.4 % at 44.1 kHz, 2.4 % at 48 kHz / 128 with sample-rate conversion. Run-to-run variation on this shared machine is around 0.3 points.
- Tests: 131 test cases, all passing (new: out-of-order timed commands, timed commands held through PAUSE).

### Known issues
- The UI is still reviewed only from headless renders; please check it on your Mac.
- MIDI (Phase 6) is not wired yet; host sync uses the plugin play head only.

### Next
Phase 6: MIDI.

---

## Phases 3 and 4: Effects, sampling, EXT SOURCE, resample (status: done, released as v0.4.0)

### Done
- **29 effects** (`src/core/fx`): all direct and MFX effects with their CTRL 1..3 parameters, display values (0..127, note values, P01..P16, -1.0..1.0, oFF/on, oFF/1..8, semitones), Effect Limit on the 17 starred effects plus an output safety limiter, tempo sync to the longest targeted sample, 4x oversampled drive stages, the manual's Slicer patterns.
- **EffectHost**: all effects prepared up front (no allocation when switching), 25 ms equal-power cross-fades, 32-sample parameter refresh, survives host re-prepare.
- **Routing**: dry and effect buses; per-pad targets (last pad, REMAIN + pads, REMAIN + effect = all, REMAIN + EXT SOURCE) with 5 ms glides.
- **Panel**: effect buttons drive the engine, MFX selection three ways, EFFECT GRAB, REMAIN presets, per-effect value display, Effect Limit and input gain from FUNC menus.
- **Input stage**: LINE (stereo, INPUT GAIN) or MIC (mono, MIC LEVEL), PEAK, digital level with red illumination on clipping.
- **Sampling**: REC, empty pad, standby monitoring through the effect, auto trigger (rdY), count-in (-1-..-4-), tempo snap, FUL; streamed to disk through a lock-free FIFO and writer thread, 16-bit bit-exact in Authentic mode.
- **EXT SOURCE**: toggle or GATE momentary, LEVEL, stereo/mono, effect target.
- **Resample**: LEU level, 4-voice budget, empty destination, armed start on the source pad.
- **Plugin FX Mode** (track audio through the effects), saved with the session.
- Tests: 112 test cases (effect stability, silence, limiter ceiling, click-free switching, sync accuracy, routing, recording bit-exactness, count-in, auto trigger, resample start, panel flows, real-time safety).

### Known issues
- The UI was reviewed from headless renders only; please check it on your Mac.
- MIC and LINE share one input device (SPEC U38).
- Pattern-related flows (pattern playback, pattern copy/exchange/delete, ErS) and backup load/save are still stubs (Phase 5 / 7).
- New working decisions U35 to U43 in SPEC section 14.

### Next
Phase 5: pattern sequencer.

---

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
