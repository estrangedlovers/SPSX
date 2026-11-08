# FIDELITY: what is documented, what is assumed

Confidence scale:

- **High**: behaviour stated in the manual, or a standard technique whose sound is not in question.
- **Medium**: manual describes the function; the algorithm and voicing are our design. Likely close in feel.
- **Low**: manual gives a name and a one-line description only. Sound is a guess until checked in the Fidelity lab (Phase 10).

No hardware is available. Every DSP block below starts at Medium or Low and is revisited in Phase 10 against reference recordings. When reference audio is lossy (streamed demos, YouTube), conclusions above ~16 kHz and about noise floors are unreliable and must be marked as such.

Undocumented control-behaviour questions are listed as U1..U22 in `SPEC.md` section 14 and are not repeated here except where they affect DSP.

## Global signal path

| Block | Documented | Assumed | Confidence |
|---|---|---|---|
| Sample format | 16-bit linear, 44.1 kHz (p. 49) | Requantize to 16-bit on record/import, no dither (prompt instruction) | High |
| Mixer headroom | Output gain -3..+12 dB is an "internal processing level"; many pads can distort (p. 39) | Hard clip at 0 dBFS after output gain, before VOLUME (Authentic). Extended: no clip. | Medium |
| VOLUME | Analog volume (p. 9) | Gain = knob^3 (audio-taper approximation), post-clip | Medium |
| Input path | Input gain -10/0 dB before PEAK LED; CTRL 3 digital level after ADC (p. 19) | PEAK threshold -6 dBFS analog-equivalent; red illumination at digital clip | Medium |
| Mic level | Cannot reach zero (p. 45) | Gain 0 dB..+40 dB, minimum not silent | Low |
| Engine rate | 44.1 kHz | Polyphase Kaiser-windowed sinc (128 taps, beta 9, cutoff 0.95 x lower Nyquist, 512 phases). Tested: 10 kHz tone error < -60 dB re 0.5, 23 kHz tone into 44.1k rejected by > 70 dB. Event latency at 48 kHz: 107 samples. | High (technique) |
| Polyphony | 12 mono / 6 stereo; 4 / 2 while resampling (p. 11, 26) | Stereo counts 2 | High |
| Voice stealing | Not documented | Oldest first; pad retrigger reuses own voice | Low |

## Sample playback

| Block | Documented | Assumed | Confidence |
|---|---|---|---|
| Gate/trigger/loop/one-shot/reverse | p. 12 | 2 ms fade on stop/steal only; no fade-in and no loop crossfade (SPEC U26) | Medium |
| LEVEL 0..127 | p. 27 | Linear gain mapping `level/127` **[could be dB-scaled]** | Low |
| LO-FI | "Switches the sample's playback quality (standard/lo-fi)" (p. 12) | Implemented (`dsp/LoFi.h`): zero-order hold at 1/4 rate (11.025 kHz) with no anti-alias filter, 10-bit mid-tread quantizer without dither, one-pole low-pass at 9 kHz. All three values are in `LoFi::Params` for Phase 10 tuning. | Low |
| Start/end | ±130 ms per operation, min 100 ms region (p. 29) | Sample-accurate points | High |
| BPM derivation | From region length, may be x2/x0.5 (p. 30) | Assume 4/4, choose beat count (1, 2, 4, 8, 16...) whose BPM lands in 80..160 | Medium |
| Time Modify | Pitch-preserving, 0.5x..~1.3x, "may sound quavery or noisy" (p. 30) | Authentic: real-time granular OLA per voice (`dsp/GranularStretcher.h`), 40 ms periodic-Hann grains, 50% overlap, flat first half-grain to keep the attack. Extended: signalsmith-stretch `exact()` render on a worker thread, granular fallback until the render arrives. Tested: length within 2 grains, pitch within 3%. | Low (Authentic voice), High (Extended quality) |

## Direct effects

| Effect | Documented | Method chosen | Confidence |
|---|---|---|---|
| FILTER+DRIVE | LPF with overdrive; cutoff, resonance★, drive★ | Implemented: 4x oversampled tanh drive (0..36 dB, Limit 18) into a ZDF SVF low-pass 40 Hz..18 kHz, Q 0.5..18 (Limit 4), per-sample smoothed cutoff. | Medium |
| VOICE TRANS | Formant, effect level, direct level | Implemented: YIN pitch tracker (4x decimated, every 256 samples) drives pitch-synchronous overlap-add; each grain reads the input at the FORMANT ratio (0.5..2), so formants scale while pitch stays. ~15 ms latency. Whether the hardware also shifts pitch is unknown. | Low |
| DELAY | Note-synced time *1, feedback★, balance; max ~2 s | Implemented: stereo, Hermite reads, 40 ms tap cross-fade on time changes (no pitch bend), 9 kHz LPF + 40 Hz HPF and soft saturation in the loop. Feedback 0..0.98 (Limit 0.7). | Medium |
| ISOLATOR | Low★/Mid★/High★ isolate/remove | Implemented: LR4 split at 300 Hz / 3 kHz with complementary mid (bands sum exactly to the input), each band kill..+6 dB (Limit +3 dB). | Medium |
| DJFX LOOPER | Length★, speed -1..1, loop switch | Implemented: 2.2 s capture, loop 15 ms..1 s (Limit 60 ms min), speed glides with ~120 ms platter inertia, silent at stop, edge fades. | Medium |

## MFX

| # | Effect | Documented | Method chosen | Confidence |
|---|---|---|---|---|
| 1 | REVERB | time, tone, balance | FDN (8 lines, Hadamard), damping as tone | Low |
| 2 | TAPE ECHO | rate (tape speed), intensity★, balance | Delay with speed-modulated read, wow/flutter, saturation and HF loss per repeat; multiple heads **[unknown]** | Low |
| 3 | CHORUS | depth, rate, balance | 2-voice modulated delay, stereo opposed LFO | Medium |
| 4 | FLANGER | depth, rate, feedback★, hidden balance | Short modulated delay with feedback, triangle LFO | Medium |
| 5 | PHASER | depth★, rate, manual, hidden balance | 4 or 8 stage allpass, ZDF, LFO around manual | Medium |
| 6 | TREMOLO/PAN | depth, rate, waveform (trem vs pan curve) | LFO morph: left half shapes tremolo curve (tri to square), right half pan curve **[interpretation]** | Low |
| 7 | DISTORTION | drive★, tone★, level★ | Hard-ish asymmetric clipper, 4x OS, post tilt EQ | Medium |
| 8 | OVERDRIVE | drive★, tone★, level★ | Soft tanh clipper, 4x OS | Medium |
| 9 | FUZZ | drive★, tone★, level★ | Full-wave/rectified fuzz with high gain, 4x OS | Medium |
| 10 | WAH | peak, rate, manual | ZDF bandpass with LFO auto-wah around manual | Medium |
| 11 | OCTAVE | -2 oct, -1 oct, direct | Monophonic: pitch-tracked flip-flop divider style sub-octaves **[analogue-style guess]** | Low |
| 12 | COMP | sustain, attack/release★, level★ | Feed-forward RMS compressor; sustain = threshold+ratio | Medium |
| 13 | EQUALIZER | low★, mid★, high★ | Low shelf 200 Hz, mid peak 1 kHz, high shelf 4 kHz, ±15 dB **[frequencies assumed]** | Medium |
| 14 | BIT CRASH | rate, bit★, filter | Sample-and-hold decimation with no anti-alias, bit reduction without dither, post LPF as FILTER | Medium |
| 15 | NOISE GEN | white, disc (pop), hum | "Applies a lo-fi effect" plus three noise generators; lo-fi part undefined | Low |
| 16 | VINYL SIM | frequency range, noise, wow/flutter | Band-limit, crackle, wow/flutter delay mod | Low |
| 17 | RADIO TUNING | detune, noise, balance | Band-pass 300..3k, AM-style distortion, noise rising with detune, heterodyne whistle | Low |
| 18 | SLICER+FLG | pattern P01..P16, rate, feedback★ (flanger depth) | Gate per section 8.4 patterns with ~1 ms ramps, followed by flanger | Medium (patterns High) |
| 19 | RING MOD | frequency★, sens★, balance | Sine carrier; sens = envelope-following modulation of carrier frequency | Low |
| 20 | CHROMATIC PS | pitch 1, pitch 2 (±12 st), balance | Two granular/delay-line pitch shifters (low latency) | Medium |
| 21 | PITCH | pitch, feedback★, balance | Delay-line pitch shifter with feedback | Medium |
| 22 | C.CANCELER | L-R balance, low boost, high boost | L - R with balance offset; low and high shelves on the mid-cancel output (restore bass) | Medium |
| 23 | SUBSONIC | pitch, threshold, balance | Envelope follower gates a sine oscillator (20..120 Hz) | Medium |
| 24 | BPM LOOPER | length★, timing, loop switch | Tempo-synced capture loop; auto-retrigger every N 8ths | Medium |

Parameter ranges for all effects: undocumented (SPEC U15). Display 0..127 except note values, P01..P16, -1.0..1.0, oFF/on, oFF/1..8 and semitones.

### Effect implementation notes (Phase 3)

- All nonlinear stages (FILTER+DRIVE, DISTORTION, OVERDRIVE, FUZZ) run 4x oversampled (JUCE polyphase IIR half-band). BIT CRASH and LO-FI keep their aliasing on purpose.
- REVERB: 8-line FDN, Householder mixing, per-line damping, two modulated lines, four Schroeder diffusers, 12 ms pre-delay, RT60 0.3..12 s.
- TAPE ECHO: 60..600 ms with a slow time smoother (moving RATE bends pitch like a tape motor), 0.7 Hz wow + 6.3 Hz flutter, saturating feedback that can exceed unity (Limit 0.75).
- OCTAVE, CHROMATIC PS, PITCH: low-latency two-tap delay-line pitch shifters (45..70 ms windows); octave voices low-passed.
- SLICER+FLG: the manual's 16 patterns (32 steps per cycle), cycle = RATE note value, 1.5 ms gate ramps, flanger depth/regeneration from CTRL 3.
- BPM LOOPER: note-value loop length; TIMING N re-captures every N eighth notes (SPEC U8).
- COMP: stereo-linked log-domain detector, soft knee 6 dB, SUSTAIN sets threshold (-6..-48 dB) and ratio (2..12) together, partial automatic make-up.
- Tests (tests/TestEffects.cpp): every effect at 27 knob combinations x Limit on/off stays finite, denormal-free and bounded; silence in -> silence out (except NOISE GEN, VINYL SIM, RADIO TUNING); DELAY lands on the synced note value within 2 samples; no allocation while processing.

### Sampling and resampling (Phase 4)

| Block | Documented | Implemented | Confidence |
|---|---|---|---|
| Recording format | 16-bit linear, 44.1 kHz WAV/AIFF (p. 49) | Authentic: 16-bit integers written bit-exact (no dither). Extended: 32-bit float WAV (24-bit for AIFF). Streamed to disk through a 10 s lock-free FIFO and a writer thread. | High |
| Count-in | One bar, -1- .. -4- (p. 26) | Clicks at the pattern tempo (accented first beat), sample-accurate start | High |
| Auto sampling | 10 levels (p. 25) | -54..-6 dBFS (SPEC U35) | Low |
| PEAK / red illumination | p. 19 | -6 dBFS / digital clip (SPEC U36, U37) | Low |
| Resample polyphony | 4 mono / 2 stereo (p. 26) | Voice budget switched while RESAMPLE is lit | High |

Effect Limit: which parameters are limited is documented (17 effects, starred, verified by a test). How much is limited is not (U7). Implemented per parameter (lower feedback ceilings, less resonance, less drive, smaller boosts, minimum loop lengths, bit depth >= 4), plus a soft limiter (knee at 0.8, ceiling 1.0) on the effect output. A test drives eight hot effects at maximum with Limit on and requires output <= 1.0.

Effect switching: "seamless" (p. 2). Implementation: 20 ms equal-power crossfade between old and new effect instances, with tails of the old effect allowed to ring out in Extended mode **[Authentic: unknown whether tails continue]**.

## Sequencer and timing

| Block | Documented | Assumed | Confidence |
|---|---|---|---|
| 96 PPQN | p. 49 | Events stored in ticks, rendered sample-accurately | High |
| Tempo steps | p. 15 | 127-value table | High |
| Quantize values | p. 32 | | High |
| Shuffle | 50 straight, 60..66 typical | Swing ratio = value/100 of the pair length for the off-beat of the grid | Medium |
| Count-in | One measure, -4..-1 | | High |
| Metronome | Volume adjustable | Click sound design | Low |

## How this file is maintained

Each row gets updated in Phase 10 with: reference material used, whether it was lossless, what matched, what did not, and the new confidence.
