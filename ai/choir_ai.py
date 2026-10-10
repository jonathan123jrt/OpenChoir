"""OpenChoir AI from the command line - the same render the plugin does.

    python choir_ai.py vocal.wav --model gospel --layers oct_up oct_down fifth_up
    python choir_ai.py vocal.wav --model shout --key "E minor" --mono --quality draft

Models : gospel (mixed), treble (high women's voices), shout (men, gang vocals)
Layers : main (always), oct_up, oct_down, third_up, third_down, fifth_up, fifth_down
Output : "<vocal> - OpenChoir <model>/" with layers/<layer>.wav, choir.wav (all
         layers summed), report.txt (singers, pitch accuracy, credits)
"""
from __future__ import annotations

import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import harmony as H  # noqa: E402
import oc_audio as A  # noqa: E402
import render_core as C  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("vocal")
    ap.add_argument("--model", choices=C.MODELS, default="gospel")
    ap.add_argument("--layers", nargs="*", default=["oct_up", "oct_down"], choices=list(H.LAYERS))
    ap.add_argument("--key", default="auto", help='"auto" or e.g. "A minor", "Eb major"')
    ap.add_argument("--mono", action="store_true", help="mono output (stereo is the default)")
    ap.add_argument("--quality", choices=C.QUALITY, default="standard")
    ap.add_argument("--voices", type=int, help="singers per layer (default from quality)")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--device", choices=("auto", "mps", "cuda", "cpu"), default="auto")
    ap.add_argument("--batch", type=int)
    ap.add_argument("--engine", choices=("auto", "fast", "compat"), default="auto")
    ap.add_argument("--no-check", action="store_true", help="skip the pitch check")
    ap.add_argument("--out")
    ap.add_argument("--test-without-ai", action="store_true", help=argparse.SUPPRESS)
    a = ap.parse_args()

    vocal = os.path.abspath(a.vocal)
    out = a.out or os.path.join(os.path.dirname(vocal),
                                f"{os.path.splitext(os.path.basename(vocal))[0]} - OpenChoir {a.model}")
    spec = {"input": vocal, "out_dir": out, "model": a.model, "layers": ["main"] + a.layers,
            "key": a.key, "stereo": not a.mono, "quality": a.quality, "voices": a.voices, "seed": a.seed,
            "device": a.device, "batch": a.batch, "check": not a.no_check,
            "engine": "test" if a.test_without_ai else a.engine}
    res = C.run(spec)

    # convenience: every layer summed (the plugin lets you balance them instead)
    mixes = [A.read_audio_multi(l["file"])[0] for l in res["layers"]]
    n = max(len(m) for m in mixes)
    total = sum(np.pad(m, ((0, n - len(m)), (0, 0))) * (1.0 if i == 0 else 0.7) for i, m in enumerate(mixes))
    A.write_wav(os.path.join(out, "choir.wav"), C.finish(total, -18.0), C.SR)
    print(f"\nDone in {res['seconds'] / 60:.1f} min, key {res['key']} -> {out}")


if __name__ == "__main__":
    main()
