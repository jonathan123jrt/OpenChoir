# OpenChoir 0.2 – review and fixes

This is an independent-style review of the plugin and the AI pipeline. Each finding was either measured (using `Tools/measure.py` and the self-tests) or found by reading the code against Seed-VC's source. It was then fixed and re-checked.

## Plugin (real-time DSP)

| # | Finding | Evidence (before → after) | Fix |
|---|---|---|---|
| P1 | The choir lagged the lead even at Timing 0, so the "Timing" knob didn't mean what it said | 22.0 ms → 5.9 ms average lag | The unison shifter window went from 45 ms to 20 ms, and only one tap plays at a time (see P2) |
| P2 | The pitch shifter cross-faded continuously, so both taps were always audible and comb-filtered each singer | Worst-case per-frame notch: −5.2 dB → −4.4 dB (a modest improvement) | Single-tap playback with a 40 ms hand-over at each reset. Octave layers still use the full cross-fade automatically |
| P3 | Grit raised the level of quiet passages instead of only saturating loud ones | +6.0 dB → −0.1 dB jump (0 → 100 %) | The soft clipper is normalised to unity gain for small signals |
| P4 | Small sections were nearly mono: stereo positions were fixed for 16 singers, so 2–4 active singers bunched in the middle | L/R correlation 0.96 → 0.75 (3 singers, 100 % width) | Active singers spread evenly across the field, widest first; pan moves are smoothed so they can't click |
| P5 | CPU cost | 9.8 % → 7.3 % of one core (Big Hall preset with reverb, server CPU) | Fast sine for the modulators; silent shifter taps are skipped |
| P6 | The reverb recalculated its filters on every audio block | (code) | Reverb parameters update only when they change |
| P7 | The preset menu didn't follow host program changes or project reloads | (code) | The editor syncs the menu 4× per second |
| P8 | The macOS deployment target was set after `project()`, so local builds ignored it | (code) | Moved before `project()` |
| P9 | "Bright Octave Choir" leaned hard on the octave-up layer, which isn't formant-corrected (chipmunk effect) | (listening/design) | The octave-up level went from 45 → 30 % and tone from 12 → 11 kHz |
| P10 | There were no automated tests or validation | — | Added `openchoir_tests`, which covers state save/restore, every preset × 44.1/48/96 kHz × mono/stereo input × odd block sizes (all outputs finite and sane), click-free voice-count changes, and an editor screenshot. CI now also runs **pluginval** (strictness 5, VST3 + AU) and Apple's **auval**, and uploads their reports |

All self-tests pass on the Linux build here. pluginval and auval run only on the Mac build, so their first real results will appear in GitHub Actions under *validation-reports*.

## AI pipeline

| # | Finding | Fix |
|---|---|---|
| A1 | **VocalSet's zip stores every recording up to three times** (by singer, by technique and by vowel). The voice bank would have filled with duplicate pieces, so references would repeat the same audio | Keep one copy per file name, preferring the by-singer tree. Tested with a mock zip that has all three layouts |
| A2 | If the server ignored a range request, the fetcher would have started downloading the whole 6 GB zip into memory | Any response other than a partial one (HTTP 206) is rejected |
| A3 | An expired signed download link mid-fetch would have failed the whole setup | The download link is refreshed and retried |
| A4 | `descript-audio-codec`, a large and fragile install, was required for one import the singing model never uses | Replaced with a small stand-in that raises an error if it is ever called; removed from requirements |
| A5 | `--batch` was applied after the model had already sized its GPU caches | The batch size is now passed in when the model starts |
| A6 | The AI's output level varies between voices, so some singers would dominate the choir | Each stem is level-matched before the random level spread is applied |
| A7 | The final "limiter" was a waveshaper on the whole mix, which distorted it | Replaced with clean gain staging (an RMS target plus a peak ceiling, no distortion) |

## Known limitations (not fixed)

- **The AI model itself hasn't been run in this environment.** The download hosts are blocked here. The engine mirrors Seed-VC's code line by line and falls back to Seed-VC's own path if it fails, but the first real render on a Mac is the real test.
- **Octave layers in the plugin aren't formant-corrected.** Formant-preserving shifting is on the roadmap.
- **pluginval and auval results are still pending** until the first GitHub Actions run.

## 0.3 – the three-screen AI instrument

The AI choir moved into the plugin itself, using the Vocal → Choir → Mix workflow. The end-to-end self-test caught two real bugs, both now fixed:

| # | Finding | Fix |
|---|---|---|
| R1 | The progress bar would have frozen and then jumped to 100 %. JUCE reads a child process's output with a blocking `fread`, so progress lines arrived in 4 KB batches | The engine's output is read line by line; the test now checks that progress updates smoothly |
| R2 | The plugin went silent if a host sent bigger audio blocks than it announced | Audio is processed in chunks of the announced size |

Also verified:
- The FFT convolver behind the mic rooms and the reverb matches direct convolution to within 1e-5 at irregular block sizes, on every FFT backend (its scale is calibrated at start-up).
- The 4th-order Lo-Cut removes a 40 Hz tone by more than 100 dB at its top setting.
- Drag-out files line up with the vocal sample for sample, plus the reverb tail.
- Harmony layers land on the correct scale notes in major and minor keys.
- Key detection matches the test melodies.
- Reopening a project at a different sample rate restores the vocal, the layers, the settings and the mutes.

## 0.3.1 – first real-Mac setup

| # | Finding | Fix |
|---|---|---|
| M1 | No voices were built: every VocalSet clip failed with "Truncated file header". VocalSet's 6 GB zip was written without proper ZIP64 records, so file positions past 4 GB wrap around, and the voice files sit past that point | `fetch_voices.py` now checks each file's stored position and, when it's wrong, finds the true one (stored position + n × 4 GB, confirmed by the file's own header). Tested on a simulated wrapped archive: all 271 files recovered byte for byte |
| M2 | A failed voice build still saved an empty voice bank, so the plugin said "ready" and then failed at render time | No voice bank is saved unless at least one voice was built |
