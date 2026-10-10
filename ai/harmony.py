"""Key detection and in-key harmony / octave layers, computed on a pitch curve.

A layer is described as a per-frame pitch *ratio* applied to the lead's pitch
curve. Octaves are a constant ratio. Thirds and fifths are diatonic: each note
of the lead is snapped to the chosen key's scale, moved 2 (third) or 4 (fifth)
scale steps, and the difference becomes that frame's ratio. Applying a ratio
(rather than replacing the pitch) keeps the singer's vibrato, scoops and
bends on the harmony line.
"""
from __future__ import annotations

import numpy as np
from scipy.ndimage import median_filter

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
SCALES = {
    "major": [0, 2, 4, 5, 7, 9, 11],
    "minor": [0, 2, 3, 5, 7, 8, 10],   # natural minor
}

# layer id -> (label, kind, amount)
LAYERS = {
    "main":     ("Lead",        "octave",  0),
    "oct_up":   ("Octave Up",   "octave", +12),
    "oct_down": ("Octave Down", "octave", -12),
    "third_up":   ("3rd Above", "degree", +2),
    "third_down": ("3rd Below", "degree", -2),
    "fifth_up":   ("5th Above", "degree", +4),
    "fifth_down": ("5th Below", "degree", -4),
}

# Krumhansl-Kessler key profiles
_KK_MAJOR = np.array([6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88])
_KK_MINOR = np.array([6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17])


def hz_to_midi(f):
    return 69.0 + 12.0 * np.log2(np.maximum(f, 1e-6) / 440.0)


def detect_key(f0: np.ndarray, weights: np.ndarray | None = None) -> tuple[int, str, float]:
    """Best key for a pitch curve -> (tonic 0-11, 'major'|'minor', confidence 0-1)."""
    v = f0 > 1
    if v.sum() < 20:
        return 0, "major", 0.0
    midi = hz_to_midi(f0[v])
    pc = np.round(midi).astype(int) % 12
    w = np.ones_like(midi) if weights is None else weights[v]
    hist = np.bincount(pc, weights=w, minlength=12)
    hist = hist / (hist.sum() + 1e-9)
    scores = []
    for tonic in range(12):
        for name, prof in (("major", _KK_MAJOR), ("minor", _KK_MINOR)):
            r = np.corrcoef(hist, np.roll(prof, tonic))[0, 1]
            scores.append((r, tonic, name))
    scores.sort(reverse=True)
    best, second = scores[0], scores[1]
    conf = float(np.clip((best[0] - second[0]) * 5 + 0.3, 0, 1)) if best[0] > 0 else 0.0
    return best[1], best[2], conf


def key_name(tonic: int, scale: str) -> str:
    return f"{NOTE_NAMES[tonic % 12]} {scale}"


def _scale_pcs(tonic: int, scale: str) -> np.ndarray:
    return (np.array(SCALES[scale]) + tonic) % 12


def degree_shift_semitones(midi_note: np.ndarray, tonic: int, scale: str, steps: int) -> np.ndarray:
    """For each (rounded) note, how many semitones to move to land `steps` scale
    degrees away. Notes outside the scale are first snapped to the nearest scale note."""
    pcs = _scale_pcs(tonic, scale)
    n = np.round(midi_note).astype(int)
    out = np.zeros_like(n)
    # build an ascending list of in-scale MIDI notes covering the range
    lo, hi = int(n.min()) - 24, int(n.max()) + 24
    ladder = np.array([m for m in range(lo, hi + 1) if m % 12 in pcs])
    for i, m in enumerate(n):
        k = int(np.argmin(np.abs(ladder - m)))       # snap to scale
        k2 = int(np.clip(k + steps, 0, len(ladder) - 1))
        out[i] = ladder[k2] - m
    return out


def layer_ratio(f0: np.ndarray, layer: str, tonic: int = 0, scale: str = "major",
                hold_frames: int = 7) -> np.ndarray:
    """Per-frame multiplier for the lead's pitch curve that produces this layer."""
    _, kind, amount = LAYERS[layer]
    ratio = np.ones_like(f0, dtype=np.float64)
    if kind == "octave":
        return ratio * 2.0 ** (amount / 12.0)
    v = f0 > 1
    if not v.any():
        return ratio
    midi = hz_to_midi(f0)
    # steady the note estimate so vibrato and scoops don't flip the harmony note
    steady = midi.copy()
    steady[v] = median_filter(midi[v], size=max(1, hold_frames), mode="nearest")
    semis = np.zeros_like(f0, dtype=np.float64)
    semis[v] = degree_shift_semitones(steady[v], tonic, scale, amount)
    # hold the interval across short unvoiced gaps so consonants don't jump
    last = 0.0
    for i in range(len(semis)):
        if v[i]:
            last = semis[i]
        else:
            semis[i] = last
    return 2.0 ** (semis / 12.0)


def layer_range(f0: np.ndarray, ratio: np.ndarray) -> tuple[float, float, float]:
    """10th/50th/90th percentile of the layer's target pitch (Hz)."""
    v = f0 > 1
    if v.sum() < 5:
        return 0.0, 0.0, 0.0
    t = f0[v] * ratio[v]
    p = np.percentile(t, [10, 50, 90])
    return float(p[0]), float(p[1]), float(p[2])
