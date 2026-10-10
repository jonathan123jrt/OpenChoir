"""Build OpenChoir's voice bank from VocalSet (20 consenting professional singers, CC BY 4.0).

VocalSet ships as a 6 GB zip on Zenodo. Instead of downloading all of it, this
script reads the zip's table of contents over HTTP range requests and pulls only
about a minute of singing per singer (roughly 200-300 MB in total).

    python fetch_voices.py                 # -> voices/vocalset/...
    python fetch_voices.py --zip local.zip # use a copy you already downloaded

Credit: J. Wilkins, P. Seetharaman, A. Wahl, B. Pardo, "VocalSet: A Singing
Voice Dataset", ISMIR 2018. https://doi.org/10.5281/zenodo.1193957
"""
from __future__ import annotations

import argparse
import io
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import zipfile
from collections import OrderedDict, defaultdict

import numpy as np

import oc_audio as A

VOCALSET_URL = "https://zenodo.org/records/1442513/files/VocalSet1-2.zip?download=1"

CREDITS = """Voice references in this folder come from:

  VocalSet: A Singing Voice Dataset
  Julia Wilkins, Prem Seetharaman, Alison Wahl, Bryan Pardo (ISMIR 2018)
  https://doi.org/10.5281/zenodo.1193957
  Licence: Creative Commons Attribution 4.0 International (CC BY 4.0)
  https://creativecommons.org/licenses/by/4.0/

Changes made by OpenChoir: short clips were selected, trimmed of silence,
joined together and level-normalised for use as voice-conversion references.
If you release music made with these voices, credit VocalSet as above.
"""

# What each bank prefers, best first. Matched against the lower-cased path.
BANKS = {
    # clean, natural singing for choirs
    "choir": [("excerpts", "straight"), ("excerpt", ""), ("arpeggios", "straight"),
              ("scales", "straight"), ("long_tones", "straight"), ("straight", "")],
    # powerful, chesty singing for gang / shout vocals
    "shout": [("belt", "excerpt"), ("belt", "arpeggio"), ("belt", ""), ("forte", ""),
              ("excerpts", "straight"), ("straight", "")],
}

BANK_SECONDS = 60.0   # sung material kept per singer per bank
CHUNK_SECONDS = 4.0  # pieces are short so references can be matched to the register
OUT_SR = 44100
FORMAT = 2


# --------------------------------------------------------------------------
# Random-access file over HTTP (only the bytes zipfile asks for are fetched)
# --------------------------------------------------------------------------
class HttpRangeFile(io.RawIOBase):
    BLOCK = 1 << 20  # 1 MiB

    def __init__(self, url: str):
        super().__init__()
        self.orig_url = url
        self.url, self.size = self._probe(url)
        self.pos = 0
        self.cache: OrderedDict[int, bytes] = OrderedDict()
        self.fetched = 0

    @staticmethod
    def _probe(url: str) -> tuple[str, int]:
        req = urllib.request.Request(url, headers={"Range": "bytes=0-0", "User-Agent": "OpenChoir/0.2"})
        with urllib.request.urlopen(req, timeout=60) as r:
            final = r.geturl()
            if r.status != 206:
                raise RuntimeError("server does not support range requests")
            cr = r.headers.get("Content-Range", "")
            m = re.search(r"/(\d+)$", cr)
            if not m:
                raise RuntimeError(f"no size in Content-Range: {cr!r}")
            return final, int(m.group(1))

    def _block(self, idx: int) -> bytes:
        if idx in self.cache:
            self.cache.move_to_end(idx)
            return self.cache[idx]
        start = idx * self.BLOCK
        end = min(self.size, start + self.BLOCK) - 1
        for attempt in range(5):
            try:
                req = urllib.request.Request(self.url, headers={"Range": f"bytes={start}-{end}",
                                                                "User-Agent": "OpenChoir/0.2"})
                with urllib.request.urlopen(req, timeout=120) as r:
                    if r.status != 206:  # server ignored the range: never read the whole 6 GB
                        raise IOError(f"expected a partial response, got HTTP {r.status}")
                    data = r.read(end - start + 2)
                if len(data) != end - start + 1:
                    raise IOError("short read")
                break
            except urllib.error.HTTPError as e:
                if attempt == 4:
                    raise
                if e.code in (401, 403, 404, 410):   # signed download link expired: get a fresh one
                    try:
                        self.url, _ = self._probe(self.orig_url)
                    except Exception:
                        pass
                time.sleep(2 * (attempt + 1))
            except (urllib.error.URLError, IOError, TimeoutError):
                if attempt == 4:
                    raise
                time.sleep(2 * (attempt + 1))
        self.fetched += len(data)
        self.cache[idx] = data
        if len(self.cache) > 64:
            self.cache.popitem(last=False)
        return data

    # io.RawIOBase interface
    def readable(self): return True
    def seekable(self): return True
    def tell(self): return self.pos

    def seek(self, offset, whence=io.SEEK_SET):
        if whence == io.SEEK_SET:   self.pos = offset
        elif whence == io.SEEK_CUR: self.pos += offset
        else:                       self.pos = self.size + offset
        return self.pos

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0:
            return 0
        out, got = memoryview(b), 0
        while got < n:
            idx, off = divmod(self.pos, self.BLOCK)
            blk = self._block(idx)
            take = min(n - got, len(blk) - off)
            out[got:got + take] = blk[off:off + take]
            got += take
            self.pos += take
        return got


# --------------------------------------------------------------------------
def singer_of(path: str):
    """-> ('f'|'m', number) or None, from either folder or file naming."""
    p = path.lower()
    m = re.search(r"(?:^|/)(female|male)\s*(\d+)(?:/|_)", p)
    if m:
        return ("f" if m.group(1) == "female" else "m", int(m.group(2)))
    m = re.search(r"(?:^|/)([fm])(\d+)_[^/]*\.wav$", p)
    if m:
        return (m.group(1), int(m.group(2)))
    return None


def pick_clips(paths: list[str], prefs) -> list[str]:
    ranked = []
    for rank, (a, b) in enumerate(prefs):
        for p in paths:
            lp = p.lower()
            if a in lp and b in lp and p not in ranked:
                ranked.append(p)
    # prefer the open vowel "a" within a rank, then alphabetical for stability
    return ranked


def build_chunks(zf: zipfile.ZipFile, clips: list[str]) -> tuple[list[np.ndarray], list[str]]:
    """Cut the preferred clips into short pieces until BANK_SECONDS of singing is collected.

    Clips are visited round-robin across techniques/contexts so the bank covers the
    singer's whole range (scales and arpeggios) as well as real lyrics (excerpts).
    """
    chunks, used, total = [], [], 0.0
    for name in clips:
        try:
            x, sr = A.read_wav(zf.read(name))
        except Exception as e:  # odd format; skip
            print(f"    skip {name}: {e}")
            continue
        x = A.normalize_rms(A.resample(x, sr, OUT_SR), -20.0)
        pieces = A.chunk_clip(x, OUT_SR, CHUNK_SECONDS)
        if not pieces:
            continue
        used.append(name)
        for c in pieces:
            chunks.append(A.normalize_rms(c, -20.0))
            total += len(c) / OUT_SR
        if total >= BANK_SECONDS:
            break
    return chunks, used


def interleave(paths: list[str]) -> list[str]:
    """Round-robin over (context, technique) folders so we don't take 60 s of one exercise."""
    groups: dict[str, list[str]] = OrderedDict()
    for p in paths:
        groups.setdefault(os.path.dirname(p), []).append(p)
    out = []
    while any(groups.values()):
        for k in list(groups):
            if groups[k]:
                out.append(groups[k].pop(0))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--zip", help="path or URL of VocalSet1-2.zip (default: Zenodo)", default=VOCALSET_URL)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "voices", "vocalset"))
    ap.add_argument("--force", action="store_true", help="rebuild even if the bank exists")
    args = ap.parse_args()

    manifest_path = os.path.join(args.out, "voices.json")
    if os.path.exists(manifest_path) and not args.force:
        try:
            current = json.load(open(manifest_path)).get("format") == FORMAT
        except Exception:
            current = False
        if current:
            print(f"Voice bank already present: {args.out}  (use --force to rebuild)")
            return 0
        print("Upgrading the voice bank to the new format...")
        import shutil
        for sub in BANKS:
            shutil.rmtree(os.path.join(args.out, sub), ignore_errors=True)

    print("Opening VocalSet...")
    if re.match(r"https?://", args.zip):
        try:
            fobj = HttpRangeFile(args.zip)
        except Exception as e:
            print(f"\nCould not stream VocalSet ({e}).\n"
                  "Download VocalSet1-2.zip manually from https://zenodo.org/records/1442513\n"
                  "then run:  python fetch_voices.py --zip /path/to/VocalSet1-2.zip")
            return 1
        print(f"  remote zip: {fobj.size / 1e6:,.0f} MB (only the needed clips will be downloaded)")
        zf = zipfile.ZipFile(io.BufferedReader(fobj, buffer_size=1 << 16))
    else:
        fobj = None
        zf = zipfile.ZipFile(args.zip)

    by_singer: dict[tuple, list[str]] = defaultdict(list)
    # VocalSet stores each recording up to three times (by singer, by technique, by
    # vowel). Keep one copy per file name, preferring the by-singer ("FULL") tree.
    names = [n for n in zf.namelist()
             if n.lower().endswith(".wav") and "__macosx" not in n.lower()
             and not os.path.basename(n).startswith("._")]
    names.sort(key=lambda n: (0 if "full" in n.lower().split("/")[0] else 1, n))
    seen_files = set()
    for name in names:
        base = os.path.basename(name).lower()
        if base in seen_files:
            continue
        seen_files.add(base)
        s = singer_of(name)
        if s:
            by_singer[s].append(name)

    if not by_singer:
        print("No singer folders found in the zip - unexpected layout.")
        return 1
    print(f"  found {len(by_singer)} singers")

    os.makedirs(args.out, exist_ok=True)
    manifest = []
    for (sex, num) in sorted(by_singer):
        files = sorted(by_singer[(sex, num)])
        for bank, prefs in BANKS.items():
            clips = pick_clips(files, prefs)
            # keep the preference order between tiers, but interleave inside the top tiers
            clips = interleave(clips[:24]) + clips[24:]
            chunks, used = build_chunks(zf, clips)
            if not chunks:
                continue
            vid = f"{sex}{num}"
            vdir = os.path.join(args.out, bank, vid)
            os.makedirs(vdir, exist_ok=True)
            entries, all_f0 = [], []
            for i, c in enumerate(chunks):
                path = os.path.join(vdir, f"{i:02d}.wav")
                A.write_wav16(path, c, OUT_SR)
                track = A.f0_track(c, OUT_SR)
                if track.size < 5:
                    os.remove(path)
                    continue
                all_f0.append(track)
                p10, p50, p90 = np.percentile(track, [10, 50, 90])
                entries.append({"file": os.path.relpath(path, args.out), "f0": round(float(p50), 1),
                                "p10": round(float(p10), 1), "p90": round(float(p90), 1),
                                "seconds": round(len(c) / OUT_SR, 2)})
            if not entries:
                continue
            allv = np.concatenate(all_f0)
            lo, med, hi = np.percentile(allv, [10, 50, 90])
            manifest.append({"id": vid, "bank": bank, "sex": "female" if sex == "f" else "male",
                             "median_f0": round(float(med), 1), "range": [round(float(lo), 1), round(float(hi), 1)],
                             "seconds": round(sum(e["seconds"] for e in entries), 1),
                             "chunks": entries, "source_clips": used, "source": "VocalSet (CC BY 4.0)"})
            dl = f", {fobj.fetched / 1e6:.0f} MB downloaded so far" if fobj else ""
            print(f"  {bank:5s} {vid:4s} {len(entries):2d} pieces, range {lo:4.0f}-{hi:4.0f} Hz{dl}")

    with open(manifest_path, "w") as f:
        json.dump({"format": FORMAT, "sample_rate": OUT_SR, "voices": manifest}, f, indent=1)
    with open(os.path.join(args.out, "CREDITS.txt"), "w") as f:
        f.write(CREDITS)
    print(f"\nDone: {len(manifest)} voice references in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
