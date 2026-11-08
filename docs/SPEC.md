# SPEC: hardware behavior reference

Source of truth: the 2009 owner's manual (English section, manual pages 2 to 52), fetched from Roland's support site as `SP-404SX_egfsipd02_W.pdf`. Page references below (`p. N`) are the manual's printed page numbers. PDF page = printed page + 2.

This document describes the **hardware**. It covers what the hardware does in each state, so the Authentic mode can be checked against it. Extended-mode behavior lives in `PLAN.md` and `ARCHITECTURE.md`. Where the manual is silent, the gap is listed in section 14 and tracked in `FIDELITY.md`; nothing here is invented without being marked **[assumed]**.

Branding note: the product name appears in this document only to identify the reference. It must not appear in the UI, bundle ID or assets.

---

## 0. Conflicts between the project prompt and the manual

The manual wins for Authentic mode. Each item is resolved as shown.

| # | Prompt says | Manual says | Resolution |
|---|---|---|---|
| C1 | "12-voice polyphony" | "12 monaural samples or 6 stereo samples simultaneously" (p. 11, p. 18). Specs: "Maximum Polyphony 12 voices" (p. 49). | Polyphony is counted in **mono channels**: a stereo voice costs 2. Budget = 12 channel-voices. |
| C2 | Resample uses "reduced voice count" | "up to four monaural samples or two stereo samples" (p. 26) | Budget during resample = 4 channel-voices. |
| C3 | Pattern length "1 to 99 measures" | Range 1 to 99 (p. 32), but "one-measure units for the range of 1 to 20 measures, and in four-measure units for longer patterns" (p. 32) | Lengths are 1..20 then 24, 28, ... in 4-bar steps. Whether 99 itself is reachable is unknown (see U12). |
| C4 | "Effect Grab (momentary effect while the button is held)" | Hold **[TAP TEMPO] (EFFECT GRAB)** and press an effect button; the effect sounds only while that effect button is held (p. 13) | Effect Grab requires TAP TEMPO as the modifier. |
| C5 | Prompt lists only MFX button for selecting MFX | MFX can also be chosen with MFX + pad 1..12 (MFX 1 to 12) and MFX + DJFX LOOPER + pad 1..12 (MFX 13 to 24) (p. 13) | Implement all three selection methods. |
| C6 | Prompt lists the Hold pad without its gesture | HOLD is pressed **while a pad is held**; the sample keeps playing after release and stops when that pad is pressed again (p. 12) | As manual. |
| C7 | Pad lighting while playing | p. 11 says the pad "will light red while the sample is playing"; p. 12 step 3 says "That pad will blink while its sample is playing" | Internal manual conflict. See U1. |
| C8 | MIDI velocity follows "whatever the manual says" | Implementation chart: Note On velocity **O** (recognized), Note Off **X** (not recognized) (p. 48). Nothing says what velocity does. | See U5 and U6. |
| C9 | Prompt lists "Slicer patterns P01 to P16: if the manual doesn't define them, design 16" | The manual **does** define all 16 in notation (p. 44, note *2) | Use the transcription in section 8.4. No approximation needed. |
| C10 | "FUNC + TAP TEMPO pauses all sound" | Pause also disables all button operations except resuming (p. 12) | As manual. |

---

## 1. Physical controls and connectors

Panel numbering follows the manual's diagram (p. 4 to 5).

### 1.1 Top panel

| # | Control | Normal function | Secondary uses |
|---|---|---|---|
| 1 | VOLUME knob | Master output volume (p. 9) | none |
| 2 | CTRL 1 knob | Effect parameter 1 | Start point fine adjust (START/END/LEVEL lit, p. 29); Time Modify (TIME/BPM lit, p. 30). Panel legend: CUTOFF / START / TIME. MFX + CTRL 1 = direct/effect balance for FLANGER and PHASER only (p. 44). |
| 2 | CTRL 2 knob | Effect parameter 2 | Pattern or sample BPM (TIME/BPM lit, p. 15, 30); end point fine adjust (START/END/LEVEL lit, p. 29); shuffle amount (QUANTIZE lit with S.8/S.16, p. 32). Legend: RESONANCE / END / BPM. |
| 2 | CTRL 3 / MFX knob | Effect parameter 3 | MFX type (with MFX held, p. 13); sample LEVEL (START/END/LEVEL lit, p. 27); sampling input level (during sampling standby and resample, p. 19, 26); metronome level (pattern rec standby + START/END/LEVEL, p. 32); pattern LENGTH (p. 32); QUANTIZE value (p. 32); value entry for every FUNC setting. Legend: DRIVE / LEVEL. |
| 3 | PEAK indicator | Lights on analog input level of LINE IN (post input gain); should light "occasionally" (p. 19) | Also the reference for the auto-sampling trigger level (p. 25) |
| 4 | Effect buttons | FILTER+DRIVE, VOICE TRANS, DELAY (left of display); ISOLATOR, DJFX LOOPER, MFX (right of display) | See section 7 |
| 5 | Display | 3-character 7-segment LED with decimal points; "BPM/VALUE" legend | See section 11 |
| 5 | Display illumination | Ring light around the display. Blinks with pattern tempo (p. 40). Lights **red** when the internal digital input level is excessive during sampling, resampling and EXT SOURCE level setting (p. 19, 24, 26). | Colour mode set by FUNC + pad 7 |
| 6 | PATTERN SELECT | Toggles pattern mode: pads select/record patterns (p. 14) | Selects data type for protect (p. 38) |
| 6 | LENGTH | Pattern length entry with CTRL 3 (p. 32) | |
| 6 | QUANTIZE | Quantize entry with CTRL 3, shuffle with CTRL 2 (p. 32) | |
| 7 | DEL | Delete sample or pattern (p. 21, 34) | Truncate (with MARK, p. 29); erase notes during pattern rec (p. 33); backup delete (p. 37) |
| 7 | REC | Start/stop sampling or pattern recording; confirms most FUNC settings (p. 18) | |
| 7 | RESAMPLE | Enter resample (p. 26) | Hold at power-on: WAV/AIFF selection (p. 25) |
| 7 | CANCEL | Cancel current operation; stop pattern playback; stop pattern recording. 4 presses in rapid succession stops all sound (p. 12). | CANCEL + DEL = delete all (p. 30, 34); CANCEL + REMAIN = format card (p. 35) |
| 7 | REMAIN | Shows remaining sampling time (p. 25). Hold + pad = set current pad / effect target (p. 13). | Hold + knobs = preset next effect's params; hold + effect button = apply to all samples (p. 13); battery level (p. 6) |
| 7 | MARK | Set start/end markers in real time; lit when region is adjusted; press while lit during playback to clear (p. 27 to 28) | Truncate step (p. 29) |
| 8 | Pads 1 to 12 | Play samples, or select patterns when PATTERN SELECT is lit (p. 11, 14) | FUNC + pad functions (section 5) |
| 9 | START/END/LEVEL | Edit mode: CTRL 1 start, CTRL 2 end, CTRL 3 level (p. 27, 29) | Sampling count-in arming (p. 26); metronome level in pattern rec standby (p. 32); EXT SOURCE level (p. 24) |
| 9 | TIME/BPM | Edit mode: CTRL 2 BPM (pattern if PATTERN SELECT lit, else current sample), CTRL 1 Time Modify (p. 15, 30) | |
| 9 | TAP TEMPO (PAUSE / EFFECT GRAB) | Tap at quarter-note intervals to set tempo (p. 15) | FUNC + TAP = PAUSE (p. 12); hold + effect button = EFFECT GRAB (p. 13); with effects off, pressing it makes the last-used effect button blink (p. 13) |
| 10 | LO-FI | Per-sample toggle: lo-fi playback (p. 12) | |
| 10 | STEREO | Sampling mono/stereo (p. 18); EXT SOURCE mono/stereo (p. 24) | |
| 10 | GATE | Per-sample: gate (lit) or trigger/latch (unlit) (p. 12) | EXT SOURCE momentary when lit (p. 24) |
| 10 | LOOP | Per-sample: loop (lit) or one-shot (p. 12) | |
| 10 | REVERSE | Per-sample: reverse playback (p. 12) | Disables Time Modify (p. 30) |
| 11 | BANK A/F, B/G, C/H, D/I, E/J | Select bank. Lit = first bank (A to E), blinking = second bank (F to J). Press again to toggle (p. 11). | Bank select inside delete/import/copy flows; battery meter |
| 12 | FUNC | Modifier (section 5) | Blinks when batteries are low (p. 6) |
| 13 | HOLD pad | While holding a pad, press HOLD to latch it (p. 12) | During pattern erase, holding HOLD erases all pads (p. 33); HOLD + pattern pad = arm pattern to wait for MIDI Start (p. 47) |
| 13 | EXT SOURCE pad | Plays live input (LINE IN or MIC) through the effects (p. 24) | MIDI note 46 |
| 13 | SUB PAD | Plays the most recently struck pad's sample (p. 12) | SUB PAD + pattern pad = switch pattern immediately (p. 15) |
| 14 | MIC button | Mic input on/off (p. 17) | |
| 15 | Internal mic | Mono condenser; disabled when a plug is in MIC IN (p. 5, 17) | |

Panel group legends visible in the diagram: "SAMPLE EDIT" (START/END/LEVEL, TIME/BPM), "SAMPLE MODE" (LO-FI, STEREO, GATE, LOOP, REVERSE), "BANK", "PATTERN SEQUENCER" (PATTERN SELECT, LENGTH, QUANTIZE). Pad legends: 1 COPY, 2 EXCHANGE, 3 IMPORT, 4 AUTO TRIG LEVEL, 5 BACKUP LOAD, 6 BACKUP SAVE, 7 ILLUMINATION, 8 EFFECT LIMIT, 9 MIDI CH, 10 MIDI SYNC, 11 INPUT GAIN, 12 OUTPUT GAIN. The MFX list 1 to 24 is printed next to the MFX button.

Pad grid (top view):

```
 [ 1] [ 2] [ 3] [ 4]   [ HOLD     ]
 [ 5] [ 6] [ 7] [ 8]   [EXT SOURCE]
 [ 9] [10] [11] [12]   [ SUB PAD  ]
```

### 1.2 Front and rear panels (p. 5)

| # | Item | Emulation mapping |
|---|---|---|
| 16 | PHONES (stereo 1/4") | Same signal as LINE OUT. Not separately emulated. |
| 17 | SD card slot | Project storage (section 12) |
| 18 | MIC LEVEL knob | Mic preamp gain. Cannot be turned fully to zero (p. 45). |
| 19 | MIC IN (1/4") | Interface input chosen as "mic" |
| 20 | Functional ground | n/a |
| 21 | DC IN | n/a |
| 22 | POWER | Power-on key combos (section 6) are reachable from a menu |
| 23 | MIDI IN | MIDI input (section 10) |
| 24 | LINE IN L/R (RCA) | Interface input chosen as "line" |
| 25 | LINE OUT L/R (RCA) | Main output |
| 26 | Security slot | n/a |

There is **no MIDI OUT** and no USB audio.

---

## 2. Data model

| Item | Value | Source |
|---|---|---|
| Sample banks | 10 (A to J) x 12 pads = 120 samples | p. 11, 49 |
| Pattern banks | 10 (A to J) x 12 pads = 120 patterns | p. 14, 49 |
| Sample format | 16-bit linear, 44.1 kHz, WAV or AIFF, mono or stereo | p. 25, 49 |
| Max single sample | ~2 GB (~180 min stereo); exceeding stops sampling with `FUL` | p. 20, 25 |
| Pattern resolution | 96 ticks per quarter note | p. 49 |
| Pattern length | 1 to 99 measures (see C3) | p. 32, 49 |
| Max notes | ~16,000 recordable notes | p. 49 |
| Backups | Up to 12 backup sets (one per pad) | p. 37 |
| Per-sample settings | GATE, LOOP, REVERSE, LO-FI, start, end, MARK state, LEVEL (0 to 127, default 127 after sampling), BPM, Time Modify value | p. 12, 27 to 30 |
| Per-bank settings | Protect on/off (separately for sample and pattern banks) | p. 38 |
| Global persisted settings | Battery type, file type, protect, input gain, output gain, illumination/sleep, effect limit, MIDI channel, MIDI sync, auto-trigger level **[assumed persisted]** | p. 6, 25, 38 to 40, 42, 47 |
| Not persisted | EXT SOURCE level (always 127 at power-on, p. 24) | |

---

## 3. Sample playback

### 3.1 Pads

- Pressing a pad plays its sample. The pressed pad becomes the **current pad**; SAMPLE MODE buttons show and edit its settings (p. 12).
- Pads with a sample are lit; pads without a sample are unlit and silent (p. 45 troubleshooting: "The sample will play when you press a lit pad").
- Multiple pads can play simultaneously within the polyphony budget (C1).
- Bank buttons switch which 12 samples the pads address. Bank F to J are selected by pressing the same bank button a second time so it blinks (p. 11).

### 3.2 Playback modes (per sample, p. 12)

| Button | Lit | Unlit |
|---|---|---|
| GATE | Plays only while pad is held | Plays on after release; next press stops it (trigger/toggle) |
| LOOP | Repeats | Plays once |
| REVERSE | Plays backward | Forward |
| LO-FI | Lo-fi playback | Standard |

Combinations: GATE+LOOP loops while held. GATE off + LOOP on loops until the pad is pressed again. GATE off + LOOP off plays to the end, a second press stops it **[assumed from "alternate between play and stop each time you press" (p. 4)]**. Retrigger behavior in GATE mode (restart vs new voice) is unknown (U2).

### 3.3 HOLD, SUB PAD, PAUSE, stop all

- **HOLD**: while a pad is held, press HOLD; the sample keeps playing after release. Press that pad again to stop (p. 12).
- **SUB PAD**: plays the sample of the most recently struck pad (p. 12). For patterns, see 9.2.
- **PAUSE**: FUNC + TAP TEMPO pauses all sound; all buttons except resume are disabled. FUNC + TAP TEMPO again resumes (p. 12).
- **Stop all**: CANCEL four times in rapid succession stops every sound (p. 12). The time window for "rapid" is unknown (U3).

### 3.4 Polyphony

12 mono channel-voices, stereo samples consume 2 (C1). During resampling, 4 channel-voices (C2). Voice stealing rule is not documented (U4).

### 3.5 Output gain

FUNC + pad 12, CTRL 3, values -3, 0, +3, +6, +9, +12 dB, REC confirms (p. 39). The manual calls it an "internal processing level": raise it when few pads play, lower it when many pads distort. It therefore sits before a clipping stage, not after the DAC.

---

## 4. Sample editing

### 4.1 LEVEL (p. 27)

PATTERN SELECT off, play pad, START/END/LEVEL lit, CTRL 3 sets 0 to 127 (127 = max, default after sampling). Press START/END/LEVEL to exit.

### 4.2 Start/end by MARK (p. 27 to 28)

- **Both points**: play the pad. If MARK is lit, clear it first. Press MARK at the desired start, press MARK again at the desired end.
- **End only**: hold MARK, press pad (it sounds), release MARK when sound starts, press MARK at desired end.
- **Start only**: play pad, press MARK at the desired start, stop the pad.
- **BPM snap**: if BPM is specified between the two MARK presses (TIME/BPM + CTRL 2, or TAP TEMPO), the end point snaps to the nearest beat measured from the start point.
- **Clearing**: pressing the lit MARK during playback clears start and end (p. 27). If MARK is pressed so it goes out while the sound plays in the end-only flow, the change is cancelled (p. 28).
- MARK lights whenever start or end differ from the sample boundaries.

### 4.3 Start/end fine adjust (p. 29)

- START/END/LEVEL lit: CTRL 1 = start, CTRL 2 = end.
- Knobs act **relative to centre**: centre = 0 (no change). Range per operation is about **±130 ms** around the current point. Returning both knobs to centre (display `0`) cancels. Exiting with knobs off-centre commits. To move further, repeat.
- Points cannot pass the sample boundaries. Minimum region about **100 ms**.
- If both points end at the boundaries, MARK goes out. Moving either point from the boundaries lights MARK.
- BPM recalculates from the new length, with "a small discrepancy".

### 4.4 Truncate (p. 29)

Destructive; "cannot be undone". Current pad must have MARK lit. DEL, then MARK (display `trC`), then DEL executes. `trC` disappears when done.

### 4.5 BPM (p. 30)

- Sample BPM is auto-derived from region length. It may read as half or double the musical tempo.
- TIME/BPM lit, CTRL 2: turn left to halve, right to double the displayed value.
- 3-beat regions may not calculate or adjust correctly (p. 27, 46).

### 4.6 Time Modify (p. 30)

- TIME/BPM lit, CTRL 1 (TIME) sets the playback length shown as BPM. Pitch is preserved.
- Full left: `oFF` (original length). Full right: `Ptn` (follow pattern tempo).
- Range: half to about 1.3x the original BPM, and always within 40 to 200 BPM. In `Ptn`, a pattern tempo outside the range is clamped to that range ("limited to the maximum value (200) or minimum value (40)").
- Disabled while REVERSE is on.
- "May sound quavery or noisy" (expected artifacting).

### 4.7 Delete, copy, exchange

- **Delete sample** (p. 21): DEL (`dEL`), select bank (protected banks show `Prt`), press one or more pads (blinking to lit), DEL executes. During deletion the display shows `dAL` per the note on p. 21.
- **Delete all samples** (p. 30): PATTERN SELECT off, hold CANCEL + press DEL (`dAL`, DEL lit, BANK buttons blink), press a bank button, DEL executes. CANCEL aborts.
- **Copy** (p. 31): FUNC + pad 1 (`CPY`); press source pad (available sources blink); press destination (any bank); REC blinks; REC executes. Not enough memory: `FUL`.
- **Exchange** (p. 31): FUNC + pad 2 (`CHG`, DEL and REC light); press pad A, then pad B (any bank); REC blinks; REC executes.

### 4.8 Import (p. 36)

FUNC + pad 3 (`iNP`, REC lit, pads blink), select bank (`Prt` if protected), select starting pad, REC executes. Files are imported in ASCII filename order starting at the selected pad and overwrite existing samples; protected banks are skipped; max 120 files; unsupported format shows `UnS`; files under ~100 ms may fail; empty import folder shows `ENP`; AIFF loop points are ignored; compressed WAV is rejected. Import takes about half the playback time.

---

## 5. FUNC combinations

FUNC held + pad (p. 39) and other FUNC combos:

| Combo | Function | Display | Values | Confirm |
|---|---|---|---|---|
| FUNC + TAP TEMPO | PAUSE / resume | | | immediate |
| FUNC + 1 | COPY | `CPY` | | REC |
| FUNC + 2 | EXCHANGE | `CHG` | | REC |
| FUNC + 3 | IMPORT | `iNP` | | REC |
| FUNC + 4 | AUTO TRIG LEVEL | `-0-` .. | 10 stages 0 to 9; `-0-` = auto sampling off. REC blinks. Press REC again "to check the sampling trigger level" (p. 25). | REC |
| FUNC + 5 | BACKUP LOAD | `Lod` | Pads with backups blink; `ENP` if none; `Prt` if protected. DEL from here = backup delete (`bdL`). | REC |
| FUNC + 6 | BACKUP SAVE | `SAU` | Pads without backups blink | REC |
| FUNC + 7 | ILLUMINATION | | `oFF`, `r b` (default), `b`, `r`, `SLP` | REC |
| FUNC + 8 | EFFECT LIMIT | `L.on` / `L.oF` | | REC |
| FUNC + 9 | MIDI CH | | 1 to 16 (Pad Base Ch) | REC |
| FUNC + 10 | MIDI SYNC | | `AUt` (default), `tNp`, `oFF` | REC (manual says "so its light is turned off") |
| FUNC + 11 | INPUT GAIN | | -10 dB, 0 dB | REC |
| FUNC + 12 | OUTPUT GAIN | | -3, 0, 3, 6, 9, 12 dB | REC |
| FUNC + REMAIN | Protect current bank (sample or pattern per PATTERN SELECT) | `P.on` | | REC |
| FUNC + CANCEL | Unprotect current bank | `P.oF` | | REC |

All FUNC settings: REC blinks when the knob is turned; CANCEL aborts; settings persist through power-off.

Illumination values (p. 40): `oFF` never blinks, even in pattern playback. `r b` blinks red and blue. `b` blue only. `r` red only. `SLP` sleep: after ~5 min idle all buttons go out, the three display dots blink in sequence, no sound is produced, any button wakes, illumination behaves as `oFF`.

## 5a. CANCEL and REMAIN combinations

| Combo | Function | Ref |
|---|---|---|
| CANCEL x4 rapid | Stop all sound | p. 12 |
| CANCEL (during flows) | Abort the flow | throughout |
| CANCEL (pattern playing) | Stop pattern | p. 15 |
| CANCEL (pattern recording) | Stop recording and commit; display dot blinks while writing | p. 33 |
| CANCEL (resample lit) | Exit resample so CTRL 3 controls the effect again | p. 26 |
| CANCEL + DEL | Delete all samples (PATTERN SELECT off) or all patterns (on) in a chosen bank (`dAL`) | p. 30, 34 |
| CANCEL + REMAIN | Format card (`FNt`), bank buttons blink, press any bank, DEL blinks, DEL executes | p. 35 |
| REMAIN (press) | Remaining time display | p. 25 |
| REMAIN (hold, PATTERN SELECT off) | Battery level on bank buttons; all lit on AC | p. 6 |
| REMAIN + pad(s) | Choose effect target pads (lit = targeted). Also "modify current pad" without playing it. | p. 13 |
| REMAIN + EXT SOURCE | Make EXT SOURCE the current pad / effect target | p. 24 |
| REMAIN + effect button | Apply that effect to all samples | p. 13 |
| REMAIN + CTRL 1..3 | Pre-specify parameters for the next effect that is selected | p. 13 |

Remaining time display formats (p. 25), always computed for stereo: the manual's legend shows `0.11` = minutes.seconds, `10.3` = minutes.tens-of-seconds, `173` = minutes, `24H` = hours. Exact switch-over thresholds are unknown (U10).

---

## 6. Power-on key combinations

| Held at power-on | Function | Display | Ref |
|---|---|---|---|
| FUNC | Battery type: CTRL 3 `AL` / `ni`, REC confirms | `AL` / `ni` | p. 6 |
| RESAMPLE | File type: PATTERN SELECT toggles, RESAMPLE confirms | `UAU` / `AiF` | p. 25 |
| REMAIN | Protect all banks | `P.on` | p. 38 |
| CANCEL | Unprotect all banks | `P.oF` | p. 38 |
| CANCEL + REMAIN + DEL | Factory reset; REC executes | `ini` | p. 40 |
| MARK | Show firmware version; CANCEL returns | version | p. 40 |

In the app these are menu commands in Authentic mode (no power switch). Battery behavior is cosmetic and out of scope beyond showing the `AL`/`ni` page for completeness.

---

## 7. Effects

### 7.1 General rules (p. 13)

- 29 effects: 5 direct buttons + MFX with 24 types. Only **one effect at a time**. Pressing a lit effect button turns it off.
- MFX type selection: hold MFX + turn CTRL 3; hold MFX + pad 1..12 = MFX 1..12; hold MFX + DJFX LOOPER + pad 1..12 = MFX 13..24.
- Target: by default the effect applies only to the **most recently pressed pad**. REMAIN + pads chooses several targets (lit). REMAIN + effect button applies to all samples. EXT SOURCE becomes the target via REMAIN + EXT SOURCE.
- REMAIN + CTRL knobs pre-sets the parameters for the next effect selected.
- EFFECT GRAB: hold TAP TEMPO and press an effect button; the effect is on only while that button is held.
- With all effect buttons off, pressing TAP TEMPO makes the last-used effect's button blink as a reminder.
- Effects "switch seamlessly during live performance" (p. 2).
- During sampling: an effect can be printed into the sample, but only CTRL 1 and CTRL 2 control it because CTRL 3 is the input level (p. 19). Same restriction during resample (p. 26).
- FLANGER and PHASER have a hidden fourth parameter: MFX + CTRL 1 = direct/effect balance (p. 44).
- Tempo-synced parameters reference the tempo of the **longest sample among the targeted pads**, or the pattern tempo while a pattern is playing (notes *1, *2, *4 on p. 44).

### 7.2 Effect Limit (p. 42)

FUNC + pad 8; `L.on` / `L.oF`; persists. Limits depth "to prevent unintentionally loud sounds or oscillation" on starred parameters. 17 effects have at least one starred parameter. How the limit is applied (range clamp vs compression) is undocumented (U7).

### 7.3 Direct effects

Starred (★) parameters are affected by Effect Limit.

| Effect | CTRL 1 (display) | CTRL 2 (display) | CTRL 3 (display) | Description |
|---|---|---|---|---|
| FILTER+DRIVE | CUTOFF (`CoF`) | RESONANCE (`rES`) ★ | DRIVE (`drU`) ★ | Low-pass filter with overdrive |
| VOICE TRANS | FORMANT (`FrN`) | EFFECT LEVEL (`EFF`) | DIRECT LEVEL (`dir`) | Voice character transformer |
| DELAY | DELAY TIME (`t32`..`t1`) *1 | FEEDBACK (`Fdb`) ★ | BALANCE (`bAL`) | Tempo-synced delay |
| ISOLATOR | LOW (`Lo`) ★ | MID (`Mid`) ★ | HIGH (`Hi`) ★ | Isolates/removes 3 bands |
| DJFX LOOPER | LENGTH (`LEn`) ★ | SPEED (`-1.0`..`1.0`) | LOOP SW (`oFF`/`on`) | Short loop with variable speed/direction; centre = stopped, left = reverse, right = forward. Turning LOOP SW on captures the current sound. |

### 7.4 MFX 1 to 24

| No. | Effect | CTRL 1 | CTRL 2 | CTRL 3 | Description |
|---|---|---|---|---|---|
| 1 | REVERB | REVERB TIME (`tiM`) | TONE (`ton`) | BALANCE (`bAL`) | |
| 2 | TAPE ECHO | RATE (`rAt`) | INTENSITY (`int`) ★ | BALANCE (`bAL`) | Rate = tape speed; intensity = repeats |
| 3 | CHORUS | DEPTH (`dPt`) | RATE (`rAt`) | BALANCE (`bAL`) | |
| 4 | FLANGER | DEPTH (`dPt`) | RATE (`rAt`) | FEEDBACK (`Fdb`) ★ | MFX + CTRL 1 = balance |
| 5 | PHASER | DEPTH (`dPt`) ★ | RATE (`rAt`) | MANUAL (`MAn`) | MFX + CTRL 1 = balance |
| 6 | TREMOLO/PAN | DEPTH (`dPt`) | RATE (`rAt`) | WAVEFORM (`trM`/`PAn`) | CTRL 3 left of centre = tremolo curve, right = pan curve |
| 7 | DISTORTION | DRIVE (`drU`) ★ | TONE (`ton`) ★ | LEVEL (`LEU`) ★ | Intense |
| 8 | OVERDRIVE | DRIVE (`drU`) ★ | TONE (`ton`) ★ | LEVEL (`LEU`) ★ | Mild |
| 9 | FUZZ | DRIVE (`drU`) ★ | TONE (`ton`) ★ | LEVEL (`LEU`) ★ | Adds overtones |
| 10 | WAH | PEAK (`PEt`) | RATE (`rAt`) | MANUAL (`MAn`) | Peak = width (Q) |
| 11 | OCTAVE | -2OCT LEVEL (`oC2`) | -1OCT LEVEL (`oC1`) | DIRECT LEVEL (`dir`) | Adds -1 and -2 octave |
| 12 | COMP | SUSTAIN (`SUS`) | ATTACK (`Att`) / RELEASE (`rEL`) ★ | LEVEL (`LEU`) ★ | With Limit on, CTRL 2 becomes RELEASE |
| 13 | EQUALIZER | LOW (`Lo`) ★ | MID (`Mid`) ★ | HIGH (`Hi`) ★ | |
| 14 | BIT CRASH | SAMPLE RATE (`rAt`) | BIT (`bit`) ★ | FILTER (`FLt`) | "Extreme lo-fi" |
| 15 | NOISE GEN | WHITE NOISE (`UHt`) | DISC NOISE (`diS`) | HUM NOISE (`HUM`) | "Applies a lo-fi effect, and also adds noises" (hiss, pop, hum) |
| 16 | VINYL SIM | FREQUENCY RANGE (`rnG`) | NOISE LEVEL (`noS`) | WOW/FLUTTER (`FLU`) | |
| 17 | RADIO TUNING | DETUNE (`tUn`) | NOISE LEVEL (`noS`) | BALANCE (`bAL`) | Detune = tuning drift |
| 18 | SLICER+FLG | TIMING PTN (`P01`..`P16`) *2 | RATE (`t32`..`t1`) *1 | FEEDBACK (`Fdb`) ★ | CTRL 3 = flanger depth |
| 19 | RING MOD | FREQUENCY (`Frq`) ★ | SENS (`SEn`) ★ | BALANCE (`bAL`) | Sens = depth to which frequency is modulated |
| 20 | CHROMATIC PS | PITCH1 (`-C`..`CC`) | PITCH2 (`-C`..`CC`) | BALANCE (`bAL`) | Two voices, semitone steps, ±1 octave |
| 21 | PITCH | PITCH (`Pit`) | FEEDBACK (`Fdb`) ★ | BALANCE (`bAL`) | |
| 22 | C.CANCELER | L-R BALANCE (`L-r`) | LOW BOOST (`Lo`) | HIGH BOOST (`Hi`) | Centre-channel cancel; L-R sets the cancel point |
| 23 | SUBSONIC | PITCH (`Pit`) | THRESHOLD (`tHr`) | BALANCE (`bAL`) | Adds a low sine triggered by input level *3 |
| 24 | BPM LOOPER | LENGTH (`LEn`) ★ | TIMING (`oFF`, `1`..`8`) *4 | LOOP SW (`oFF`/`on`) | Auto-start loop every N 8th notes |

Effects with at least one ★ (17): FILTER+DRIVE, DELAY, ISOLATOR, DJFX LOOPER, TAPE ECHO, FLANGER, PHASER, DISTORTION, OVERDRIVE, FUZZ, COMP, EQUALIZER, BIT CRASH, SLICER+FLG, RING MOD, PITCH, BPM LOOPER.

Display glyph note: the manual's 7-segment font renders M as a two-hump glyph. Text extraction shows it as "N" (`Nid`, `NAn`, `tiN`, `FrN`, `trN`, `HUN`). Codes in this document use `M` where the glyph is an M. The 7-segment renderer must reproduce the manual's glyphs, not ASCII.

### 7.5 Footnotes (p. 44), verbatim in substance

- **\*1 Note values** (DELAY TIME, SLICER RATE): relative to the tempo of the longest target sample, or the pattern tempo if a pattern plays. 13 values in order: `t32` 32nd, `t16` 16th, `t8t` 8th triplet, `t16.` dotted 16th, `t8` 8th, `t4t` quarter triplet, `t8.` dotted 8th, `t4` quarter, `t2t` half triplet, `t4.` dotted quarter, `t2` half, `t2.` dotted half, `t1` whole. Values whose delay would exceed **~2000 ms** cannot be selected.
- **\*2 Slicer TIMING PTN**: P01 to P16 (section 8.4). The pattern cycle syncs to the longest target sample's tempo or the pattern tempo. RATE max (full right) = one cycle per measure; RATE min (full left) = one cycle per 32nd note; anywhere between from 32nd to 1 measure.
- **\*3 Subsonic setup**: balance toward 12 o'clock, pitch fully right, adjust threshold until the sine sounds appropriately, then adjust pitch and balance. Intended to reinforce kick drums.
- **\*4 BPM LOOPER TIMING**: cycle syncs to the longest target sample's tempo or the pattern tempo.

---

## 8. Effect detail tables used by implementation

### 8.1 Delay/Slicer note-value table (beats at 4/4, quarter = 1)

| Code | Beats |
|---|---|
| t32 | 0.125 |
| t16 | 0.25 |
| t8t | 0.3333 |
| t16. | 0.375 |
| t8 | 0.5 |
| t4t | 0.6667 |
| t8. | 0.75 |
| t4 | 1.0 |
| t2t | 1.3333 |
| t4. | 1.5 |
| t2 | 2.0 |
| t2. | 3.0 |
| t1 | 4.0 |

Delay: selectable iff `beats * 60 / bpm <= ~2.0 s`.

### 8.2 DJFX LOOPER speed

Continuous -1.0 to 1.0, 0.0 at 12 o'clock = stopped; negative = backward. Display shows one decimal.

### 8.3 BPM LOOPER timing

`oFF` or 1 to 8, in 8th-note intervals: the captured loop restarts automatically every N eighth notes (interpretation of "the timing at which the looped sound will automatically start sounding (in 8th note intervals)"; see U8).

### 8.4 SLICER+FLG timing patterns (p. 44, transcribed)

One 4/4 bar in 32nd-note steps. `X` = slice opens (onset), `=` = still open, `.` = muted. Generated and checked by `tools/slicer_patterns.py`; every row sums to 32.

```
P01 X===....X===....X===....X===....
P02 X=====..X=====..X=====..X=====..
P03 X=......X=......X=......X=......
P04 X=====..X=..X=..X=====..X=..X=..
P05 X=..X=..X=......X=..X=..X=......
P06 X=..X=..X=..X=..X=..X=..X=......
P07 X===....X=..X=..X===....X=..X=..
P08 X===......X===..X===......X===..
P09 X===....X=....X=====....X=......
P10 X===..X=..X=....X===......X=....
P11 ..X===......X=....X===......X=..
P12 ....X===..X=..X=....X===......X=
P13 X=====..X=..X=..X=..X=====..X=..
P14 X=..X=..X=..X=..X=====..X=..X=..
P15 X===....X=..X=..X=..X=..X=......
P16 X=====..X=====..X=..X=====..X=..
```

P09 contains a tied 16th + 8th (steps 14 to 19). P15 has no closing barline in the figure; it still sums to one bar.

---

## 9. Pattern sequencer

### 9.1 Specs (p. 49)

96 PPQN; 1 to 99 measures (C3); realtime loop recording with shuffle quantize; ~16,000 notes; 120 patterns.

### 9.2 Playback (p. 14 to 15)

- PATTERN SELECT lit: display `Ptn`; pads holding a pattern blink.
- Press a blinking pad: it goes steady and the pattern plays.
- Press another pad while playing: that pattern is **reserved** (its pad blinks) and starts when the current pattern finishes its loop.
- SUB PAD + pad: switch immediately.
- Press the playing pad, or CANCEL: stop.
- Display illumination blinks with the pattern tempo (unless illumination = `oFF`).
- With MIDI sync `AUt`: HOLD + pattern pad arms the pattern to start on MIDI Start (p. 47).
- Patterns record pad events only. Whether they record bank changes, effect changes or knob moves: the manual describes only "sample-playback operations" (p. 32) and "You can also press a bank button to switch sample banks" during recording (p. 33). Effect automation is **not** recorded **[assumed]** (U9).

### 9.3 Tempo (p. 15)

TIME/BPM lit, CTRL 2. With PATTERN SELECT lit it edits the pattern tempo; otherwise the current sample's BPM.

Allowed values:

- 40 to 60: even numbers only.
- 61 to 159: every integer.
- 160 to 180: even numbers only.
- above 180: 180, 183, 186, 190, 193, 196, 200 only.

TAP TEMPO at quarter-note intervals also sets tempo. Tap-tempo averaging and quantization to the allowed set are unknown (U11).

Full allowed set (count = 11 + 99 + 11 + 6 = 127 values; 127 knob positions **[assumed mapping]**):
`40,42,...,60, 61..159, 160,162,...,180, 183,186,190,193,196,200`.

### 9.4 Recording (p. 32 to 33)

1. PATTERN SELECT lit (`Ptn`).
2. REC lit: empty pattern pads blink.
3. Choose a pattern bank.
4. Press the destination pad: it lights, others go out, REC blinks, **metronome starts**.
5. START/END/LEVEL lit, CTRL 3 = metronome volume.
6. TIME/BPM lit, CTRL 2 or TAP = tempo.
7. LENGTH lit, CTRL 3 = length (C3). A pattern with data cannot be shortened.
8. QUANTIZE lit, CTRL 3 = quantize; CTRL 2 = shuffle when S.8 or S.16.
9. REC: REC goes steady after a **one-measure count-in** (display `-4`, `-3`, `-2`, `-1`), then recording starts and the display shows measure and beat.
10. Recording loops over the length; previous passes play back; new hits overdub.
11. REC again: REC blinks and display shows `rEH` (**rehearsal**: pads play but are not recorded). REC again returns to recording.
12. QUANTIZE can be changed during recording (QUANTIZE lit, CTRL 3, QUANTIZE off); takes effect immediately.
13. Bank buttons may be pressed during recording to play other sample banks.
14. CANCEL stops recording; the display dot blinks while saving.

Quantize values (CTRL 3):

| Display | Grid |
|---|---|
| `S.8` | 8th with shuffle |
| `S.16` | 16th with shuffle |
| `4` | quarter |
| `4.3` | quarter triplet |
| `8` | 8th |
| `8.3` | 8th triplet |
| `16` | 16th |
| `16.3` | 16th triplet |
| `32` | 32nd |
| `oFF` | no quantize, raw timing |

Shuffle (CTRL 2 while quantize is S.8/S.16): 50 = straight; 60 to 66 typical swing. Range limits are not stated (U13). Shuffle is a recording-time quantize ("shuffle quantize"), not a playback groove **[assumed from wording]**.

### 9.5 Erasing notes (p. 33)

During pattern recording: DEL lit (`ErS`); hold a pad to erase that pad's events for as long as it is held (display dot blinks). Holding HOLD erases all pads. Pressing a sample pad while not erasing records it. DEL off returns to normal recording. CANCEL ends and writes.

### 9.6 Pattern management

- Delete (p. 34): PATTERN SELECT lit, DEL (`dEL`; `ENP` if the bank is empty), select bank and pad(s) (same bank only), DEL executes.
- Delete all in bank: CANCEL + DEL (`dAL`), bank, DEL.
- Copy and exchange: as for samples, with PATTERN SELECT lit (p. 34).
- Protect: per pattern bank (p. 38).

### 9.7 Pattern note semantics

A pattern stores pad-on events referencing a bank and pad. On playback each event behaves as if the pad were pressed, so the target sample's GATE/LOOP settings decide the result. Whether note durations (pad release) are recorded for GATE samples is not documented (U14).

---

## 10. Sampling, EXT SOURCE and resample

### 10.1 Signal flow (p. 19, 39)

```
LINE IN -> INPUT GAIN (-10/0 dB) -+-> [analog] -> PEAK LED -> ADC -> CTRL 3 (digital level) -> effects -> sample
MIC IN / internal mic -> MIC LEVEL-+                                  |-> display illumination red if too hot
                                                                      +-> resampling path
```

### 10.2 Sampling (p. 18 to 20)

1. PATTERN SELECT off. 2. REC lit (CANCEL aborts). 3. Choose bank; empty pads blink. 4. Press pad: it lights, REC blinks (standby). 5. STEREO on/off. Optional: select an effect (CTRL 1/2 only). 6. Set source level until PEAK lights occasionally; MIC LEVEL for mic; CTRL 3 for digital level until illumination is no longer red. 7. REC: REC steady, sampling. 8. REC: stop.

- No free pad: cannot sample (p. 46). Sampling in pattern mode or during DEL is not possible.
- `FUL` when the card is full or the sample exceeds 2 GB.
- Mono gives ~2x time and 2x polyphony vs stereo.

### 10.3 Auto sampling (p. 25)

FUNC + 4 sets trigger level 0 to 9 (`-0-` = off). With a level set, starting sampling shows `rdY` until the input exceeds the level (measured where the PEAK LED is).

### 10.4 Sampling to tempo and count-in (p. 26)

- Set BPM in standby (TIME/BPM or TAP). After sampling, the end point snaps to a beat of that BPM and MARK lights. BPM may display half/double.
- Count-in: with tempo set, START/END/LEVEL lit, then REC: metronome sounds, display `-1-`, `-2-`, `-3-`, `-4-`, `rEC`; sampling starts at `rEC`.

### 10.5 EXT SOURCE (p. 24)

- Press EXT SOURCE so it is lit: live input is heard (MIC button for mic).
- GATE lit: heard only while held.
- STEREO lit: stereo input, unlit: mono.
- Effects apply when an effect is on; if not applied, REMAIN + EXT SOURCE targets it.
- Level: START/END/LEVEL lit, CTRL 3; default 127 at every power-on.
- MIDI note 46 on Pad Base Ch.

### 10.6 Resample (p. 26)

1. PATTERN SELECT off. 2. Apply and set an effect. 3. RESAMPLE lit (`LEU`). 4. Play the source. 5. CTRL 3 = level until illumination is not red (CTRL 3 no longer controls the effect; CANCEL exits resample). 6. REC blinks; banks and empty pads blink. 7. Press destination pad (any bank; must be empty). 8. STEREO. 9. REC lit (not yet recording). 10. Press the **source** pad: playback and resampling start together. 11. REC stops.

Polyphony during resample: 4 mono / 2 stereo.

### 10.7 Input gain and file type

INPUT GAIN: FUNC + 11, -10 or 0 dB (p. 39). File type WAV/AIFF (power-on + RESAMPLE).

---

## 11. Display

### 11.1 Hardware

3 characters, 7 segments each, plus decimal points (the dots are used for `S.8`, `P.on`, `4.3`, minute/second separators and the busy indicator). Illumination ring with red/blue colours.

### 11.2 Busy indicator

"The dot in the display will blink" during write operations (pattern write, delete, import, backup, format, erase). The dot in the lower right blinks for low battery (p. 6). Sleep: the three dots blink sequentially.

### 11.3 Display code index

Effect parameter codes: see 7.3 and 7.4.

| Code | Meaning | Ref |
|---|---|---|
| `Ptn` | Pattern select mode; Time Modify synced to pattern | p. 14, 30 |
| `oFF` / `on` | Off / on | various |
| `dEL` | Delete (select pads) | p. 21, 34 |
| `dAL` | Delete all / deletion in progress | p. 21, 30, 34 |
| `trC` | Truncate | p. 29 |
| `CPY` | Copy | p. 31 |
| `CHG` | Exchange | p. 31 |
| `iNP` | Import | p. 36 |
| `SAU` | Backup save | p. 37 |
| `Lod` | Backup load | p. 37 |
| `bdL` | Backup delete | p. 37 |
| `FNt` | Format | p. 35 |
| `P.on` / `P.oF` | Protect on / off | p. 38 |
| `L.on` / `L.oF` | Effect Limit on / off | p. 42 |
| `AUt` / `tNp` / `oFF` | MIDI sync auto / tempo / off (`tNp` is "tMp" in 7-seg) | p. 47 |
| `r b` / `b` / `r` / `SLP` | Illumination modes | p. 40 |
| `UAU` / `AiF` | WAV / AIFF | p. 25 |
| `AL` / `ni` | Alkaline / Ni-MH | p. 6 |
| `ini` | Factory reset standby | p. 40 |
| `-0-` .. `-9-` **[format of 1..9 assumed]** | Auto trigger level | p. 25 |
| `rdY` | Waiting for auto-sampling threshold | p. 25 |
| `-1-` .. `-4-`, `rEC` | Sampling count-in, recording | p. 26 |
| `-4` .. `-1` | Pattern recording count-in | p. 33 |
| `rEH` | Pattern rehearsal | p. 33 |
| `ErS` | Erase notes | p. 33 |
| `LEU` | Resample level / level parameter | p. 26 |
| `S.8` `S.16` `4` `4.3` `8` `8.3` `16` `16.3` `32` | Quantize | p. 32 |
| `Lo` | Battery exhausted (also EQ/Isolator LOW) | p. 6 |

### 11.4 Error messages (p. 45)

| Code | Name | Meaning |
|---|---|---|
| `EJC` | Eject | Card removed during processing |
| `LoC` | Lock | Card is write-locked |
| `ENP` | Empty | Nothing to delete / no backup / no file to import |
| `Err` | Error | Invalid data found; it was repaired or deleted |
| `FUL` | Memory Full | Insufficient memory; or sample exceeded 2 GB |
| `Prt` | Protected | Operation blocked by protect |
| `UnS` | Unsupported | WAV/AIFF format not supported |

App mapping: `EJC`/`LoC` map to project folder unavailable/read-only. `Err` maps to recovered/corrupt project data.

### 11.5 Values

The display shows BPM, parameter values and settings. Numeric range for most effect parameters is **not documented** (U15). The project assumes 0 to 127 shown as `0`..`127` unless the manual gives a different scale (BPM, note codes, P01..P16, -1.0..1.0, oFF/1..8, semitones).

---

## 12. Storage (SD card)

- Folder `ROLAND/SP-404SX/SMPL`, files `A0000001.WAV` .. `J0000012.WAV` (bank letter + 7-digit pad number) (p. 35).
- Import folder `ROLAND/IMPORT` (files are deleted after import) (p. 36).
- Factory data folder `FCTRY` (p. 40).
- Pattern, settings and backup file formats are **not documented** in the manual (U16).

App equivalent: a project folder (Phase 7). The SD layout export is a low-priority Extended item.

---

## 13. MIDI

### 13.1 Note map (p. 47)

| Note | # | Pad Base Ch | Pad Base Ch + 1 |
|---|---|---|---|
| A#2 | 46 | EXT SOURCE | |
| B2..A#3 | 47..58 | Bank A pads 1..12 | Bank F pads 1..12 |
| B3..A#4 | 59..70 | Bank B pads 1..12 | Bank G pads 1..12 |
| B4..A#5 | 71..82 | Bank C pads 1..12 | Bank H pads 1..12 |
| B5..A#6 | 83..94 | Bank D pads 1..12 | Bank I pads 1..12 |
| B6..A#7 | 95..106 | Bank E pads 1..12 | Bank J pads 1..12 |

Formula: for note `n` in 47..106, `bankIndex = (n - 47) / 12` (0..4), `pad = (n - 47) % 12 + 1`; channel Base selects banks A..E, Base+1 selects F..J. Behavior when Base Ch = 16 is unknown (U17).

Note numbering: Roland convention, middle C = C4 = 60.

MIDI notes trigger samples directly regardless of the bank shown on the panel **[assumed from the table]**.

### 13.2 MIDI sync (FUNC + 10, p. 47)

| Mode | Clock | Start/Stop/Continue | Song Position |
|---|---|---|---|
| `AUt` (default) | followed | start/stop patterns; ignored during pattern recording | received |
| `tNp` | followed | ignored | ignored |
| `oFF` | ignored | ignored | ignored |

Sync tempo range 40 to 200 BPM (p. 46). HOLD + pattern pad arms a pattern to start with MIDI Start; pressing the pad without HOLD starts immediately.

### 13.3 MIDI Implementation Chart (p. 48), full transcription

Model SP-404SX, Date July 01, 2009, Version 1.00.

| Function | | Transmitted | Recognized | Remarks |
|---|---|---|---|---|
| Basic Channel | Default | X | 1-16 | |
| | Changed | X | 1-16 | |
| Mode | Default | X | Mode 3 | |
| | Messages | X | X | |
| | Altered | ************** | | |
| Note Number | | X | 46-106 | |
| | : True Voice | ************** | 46-106 | |
| Velocity | Note On | X | O | |
| | Note Off | X | X | |
| After Touch | Key's | X | X | |
| | Channel's | X | X | |
| Pitch Bend | | X | X | |
| Control Change | | X | X | |
| Program Change | | X | X | |
| | : True Number | ************** | | |
| System Exclusive | | X | X | |
| System Common | : Song Position | X | O | *1 |
| | : Song Select | X | X | |
| | : Tune Request | X | X | |
| System Real Time | : Clock | X | O | *2 |
| | : Commands | X | O | *1 |
| Aux Messages | : All Sound Off | X | O | *3 |
| | : Reset All Controllers | X | X | |
| | : Local On/Off | X | X | |
| | : All Notes Off | X | X | |
| | : Active Sensing | X | O | |
| | : System Reset | X | X | |

Notes: \*1 Received if Sync Mode is AUTO. \*2 Received if Sync Mode is AUTO or TEMPO. \*3 All notes will be silenced regardless of their channel.

Mode 1: OMNI ON, POLY. Mode 2: OMNI ON, MONO. Mode 3: OMNI OFF, POLY. Mode 4: OMNI OFF, MONO. O: Yes. X: No.

Implications: no MIDI out of any kind; no CC; Note Off is not recognized (U6); All Sound Off (CC 120) is the only controller message recognized.

---

## 14. Undocumented behavior (tracked in FIDELITY.md)

| ID | Question | Working decision |
|---|---|---|
| U1 | Does a playing pad light steady or blink? (p. 11 vs p. 12) | Blink while playing, steady when loaded and idle **[assumed]**; switchable for Fidelity lab verification |
| U2 | GATE retrigger: restart same voice or layer a new voice? | Restart same voice (monophonic per pad) **[assumed]** |
| U3 | CANCEL x4 time window | 4 presses within 1.0 s **[assumed]** |
| U4 | Voice stealing rule | Steal oldest voice; a pad retrigger reuses its own voice first **[assumed, documented in ARCHITECTURE]** |
| U5 | Does Note On velocity change level? | Authentic: ignore velocity (pads are not velocity sensitive) **[assumed; chart marks velocity "recognized"]** |
| U6 | With Note Off unrecognized, how does GATE behave via MIDI? | Authentic: Note On acts like a pad press and release at once; GATE samples play one-shot behaviour per the trigger rule. Note On vel 0 also ignored **[assumed]**. Extended: Note Off honoured. |
| U7 | How Effect Limit limits | Clamp starred parameters to a reduced range and add a soft limiter on feedback paths **[assumed]** |
| U8 | BPM LOOPER TIMING exact semantics | Loop retriggers every N 8th notes from the moment LOOP SW is set **[assumed]** |
| U9 | Are bank changes, effect changes or knob moves recorded in patterns? | Only pad events (with their bank) **[assumed]** |
| U10 | Remaining time display thresholds | `M.SS` under 10 min, `MM.S` (tens of seconds) under 100 min, `MMM` under 1000 min, `NNH` hours above **[assumed]** |
| U11 | Tap tempo algorithm | Average the last 4 intervals, snap to the allowed set **[assumed]** |
| U12 | Pattern length above 20: is 99 reachable? | 1..20, 24, 28, ..., 96, 99 **[assumed]** |
| U13 | Shuffle range | 50..75 **[assumed]** |
| U14 | Do patterns record note length for GATE samples? | Yes, pad release is recorded **[assumed]** |
| U15 | Numeric ranges of effect parameters | 0..127 **[assumed]** |
| U16 | Pattern and settings file formats on SD | Unknown; not needed for Authentic mode |
| U17 | Pad Base Ch = 16: where do banks F..J go? | Wrap to channel 1 **[assumed]** |
| U18 | What exactly does LO-FI do (rate, bits, filter)? | See FIDELITY.md |
| U19 | Metronome sound | Short click, accented downbeat **[assumed]** |
| U20 | Behaviour of CTRL knobs when switching effects (pickup vs jump) | Knobs are absolute; switching effect reads current knob positions unless REMAIN presets exist **[assumed]** |
| U21 | Does the effect process the whole mix or only targeted pads? | Only targeted pads go through the effect; others are dry (p. 13 "applied only to the pad you pressed most recently") |
| U22 | Is the metronome audible during sampling count-in only, or throughout? | Count-in only **[assumed]** |
| U23 | SUB PAD on a trigger-mode pad that is still playing: toggle off or retrigger? | Retrigger from the start (the manual pitches SUB PAD for rapid repeats); in GATE mode SUB PAD release stops it **[assumed]** |
| U24 | Default GATE/LOOP/REVERSE/LO-FI for a new sample | All off; LEVEL 127 (documented); region = whole sample **[assumed]** |
| U25 | LEVEL curve | Linear gain level/127 **[assumed]** |
| U26 | Fades at stop, steal and loop points | 2 ms linear fade on stop/steal; no fade-in, no loop crossfade (keeps the hardware's possible clicks) **[assumed]** |
| U27 | BPM display resolution for samples | 0.1 BPM internally; shown as `98.5` below 100 and as an integer from 100 **[assumed]** |
| U28 | What the display shows when idle | Sample mode: the current pad's BPM (`---` for an empty pad). Pattern mode: `Ptn` (documented) **[assumed for sample mode]** |
| U29 | How long a turned knob's value stays on the display | 1.5 s, then back to the idle display; entering a mode clears it at once **[assumed]** |
| U30 | Display units for START/END fine adjust | Offset from the knob centre in tens of milliseconds, `-13`..`13` (the manual only says centre shows `0`) **[assumed]** |
| U31 | MFX button: toggle on press or release? | On release, and only when MFX was not used as a modifier (MFX + pad / CTRL 3) in between, so selecting a type never toggles the effect **[assumed]** |
| U32 | HOLD and SUB PAD LEDs | Lit while pressed **[assumed]** |
| U33 | Display during PAUSE | Unchanged **[assumed]** |
| U34 | Effect knob values when switching effects | The CTRL knobs are absolute pots: a newly selected effect takes the knobs' current positions, unless REMAIN + knobs pre-set values (p. 13). While sampling or resampling, CTRL 3 is the level and the effect keeps its own CTRL 3 value **[assumed]** |
| U35 | AUTO TRIG LEVEL in dB | Levels 1..9 = -54..-6 dBFS in 6 dB steps, measured at the PEAK point (after INPUT GAIN / MIC LEVEL) **[assumed]** |
| U36 | PEAK threshold | -6 dBFS with a 100 ms hold **[assumed]** |
| U37 | CTRL 3 digital level range while sampling / resampling | Silent at the far left, 0 dB at about 80 % of travel, +12 dB at the right, so "all the way right, then back until the red goes out" (p. 19) works **[assumed]**; red illumination when the level clips (with 300 ms hold) |
| U38 | MIC vs LINE on a computer | The app has one input device. MIC on = mono from the first input channel with MIC LEVEL (+6..+46 dB); MIC off = stereo LINE with INPUT GAIN. Choose the Mac's built-in mic or an interface as the input device **[deviation, documented]** |
| U39 | Display in sampling standby | The pattern tempo (it is the tempo used for count-in and beat snapping) **[assumed]** |
| U40 | What the effect processes while sampling | Only the input being sampled; pads play dry during sampling **[assumed from p. 19]** |
| U41 | What resample records | The mix after the effect, before OUTPUT GAIN and VOLUME, times CTRL 3 level **[assumed]** |
| U42 | EXT SOURCE STEREO | The same STEREO setting as sampling (one button) **[assumed]** |
| U43 | Slicer: repeated slices | A 1 ms dip at each onset that follows a sounding step, so repeated slices articulate **[design choice]** |
| U44 | Pattern tempo | Each pattern stores its tempo; a playing pattern runs at it, TIME/BPM in pattern mode edits the playing (or last chosen) pattern, a new recording takes the tempo set in standby **[assumed]** |
| U45 | Display while recording a pattern | Measure and beat as `M.b` (` 1.1` .. `99.4`); count-in `-4` .. `-1` (documented) **[format assumed]** |
| U46 | Reserved / armed pattern pads | Blink fast, to tell them from pads that merely hold a pattern (which blink) **[assumed]** |
| U47 | Pads in ErS (erase) mode | Erase only; they do not sound **[assumed]** |
| U48 | Hits during the count-in | Not recorded **[assumed]** |
| U49 | Stopping a pattern | Stops the sequencer; samples already sounding play on (CANCEL x4 stops them) **[assumed]** |
| U50 | Plugin host sync | Like MIDI sync `AUt`: the host's tempo drives the sequencer; HOLD + pattern pad arms it to start with the host's transport (at the host's bar position); the host stopping stops it; with the host stopped, patterns still play by hand **[design choice]** |
| U51 | Note length of recorded hits | Recorded from pad release (needed for GATE samples); a pad still held when recording stops ends there (SPEC U14) **[assumed]** |
