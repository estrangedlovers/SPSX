# ARCHITECTURE

## 1. Targets and build

- One CMake project, dependencies pinned in `cmake/Dependencies.cmake` via `FetchContent` (JUCE 8.0.17-security, signalsmith-stretch 1.4.0, Catch2 3.8.1). C++20.
- `juce_add_plugin(SPSX ...)` creates the static shared-code target `SPSX` (all of `src/`) and the format targets `SPSX_Standalone`, `SPSX_VST3`, `SPSX_AU` (Apple only). `SPSXTests` (Catch2) and `render_cli` link `SPSX` through `spsx_use_shared_code()`, which borrows its include paths and module definitions so JUCE modules compile once.
- `CMAKE_OSX_ARCHITECTURES=arm64` by default; `-DUNIVERSAL=ON` adds `x86_64`.
- Generators: Ninja (CLion) and Xcode both supported; no generator-specific logic.
- Third-party: JUCE (AGPLv3 / commercial; you choose), Catch2 (BSL-1.0), signalsmith-stretch (MIT), nlohmann/json (MIT) **or** juce::JSON. Each added dependency gets a line in `THIRD_PARTY.md` with its license.

## 2. Module breakdown

Phase 1 implemented: `core/` (Types, Limits, Tempo, CancelDetector, RealtimeGuard, SamplerController, state/, engine/, dsp/, io/, sampler/) and a test editor in `plugin/`. The tree below is the target layout; names in it that do not exist yet arrive in later phases.

```
src/
  core/                 no JUCE GUI dependency; juce_audio_basics/formats/dsp allowed
    Types.h             BankId, PadId, ChannelVoices, Tick, etc.
    Limits.h            AuthenticLimits / ExtendedLimits (polyphony, effect slots, undo, sample format)
    state/              plain data: Project, Sample, Pad, Pattern, Settings (value types, versioned)
    engine/
      Engine            owns everything audio-side; processBlock entry
      RateConverter     44.1k <-> device rate, polyphase sinc, fixed latency reported to host
      VoicePool         channel-voice budget, stealing policy (testable)
      Voice             one playing sample: gate/loop/reverse/lofi/stretch, fades
      PadRouter         pad press/release -> voices, HOLD, SUB PAD, current pad, effect targets
      Mixer             dry bus + effect bus, output gain, clip, volume
      Metronome
    fx/
      Effect            interface: prepare, reset, process(stereo), setParam(i, 0..1), limit flag
      EffectHost        active slot(s), seamless crossfade, Effect Grab, REMAIN presets
      direct/*, mfx/*   29 implementations
      dsp/*             shared: ZDF SVF, oversampler, delay line, LFO, envelope, smoother, noise
    seq/
      Pattern           tick-stamped events, length, tempo, quantize, shuffle
      Sequencer         transport, loop recording, overdub, rehearsal, erase, reservation queue
      Clock             internal / MIDI clock / host transport
      Tempo             allowed-value table, tap tempo
    sampler/
      Recorder          input capture to a lock-free ring, auto trigger, count-in
      Resampler         render path + reduced polyphony
      Stretcher         Time Modify render jobs (background)
    midi/
      MidiIn            note map, channel, sync modes, All Sound Off
      MidiLearn (Ext)   learn table, profiles
      MidiOut (Ext)     pad/pattern out, clock out, SMF export
    io/
      AudioFileIO       WAV/AIFF read/write, 16-bit requantize (no dither in Authentic)
      ProjectStore      folder + JSON, autosave, crash recovery
      SdCardLayout (Ext, optional)
    ui_model/
      PanelModel        the hardware state machine: modes, lit/blink states, display text
      Commands          ButtonDown/Up, PadDown/Up, KnobMoved... (from UI, keys, MIDI learn)
  ui/                   JUCE GUI only
    PanelComponent      vector-drawn panel, scales with window
    SevenSegment        glyph table matching the manual's font
    Pad, Knob, Button, Led
    side/               Extended panels: waveform, pad info, pattern view
    stickers/           Phase 8
    settings/           Phase 9
    KeyMap              remappable shortcuts
  app/
    StandaloneApp       menu bar, audio/MIDI device setup, document window
  plugin/
    Processor           juce::AudioProcessor wrapping Engine + PanelModel
    Editor              hosts ui::PanelComponent
tests/
tools/
```

The **PanelModel** is the central idea. It is a pure state machine that receives `Command`s (button down/up, pad down/up, knob moved, time tick) and produces (a) engine messages and (b) a `PanelView` snapshot (each LED's state, display glyphs, pad lights). It has no JUCE GUI dependency, so it is unit-testable: a test can script "hold FUNC, press pad 8, turn CTRL 3, press REC" and assert the display shows `L.on` and the setting changed. Mouse, keyboard, MIDI learn and tests all feed the same command stream, which keeps "holding a key behaves like holding the hardware button" true by construction.

### UI layer (Phase 2)

- `core/ui_model/PanelModel` is the panel state machine. Input is `press/release(Control)`, `turn(Knob, 0..1)` and `tick(now)`, all with explicit time, so every flow is scripted in `tests/TestPanelModel.cpp`. Output is a `PanelView` value (LED states, display cells, illumination), compared by value so the UI repaints only on change.
- `ui/PanelLayout` holds every control's geometry in logical units (1000 x 1450, the hardware's aspect). Drawing, hit-testing and the key overlay all read it.
- `ui/PanelComponent` draws the panel with vector paths. Static artwork is cached in an image at physical resolution and rebuilt on resize; LEDs, pads, knobs and the 7-segment display are drawn live. `refresh(now)` runs from a `VBlankAttachment` and repaints only when the view or a blink phase changed.
- `ui/KeyMap` maps keys to controls. Key down/up become press/release (held keys hold buttons); Option is FUNC; Shift + Z..B selects banks F..J directly. Shift-click latches a button so mouse users can do two-handed combinations.
- `ui/SidePanel` (Extended only): pad info, waveform editor with draggable start/end and playhead, pattern view placeholder.
- The processor owns the `PanelModel`, so panel state survives closing the plugin window. Its 30 Hz timer calls `tick`.
- `tools/snapshot` (`panel_snapshot`) renders the panel to PNG without a display, for review and docs.

### Effects (Phase 3)

- `core/fx/`: `Effect` (interface), `EffectInfo` (the 29 effects: names, display codes, value formats, Effect Limit stars, note values, Slicer patterns: one table shared by the panel display and the DSP), `DspUtil` (ZDF SVF, biquads, LR4, delay line, LFO, envelope follower, pitch shifter, smoothers), five effect families, `EffectHost`.
- `EffectHost` creates and prepares all 29 effects per slot up front (nothing is allocated when switching), cross-fades switches over 25 ms, processes in chunks of at most 32 samples (block-rate parameters refresh often), and adds a soft limiter when Effect Limit is on. `prepare()` keeps the selection and parameters when a host re-prepares.
- Routing: each voice renders into a dry bus or the effect bus depending on whether its pad is an effect target, gliding over 5 ms when the target changes. The panel decides the target set (current pad, REMAIN pads, all, EXT SOURCE) and sends it as commands. Synced effects take the BPM of the longest targeted sample, else the pattern tempo.

### Input, sampling, resampling (Phase 4)

```
input -> INPUT GAIN (LINE, stereo) | MIC LEVEL (MIC, mono) -> PEAK
      -> EXT SOURCE (level, stereo/mono) -> dry or effect bus
      -> sampling monitor x digital level -> effect bus
effect bus -> EffectHost -> + dry -> OUTPUT GAIN -> clip -> VOLUME (+ count-in click)
recorder <- effect bus (sampling) or (dry + effect) x level (resample)
```

- The recorder is a state machine on the audio thread (waiting for start, waiting for trigger, count-in, recording, full) writing into a `RecordTap` (lock-free FIFO, 10 s). `SamplerController::RecordingSession` owns the tap and a writer thread that drains it to the file. The engine signals with an atomic when it has let go of the tap; only then does the session close the file, and the new sample is loaded on the worker pool and assigned to the pad.
- Resample arms with "wait for start"; the panel posts the source pad's `padDown` and `recStart` together, so playback and recording begin on the same sample.

### Pattern sequencer (Phase 5)

- `state/PatternData`: immutable, reference-counted pattern (length in bars, tempo, events sorted by tick at 96 PPQN). Published to the engine like samples (`Engine::postPattern`), retired through the same garbage FIFO.
- `engine/Sequencer`: owned by the engine, audio thread only. Each block it emits pad down / pad up / metronome-click events with absolute sample times into the engine's timed queue (4096 entries), so the segment loop triggers them sample-accurately. The play head maps to engine time through `(pos, posTime)`, so a pad hit from the panel (block start) or a timed command (mid-block) is placed on the right tick.
- Recording writes into a preallocated 16,000-event buffer kept sorted by tick. Hits are quantized as they are recorded; events recorded in the current pass are skipped until the next pass, so a hit never sounds twice. On stop the take is published through an atomic flag; the panel's `tick()` hands it to `SamplerController::commitRecordedPattern()`, which builds the new `PatternData`. The engine does not touch the buffer again until the next standby command.
- Tempo: host tempo when following a host, else the playing pattern's tempo, else the tempo set on the panel. Effects sync to the pattern tempo while a pattern runs.

### MIDI (Phase 6)

- `midi/MidiInput`: audio-thread translator, owned by the processor. Notes become engine pad commands scheduled at the message's sample (`EngineHost::scheduleAt`); index 2 marks an Authentic one-shot (no Note Off follows), which the sequencer records with length 0. Clock, Start, Continue, Stop and Song Position produce a transport (tempo, playing, position) that replaces the DAW play head while a clock runs. The processor splits its block at Start / Stop / Continue / Song Position so the sequencer changes state on the message's sample.
- Settings (Pad Base Ch, MIDI SYNC, the mode's Note Off / velocity / learn limits, the velocity curve) travel from PanelModel to the engine as one `setMidi` command; the processor reads them back on the audio thread.
- EXT SOURCE notes and learn Control Changes go to the message thread through a 256-entry lock-free FIFO and are applied by PanelModel (`press` / `release`, `handleMidiControl`), so the panel stays the single source of panel behaviour.
- `midi/MidiFileExport`: patterns as type 0 SMF at 96 PPQ on the note map, so an exported file drives SPSX from a DAW.

### Output safety

- `engine/OutputGuard`: last stage in `EngineHost::process`, at the device rate. Instant-attack brickwall at 0 dBFS with an 80 ms release (gain is exactly 1 below the ceiling, so it is bit-transparent), NaN / Inf mute the rest of the block with a 20 ms fade back in, and a re-prepare fades in. A fault tells the engine to reset all effect state.
- The engine resets the effect bus itself when it exceeds +36 dBFS (summed L+R) or turns non-finite, before it reaches the mix.
- Imported audio and plugin input are sanitised before they reach the engine.

## 3. Authentic vs Extended

A single `Limits` struct is read by engine and model code:

```cpp
struct Limits {
    int  channelVoices        = 12;   // Ext: user-set
    int  resampleChannelVoices = 4;
    int  effectSlots          = 1;    // Ext: 2
    bool requantize16         = true; // Ext: false (float storage)
    bool destructiveTruncate  = true; // Ext: false (undoable)
    bool steppedTempo         = true; // Ext: any 20.00..300.00
    bool honourNoteOff        = false;
    bool velocity             = false;
    bool midiLearn            = false;  // Ext: CCs reach MIDI learn
    double syncTempoMin = 40, syncTempoMax = 200;  // Ext: 20..300
    bool undo                 = false;
    int  banks                = 10;
    // ...
};
```

There are no `if (authentic)` branches in DSP; behaviour follows values. The UI hides Extended panels when `Limits` says they are off.

## 4. Threading model

| Thread | Owns | Talks to audio via |
|---|---|---|
| Audio (real-time) | Engine, VoicePool, EffectHost, Sequencer playback, Recorder write head | reads `UiToAudio` SPSC FIFO each block; writes `AudioToUi` SPSC FIFO |
| Message (UI) | PanelModel, components, timers (60 Hz repaint via VBlankAttachment) | pushes Commands to engine FIFO; reads `AudioToUi` (voice activity, peak, tick position, recorder status) |
| Loader pool (`juce::ThreadPool`, 2 threads) | file decode, Time Modify render, truncate, exports, resample commit | results published via atomic pointer swap of immutable `SampleData` |
| Recorder writer | drains the recorder ring to disk | lock-free ring |
| MIDI input | standalone: JUCE's device callback; plugin: the host | both arrive as the processor's MidiBuffer, parsed on the audio thread by `MidiInput` |

Rules:

- No allocation, locks, file I/O or logging on the audio thread. `Engine::process` opens an `rt::ScopedRealtime` section; the test binary replaces global operator new (`tests/AllocGuard.cpp`) and counts allocations inside such sections, and tests require zero. Plugins do not replace operator new, since that would leak into the host process.
- Sample data is immutable once published. Replacing a sample: build new `SampleData` on a worker, then hand a `shared_ptr`-like intrusive handle to the audio thread through the FIFO; the old one is returned through a "garbage" FIFO and freed on the message thread. No ref-count release on the audio thread that could free.
- Parameter changes from UI: atomics for continuous values (knobs), FIFO messages for discrete events (pad hits, mode changes). Effects smooth continuous params internally.

## 5. Engine rate and latency

The engine runs at 44.1 kHz internally (Authentic default). `RateConverter` wraps the engine:

```
host/device buffer (fs_dev) -> input SRC -> engine @44.1k (fixed internal block = 32 samples) -> output SRC -> host
```

- Polyphase Kaiser-windowed sinc (`dsp/SincResampler.h`), 128 taps, passband to about 20 kHz at 44.1k.
- `EngineHost` renders the engine in 32-sample chunks on demand. Every event is scheduled at an absolute engine time `(deviceTime + offset) * 44100/fs + S`, with S = output look-ahead + one chunk, so all events have the same latency (107 samples at 48 kHz). That latency is reported via `setLatencySamples`. The input path has a zero pre-roll so the engine never reads input that has not arrived.
- When `fs_dev == 44100` the converter is bypassed (zero added latency).
- Extended "native rate" setting bypasses the converter and runs DSP at `fs_dev`; lo-fi/bitcrush then alias differently, which is the documented trade-off.
- Fixed internal block size makes effect behaviour independent of host buffer size and simplifies sample-accurate sequencing.

## 6. State model

`SamplerController` (message thread) is the single entry point for document edits: it updates `Project` and posts the matching engine commands, runs file loads and Time Modify renders on a 2-thread pool, and applies their results in `tick()`.

Sample lifetime: the engine holds one reference per published `SampleData`. When a pad's sample is replaced, the old pointer is parked in a retired list until no voice reads it, then returned through a FIFO and released on the message thread (`Engine::collectGarbage`). The audio thread never drops the last reference.

Two layers:

1. **Document state** (`state::Project`): samples (file refs + per-pad settings), patterns, per-bank protect, song (Ext), stickers override, mixer settings. Serialized to `project.json` + `samples/*.wav`. Versioned schema with migrations.
2. **Device settings** (`state::Settings`): the FUNC-menu settings that the hardware stores internally (input/output gain, illumination, effect limit, MIDI channel, MIDI sync, auto trigger), plus app preferences. Stored per user in `~/Library/Application Support/<app>/settings.json`.

Runtime-only state (current pad, lit modes, effect knob positions, pattern playing) lives in PanelModel and Engine.

Standalone vs plugin:

- **Standalone**: project is a folder on disk; autosave every N s to `project.autosave/`; crash marker file triggers recovery on next launch.
- **Plugin**: `getStateInformation` writes the Project JSON plus either embedded sample data (default, so sessions are self-contained) or references to a project folder (user option for large sets). Device settings for the plugin instance are also embedded so each instance is reproducible.

Undo (Extended): command pattern over document state; non-destructive edits keep the original audio, so truncate becomes a region change until "commit".

## 7. Plugin specifics

- Buses: stereo out (main); stereo in (sidechain-style input used for sampling / EXT SOURCE / FX mode).
- FX processor mode: track audio feeds EXT SOURCE permanently; effect applies to it; pads still playable.
- Host transport and tempo drive the sequencer when "follow host" is on; tempo is not quantized to the hardware table when following a host.
- Parameters exposed to host automation: VOLUME, CTRL 1..3, effect type, effect on/off, output gain. Everything else is state, not automatable parameters, to keep pluginval and hosts happy.
- MIDI: host MidiBuffer replaces MIDI IN; MIDI out (Extended) via the plugin's MIDI output.

## 8. Testing strategy

- `core_tests` (Catch2):
  - DSP: deterministic offline renders; golden hashes for bit-exact blocks (16-bit requantize, bitcrush), tolerance comparisons for float paths; NaN/Inf/denormal sweeps at parameter extremes; silence-in/silence-out where applicable.
  - Sequencer: quantize grids, shuffle offsets, count-in length, loop wrap, overdub, reservation, immediate switch, length changes, tempo table.
  - PanelModel scripts for every flow in SPEC (FUNC menus, delete, copy, MARK, truncate...).
  - IO: WAV/AIFF round-trip, project JSON round-trip, plugin state round-trip.
- `render_cli`: `render_cli --project p --script hits.txt --out out.wav` for headless checks and the Fidelity lab.
- pluginval at strictness 10 on AU and VST3 at each milestone (macOS only).
- CI (GitHub Actions, macOS runner) builds all targets and runs tests; Linux job builds core + tests for speed.

## 9. Voice stealing policy (SPEC U4)

Documented here because the manual is silent.

1. A pad that is already sounding (trigger, gate, loop) restarts its own voice instead of allocating a new one.
2. Otherwise, if the free channel-voice budget is insufficient (stereo needs 2), steal the oldest voice(s) by start time, excluding EXT SOURCE.
3. A stolen voice gets a 2 ms fade-out, so stealing never clicks.
4. Policy is a small strategy class with tests; Extended can choose "quietest" instead.
