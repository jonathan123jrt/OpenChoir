"""Small audio helpers shared by the OpenChoir AI tools (numpy + scipy only)."""
from __future__ import annotations

import io
from fractions import Fraction

import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly, lfilter


def read_wav(src) -> tuple[np.ndarray, int]:
    """Read a WAV from a path, bytes or file object -> (float32 mono, sr)."""
    if isinstance(src, (bytes, bytearray)):
        src = io.BytesIO(src)
    sr, x = wavfile.read(src)
    x = to_float(x)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float32), int(sr)


def read_audio_any(path: str) -> tuple[np.ndarray, int]:
    """Read WAV directly; other formats (AIFF, MP3, FLAC...) via soundfile/librosa if present."""
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except Exception:
            pass
    try:
        import soundfile as sf
        x, sr = sf.read(path, dtype="float32", always_2d=True)
        return x.mean(axis=1), int(sr)
    except Exception:
        import librosa
        x, sr = librosa.load(path, sr=None, mono=True)
        return x.astype(np.float32), int(sr)


def read_audio_multi(path: str) -> tuple[np.ndarray, int]:
    """Read any supported file keeping channels: (n,) mono or (n, 2) stereo, float32."""
    x = sr = None
    if path.lower().endswith(".wav"):
        try:
            sr, x = wavfile.read(path)
            x = to_float(x)
        except Exception:
            x = None
    if x is None:
        try:
            import soundfile as sf
            x, sr = sf.read(path, dtype="float32", always_2d=True)
        except Exception:
            import librosa
            x, sr = librosa.load(path, sr=None, mono=False)
            x = x.T if x.ndim == 2 else x
    x = np.asarray(x, np.float32)
    if x.ndim == 2:
        if x.shape[1] == 1:
            x = x[:, 0]
        elif x.shape[1] > 2:
            x = x[:, :2]
    return x, int(sr)


def to_float(x: np.ndarray) -> np.ndarray:
    if x.dtype == np.int16:
        return x / 32768.0
    if x.dtype == np.int32:
        return x / 2147483648.0
    if x.dtype == np.uint8:
        return (x.astype(np.float32) - 128) / 128.0
    return x.astype(np.float32)


def resample(x: np.ndarray, sr_in: int, sr_out: int) -> np.ndarray:
    if sr_in == sr_out:
        return x
    f = Fraction(sr_out, sr_in).limit_denominator(1000)
    return resample_poly(x, f.numerator, f.denominator, axis=0).astype(np.float32)


def write_wav(path: str, x: np.ndarray, sr: int) -> None:
    """Write 32-bit float WAV (mono or [n, 2] stereo)."""
    wavfile.write(path, sr, np.ascontiguousarray(x, dtype=np.float32))


def write_wav16(path: str, x: np.ndarray, sr: int) -> None:
    y = np.clip(x, -1.0, 1.0)
    wavfile.write(path, sr, (y * 32767).astype(np.int16))


def rms_db(x: np.ndarray) -> float:
    return 20 * np.log10(np.sqrt(np.mean(np.square(x))) + 1e-12)


def normalize_rms(x: np.ndarray, target_db: float = -20.0) -> np.ndarray:
    voiced = x[np.abs(x) > 1e-4]
    level = rms_db(voiced) if voiced.size else rms_db(x)
    y = x * 10 ** ((target_db - level) / 20)
    peak = np.max(np.abs(y)) + 1e-12
    return y / peak * 0.98 if peak > 0.98 else y


def trim_silence(x: np.ndarray, sr: int, thresh_db: float = -35.0, pad_s: float = 0.05) -> np.ndarray:
    """Remove leading/trailing silence."""
    hop = max(1, int(sr * 0.01))
    frames = np.lib.stride_tricks.sliding_window_view(np.pad(x, (0, hop)), hop)[::hop]
    level = 20 * np.log10(np.sqrt(np.mean(frames ** 2, axis=1)) + 1e-12)
    ref = np.max(level)
    on = np.where(level > ref + thresh_db)[0] if ref > -100 else np.array([])
    if on.size == 0:
        return x
    pad = int(pad_s * sr)
    a = max(0, on[0] * hop - pad)
    b = min(len(x), (on[-1] + 1) * hop + pad)
    return x[a:b]


def median_f0(x: np.ndarray, sr: int, fmin: float = 60.0, fmax: float = 1200.0) -> float:
    """Median pitch of the voiced parts. 0 if unvoiced."""
    v = f0_track(x, sr, fmin, fmax)
    return float(np.median(v)) if v.size >= 5 else 0.0


def f0_stats(x: np.ndarray, sr: int) -> tuple[float, float, float]:
    """(10th, 50th, 90th percentile) pitch of the voiced parts, Hz. Zeros if unvoiced."""
    v = f0_track(x, sr)
    if v.size < 5:
        return 0.0, 0.0, 0.0
    p10, p50, p90 = np.percentile(v, [10, 50, 90])
    return float(p10), float(p50), float(p90)


def f0_track(x: np.ndarray, sr: int, fmin: float = 60.0, fmax: float = 1200.0) -> np.ndarray:
    """Pitch (Hz) of each voiced 16 ms frame. Unvoiced/quiet frames are dropped."""
    f0 = f0_curve(x, sr, fmin, fmax)
    return f0[f0 > 0]


F0_HOP_S = 256 / 16000  # 16 ms


def f0_curve(x: np.ndarray, sr: int, fmin: float = 60.0, fmax: float = 1200.0) -> np.ndarray:
    """Pitch (Hz) every 16 ms, YIN-style; 0 where unvoiced or quiet."""
    y = resample(x, sr, 16000)
    sr = 16000
    frame, hop = 1024, 256
    tmin, tmax = int(sr / fmax), int(sr / fmin)
    if len(y) < frame + tmax:
        return np.zeros(0)
    y = np.concatenate([np.zeros(frame // 2), y, np.zeros(frame + tmax)])  # centre frames
    f0s, energies = [], []
    for start in range(0, len(y) - frame - tmax, hop):
        seg = y[start:start + frame + tmax]
        e = float(np.mean(seg[:frame] ** 2))
        energies.append(e)
        # difference function via FFT autocorrelation
        w = seg[:frame]
        n = 1 << int(np.ceil(np.log2(frame + tmax + frame)))
        acf = np.fft.irfft(np.fft.rfft(seg, n) * np.conj(np.fft.rfft(w, n)), n)[: tmax + 1]
        cum = np.cumsum(seg ** 2)
        e0 = cum[frame - 1]
        lag = np.arange(tmax + 1)
        e_lag = cum[lag + frame - 1] - np.concatenate(([0.0], cum[lag[1:] - 1]))
        d = e0 + e_lag - 2 * acf
        d[0] = 0
        cmnd = d[1:] * np.arange(1, tmax + 1) / (np.cumsum(d[1:]) + 1e-12)
        cmnd = np.concatenate(([1.0], cmnd))
        cand = np.where(cmnd[tmin:tmax] < 0.15)[0]
        if cand.size == 0:
            f0s.append(0.0)
            continue
        t = cand[0] + tmin
        while t + 1 < tmax and cmnd[t + 1] < cmnd[t]:
            t += 1
        # parabolic refinement
        if 1 <= t < tmax:
            a, b, c = cmnd[t - 1], cmnd[t], cmnd[t + 1]
            denom = a - 2 * b + c
            t = t + 0.5 * (a - c) / denom if abs(denom) > 1e-12 else t
        f0s.append(sr / t)
    f0s = np.array(f0s)
    energies = np.array(energies)
    gate = energies > (np.max(energies) * 10 ** (-35 / 10))
    f0s[~gate] = 0.0
    return f0s


def one_pole_lowpass(x: np.ndarray, sr: int, fc: float) -> np.ndarray:
    a = np.exp(-2 * np.pi * fc / sr)
    return lfilter([1 - a], [1, -a], x, axis=0).astype(np.float32)


def one_pole_highpass(x: np.ndarray, sr: int, fc: float) -> np.ndarray:
    return (x - one_pole_lowpass(x, sr, fc)).astype(np.float32)


# --------------------------------------------------------------------------
# Phrases: only the parts that are actually sung get sent to the AI
# --------------------------------------------------------------------------
def frame_levels_db(x: np.ndarray, sr: int, hop_s: float = 0.01) -> tuple[np.ndarray, int]:
    hop = max(1, int(sr * hop_s))
    n = len(x) // hop
    if n == 0:
        return np.full(1, -120.0), hop
    fr = x[: n * hop].reshape(n, hop)
    return 20 * np.log10(np.sqrt(np.mean(fr ** 2, axis=1)) + 1e-9), hop


def find_phrases(x: np.ndarray, sr: int, max_len_s: float, min_gap_s: float = 0.30,
                 min_len_s: float = 0.15, pre_s: float = 0.08, post_s: float = 0.15,
                 overlap_s: float = 0.25) -> list[tuple[int, int]]:
    """Return (start, end) sample ranges covering everything sung.

    Silences longer than min_gap_s are skipped. Phrases longer than max_len_s
    are split at their quietest point, with overlap_s of overlap so the pieces
    can be cross-faded back together.
    """
    lv, hop = frame_levels_db(x, sr)
    top = np.percentile(lv, 99)
    active = lv > max(top - 40.0, -65.0)

    # close short gaps, drop tiny islands
    gap = int(min_gap_s * sr / hop)
    idx = np.where(active)[0]
    if idx.size == 0:
        return []
    regions, a, b = [], idx[0], idx[0]
    for i in idx[1:]:
        if i - b <= gap:
            b = i
        else:
            regions.append((a, b + 1)); a = b = i
    regions.append((a, b + 1))
    regions = [(a, b) for a, b in regions if (b - a) * hop >= min_len_s * sr]

    out = []
    for a, b in regions:
        s = max(0, a * hop - int(pre_s * sr))
        e = min(len(x), b * hop + int(post_s * sr))
        out += _split_long(lv, hop, s, e, int(max_len_s * sr), int(overlap_s * sr))
    return out


def _split_long(lv, hop, s, e, max_len, overlap):
    pieces = []
    while e - s > max_len:
        # look for the quietest 10 ms in the last 40 % of the allowed length
        lo = s + int(max_len * 0.6)
        hi = s + max_len - overlap
        f_lo, f_hi = lo // hop, max(lo // hop + 1, hi // hop)
        cut = (f_lo + int(np.argmin(lv[f_lo:f_hi]))) * hop
        pieces.append((s, min(e, cut + overlap)))
        s = cut
    pieces.append((s, e))
    return pieces


def place(out: np.ndarray, y: np.ndarray, start: int, fade_in: int, fade_out: int) -> None:
    """Add y into out at start with raised-cosine fades (overlaps cross-fade to unity)."""
    n = min(len(y), len(out) - start)
    if n <= 0:
        return
    y = y[:n].astype(np.float32).copy()
    if fade_in > 0:
        k = min(fade_in, n)
        y[:k] *= np.sin(np.linspace(0, np.pi / 2, k)) ** 2
    if fade_out > 0:
        k = min(fade_out, n)
        y[n - k:] *= np.cos(np.linspace(0, np.pi / 2, k)) ** 2
    out[start:start + n] += y


def chunk_clip(x: np.ndarray, sr: int, chunk_s: float = 4.0, min_s: float = 1.5) -> list[np.ndarray]:
    """Cut a clip into ~chunk_s pieces at quiet points, dropping mostly-silent ones."""
    pieces = []
    for a, b in find_phrases(x, sr, max_len_s=chunk_s, min_gap_s=0.25, overlap_s=0.0):
        seg = x[a:b]
        if len(seg) >= min_s * sr:
            pieces.append(seg)
    return pieces
