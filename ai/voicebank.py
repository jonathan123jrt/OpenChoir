"""Voice bank: which singers exist, what range they sing in, and how to build the
reference clip that tells the AI what each singer sounds like.

Accuracy notes
--------------
* A zero-shot voice converter copies the timbre of its reference clip. A voice
  sounds different at the bottom and the top of its range, so each singer's
  reference is assembled from the pieces of their bank that sit closest to the
  pitch they are about to sing (after the octave choice).
* Every reference is exactly the same length. That keeps the AI's context
  window predictable and lets several singers be rendered in one batch.
"""
from __future__ import annotations

import json
import os

import numpy as np

import oc_audio as A

SR = 44100


class Voice:
    def __init__(self, vid, bank, sex, chunks, root, source, rng_lo, rng_hi, median):
        self.id, self.bank, self.sex = vid, bank, sex
        self.chunks = chunks            # list of dicts: path, f0, p10, p90, seconds
        self.root, self.source = root, source
        self.range = (rng_lo, rng_hi)   # comfortable range (10th-90th percentile), Hz
        self.median = median

    def __repr__(self):
        return f"Voice({self.id}, {self.sex}, {self.range[0]:.0f}-{self.range[1]:.0f} Hz)"


def load_bank(voice_dirs: list[str], custom_cache: str | None = None) -> list[Voice]:
    voices: list[Voice] = []
    for d in voice_dirs:
        man = os.path.join(d, "voices.json")
        if os.path.exists(man):
            data = json.load(open(man))
            if not isinstance(data, dict) or data.get("format") != 2:
                raise SystemExit(f"The voice bank in {d} is from an older version.\n"
                                 f"Run:  .venv/bin/python fetch_voices.py --force")
            for v in data["voices"]:
                chunks = [dict(c, path=os.path.join(d, c["file"])) for c in v["chunks"]]
                voices.append(Voice(v["id"], v["bank"], v["sex"], chunks, d, v.get("source", ""),
                                    v["range"][0], v["range"][1], v["median_f0"]))
        elif os.path.isdir(d):
            voices += _load_custom(d, custom_cache)
    return voices


def _load_custom(d: str, cache_dir: str | None) -> list[Voice]:
    """Any WAVs in voices/custom (optionally in female/ or male/ subfolders).
    Each file is cut into pieces and pitch-tagged once, then cached."""
    out = []
    for root, dirs, files in os.walk(d):
        dirs[:] = [x for x in dirs if not x.startswith(".")]  # skip caches
        for fn in sorted(files):
            if not fn.lower().endswith((".wav", ".aif", ".aiff", ".flac")):
                continue
            path = os.path.join(root, fn)
            sub = os.path.basename(root).lower()
            sex = "female" if sub.startswith("f") else "male" if sub.startswith("m") else "any"
            vid = "custom-" + os.path.splitext(fn)[0]
            cdir = os.path.join(cache_dir or os.path.join(d, ".cache"), vid)
            meta = os.path.join(cdir, "meta.json")
            stamp = os.path.getmtime(path)
            if os.path.exists(meta) and json.load(open(meta)).get("stamp") == stamp:
                m = json.load(open(meta))
            else:
                x, sr = A.read_audio_any(path)
                x = A.normalize_rms(A.resample(x, sr, SR), -20.0)
                os.makedirs(cdir, exist_ok=True)
                chunks, allf = [], []
                for i, c in enumerate(A.chunk_clip(x, SR)):
                    tr = A.f0_track(c, SR)
                    if tr.size < 5:
                        continue
                    allf.append(tr)
                    p = os.path.join(cdir, f"{i:02d}.wav")
                    A.write_wav16(p, A.normalize_rms(c, -20.0), SR)
                    p10, p50, p90 = np.percentile(tr, [10, 50, 90])
                    chunks.append({"path": p, "f0": float(p50), "p10": float(p10), "p90": float(p90),
                                   "seconds": len(c) / SR})
                if not chunks:
                    print(f"  skipping {fn}: no singing found")
                    continue
                allv = np.concatenate(allf)
                lo, med, hi = np.percentile(allv, [10, 50, 90])
                m = {"stamp": stamp, "chunks": chunks, "range": [float(lo), float(hi)], "median": float(med)}
                json.dump(m, open(meta, "w"))
            out.append(Voice(vid, "any", sex, m["chunks"], d, "your own recording",
                             m["range"][0], m["range"][1], m["median"]))
    return out


# --------------------------------------------------------------------------
def octave_shift(src: tuple[float, float, float], voice: Voice, mode: str = "auto") -> int:
    """Semitone shift (a multiple of 12) that best fits the melody into the singer's range.

    Scores each candidate by how far the melody's 10th/50th/90th percentile notes
    fall outside the singer's comfortable range (in octaves), then by how far the
    melody's middle sits from the singer's middle.
    """
    if mode == "off" or not src[1] or not voice.median:
        return 0
    lo, hi = np.log2(max(voice.range[0], 40)), np.log2(max(voice.range[1], 41))
    mid = np.log2(voice.median)

    def score(k):
        q = np.log2(np.array(src)) + k / 12
        outside = np.sum(np.maximum(lo - q, 0) + np.maximum(q - hi, 0))
        return (round(outside, 3), abs(q[1] - mid))

    return min((-24, -12, 0, 12, 24), key=score)


def build_reference(voice: Voice, target_f0: float, seconds: float, take: int = 0,
                    gap_s: float = 0.06) -> np.ndarray:
    """Assemble exactly `seconds` of this singer from the pieces nearest target_f0.

    take > 0 starts further down the ranked list, so a singer used twice gets a
    genuinely different reference (and therefore a slightly different performance).
    """
    if not voice.chunks:
        raise ValueError(f"{voice.id} has no audio")
    t = np.log2(max(target_f0, 40.0)) if target_f0 else np.log2(voice.median)

    def dist(c):
        # distance from the target to the piece's pitch span (0 if the span covers it)
        lo, hi = np.log2(max(c["p10"], 40)), np.log2(max(c["p90"], 41))
        return max(lo - t, 0) + max(t - hi, 0) + 0.25 * abs(np.log2(max(c["f0"], 40)) - t)

    ranked = sorted(voice.chunks, key=dist)
    if take:
        k = (take * max(1, len(ranked) // 3)) % len(ranked)
        ranked = ranked[k:] + ranked[:k]

    need = int(seconds * SR)
    gap = np.zeros(int(gap_s * SR), np.float32)
    parts, have, i = [], 0, 0
    while have < need:
        c = ranked[i % len(ranked)]
        x, sr = A.read_wav(c["path"])
        x = A.resample(x, sr, SR)
        parts += [x, gap]
        have += len(x) + len(gap)
        i += 1
        if i > 4 * len(ranked) + 50:
            break
    ref = np.concatenate(parts)[:need]
    ref = np.pad(ref, (0, need - len(ref)))
    return A.normalize_rms(ref, -20.0)


def choose_voices(bank: list[Voice], bank_name: str, sexes: tuple, n: int, rng) -> list[tuple[Voice, int]]:
    """Pick n (voice, take) pairs, alternating sexes for mixed choirs."""
    def ok(v, want):
        return v.bank in (want, "any") and (v.sex in sexes or v.sex == "any")

    pool = [v for v in bank if ok(v, bank_name)] or [v for v in bank if ok(v, "choir")]
    if not pool:
        raise SystemExit("No voices found. Run fetch_voices.py first (or put WAVs in voices/custom).")
    by_sex = {s: [v for v in pool if v.sex == s] for s in ("female", "male", "any")}
    for lst in by_sex.values():
        rng.shuffle(lst)
    order = []
    while any(by_sex.values()):
        for s in ("female", "male", "any"):
            if by_sex[s]:
                order.append(by_sex[s].pop())
    return [(order[i % len(order)], i // len(order)) for i in range(n)]
