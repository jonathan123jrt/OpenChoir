"""Synthesise a short sung phrase to test OpenChoir without a real recording.

Additive glottal source + vowel formant filters + vibrato + breath noise.
    python3 make_test_vocal.py out.wav
"""
import sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter

SR = 48000
rng = np.random.default_rng(7)

# (midi note, beats, vowel)
PHRASE = [
    (62, 1.0, "a"), (64, 0.5, "e"), (66, 0.5, "u"), (67, 2.0, "a"),
    (69, 1.0, "o"), (67, 0.5, "e"), (66, 0.5, "i"), (64, 2.5, "a"),
]
BPM = 96

FORMANTS = {  # (freq, bandwidth, gain)
    "a": [(800, 80, 1.0), (1150, 90, 0.5), (2900, 120, 0.25), (3900, 130, 0.1)],
    "e": [(400, 60, 1.0), (1700, 80, 0.35), (2600, 100, 0.3), (3200, 120, 0.1)],
    "i": [(300, 50, 1.0), (2200, 90, 0.25), (3000, 100, 0.25), (3700, 120, 0.1)],
    "o": [(450, 70, 1.0), (800, 80, 0.6), (2830, 100, 0.1), (3500, 130, 0.05)],
    "u": [(325, 50, 1.0), (700, 60, 0.3), (2530, 170, 0.05), (3500, 180, 0.03)],
}


def resonator(x, f, bw):
    r = np.exp(-np.pi * bw / SR)
    a = [1, -2 * r * np.cos(2 * np.pi * f / SR), r * r]
    b = [1 - r]
    return lfilter(b, a, x)


def note(midi, dur, vowel, prev_midi):
    n = int(dur * SR)
    t = np.arange(n) / SR
    f_target = 440 * 2 ** ((midi - 69) / 12)
    f_prev = 440 * 2 ** ((prev_midi - 69) / 12)
    # portamento into the note + delayed-onset vibrato
    glide = np.minimum(t / 0.06, 1.0)
    f0 = f_prev + (f_target - f_prev) * (1 - (1 - glide) ** 3)
    vib_depth = np.clip((t - 0.25) / 0.4, 0, 1) * 0.012
    f0 = f0 * (1 + vib_depth * np.sin(2 * np.pi * 5.4 * t)) * (1 + 0.002 * rng.standard_normal(n).cumsum() / np.sqrt(n))
    phase = 2 * np.pi * np.cumsum(f0) / SR

    src = np.zeros(n)
    for h in range(1, 40):
        amp = 1.0 / h ** 1.1
        mask = (h * f0) < SR * 0.45
        src += amp * np.sin(h * phase) * mask
    src += 0.03 * rng.standard_normal(n)  # breath

    out = np.zeros(n)
    for f, bw, g in FORMANTS[vowel]:
        out += g * resonator(src, f, bw)

    att, rel = int(0.04 * SR), int(0.08 * SR)
    env = np.ones(n)
    env[:att] = np.linspace(0, 1, att)
    env[-rel:] = np.linspace(1, 0, rel)
    return out * env


def main(path):
    beat = 60 / BPM
    parts, prev = [np.zeros(int(0.3 * SR))], PHRASE[0][0]
    for midi, beats, vowel in PHRASE:
        parts.append(note(midi, beats * beat, vowel, prev))
        prev = midi
    parts.append(np.zeros(int(0.3 * SR)))
    y = np.concatenate(parts)
    y = y / np.max(np.abs(y)) * 0.5
    wavfile.write(path, SR, (y * 32767).astype(np.int16))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "test_vocal.wav")
