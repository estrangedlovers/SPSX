# SPSX 0.1.0 (preview)

This is a **preview build**: phases 0 and 1 of the plan are done (spec and engine core). The engine runs, but the hardware panel UI, the effects, sampling, the pattern sequencer and MIDI are not built yet. The window is a temporary test editor.

## What works
- Drop WAV/AIFF/FLAC files onto the pads. Files are converted to 44.1 kHz, 16-bit, like the hardware.
- Click pads to play (hold for GATE). Banks A to J; GATE, LOOP, REVERSE and LO-FI per pad; HOLD; SUB PAD; STOP ALL; VOLUME.
- 12-voice polyphony (stereo samples use 2), oldest-note stealing.
- The engine runs at 44.1 kHz internally at any host sample rate.

## Not yet
Panel UI and keyboard map (Phase 2), the 29 effects (Phase 3), sampling and resample (Phase 4), the pattern sequencer (Phase 5), MIDI (Phase 6). Plugin state saves sample file paths, not the audio itself.

## Install (macOS)
The download contains `SPSX.component` (AU), `SPSX.vst3` and `SPSX.app` (standalone). It is a universal binary (Apple Silicon and Intel).

1. Copy `SPSX.component` to `~/Library/Audio/Plug-Ins/Components/` and `SPSX.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. Put `SPSX.app` wherever you like.
2. The build is ad-hoc signed, not notarized, so macOS quarantines it when downloaded. Clear the flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/SPSX.component ~/Library/Audio/Plug-Ins/VST3/SPSX.vst3 /path/to/SPSX.app
```

3. If Logic does not list it, rescan in Logic's Plug-in Manager (or run `killall -9 AudioComponentRegistrar` and restart the host).

## Install (Linux)
Copy `SPSX.vst3` to `~/.vst3/`. `SPSX` is the standalone binary.

## Validation for this build
Unit tests pass on macOS and Linux. AU and VST3 passed pluginval at strictness 10 on macOS and the AU passed `auval -strict`.
