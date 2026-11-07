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
| FILTER+DRIVE | LPF with overdrive; cutoff, resonance★, drive★ | 2-pole ZDF (TPT) SVF low-pass, tanh drive stage before the filter, 4x oversampled | Medium |
| VOICE TRANS | Formant, effect level, direct level | Real-time formant shift via LPC envelope + pitch-synchronous resynthesis (TD-PSOLA). Original likely also shifts pitch; unknown. | Low |
| DELAY | Note-synced time *1, feedback★, balance; max ~2 s | Stereo digital delay, interpolated read, LPF in loop (gentle) | Medium |
| ISOLATOR | Low★/Mid★/High★ isolate/remove | 3-band Linkwitz-Riley crossover (LR4), each band -inf..+6 dB, centre = unity | Medium |
| DJFX LOOPER | Length★, speed -1..1, loop switch | Capture buffer, variable-rate read (cubic interpolation), speed slew to mimic turntable inertia | Medium |

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

Parameter ranges for all effects: undocumented (SPEC U15). Display assumed 0..127.

Effect Limit: which parameters are limited is documented (17 effects, starred). How much is limited is not (U7). Starting point: limited parameters capped at 75% of travel, plus a soft limiter at -1 dBFS on any feedback loop output.

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
