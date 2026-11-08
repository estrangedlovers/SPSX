# PLAN

Each phase ends with: clean build, tests green, `docs/PROGRESS.md` updated, commit, stop for review. pluginval (strictness 10, AU + VST3) runs at the end of Phases 3, 5, 6, 9 and before final sign-off, on macOS.

## Phase 0: Spec (this phase)
- [x] Obtain manual, read English section in full
- [x] SPEC.md, ARCHITECTURE.md, PLAN.md, FIDELITY.md
- [x] Transcribe Slicer patterns (tools/slicer_patterns.py)
- [x] Review with owner (name SPSX, JUCE personal use, manual committed)

## Phase 1: Engine core (done)
- [x] CMake skeleton, pinned deps, Standalone/VST3 (+AU on Apple), Catch2, `render_cli`
- [x] `state`: SampleData, PadSettings, Project + JSON round trip
- [x] AudioFileIO: WAV/AIFF, rate conversion on import, 16-bit requantize without dither, bit-exact integer writer
- [x] SincResampler + EngineHost (44.1k internal, constant reported latency, native-rate bypass)
- [x] VoicePool: 12 channel-voices (stereo = 2), 4 while resampling, oldest-first stealing with 2 ms fade
- [x] Voice: gate/trigger, loop/one-shot, reverse, region, LEVEL, LO-FI, Time Modify (granular / pre-rendered)
- [x] Engine routing: HOLD, SUB PAD, banks, stop all, PAUSE, sample-accurate timed commands, safe sample replacement
- [x] Sample editing: MARK flows, fine adjust, truncate, BPM derivation, halve/double, Time Modify knob mapping, tempo table
- [x] CancelDetector (CANCEL x4)
- [x] Output gain, clip, VOLUME
- [x] Allocation guard in tests (zero allocations in Engine/EngineHost process)
- [x] Phase 1 test editor (pad grid, drag-drop load, mode toggles)

## Phase 2: Hardware UI (done)
- [x] PanelModel state machine: banks, pads, SAMPLE MODE, START/END/LEVEL, TIME/BPM, TAP TEMPO, MARK (3 flows), DEL / truncate / delete all / format, FUNC 1..12 (copy, exchange, import, auto trig, illumination + sleep, effect limit, MIDI ch/sync, input/output gain), protect, PAUSE, CANCEL x4, REMAIN (time, current pad, targets, presets), effect buttons (exclusive, MFX selection 3 ways, EFFECT GRAB, last-effect reminder), PATTERN SELECT / LENGTH / QUANTIZE values. Later-phase actions show display codes.
- [x] Vector panel following the manual's diagram, resizable, cached static layer at physical resolution
- [x] 7-segment display with the manual's glyphs, dots, busy dot, sleep chase; illumination ring (red/blue/off)
- [x] Pads and buttons with lit/blink states, PEAK LED, knobs (drag, wheel, Shift fine, double-click centre)
- [x] Keyboard map from the shortcut table; held keys hold buttons; Option = FUNC; overlay with key labels
- [x] Extended side panel: pad info, waveform editor with draggable start/end, playhead; pattern view placeholder
- [x] VBlank-driven refresh, repaint only on change
- [x] panel_snapshot tool for headless screenshots
- Deferred: backup load/save (needs projects, Phase 7); pattern copy/exchange/delete (Phase 5); sticker layer (Phase 8)

## Phase 3: Effects (done)
- [x] Shared DSP: ZDF SVF, biquads, LR4, 4x oversampling, delay lines, LFOs, smoothers, envelope followers, pitch shifter, noise
- [x] EffectHost: crossfaded switching, chunked processing, Effect Limit safety limiter, no allocation; keeps state across re-prepare
- [x] All 29 effects; Effect Limit on the 17 starred effects
- [x] Routing: dry/effect buses, targets (current pad, REMAIN pads, all, EXT SOURCE), glide on target change, tempo reference
- [x] Panel: effect selection (buttons, MFX 3 ways, EFFECT GRAB, REMAIN presets), per-effect value displays
- [x] Tests: stability at extremes, silence, limiter ceiling, click-free switching, sync accuracy, real-time safety
- [ ] pluginval at strictness 10 (CI, macOS)

## Phase 4: Sampling, EXT SOURCE, resample (done)
- [x] Input stage: LINE with INPUT GAIN, MIC with MIC LEVEL, PEAK, digital level, red illumination
- [x] Sampling: REC, empty-pad choice, standby monitoring through the effect, REC/REC, auto trigger (rdY), count-in (-1-..-4-), tempo snap, FUL
- [x] Disk recording through a lock-free FIFO and writer thread; 16-bit bit-exact (Authentic) / float (Extended); WAV or AIFF per setting
- [x] EXT SOURCE: toggle or GATE momentary, level, stereo/mono, effect target (REMAIN + EXT)
- [x] Resample: RESAMPLE (LEU), CTRL 3 level, 4-voice budget, empty destination, armed start on the source pad
- [x] Plugin: input bus sampling; FX Mode (track audio through the effects)

## Phase 5: Pattern sequencer
1. Pattern data (96 PPQN), tempo table, length rules (1..20, then 4-bar steps).
2. Recording flow: standby, metronome, count-in, loop recording, overdub, rehearsal, quantize (10 settings) + shuffle, change quantize live, erase (pad / HOLD = all), CANCEL commit.
3. Playback: start, reserve next, SUB PAD immediate, stop, CANCEL, illumination tempo blink.
4. Delete, delete all, copy, exchange, protect for patterns.
5. Plugin: host tempo/transport follow.
6. Tests for every item above.

## Phase 6: MIDI
1. Authentic: note map (46..106, Base Ch / Base Ch+1), channel setting, sync modes AUt/tNp/oFF, Song Position, All Sound Off, Active Sensing, HOLD+pad wait-for-start.
2. Plugin: host MIDI + host tempo.
3. Extended: MIDI learn (all controls), profiles, velocity curves, Note Off, MIDI out for pads and patterns, clock out, SMF export, optional Ableton Link (license check: Link is GPLv2 or commercial; needs approval).

## Phase 7: Extended song features
Song mode; second effect slot; per-pad pitch/pan/fades; non-destructive editing + undo/redo; offline mixdown (16/24-bit, 44.1/48 kHz, tails); stems (per pad, per bank, effect return); projects with autosave and crash recovery; optional SD layout export (samples only unless pattern format is found).

## Phase 8: Stickers
Import (PNG/SVG/JPG, drag and drop), library folder, edit mode, transforms, seeded wear (scuffs, fade, peel with backside + shadow, die-cut border, gloss/matte), click-through outside edit mode, keep-off-controls option, global "my shell" + per-project override, render order below LEDs and pad glow.

## Phase 9: Settings and persistence
Settings window with every item in the prompt; shortcut remapper with conflict detection; full plugin state in host session.

## Phase 10: Fidelity lab
A/B window: reference + dry source load, render through emulation, loudness match (EBU R128), spectrum + spectrogram overlay, alignment (cross-correlation + manual nudge), null test. Tune effects and LO-FI; update FIDELITY.md with material and lossiness notes.

## Final
Definition-of-done checklist from the prompt; worst-case CPU report (12 voices, effect on, pattern playing).
