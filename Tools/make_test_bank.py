"""Write a small synthetic voice bank (same format as fetch_voices.py) so the
plugin and AI pipeline can be tested without downloading VocalSet.

    python3 Tools/make_test_bank.py ai/voices/vocalset
"""
import json
import os
import sys

import numpy as np
from scipy.io import wavfile

SR = 44100


def tone(f0, seconds, rng):
    t = np.arange(int(seconds * SR)) / SR
    f = f0 * (1 + 0.01 * np.sin(2 * np.pi * 5.2 * t))
    ph = 2 * np.pi * np.cumsum(f) / SR
    x = sum(np.sin(h * ph) / h for h in range(1, 12)) * 0.15
    return (x + 0.002 * rng.standard_normal(len(t))).astype(np.float32)


def main(out):
    rng = np.random.default_rng(0)
    singers = [("f", i, 260 + 30 * i) for i in range(1, 7)] + [("m", i, 110 + 15 * i) for i in range(1, 6)]
    voices = []
    for sex, num, base in singers:
        for bank in ("choir", "shout"):
            d = os.path.join(out, bank, f"{sex}{num}")
            os.makedirs(d, exist_ok=True)
            chunks, allf = [], []
            for k, semis in enumerate([-5, -2, 0, 3, 7, 10]):
                f0 = base * 2 ** (semis / 12)
                x = tone(f0, 3.0, rng)
                p = os.path.join(d, f"{k:02d}.wav")
                wavfile.write(p, SR, (x * 32767).astype(np.int16))
                chunks.append({"file": os.path.relpath(p, out), "f0": round(f0, 1), "p10": round(f0 * 0.99, 1),
                               "p90": round(f0 * 1.01, 1), "seconds": 3.0})
                allf.append(f0)
            voices.append({"id": f"{sex}{num}", "bank": bank, "sex": "female" if sex == "f" else "male",
                           "median_f0": float(np.median(allf)), "range": [min(allf), max(allf)],
                           "seconds": 18.0, "chunks": chunks, "source_clips": [], "source": "synthetic test voice"})
    json.dump({"format": 2, "sample_rate": SR, "voices": voices}, open(os.path.join(out, "voices.json"), "w"), indent=1)
    open(os.path.join(out, "CREDITS.txt"), "w").write("Synthetic test voices (not real singers).\n")
    print(f"wrote {len(voices)} test voices to {out}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "ai/voices/vocalset")
