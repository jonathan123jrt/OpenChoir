"""OpenChoir's fast Seed-VC engine.

Uses Seed-VC's singing model (44.1 kHz, pitch-conditioned) and its own modules,
with the same maths as Seed-VC's inference, but organised for rendering many
singers from one vocal:

* Loads only what singing needs. The stock wrapper also loads the 22 kHz speech
  model and its vocoder, which a choir render never uses.
* Analyses your vocal once (Whisper content features, RMVPE pitch, mel length)
  and reuses that for every singer. The stock wrapper redoes it per singer.
* Analyses each singer's reference once and caches it on disk between runs.
* Renders several singers in one batch on the GPU. This is possible because
  every reference is the same length.
* Only sung phrases are rendered (the caller passes phrases, not the whole song),
  and each phrase fits in a single model pass with no mid-note chunk seams.
* Half precision on Apple Silicon / NVIDIA, falling back to full precision
  automatically if the GPU refuses. Full precision on CPU.

Everything that runs the model is mirrored from seed_vc_wrapper.py and
modules/flow_matching.py (Seed-VC commit 51383ef).
"""
from __future__ import annotations

import hashlib
import os
import sys
import time

import numpy as np

MODEL = ("Plachta/Seed-VC",
         "DiT_seed_v2_uvit_whisper_base_f0_44k_bigvgan_pruned_ft_ema.pth",
         "config_dit_mel_seed_uvit_whisper_base_f0_44k.yml")
VOCODER = "nvidia/bigvgan_v2_44khz_128band_512x"
ENGINE_VERSION = "fast-2"


def install_dac_stub() -> None:
    """Seed-VC's length_regulator.py imports `dac` (descript-audio-codec) at module
    level, but the singing model never builds that quantizer (vector_quantize: false).
    descript-audio-codec drags in a large, fragile dependency tree, so when it isn't
    installed we register a tiny stand-in that fails loudly if it is ever used."""
    import importlib.util
    import types
    if importlib.util.find_spec("dac") is not None:
        return

    class VectorQuantize:  # noqa: D401 - placeholder
        def __init__(self, *a, **k):
            raise RuntimeError("descript-audio-codec is required for this Seed-VC model: "
                               "pip install descript-audio-codec")

    dac = types.ModuleType("dac")
    nn = types.ModuleType("dac.nn")
    q = types.ModuleType("dac.nn.quantize")
    q.VectorQuantize = VectorQuantize
    dac.nn, nn.quantize = nn, q
    sys.modules.update({"dac": dac, "dac.nn": nn, "dac.nn.quantize": q})


class FastSeedVC:
    def __init__(self, seed_vc_dir: str, device: str = "auto", cache_dir: str | None = None,
                 max_batch: int | None = None, verbose: bool = True):
        if not os.path.isdir(seed_vc_dir):
            raise SystemExit("Seed-VC is not installed. Run setup.command first.")
        os.environ.setdefault("PYTORCH_ENABLE_MPS_FALLBACK", "1")
        if seed_vc_dir not in sys.path:
            sys.path.insert(0, seed_vc_dir)
        install_dac_stub()
        self.cache_dir = cache_dir
        self.verbose = verbose
        cwd = os.getcwd()
        os.chdir(seed_vc_dir)  # Seed-VC downloads its checkpoints to ./checkpoints
        try:
            self._load(device, max_batch)
        finally:
            os.chdir(cwd)

    # ------------------------------------------------------------------
    def _load(self, device, max_batch):
        import torch
        import torchaudio
        import yaml
        from hf_utils import load_custom_model_from_hf
        from modules.audio import mel_spectrogram
        from modules.bigvgan import bigvgan
        from modules.campplus.DTDNN import CAMPPlus
        from modules.commons import build_model, load_checkpoint, recursive_munch
        from modules.rmvpe import RMVPE
        from transformers import AutoFeatureExtractor, WhisperModel

        self.torch, self.torchaudio = torch, torchaudio
        self._RMVPE = RMVPE
        if device == "auto":
            if torch.cuda.is_available():
                device = "cuda"
            elif getattr(torch.backends, "mps", None) and torch.backends.mps.is_available():
                device = "mps"
            else:
                device = "cpu"
        self.device = torch.device(device)
        self.cpu = torch.device("cpu")
        # Each model runs on the GPU by default. If the Apple GPU (MPS) can't run one of
        # them for some audio (e.g. "Output channels > 65536 not supported at the MPS
        # device" on long phrases), only that model moves to the CPU; the rest stay put.
        # Plain audio maths (resampling, spectrograms) always runs on the CPU.
        self.dev = {k: self.device for k in ("dit", "whisper", "campplus", "vocoder", "rmvpe")}
        self.use_amp = self.device.type in ("cuda", "mps")

        if max_batch is None:
            max_batch = 1
            if self.device.type in ("cuda", "mps"):
                ram_gb = _ram_gb()
                max_batch = 4 if ram_gb >= 24 else 3 if ram_gb >= 16 else 2
        self.max_batch = max_batch

        t0 = time.time()
        ckpt, cfg_path = load_custom_model_from_hf(*MODEL)
        config = yaml.safe_load(open(cfg_path, "r"))
        mp = recursive_munch(config["model_params"])
        model = build_model(mp, stage="DiT")
        model, _, _, _ = load_checkpoint(model, None, ckpt, load_only_params=True,
                                         ignore_modules=[], is_distributed=False)
        for k in model:
            model[k].eval()
            model[k].to(self.device)
        # the CFG pass doubles the batch
        model.cfm.estimator.setup_caches(max_batch_size=2 * self.max_batch, max_seq_length=8192)
        self.model = model

        sp = config["preprocess_params"]["spect_params"]
        self.sr = config["preprocess_params"]["sr"]
        self.hop = sp["hop_length"]
        mel_args = {"n_fft": sp["n_fft"], "win_size": sp["win_length"], "hop_size": sp["hop_length"],
                    "num_mels": sp["n_mels"], "sampling_rate": self.sr, "fmin": 0, "fmax": None,
                    "center": False}
        self.to_mel = lambda x: mel_spectrogram(x, **mel_args)
        self.max_context = self.sr // self.hop * 30  # frames, prompt + source (as in Seed-VC)

        whisper_name = (mp.speech_tokenizer.whisper_name
                        if hasattr(mp.speech_tokenizer, "whisper_name") else "openai/whisper-small")
        wdtype = torch.float16 if self.use_amp else torch.float32
        self.whisper = WhisperModel.from_pretrained(whisper_name, torch_dtype=wdtype).to(self.device)
        del self.whisper.decoder
        self.whisper_fe = AutoFeatureExtractor.from_pretrained(whisper_name)

        cp = load_custom_model_from_hf("funasr/campplus", "campplus_cn_common.bin", config_filename=None)
        self.campplus = CAMPPlus(feat_dim=80, embedding_size=192)
        self.campplus.load_state_dict(torch.load(cp, map_location="cpu"))
        self.campplus.eval().to(self.device)

        self.vocoder = bigvgan.BigVGAN.from_pretrained(VOCODER, use_cuda_kernel=False)
        self.vocoder.remove_weight_norm()
        self.vocoder = self.vocoder.eval().to(self.device)

        self.rmvpe_path = load_custom_model_from_hf("lj1995/VoiceConversionWebUI", "rmvpe.pt", None)
        self.rmvpe = RMVPE(self.rmvpe_path, is_half=False, device=self.device)

        self.model_tag = hashlib.md5((MODEL[1] + ENGINE_VERSION).encode()).hexdigest()[:8]
        if self.verbose:
            print(f"  models ready in {time.time() - t0:.0f}s on {self.device} "
                  f"(batch {self.max_batch}, {'half' if self.use_amp else 'full'} precision)", flush=True)

    # ------------------------------------------------------------------
    # running each model on the GPU, or on the CPU if the GPU refuses
    # ------------------------------------------------------------------
    LABELS = {"dit": "voice model", "whisper": "lyrics analyser", "campplus": "voice-colour analyser",
              "vocoder": "audio generator", "rmvpe": "pitch tracker"}

    @staticmethod
    def _gpu_refused(e: BaseException) -> bool:
        m = str(e)
        if "out of memory" in m.lower():
            return False
        return isinstance(e, (NotImplementedError, RuntimeError, TypeError, ValueError)) and \
            ("MPS" in m or "mps" in m or "Metal" in m)

    def _move_to_cpu(self, comp: str):
        torch, cpu = self.torch, self.cpu
        print(f"@@STATUS The Apple GPU can't run the {self.LABELS[comp]} for this audio, "
              f"so that part runs on the CPU (a little slower)", flush=True)
        if comp == "dit":
            for k in self.model:
                self.model[k].to(cpu)
                # tables the transformer keeps outside PyTorch's buffer tracking
                for m in self.model[k].modules():
                    for name in ("freqs_cis", "causal_mask", "mask_cache"):
                        t = getattr(m, name, None)
                        if torch.is_tensor(t):
                            setattr(m, name, t.to(cpu))
            self.use_amp = False
        elif comp == "whisper":
            self.whisper = self.whisper.to(cpu, dtype=torch.float32)
        elif comp == "campplus":
            self.campplus = self.campplus.to(cpu)
        elif comp == "vocoder":
            self.vocoder = self.vocoder.to(cpu)
        elif comp == "rmvpe":
            self.rmvpe = self._RMVPE(self.rmvpe_path, is_half=False, device=cpu)
        self.dev[comp] = cpu

    def _run(self, comp: str, fn):
        """fn(device) on the component's device; on a GPU refusal, move it to the CPU and retry."""
        try:
            return fn(self.dev[comp])
        except Exception as e:
            if self.dev[comp].type == "cpu" or not self._gpu_refused(e):
                raise
            self._move_to_cpu(comp)
            return fn(self.dev[comp])

    # ------------------------------------------------------------------
    # analysis (mirrors seed_vc_wrapper.SeedVCWrapper.convert_voice)
    # ------------------------------------------------------------------
    def _tensor(self, x_np):
        """Audio as a (1, n) float tensor on the CPU."""
        return self.torch.tensor(np.asarray(x_np, dtype=np.float32))[None].float()

    def _resample16(self, x):
        return self.torchaudio.functional.resample(x, self.sr, 16000)

    def _whisper(self, wav16):
        torch = self.torch
        if wav16.size(-1) > 16000 * 30:
            raise ValueError("segment longer than 30 s")
        inputs = self.whisper_fe([wav16.squeeze(0).cpu().numpy()], return_tensors="pt",
                                 return_attention_mask=True, sampling_rate=16000)
        feats = self.whisper._mask_input_features(inputs.input_features, attention_mask=inputs.attention_mask)

        def fn(dev):
            out = self.whisper.encoder(feats.to(dev, dtype=self.whisper.encoder.dtype), head_mask=None,
                                       output_attentions=False, output_hidden_states=False, return_dict=True)
            return out.last_hidden_state.to(torch.float32)

        f = self._run("whisper", fn)
        return f[:, : wav16.size(-1) // 320 + 1]

    def _f0(self, wav16):
        """Pitch curve as a (1, frames) CPU tensor."""
        f = self._run("rmvpe", lambda dev: self.rmvpe.infer_from_audio(wav16[0].cpu(), thred=0.03))
        return self.torch.from_numpy(np.asarray(f)).float()[None]

    def prepare_reference(self, ref_np: np.ndarray) -> dict:
        """Singer reference at self.sr -> prompt features (kept on the CPU). Cached on disk by content."""
        torch = self.torch
        key = hashlib.md5(np.asarray(ref_np, np.float32).tobytes()).hexdigest()[:16]
        path = os.path.join(self.cache_dir, f"ref_{self.model_tag}_{key}.pt") if self.cache_dir else None
        if path and os.path.exists(path):
            try:
                return torch.load(path, map_location="cpu", weights_only=True)
            except Exception:
                pass
        with torch.inference_mode():
            ref = self._tensor(ref_np)
            ref16 = self._resample16(ref)
            S_ori = self._whisper(ref16)
            mel2 = self.to_mel(ref)
            feat2 = self.torchaudio.compliance.kaldi.fbank(ref16, num_mel_bins=80, dither=0,
                                                           sample_frequency=16000)
            feat2 = feat2 - feat2.mean(dim=0, keepdim=True)
            style2 = self._run("campplus", lambda dev: self.campplus(feat2.unsqueeze(0).to(dev))).cpu()
            F0_ori = self._f0(ref16)

            def regulate(dev):
                ylen = torch.LongTensor([mel2.size(2)]).to(dev)
                return self.model.length_regulator(S_ori.to(dev), ylens=ylen, n_quantizers=3,
                                                   f0=F0_ori.to(dev))[0]

            prompt = self._run("dit", regulate).cpu()
        d = {"mel2": mel2.cpu(), "style": style2, "prompt": prompt}
        if path:
            os.makedirs(self.cache_dir, exist_ok=True)
            torch.save({k: v.detach().cpu() for k, v in d.items()}, path)
        return d

    def prepare_source(self, seg_np: np.ndarray) -> dict:
        """One sung phrase at self.sr -> content features + pitch (on the CPU). Done once per phrase."""
        torch = self.torch
        with torch.inference_mode():
            src = self._tensor(seg_np)
            src16 = self._resample16(src)
            S_alt = self._whisper(src16).cpu()
            T = self.to_mel(src).size(2)
            F0_alt = self._f0(src16)
        return {"S": S_alt, "T": T, "F0": F0_alt, "n": len(seg_np)}

    def src_f0(self, src: dict) -> np.ndarray:
        """The phrase's pitch curve (Hz per RMVPE frame, 0 = unvoiced)."""
        return src["F0"][0].detach().float().cpu().numpy()

    def _cond(self, src: dict, pitch, dev):
        """Content+pitch condition for one singer.

        pitch: int semitones (constant shift) or a numpy array with one ratio per
        pitch frame (harmony layers). auto_f0_adjust is off, as in Seed-VC's SVC mode.
        """
        torch = self.torch
        F0 = src["F0"].to(dev)
        shifted = torch.exp(torch.log(F0 + 1e-5))
        voiced = F0 > 1
        if isinstance(pitch, np.ndarray):
            r = np.ones(F0.size(-1), np.float32)
            m = min(len(r), len(pitch))
            r[:m] = pitch[:m]
            if 0 < m < len(r):
                r[m:] = pitch[m - 1]
            rt = torch.from_numpy(r).to(dev)[None]
            shifted = torch.where(voiced, shifted * rt, shifted)
        elif pitch:
            shifted[voiced] = shifted[voiced] * (2 ** (pitch / 12))
        ylen = torch.LongTensor([src["T"]]).to(dev)
        return self.model.length_regulator(src["S"].to(dev), ylens=ylen, n_quantizers=3, f0=shifted)[0]

    # ------------------------------------------------------------------
    # generation
    # ------------------------------------------------------------------
    def _euler(self, mu, prompt_mel, style, steps, cfg_rate, seeds, dev):
        """Batched copy of Seed-VC's flow-matching Euler solver."""
        torch = self.torch
        cfm = self.model.cfm
        B, T = mu.size(0), mu.size(1)
        z = torch.stack([torch.randn([cfm.in_channels, T], generator=torch.Generator().manual_seed(int(s)))
                         for s in seeds]).to(dev)
        t_span = torch.linspace(0, 1, steps + 1, device=dev)
        x = z
        prompt_len = prompt_mel.size(-1)
        prompt_x = torch.zeros_like(x)
        prompt_x[..., :prompt_len] = prompt_mel[..., :prompt_len]
        x[..., :prompt_len] = 0
        if getattr(cfm, "zero_prompt_speech_token", False):
            mu[..., :prompt_len] = 0
        x_lens = torch.LongTensor([T]).to(dev)
        t = t_span[0]
        for step in range(1, len(t_span)):
            dt = t_span[step] - t_span[step - 1]
            if cfg_rate > 0:
                d = cfm.estimator(torch.cat([x, x], 0),
                                  torch.cat([prompt_x, torch.zeros_like(prompt_x)], 0),
                                  x_lens,
                                  t.reshape(1).repeat(2 * B),
                                  torch.cat([style, torch.zeros_like(style)], 0),
                                  torch.cat([mu, torch.zeros_like(mu)], 0))
                dphi, dnull = d.chunk(2, dim=0)
                dphi = (1.0 + cfg_rate) * dphi - cfg_rate * dnull
            else:
                dphi = cfm.estimator(x, prompt_x, x_lens, t.reshape(1).repeat(B), style, mu)
            x = x + dt * dphi
            t = t + dt
            x[:, :, :prompt_len] = 0
        return x

    def generate(self, src: dict, refs: list[dict], shifts: list, seeds: list[int],
                 steps: int = 25, cfg_rate: float = 0.7) -> list[np.ndarray]:
        """Render one phrase for a batch of singers. Returns one waveform per singer."""
        torch = self.torch
        Lp = refs[0]["mel2"].size(-1)
        if any(r["mel2"].size(-1) != Lp for r in refs):
            raise ValueError("references in a batch must be the same length")
        if Lp + src["T"] > self.max_context:
            raise ValueError("phrase + reference longer than the model's 30 s window")

        def diffuse(dev):
            conds = torch.cat([self._cond(src, s, dev) for s in shifts], 0)
            prompts = torch.cat([r["prompt"].to(dev) for r in refs], 0)
            mu = torch.cat([prompts, conds], dim=1)
            mel2 = torch.cat([r["mel2"].to(dev) for r in refs], 0)
            style = torch.cat([r["style"].to(dev) for r in refs], 0)
            if self.use_amp and dev.type in ("cuda", "mps"):
                try:
                    with torch.autocast(device_type=dev.type, dtype=torch.float16):
                        return self._euler(mu.clone(), mel2, style, steps, cfg_rate, seeds, dev)
                except RuntimeError as e:
                    if "out of memory" in str(e).lower() or self._gpu_refused(e):
                        raise
                    print(f"@@STATUS half precision not supported here; using full precision", flush=True)
                    self.use_amp = False
            return self._euler(mu.clone(), mel2, style, steps, cfg_rate, seeds, dev)

        with torch.inference_mode():
            mel = self._run("dit", diffuse)[:, :, Lp:].float()
            wav = self._run("vocoder", lambda dev: self.vocoder(mel.to(dev))[:, 0].float().cpu())
        out = wav.detach().numpy()
        return [np.pad(w, (0, max(0, src["n"] - len(w))))[: src["n"]] for w in out]

    # ------------------------------------------------------------------
    def pitch_error(self, out_np: np.ndarray, src: dict, pitch) -> tuple[float, float]:
        """How well a render follows its target line: (median |cents| error, fraction >80 cents off)."""
        torch = self.torch
        with torch.inference_mode():
            f_out = self._f0(self._resample16(self._tensor(out_np)))[0].numpy()
        f_src = self.src_f0(src)
        if isinstance(pitch, np.ndarray):
            r = np.ones(len(f_src))
            m = min(len(r), len(pitch))
            r[:m] = pitch[:m]
            f_src = f_src * r
        else:
            f_src = f_src * (2 ** (pitch / 12))
        n = min(len(f_out), len(f_src))
        f_out, f_src = f_out[:n], f_src[:n]
        both = (f_out > 1) & (f_src > 1)
        if both.sum() < 10:
            return 0.0, 0.0
        cents = np.abs(1200 * np.log2(f_out[both] / f_src[both]))
        return float(np.median(cents)), float(np.mean(cents > 80))


def _ram_gb() -> float:
    try:
        return os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") / 1e9
    except (ValueError, OSError, AttributeError):
        return 8.0
