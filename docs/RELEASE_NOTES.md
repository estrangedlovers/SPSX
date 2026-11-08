# SPSX 0.4.0 (preview)

Phases 0 to 4 of the plan are done: spec, engine, hardware panel, all 29 effects, and sampling / EXT SOURCE / resample. The pattern sequencer (Phase 5) and MIDI (Phase 6) are next, so this is still a preview.

## New in 0.4.0

**All 29 effects**, with the hardware's controls, display codes and behaviour:
- Direct buttons: FILTER+DRIVE, VOICE TRANS, DELAY, ISOLATOR, DJFX LOOPER.
- MFX 1 to 24: REVERB, TAPE ECHO, CHORUS, FLANGER, PHASER, TREMOLO/PAN, DISTORTION, OVERDRIVE, FUZZ, WAH, OCTAVE, COMP, EQUALIZER, BIT CRASH, NOISE GEN, VINYL SIM, RADIO TUNING, SLICER+FLG (the manual's 16 patterns), RING MOD, CHROMATIC PS, PITCH, C.CANCELER, SUBSONIC, BPM LOOPER.
- Pick an MFX with MFX + CTRL 3, MFX + pad (1 to 12) or MFX + DJFX LOOPER + pad (13 to 24). FLANGER and PHASER have the hidden balance on MFX + CTRL 1.
- The effect applies to the last pad you played, to the pads you pick with REMAIN + pads, or to everything with REMAIN + effect button. EFFECT GRAB (hold TAP TEMPO + effect button), REMAIN + knobs presets and the last-effect reminder work as on the hardware.
- Delay times, Slicer rates and BPM Looper lengths follow the tempo of the longest targeted sample (or the pattern tempo).
- Effect Limit (FUNC + pad 8) tames the 17 starred effects and adds a safety limiter.
- Switching effects cross-fades, so it never clicks. Drive effects are 4x oversampled; BIT CRASH and LO-FI keep their aliasing on purpose.

**Sampling**: REC, choose an empty pad, adjust the level (PEAK, CTRL 3, red display ring when clipping), REC to start and stop. Effects can be printed while sampling. Auto sampling (FUNC + pad 4, display `rdY`), one-bar count-in (START/END/LEVEL lit), end point snapped to the beat when you set a tempo. Recordings are 16-bit 44.1 kHz in Authentic mode (WAV or AIFF), written to `~/Music/SPSX/Recordings`.

**EXT SOURCE**: play the live input like a pad, through the effects, with its own level and GATE.

**Resample**: RESAMPLE, set the level with CTRL 3, REC, pick an empty pad, REC, then hit the source pad: playback and recording start together. Polyphony drops to 4 mono / 2 stereo while resampling, as on the hardware.

**MIC**: MIC uses the first input channel of the selected input device with MIC LEVEL; otherwise the input is stereo LINE with INPUT GAIN (FUNC + pad 11). Pick your Mac's built-in mic or your interface in the audio settings.

**Plugin FX Mode**: run the track's audio through the effects (top bar, plugin only). For instruments in Logic, route audio via the side-chain input.

## Still to come
Pattern sequencer (Phase 5), MIDI (Phase 6), song mode and export (Phase 7), stickers (Phase 8), settings window (Phase 9). Backup load/save and pattern copy/exchange show `ENP` until then.

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
Unit tests pass on macOS and Linux (including every effect at its parameter extremes, click-free switching and zero allocations on the audio thread). AU and VST3 passed pluginval at strictness 10 on macOS and the AU passed `auval -strict`.
