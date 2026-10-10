# OpenChoir AI – a choir of real singers

This is the AI companion to the OpenChoir plugin. You give it one lead vocal, and every singer in the voice bank re-sings your exact performance (same melody, words and timing) in their own voice. Those takes are then mixed into a choir or gang vocal.

## Where the voices come from

The bank contains **20 professional singers (11 women, 9 men) from [VocalSet](https://doi.org/10.5281/zenodo.1193957)**, an academic dataset that the singers recorded for open release under **CC BY 4.0**. That licence allows you to use the voices in your music, including commercially, as long as you credit VocalSet. Each render writes the credit into its `report.txt`.

The fetcher downloads about a minute of singing per singer from the 6 GB dataset (roughly 250 MB in total) and cuts it into short, pitch-tagged pieces.

**Why no celebrity or "community" voice models?** Most free RVC models online are clones of famous singers made without their consent. Releasing music with them can get it taken down and can break right-of-publicity laws. Every voice here was recorded by a consenting singer under an open licence.

**Adding your own voices.** Put WAV files (30–60 s of clean singing each, ideally covering low and high notes) in `voices/custom/female/` or `voices/custom/male/`. Use only recordings of people who have agreed to this, such as your own choir members, friends, or yourself.

## The AI engine

The engine is [Seed-VC](https://github.com/Plachtaa/seed-vc) (GPL-3.0), a zero-shot singing voice converter. It needs no training per singer: a few seconds of a voice is enough. The engine runs locally on your Mac and uses Apple Silicon's GPU when it can.

The AI runs in this companion app rather than inside the VST. A plugin has to process audio in real time, and these models are far too heavy for that.

### How OpenChoir makes it faster

OpenChoir drives Seed-VC's singing model with its own engine (`oc_engine.py`). The engine uses the same maths as Seed-VC but is organised for rendering many singers from one vocal:

- **It only renders the parts you sing.** Silence between phrases is skipped entirely.
- **It analyses your vocal once.** The stock tool re-analyses it for every singer.
- **It caches singer analysis.** Each singer's reference is analysed once and kept on disk between runs.
- **It renders several singers at once** on the GPU: 2–4 at a time, depending on your Mac's memory.
- **It loads only the singing model.** The stock tool also loads a speech model and vocoder that a choir never uses.
- **It uses half precision** on Apple Silicon, and switches to full precision automatically if your Mac's GPU can't run it.
- **Shorter references** in fast/good quality leave more of the model's 30-second window for your vocal, so phrases need fewer passes.

If the fast engine can't start on your machine, it falls back to Seed-VC's standard path automatically. That path is slower, but it works.

### How OpenChoir makes it more accurate

- **Register-matched voices.** A voice sounds different low and high, so each singer's reference is built from the pieces of their recordings closest to the notes they're about to sing.
- **Smarter octaves.** The octave for each singer is chosen so your whole melody, low notes and high notes, sits inside that singer's comfortable range, not just its average note.
- **Pitch check.** Every phrase each singer renders is measured against your melody. If a singer drifts off, that phrase is re-rendered once with fresh randomness, and the better take is kept. `report.txt` lists each singer's pitch accuracy in cents.
- **Clean phrase boundaries.** Each phrase fits in one model pass, so there are no seams in the middle of notes. Long phrases are split at their quietest point and cross-faded.
- **A cleaned-up input.** The AI gets a version of your vocal with rumble removed and a steady level. Your lead itself is untouched.

## Setup (one time)

1. Double-click **`setup.command`**. The first time, you may need to right-click it and choose **Open**.
2. Wait 10–30 minutes. It needs about 6 GB of disk space.

Setup installs everything inside this folder: a private Python, the pinned Seed-VC version, the VocalSet voices, and the AI models.

## Making a choir

The usual way is the **OpenChoir plugin** on an instrument track (see the main README). It runs this engine for you and shows its progress.

Without a DAW, double-click **`OpenChoir AI.command`**. It asks for the same choices as the plugin's Choir screen: choir, octaves, harmonies, key, stereo/mono and quality.

Each render writes a folder containing:

- `layers/<layer>.wav`: one stereo file per layer (Lead, Octave Up, 3rd Above and so on), dry and in sync with your vocal
- `choir.wav`: all layers summed (command line only; the plugin lets you balance them instead)
- `stems/`: every singer on every layer, separately
- `report.txt`: which singers sang each layer, how closely each followed their line, and the voice credits

**Layers and singers.** Each layer is sung by singers whose comfortable range fits that line. Octave Down usually gets the men and Octave Up the women. 3rds and 5ths are diatonic: each note of your melody moves to the 3rd or 5th *in the chosen key*. The harmony keeps your vibrato and phrasing, because the engine shifts your own pitch curve rather than replacing it.

**Quality**

| Quality | Singers per layer | AI steps | Reference length |
|---|---|---|---|
| draft | 8 | 15 | 8 s |
| standard (default) | 12 | 25 | 12 s |
| high | 20 | 30 | 14 s |

**How long it takes** depends on your Mac, the quality setting, how many layers you pick, and how much of the song is sung. Every layer is a full render, so four layers take about four times as long as one. Render times haven't been measured on a Mac yet. The plugin and the terminal both show time remaining as they go. If a render is interrupted, running it again with the same settings reuses every singer already finished.

### Command line

```bash
.venv/bin/python choir_ai.py vocal.wav --model gospel --layers oct_up oct_down fifth_up --key "A minor"
# models: gospel, treble, shout
# layers: oct_up oct_down third_up third_down fifth_up fifth_down (the lead line is always included)
# other options: --mono, --quality draft|standard|high, --voices N, --seed N,
#                --batch N, --no-check, --engine compat, --device cpu
```

## Limits

- There are **no children's voices**: VocalSet only contains adults. The Treble Choir (the highest women's voices) is the closest option. You can add consented children's recordings to `voices/custom/`.
- Use a **clean, dry, monophonic vocal**. Reverb, doubles, or a backing track bleeding into the take will confuse the model.
- The output is 44.1 kHz.
