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

## Phase 2: Hardware UI
1. PanelModel state machine for all SPEC flows that exist so far (others stubbed with display codes).
2. Vector panel: layout from the manual diagram, resizable, Retina-sharp.
3. SevenSegment with the manual's glyphs (incl. M glyph, dots).
4. Pads with lit/blink, buttons with lit/blink, PEAK, illumination ring with tempo blink and red overload.
5. Knobs: drag, scroll, Shift fine; centre-detent behaviour for START/END fine adjust.
6. KeyMap defaults from the prompt table; modifiers held = hardware held; shortcut overlay (`?`).
7. Extended side panels: waveform editor (draggable start/end), pad info, pattern note view.
8. 60 fps via VBlankAttachment; UI reads AudioToUi FIFO only.

## Phase 3: Effects
1. Shared DSP: ZDF SVF, oversampler (4x, polyphase IIR or FIR), delay lines, LFOs, smoothers, envelope followers, noise.
2. EffectHost: one slot (Authentic) / two (Extended), crossfade switching, Effect Grab, REMAIN targets, REMAIN presets, MFX selection by CTRL 3 / MFX+pad / MFX+DJFX+pad, hidden FLANGER/PHASER balance.
3. Tempo reference: longest targeted sample vs pattern tempo.
4. 29 effects, by family (analog-style, lo-fi, time-based, pitch/voice, spatial/mod).
5. Effect Limit on the 17 starred effects.
6. Per-effect tests: NaN/Inf/denormal at extremes, bounded output with Limit on, silence in/out (except Noise Gen, Vinyl Sim, Subsonic edge cases), tempo sync accuracy.
7. FIDELITY.md updated per effect.

## Phase 4: Sampling, EXT SOURCE, resample
1. Recorder: device input (Mac mic = MIC, interface = LINE), input gain, MIC LEVEL, digital level, PEAK, red illumination, STEREO.
2. Flows: sampling, auto trigger (0..9), tempo sampling with end snap, count-in, FUL, REMAIN display from free disk space.
3. EXT SOURCE pass-through, GATE momentary, stereo/mono, level (not persisted).
4. Resample with 4 / 2 polyphony, empty-pad rule, CTRL 3 level, CANCEL exit.
5. Plugin: sample from input bus; FX processor mode.

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
