"""Objective checks of the OpenChoir DSP, run against the offline renderer.

    python3 Tools/measure.py path/to/openchoir_render [workdir]

Measures, for the choir signal alone (lead off, room off):
  lag      how late the choir is versus the lead (cross-correlation), ms
  comb     spectral ripple a single singer adds to steady pink noise, dB
  grit     level change when Grit goes 0 -> 100 % on a quiet signal, dB
  width    L/R correlation of a 3-voice section (1 = mono)
  cpu      heaviest preset, % of one core
"""
import os
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate, welch

RENDER = sys.argv[1]
WORK = sys.argv[2] if len(sys.argv) > 2 else "measure_tmp"
SR = 48000
os.makedirs(WORK, exist_ok=True)
rng = np.random.default_rng(0)


def write(name, x):
    p = os.path.join(WORK, name)
    wavfile.write(p, SR, (np.clip(x, -1, 1) * 32767).astype(np.int16))
    return p


def render(inp, preset=0, **kw):
    out = os.path.join(WORK, "out.wav")
    args = [RENDER, inp, out, str(preset)] + [f"{k}={v}" for k, v in kw.items()]
    res = subprocess.run(args, capture_output=True, text=True, check=True).stdout
    sr, y = wavfile.read(out)
    y = y.astype(np.float64) / (2 ** 23 if y.dtype == np.int32 else 32768)
    return y, res


def pink(n):
    w = rng.standard_normal(n)
    f = np.fft.rfft(w)
    k = np.arange(len(f)); k[0] = 1
    return np.fft.irfft(f / np.sqrt(k), n)


base = dict(room=0, dry=-60, choir=0, octdown=0, octup=0, grit=0, vibrato=0)

# 1. lag: clicks -> where does the choir energy centre sit?
x = np.zeros(SR * 4)
x[SR // 2::SR // 2] = 0.8
pin = write("clicks.wav", x)
y, _ = render(pin, 0, **dict(base, voices=8, timing=0, detune=14))
m = y.mean(axis=1)[: len(x)]
c = correlate(m, x, mode="full")[len(x) - 1:len(x) - 1 + int(0.2 * SR)]
env = np.abs(c)
lag_ms = float(np.sum(env * np.arange(len(env))) / np.sum(env) / SR * 1000)

# 2. comb ripple: 1 voice, 14 cents, pink noise, compare spectra
n = SR * 8
pn = pink(n); pn = pn / np.max(np.abs(pn)) * 0.3
pin = write("pink.wav", pn)
y, _ = render(pin, 0, **dict(base, voices=1, timing=0, detune=14, tone=18000, width=0))
f, pi = welch(pn, SR, nperseg=8192)
_, po = welch(y.mean(axis=1)[:n], SR, nperseg=8192)
band = (f > 200) & (f < 4000)
ratio_db = 10 * np.log10(po[band] / pi[band])
ratio_db -= np.median(ratio_db)
# short-time worst case: per 0.5 s frames
worst = []
hop = SR // 2
for s0 in range(SR, n - hop, hop):
    _, a = welch(pn[s0:s0 + hop], SR, nperseg=4096)
    fo, b = welch(y.mean(axis=1)[s0:s0 + hop], SR, nperseg=4096)
    bb = (fo > 200) & (fo < 4000)
    r = 10 * np.log10(b[bb] / a[bb]); r -= np.median(r)
    worst.append(np.percentile(r, 5))
comb_db = float(np.median(worst))

# 3. grit level jump on a quiet sine
t = np.arange(SR * 3) / SR
q = 0.05 * np.sin(2 * np.pi * 220 * t)
pin = write("quiet.wav", q)
y0, _ = render(pin, 0, **dict(base, voices=8, grit=0))
y1, _ = render(pin, 0, **dict(base, voices=8, grit=100))
rms = lambda v: np.sqrt(np.mean(v[SR // 2: 2 * SR] ** 2))
grit_db = float(20 * np.log10(rms(y1) / rms(y0)))

# 4. width of a 3-voice section
y, _ = render(write("pink2.wav", pn), 0, **dict(base, voices=3, width=100, detune=8, timing=12))
width_corr = float(np.corrcoef(y[SR:n, 0], y[SR:n, 1])[0, 1])

# 5. cpu, heaviest preset
voc = os.path.join(WORK, "..", "render", "dry.wav")
_, out = render(voc if os.path.exists(voc) else pin, 1)
cpu = float(out.split("cpu=")[1].split("%")[0])

print(f"lag   {lag_ms:6.1f} ms   (choir behind the lead at Timing 0)")
print(f"comb  {comb_db:6.1f} dB   (typical deepest notch a single singer adds; 0 is ideal)")
print(f"grit  {grit_db:+6.1f} dB   (level change on quiet input, 0 -> 100 %)")
print(f"width {width_corr:6.2f}      (L/R correlation, 3 singers at 100 % width)")
print(f"cpu   {cpu:6.1f} %    (Big Hall preset, one core, incl. reverb)")
