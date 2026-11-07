# ARCHITECTURE

## 1. Targets and build

- One CMake project, JUCE pinned to a release tag via `FetchContent` (submodule is an alternative; FetchContent keeps the repo small). C++20.
- One shared static library `core` holds everything except the plugin/app shells. Targets: `Standalone`, `AU`, `VST3` from `juce_add_plugin`, plus `core_tests` (Catch2) and `render_cli` (headless renderer).
- `CMAKE_OSX_ARCHITECTURES=arm64` by default; `-DUNIVERSAL=ON` adds `x86_64`.
- Generators: Ninja (CLion) and Xcode both supported; no generator-specific logic.
- Third-party: JUCE (AGPLv3 / commercial; you choose), Catch2 (BSL-1.0), signalsmith-stretch (MIT), nlohmann/json (MIT) **or** juce::JSON. Each added dependency gets a line in `THIRD_PARTY.md` with its license.

## 2. Module breakdown

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
| MIDI input callback | converts to timestamped events | SPSC FIFO into audio thread (standalone); plugin uses host MidiBuffer |

Rules:

- No allocation, locks, file I/O or logging on the audio thread. Enforced in debug builds by an allocation guard (operator new hook armed during `processBlock`) that asserts.
- Sample data is immutable once published. Replacing a sample: build new `SampleData` on a worker, then hand a `shared_ptr`-like intrusive handle to the audio thread through the FIFO; the old one is returned through a "garbage" FIFO and freed on the message thread. No ref-count release on the audio thread that could free.
- Parameter changes from UI: atomics for continuous values (knobs), FIFO messages for discrete events (pad hits, mode changes). Effects smooth continuous params internally.

## 5. Engine rate and latency

The engine runs at 44.1 kHz internally (Authentic default). `RateConverter` wraps the engine:

```
host/device buffer (fs_dev) -> input SRC -> engine @44.1k (fixed internal block = 32 samples) -> output SRC -> host
```

- Polyphase windowed-sinc, ~ 64 taps, passband to 20 kHz at 44.1k. Latency is constant and reported via `setLatencySamples`.
- When `fs_dev == 44100` the converter is bypassed (zero added latency).
- Extended "native rate" setting bypasses the converter and runs DSP at `fs_dev`; lo-fi/bitcrush then alias differently, which is the documented trade-off.
- Fixed internal block size makes effect behaviour independent of host buffer size and simplifies sample-accurate sequencing.

## 6. State model

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
