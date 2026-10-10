"""OpenChoir render core - shared by the plugin (via openchoir_job.py) and the
command line (choir_ai.py).

Workflow (mirrors the plugin's three screens):
  1. a dry, monophonic lead vocal (mono or stereo with distinct L/R takes)
  2. a choir model, the layers to generate (lead line, octaves, 3rds, 5ths in key),
     the key/scale, stereo or mono output
  3. every layer is rendered as its own stereo WAV; level, mic colour and post-FX
     (lo-cut, brightness, reverb) are applied afterwards by the plugin.
"""
from __future__ import annotations

import hashlib
import json
import os
import sys
import time

import numpy as np

import harmony as H
import oc_audio as A
import voicebank as VB

HERE = os.path.dirname(os.path.abspath(__file__))
SEED_VC_DIR = os.path.join(HERE, "seed-vc")
VOICE_DIRS = [os.path.join(HERE, "voices", "vocalset"), os.path.join(HERE, "voices", "custom")]
CACHE_DIR = os.path.join(HERE, "voices", ".cache")
SR = 44100

# pitch check: re-render a phrase if a singer drifts off their line
QC_MEDIAN_CENTS = 45.0
QC_BAD_FRACTION = 0.20

MODELS = {
    "gospel": dict(label="Gospel Choir", bank="choir", sexes=("female", "male"), high_only=False,
                   offset=(0, 28), wander=2.5, level=2.5, width=0.9, grit=0.0, hp=80),
    "treble": dict(label="Treble Choir", bank="choir", sexes=("female",), high_only=True,
                   offset=(0, 22), wander=2.0, level=2.0, width=0.85, grit=0.0, hp=180),
    "shout":  dict(label="Shout Choir", bank="shout", sexes=("male",), high_only=False,
                   offset=(0, 70), wander=4.0, level=3.5, width=0.95, grit=0.25, hp=110),
}

QUALITY = {
    #            singers per layer, AI steps, reference seconds
    "draft":    dict(voices=8,  steps=15, ref=8.0),
    "standard": dict(voices=12, steps=25, ref=12.0),
    "high":     dict(voices=20, steps=30, ref=14.0),
}


class Progress:
    """Machine-readable progress for the plugin (lines starting with @@)."""

    def __init__(self, total: float):
        self.total = max(total, 1.0)
        self.done = 0.0
        self.t0 = time.time()

    def step(self, amount: float, msg: str):
        self.done += amount
        frac = min(self.done / self.total, 1.0)
        el = time.time() - self.t0
        eta = el / frac * (1 - frac) if frac > 0.02 else -1
        print(f"@@PROGRESS {frac:.4f} {eta:.0f} {msg}", flush=True)

    @staticmethod
    def say(msg: str):
        print(f"@@STATUS {msg}", flush=True)


# --------------------------------------------------------------------------
# Engines
# --------------------------------------------------------------------------
class FakeEngine:
    """Stand-in for testing the pipeline without the AI models."""
    sr = SR
    max_batch = 3
    device = "test"

    def __init__(self, *_, **__):
        print("@@STATUS test mode: no AI conversion", flush=True)
        self.calls = 0

    def prepare_reference(self, ref):
        return {"mel2_len": len(ref), "tone": 2500 + int(abs(hash(ref[:2000].tobytes())) % 6000)}

    def prepare_source(self, seg):
        return {"x": seg, "n": len(seg), "f0": A.f0_curve(seg, SR)}

    def src_f0(self, src):
        return src["f0"]

    def generate(self, src, refs, shifts, seeds, steps=25, cfg_rate=0.7):
        """Test stand-in: a filtered copy, crudely pitch-shifted by the mean ratio."""
        self.calls += 1
        out = []
        for r, s in zip(refs, shifts):
            ratio = float(np.mean(s)) if isinstance(s, np.ndarray) else 2 ** (s / 12)
            x = src["x"]
            if abs(ratio - 1) > 1e-3:
                idx = np.arange(len(x)) * ratio
                x = np.interp(idx % len(x), np.arange(len(x)), x)
            out.append(A.one_pole_lowpass(x, SR, r["tone"]).astype(np.float32))
        return out

    def pitch_error(self, out, src, shift):
        return 5.0, 0.0


class CompatEngine:
    """Seed-VC's own wrapper, one singer and one phrase at a time. Slower, used only
    if the fast engine cannot start on this machine."""
    max_batch = 1

    def __init__(self, device="auto"):
        sys.path.insert(0, SEED_VC_DIR)
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        cwd = os.getcwd()
        os.chdir(SEED_VC_DIR)
        try:
            import torch
            from oc_engine import install_dac_stub
            install_dac_stub()
            from seed_vc_wrapper import SeedVCWrapper
            self.w = SeedVCWrapper(device=None if device == "auto" else torch.device(device))
        finally:
            os.chdir(cwd)
        self.device = self.w.device
        self.sr = SR
        self.tmp = os.path.join(CACHE_DIR, "compat")
        os.makedirs(self.tmp, exist_ok=True)

    def prepare_reference(self, ref):
        p = os.path.join(self.tmp, hashlib.md5(ref.tobytes()).hexdigest()[:12] + ".wav")
        A.write_wav(p, ref, SR)
        return {"path": p}

    def prepare_source(self, seg):
        p = os.path.join(self.tmp, "src_" + hashlib.md5(seg.tobytes()).hexdigest()[:12] + ".wav")
        A.write_wav(p, seg, SR)
        return {"path": p, "n": len(seg), "f0": A.f0_curve(seg, SR)}

    def src_f0(self, src):
        return src["f0"]

    def generate(self, src, refs, shifts, seeds, steps=25, cfg_rate=0.7):
        import inspect
        import torch
        outs = []
        for r, s, seed in zip(refs, shifts, seeds):
            if isinstance(s, np.ndarray):
                raise SystemExit("3rd/5th harmonies need the fast engine, which could not start on this Mac. "
                                 "Turn the harmony layers off, or send the setup log for help.")
            torch.manual_seed(int(seed))
            g = self.w.convert_voice(src["path"], r["path"], diffusion_steps=steps, length_adjust=1.0,
                                     inference_cfg_rate=cfg_rate, f0_condition=True,
                                     auto_f0_adjust=False, pitch_shift=s, stream_output=False)
            if inspect.isgenerator(g):
                try:
                    while True:
                        next(g)
                except StopIteration as stop:
                    g = stop.value
            y = np.asarray(g, np.float32).reshape(-1)
            outs.append(np.pad(y, (0, max(0, src["n"] - len(y))))[: src["n"]])
        return outs

    def pitch_error(self, out, src, shift):
        return 0.0, 0.0  # not measured in compatibility mode


def start_engine(kind: str, device: str, batch: int | None = None):
    if kind == "test":
        return FakeEngine()
    print("@@STATUS Loading the AI models (first run downloads ~2.5 GB)", flush=True)
    if kind in ("auto", "fast"):
        try:
            from oc_engine import FastSeedVC
            return FastSeedVC(SEED_VC_DIR, device=device, cache_dir=CACHE_DIR, max_batch=batch)
        except SystemExit:
            raise
        except Exception as e:
            if kind == "fast":
                raise
            print(f"  fast engine could not start ({type(e).__name__}: {e});\n"
                  f"  using Seed-VC's standard path instead (slower).")
    return CompatEngine(device)



# --------------------------------------------------------------------------
# Mixing helpers
# --------------------------------------------------------------------------
def smooth_noise(n: int, sr: int, rate_hz: float, rng) -> np.ndarray:
    """Band-limited random wander in [-1, 1] at roughly rate_hz."""
    pts = max(4, int(n / sr * rate_hz) + 4)
    knots = rng.uniform(-1, 1, pts)
    t = np.linspace(0, pts - 1, n)
    i = np.floor(t).astype(int).clip(0, pts - 2)
    f = t - i
    f = f * f * (3 - 2 * f)  # smoothstep
    return knots[i] * (1 - f) + knots[i + 1] * f


def humanise(y: np.ndarray, offset_ms: float, wander_ms: float, rng) -> np.ndarray:
    """Delay by offset_ms, plus a slow wandering delay (-> small natural tuning/timing drift)."""
    n = len(y)
    d = (offset_ms + wander_ms * (0.5 + 0.5 * smooth_noise(n, SR, 0.35, rng))) * SR / 1000.0
    pos = np.arange(n) - d
    return np.interp(pos, np.arange(n), y, left=0.0, right=0.0).astype(np.float32)


def finish(x: np.ndarray, target_db: float = -18.0, ceiling: float = 0.95) -> np.ndarray:
    """Set the level to target_db RMS, then turn down (cleanly, no clipping or
    waveshaping) if that would put peaks above the ceiling."""
    voiced = x[np.abs(x).max(axis=1) > 1e-4] if x.ndim == 2 else x[np.abs(x) > 1e-4]
    if voiced.size == 0:
        return x.astype(np.float32)
    y = x * 10 ** ((target_db - A.rms_db(voiced)) / 20)
    peak = float(np.max(np.abs(y)))
    if peak > ceiling:
        y = y * (ceiling / peak)
    return y.astype(np.float32)


def condition_vocal(x: np.ndarray) -> np.ndarray:
    """What the AI hears: DC and rumble removed, steady level. (Your lead is not changed.)"""
    y = x - np.mean(x)
    y = A.one_pole_highpass(A.one_pole_highpass(y, SR, 60.0), SR, 60.0)
    return A.normalize_rms(y, -20.0)


def channel_plan(audio: np.ndarray, stereo: bool) -> tuple[list[np.ndarray], str]:
    """Decide which channels to process and how they come back (see the plugin's
    Stereo/Mono switch):
      mono input   + stereo -> one choir, spread across the stereo field
      mono input   + mono   -> one choir, mono
      stereo input + stereo -> each side processed separately, returned to its side
      stereo input + mono   -> left channel only, mono
    A stereo file whose channels are (nearly) identical counts as mono."""
    if audio.ndim == 1 or audio.shape[1] == 1:
        mono = audio if audio.ndim == 1 else audio[:, 0]
        return [mono], ("spread" if stereo else "mono")
    L, R = audio[:, 0], audio[:, 1]
    same = np.corrcoef(L, R)[0, 1] > 0.98 if np.std(L) > 1e-6 and np.std(R) > 1e-6 else True
    if same:
        return [0.5 * (L + R)], ("spread" if stereo else "mono")
    return ([L, R], "sides") if stereo else ([L], "mono")


def pick_singers(pool: list, target: tuple, n: int, model: dict, rng) -> list:
    """n (voice, take) pairs whose comfortable range best covers the target line."""
    if model["high_only"]:  # treble: the highest voices in the pool
        pool = sorted(pool, key=lambda v: -v.median)[: max(3, (len(pool) + 1) // 2)]
    if target[1] <= 0:
        fit = list(pool)
    else:
        q = np.log2(np.array(target))

        def outside(v):
            lo, hi = np.log2(max(v.range[0], 40)), np.log2(max(v.range[1], 41))
            return float(np.sum(np.maximum(lo - q, 0) + np.maximum(q - hi, 0)))

        ranked = sorted(pool, key=outside)
        fit = [v for v in ranked if outside(v) <= 0.35]
        if len(fit) < min(4, len(ranked)):
            fit = ranked[: max(3, len(ranked) // 2)]
    by_sex = {s: [v for v in fit if v.sex == s] for s in ("female", "male", "any")}
    for lst in by_sex.values():
        rng.shuffle(lst)
    order = []
    while any(by_sex.values()):
        for s in ("female", "male", "any"):
            if by_sex[s]:
                order.append(by_sex[s].pop())
    return [(order[i % len(order)], i // len(order)) for i in range(n)]


def mix_layer(stems: list[np.ndarray], model: dict, mode: str, rng) -> np.ndarray:
    """Humanise and pan one layer's singers. No reverb: the plugin adds the room."""
    n = max(len(s) for s in stems)
    k = len(stems)
    offsets = rng.uniform(*model["offset"], size=k)
    offsets -= offsets.min()
    pans = np.linspace(-1, 1, k) if k > 1 else np.zeros(1)
    rng.shuffle(pans)
    pans *= model["width"] if mode == "spread" else 0.0
    out = np.zeros((n, 2), np.float32)
    for s, off, pan in zip(stems, offsets, pans):
        s = np.pad(s, (0, n - len(s)))
        s = A.normalize_rms(s, -20.0) if np.any(np.abs(s) > 1e-4) else s
        s = A.one_pole_highpass(s, SR, model["hp"])
        s = humanise(s, off, model["wander"], rng)
        g = 10 ** (rng.uniform(-model["level"], model["level"]) / 20)
        ang = (pan + 1) * np.pi / 4
        out[:, 0] += s * g * np.cos(ang)
        out[:, 1] += s * g * np.sin(ang)
    out /= np.sqrt(k)
    if model["grit"] > 0:
        peak = float(np.max(np.abs(out))) + 1e-9
        drive = 1 + 6 * model["grit"]
        sat = np.tanh(out / peak * drive) / np.tanh(drive) * peak
        out = (1 - model["grit"]) * out + model["grit"] * sat
    return out


def render_line(engine, sources, phrases, src_f0s, jobs, refs, steps, n_total, ratio_fn,
                prog: Progress, label: str, check=True):
    """Render every phrase for a batch of singers on one layer."""
    stems = [np.zeros(n_total, np.float32) for _ in jobs]
    report = [{"cents": [], "retried": 0} for _ in jobs]
    for pi, ((a, b), src) in enumerate(zip(phrases, sources)):
        pitch = ratio_fn(src_f0s[pi])
        pitches = [pitch] * len(jobs)
        seeds = [j["seed"] * 1000 + pi for j in jobs]
        outs = engine.generate(src, refs, pitches, seeds, steps=steps)
        for k, (y, j) in enumerate(zip(outs, jobs)):
            if not check:
                continue
            med, bad = engine.pitch_error(y, src, pitch)
            if med > QC_MEDIAN_CENTS or bad > QC_BAD_FRACTION:
                y2 = engine.generate(src, [refs[k]], [pitch], [seeds[k] + 777], steps=steps)[0]
                med2, bad2 = engine.pitch_error(y2, src, pitch)
                report[k]["retried"] += 1
                if (bad2, med2) < (bad, med):
                    outs[k], med, bad = y2, med2, bad2
            report[k]["cents"].append(med)
        fade_in = (phrases[pi - 1][1] - a) if pi and phrases[pi - 1][1] > a else int(0.01 * SR)
        nxt = phrases[pi + 1][0] if pi + 1 < len(phrases) else n_total
        fade_out = (b - nxt) if b > nxt else int(0.01 * SR)
        for k, y in enumerate(outs):
            A.place(stems[k], y, a, fade_in, fade_out)
        prog.step(len(jobs) * (b - a) / SR, f"{label}: phrase {pi + 1}/{len(phrases)}")
    return stems, report


def parse_key(key) -> tuple[int, str] | None:
    """'auto' -> None; 'A minor', 'Bb major', {'tonic': 9, 'scale': 'minor'} -> (tonic, scale)."""
    if key in (None, "", "auto"):
        return None
    if isinstance(key, dict):
        return int(key["tonic"]) % 12, key.get("scale", "major")
    parts = str(key).strip().split()
    note = parts[0][0].upper() + parts[0][1:]
    flats = {"Db": "C#", "Eb": "D#", "Gb": "F#", "Ab": "G#", "Bb": "A#"}
    note = flats.get(note, note)
    scale = parts[1].lower() if len(parts) > 1 else "major"
    if note not in H.NOTE_NAMES or scale not in H.SCALES:
        raise SystemExit(f"Unknown key: {key}")
    return H.NOTE_NAMES.index(note), scale


def run(spec: dict) -> dict:
    """Render a job. spec keys: input, out_dir, model, layers, key, stereo, quality,
    seed, engine, device, batch, check."""
    t_start = time.time()
    model_id = spec.get("model", "gospel")
    model = MODELS[model_id]
    q = QUALITY[spec.get("quality", "standard")]
    layers = [l for l in spec.get("layers", ["main"]) if l in H.LAYERS] or ["main"]
    if "main" not in layers:
        layers = ["main"] + layers
    rng = np.random.default_rng(int(spec.get("seed", 1)))
    out_dir = spec["out_dir"]
    os.makedirs(os.path.join(out_dir, "layers"), exist_ok=True)
    stems_root = os.path.join(out_dir, "stems")

    # ---- vocal ---------------------------------------------------------------
    Progress.say("Reading the vocal")
    audio, sr = A.read_audio_multi(spec["input"])
    audio = A.resample(audio, sr, SR)
    chans, mode = channel_plan(audio, bool(spec.get("stereo", True)))
    n_total = len(audio)

    ai_chans = [condition_vocal(c) for c in chans]
    curve = np.concatenate([A.f0_curve(c, SR) for c in ai_chans])
    key = parse_key(spec.get("key"))
    if key is None:
        tonic, scale, conf = H.detect_key(curve)
        print(f"@@KEY {H.NOTE_NAMES[tonic]} {scale} {conf:.2f}", flush=True)
    else:
        tonic, scale = key
    phr = [A.find_phrases(c, SR, max_len_s=29.0 - q["ref"] - 1.0) for c in ai_chans]
    if not any(phr):
        raise SystemExit("No singing found in that file.")
    sung = sum(b - a for p in phr for a, b in p) / SR
    Progress.say(f"{sung:.0f}s of singing, {len(layers)} layer(s), key {H.key_name(tonic, scale)}")

    # ---- plan ----------------------------------------------------------------
    bank = VB.load_bank(VOICE_DIRS, os.path.join(CACHE_DIR, "custom"))
    pool = [v for v in bank if v.bank in (model["bank"], "any") and (v.sex in model["sexes"] or v.sex == "any")]
    if not pool:
        pool = [v for v in bank if v.bank in ("choir", "any") and (v.sex in model["sexes"] or v.sex == "any")]
    if not pool:
        raise SystemExit("No voices found. Run setup again (fetch_voices.py).")
    n_voices = int(spec.get("voices") or q["voices"])
    work = len(layers) * n_voices * sung
    prog = Progress(work)

    engine = start_engine(spec.get("engine", "auto"), spec.get("device", "auto"), spec.get("batch"))
    bsz = max(1, getattr(engine, "max_batch", 1))
    check = bool(spec.get("check", True))
    src_hash = hashlib.md5(audio.tobytes()).hexdigest()[:12]

    Progress.say("Analysing your vocal")
    sources = [[engine.prepare_source(c[a:b]) for a, b in p] for c, p in zip(ai_chans, phr)]
    src_f0s = [[engine.src_f0(s) for s in ss] for ss in sources]

    result_layers, report_lines = [], []
    for layer in layers:
        label = H.LAYERS[layer][0]
        ratio_fn = (lambda f0, L=layer: H.layer_ratio(f0, L, tonic, scale)) if H.LAYERS[layer][1] == "degree" \
            else (lambda f0, L=layer: H.LAYERS[L][2])
        target = H.layer_range(curve, H.layer_ratio(curve, layer, tonic, scale))
        chan_mixes = []
        for ci, (c, p) in enumerate(zip(ai_chans, phr)):
            picks = pick_singers(pool, target, n_voices, model, rng)
            sdir = os.path.join(stems_root, layer + (f"_ch{ci}" if len(ai_chans) > 1 else ""))
            os.makedirs(sdir, exist_ok=True)
            jobs = []
            for i, (v, take) in enumerate(picks):
                tag = f"{i + 1:02d}_{v.id}" + (f"_take{take + 1}" if take else "")
                k = {"src": src_hash, "ch": ci, "layer": layer, "key": [tonic, scale], "voice": v.id, "take": take,
                     "steps": q["steps"], "ref": q["ref"], "seed": int(spec.get("seed", 1)), "i": i, "v": 3}
                jobs.append({"voice": v, "take": take, "tag": tag, "key": k,
                             "seed": int(spec.get("seed", 1)) * 100 + i + 37 * ci,
                             "path": os.path.join(sdir, tag + ".wav"), "meta": os.path.join(sdir, "." + tag + ".json")})
            todo = [j for j in jobs if not (os.path.exists(j["path"]) and os.path.exists(j["meta"])
                                            and json.load(open(j["meta"])) == j["key"])]
            ch_sung = sum(b - a for a, b in p) / SR
            prog.step(ch_sung * (len(jobs) - len(todo)), f"{label}: cached singers")
            for bi in range(0, len(todo), bsz):
                batch = todo[bi:bi + bsz]
                refs = [engine.prepare_reference(VB.build_reference(j["voice"], target[1], q["ref"], j["take"]))
                        for j in batch]
                try:
                    stems, rep = render_line(engine, sources[ci], p, src_f0s[ci], batch, refs, q["steps"],
                                             len(c), ratio_fn, prog, label, check)
                except RuntimeError as e:
                    if "out of memory" in str(e).lower():
                        raise SystemExit("Ran out of memory. Choose Draft quality or close other apps, then try again.")
                    raise
                for j, s, r in zip(batch, stems, rep):
                    A.write_wav(j["path"], s, SR)
                    json.dump(j["key"], open(j["meta"], "w"))
                    cents = f"{np.median(r['cents']):.0f} cents" if r["cents"] else "-"
                    report_lines.append(f"{label:12s} {j['tag']:22s} pitch error {cents:10s} "
                                        f"re-rendered {r['retried']}   {j['voice'].source}")
            stems = [A.read_wav(j["path"])[0] for j in jobs]
            chan_mixes.append(mix_layer(stems, model, "spread" if mode == "spread" else "mono", rng))

        if mode == "sides":
            Lm, Rm = chan_mixes[0].mean(axis=1), chan_mixes[1].mean(axis=1)
            m = max(len(Lm), len(Rm))
            out = np.stack([np.pad(Lm, (0, m - len(Lm))), np.pad(Rm, (0, m - len(Rm)))], axis=1)
        elif mode == "mono":
            mono = chan_mixes[0].mean(axis=1)
            out = np.stack([mono, mono], axis=1)
        else:
            out = chan_mixes[0]
        out = finish(out[:n_total], -20.0)
        path = os.path.join(out_dir, "layers", f"{layer}.wav")
        A.write_wav(path, out, SR)
        result_layers.append({"id": layer, "label": label, "file": path})
        print(f"@@LAYER {layer} {path}", flush=True)

    result = {"model": model_id, "model_label": model["label"], "key": H.key_name(tonic, scale),
              "stereo_mode": mode, "quality": spec.get("quality", "standard"), "voices_per_layer": n_voices,
              "layers": result_layers, "seconds": round(time.time() - t_start, 1)}
    json.dump(result, open(os.path.join(out_dir, "result.json"), "w"), indent=1)
    write_report(out_dir, result, report_lines, pool)
    print(f"@@DONE {os.path.join(out_dir, 'result.json')}", flush=True)
    return result


def write_report(out_dir, result, lines, pool):
    txt = [f"OpenChoir render: {result['model_label']}, key {result['key']}, "
           f"{result['voices_per_layer']} singers per layer, quality {result['quality']}, "
           f"{result['seconds'] / 60:.1f} min", "",
           "Pitch error = how far a singer sits from their line (100 cents = a semitone).", ""] + lines + [
           "", "Voice conversion: Seed-VC (GPL-3.0) https://github.com/Plachtaa/seed-vc", ""]
    for root in sorted({v.root for v in pool}):
        c = os.path.join(root, "CREDITS.txt")
        if os.path.exists(c):
            txt.append(open(c).read())
    open(os.path.join(out_dir, "report.txt"), "w").write("\n".join(txt) + "\n")


def run_job_file(path: str) -> int:
    spec = json.load(open(path))
    try:
        run(spec)
        return 0
    except SystemExit as e:
        print(f"@@ERROR {e}", flush=True)
        return 2
    except Exception as e:  # report anything else to the plugin, with detail in the log
        import traceback
        traceback.print_exc()
        print(f"@@ERROR {type(e).__name__}: {e}", flush=True)
        return 3
