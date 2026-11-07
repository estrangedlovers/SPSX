# Third-party code

All dependencies are fetched at configure time by `cmake/Dependencies.cmake` at pinned tags.

| Library | Version | License | Used for |
|---|---|---|---|
| JUCE | 8.0.17-security | JUCE 8 licence (owner uses it under the personal-use terms) or AGPLv3 | Framework, plugin formats, audio I/O, GUI |
| signalsmith-stretch | 1.4.0 | MIT | Extended-mode Time Modify (offline render) |
| signalsmith-linear | 0.6.4 (pulled by stretch) | MIT | FFT/STFT for signalsmith-stretch |
| Catch2 | 3.8.1 | BSL-1.0 | Unit tests |

Needs owner approval before adding: Rubber Band (GPL/commercial), Ableton Link (GPLv2/commercial).

The hardware owner's manual in `docs/` is Roland Corporation copyright and is included for personal reference only.
