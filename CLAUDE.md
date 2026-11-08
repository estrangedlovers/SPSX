# CLAUDE.md

SPSX: a recreation of a 2009 12-pad phrase sampler (7-segment display model) as a JUCE app. Standalone + AU + VST3 from one codebase, with an Authentic mode (hardware limits) and an Extended mode (song-finishing features).

## Start here
1. `docs/PROGRESS.md`: current phase, open questions, next step.
2. `docs/SPEC.md`: hardware behaviour (the manual is the source of truth for Authentic mode). Section 0: prompt-vs-manual conflicts. Section 14: undocumented behaviours and working decisions (U1..U43).
3. `docs/ARCHITECTURE.md`: modules, threading, timing model, state, Authentic/Extended via `Limits`.
4. `docs/PLAN.md`: phase tasks. `docs/FIDELITY.md`: per-DSP-block confidence and parameters.

Manual: `docs/SP-404SX_Owners_Manual.pdf` (70 MB, plain git; LFS uploads are refused by the cloud session proxy). English is PDF pages 3 to 54; printed page = PDF page - 2.

## Build and test
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure        # or ./build/tests/SPSXTests "[tag]"
```
- Xcode: `cmake -S . -B build-xcode -G Xcode`. Universal macOS binary: add `-DSPSX_UNIVERSAL=ON`.
- Linux needs: libasound2-dev libxrandr-dev libxinerama-dev libxcursor-dev libxcomposite-dev libfontconfig1-dev libfreetype-dev libgl1-mesa-dev libjack-jackd2-dev.
- Artefacts: `build/SPSX_artefacts/<Config>/{Standalone,VST3,AU}`.
- Panel screenshot without a display: `build/tools/snapshot/panel_snapshot --out p.png [--extended] [--pad A1=kick.wav] [--script "press A1; turn ctrl3 0.7"]`.
- Headless render: `build/tools/render_cli/render_cli --pad A1=kick.wav --script hits.txt --out out.wav --rate 48000` (script format at the top of `tools/render_cli/main.cpp`).

## Layout
- `src/core/`: everything testable without a GUI. `engine/` (Engine, EngineHost, Voice, VoicePool, RecordTap), `fx/` (29 effects, EffectHost, EffectInfo, DspUtil), `dsp/` (SincResampler, LoFi, GranularStretcher), `sampler/` (SampleEdit, TimeModify, StretchRenderer), `state/` (SampleData, PadSettings, Project), `io/` (AudioFileIO), `SamplerController` (message-thread facade), `Limits.h`, `Tempo.h`, `CancelDetector.h`, `RealtimeGuard.h`.
- `src/core/ui_model/`: PanelModel (panel state machine), Controls, DisplayText (7-segment glyphs).
- `src/ui/`: PanelLayout (geometry), PanelComponent (drawing + mouse), KeyMap, SidePanel (Extended).
- `src/plugin/`: AudioProcessor (owns Engine, SamplerController, PanelModel) + editor.
- `tests/`: Catch2; `AllocGuard.cpp` replaces operator new in the test binary to catch audio-thread allocations.
- `tools/`: `render_cli`, `snapshot`, `slicer_patterns.py`, `fetch_manual.sh`.

## Conventions
- Process: work phase by phase. End of phase: clean build, tests pass, update `docs/PROGRESS.md`, commit, **stop and wait for review**.
- Unknown hardware behaviour: add a U-entry to SPEC section 14 and a FIDELITY row; never guess silently.
- Real-time: nothing on the audio thread allocates, locks, does file I/O or logs. Engine state changes travel as `Command`s (UI FIFO, or `schedule()` with an absolute engine time from the audio thread). Samples are immutable `SampleData`; only `SamplerController` creates them.
- Authentic vs Extended differences live in `Limits`, never as mode checks in DSP.
- Tests compare floats exactly where a path is meant to be bit-exact; `-Wfloat-equal` is off for tests only. In `src/` use `juce::exactlyEqual` for intentional exact compares.
- JUCE style: 4-space indent, space before call parentheses, `juce::` qualified, braces on new lines.
- No vendor logos, wordmarks or the hardware's product name in UI, bundle ID or assets.
- UI: all panel behaviour goes in PanelModel (tested); components only draw and forward input.
- Docs: no em dashes.
- Dependencies: check licenses first; Rubber Band and Ableton Link need owner approval.
