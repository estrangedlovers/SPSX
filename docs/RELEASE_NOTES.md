# SPSX 0.6.0 (preview)

Phases 0 to 6 of the plan are done: engine, hardware panel, all 29 effects, sampling, the pattern sequencer and MIDI. Song features and export (Phase 7), stickers and the settings window come next, so this is still a preview.

## New in 0.6.0

**Pattern sequencer**: PATTERN SELECT, then a pad plays its pattern; another pad reserves the next one (SUB PAD switches at once). Record with REC: metronome in standby, one bar count-in, loop overdub, quantize (QUANTIZE, 10 grids with shuffle), rehearsal (rEH), erase (DEL, then hold pads or HOLD for all), LENGTH, per-pattern tempo with TIME/BPM, delete / copy / exchange patterns. Patterns are saved with the session.

**MIDI**:
- Play pads from a keyboard or DAW on the hardware's note map: note 46 is EXT SOURCE, 47 to 106 are banks A to E on the Pad Base Ch (FUNC + pad 9) and F to J on the next channel.
- Authentic mode behaves like the hardware: velocity and Note Off are ignored, so a note is a hit and GATE samples play out.
- MIDI SYNC (FUNC + pad 10): `AUt` follows MIDI clock and Start / Stop / Song Position (HOLD + a pattern pad waits for Start), `tNp` follows tempo only, `oFF` ignores sync. As a plugin, the DAW's tempo and transport follow the same setting.
- All Sound Off (CC 120) and Active Sensing are recognised.
- Extended mode adds velocity (fixed, linear or soft curve), Note Off, MIDI learn for every knob and button, and export of your patterns as MIDI files (MIDI button in the top bar).

**Safety**: nothing louder than full scale and nothing broken (NaN / Inf) ever leaves SPSX, in either mode. A final brickwall sits after everything else and is completely transparent below 0 dBFS, a runaway effect is reset, corrupt audio files and bad host input are cleaned before use, and the output fades in after a fault or an audio device change.

**Stability and speed**: PAUSE no longer drops scheduled note-offs, faster event queue and sample-rate converter, and the panel redraws only what changed.

## Earlier (0.4.0)
All 29 effects with Effect Limit, Effect Grab and REMAIN targets; sampling with auto trigger and count-in; EXT SOURCE; resample; plugin FX Mode.

## Still to come
Song mode, second effect slot, mixdown and stems, projects with autosave (Phase 7), stickers (Phase 8), settings window with MIDI out and learn profiles (Phase 9). Backup load/save shows `ENP` until then.

## Install (macOS)
The download contains `SPSX.component` (AU), `SPSX.vst3` and `SPSX.app` (standalone), universal (Apple Silicon and Intel).

1. Copy `SPSX.component` to `~/Library/Audio/Plug-Ins/Components/` and `SPSX.vst3` to `~/Library/Audio/Plug-Ins/VST3/`. Put `SPSX.app` wherever you like.
2. The build is ad-hoc signed, not notarized, so macOS quarantines it when downloaded. Clear the flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/SPSX.component ~/Library/Audio/Plug-Ins/VST3/SPSX.vst3 /path/to/SPSX.app
```

3. The standalone app asks for microphone access the first time you sample.
4. If Logic does not list it, rescan in Logic's Plug-in Manager (or run `killall -9 AudioComponentRegistrar` and restart the host).

## Install (Linux)
Copy `SPSX.vst3` to `~/.vst3/`. `SPSX` is the standalone binary.

## Validation for this build
156 unit tests pass on macOS and Linux, including every effect at its parameter extremes, sample-accurate sequencing and MIDI, an output-safety fuzz test and zero allocations on the audio thread. AU and VST3 passed pluginval at strictness 10 on macOS and the AU passed `auval -strict`.
