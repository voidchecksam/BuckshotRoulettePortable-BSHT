"""Compare the port's playback of every bank cue with Godot 4.1.4's.

    python tools/compare_cues.py <port dir> <godot dir>

<port dir>  tools/render_cues.cpp output (48 kHz, the device rate)
<godot dir> tools/godot/record_cues.gd output (Godot's mix rate)

Per cue: the level difference (port - Godot, dB, after alignment), the SNR of
the port against Godot with the level matched (and against Godot's mid
channel), the stereo side energy Godot has (dB below the mid; the bank keeps a
cue stereo when it is within -40 dB), and the band balance.
Then Godot's measured 2D and 3D player gains against godot::OutputGain's.
"""
import math
import os
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate, resample_poly

BANDS = [(0, 200), (200, 2000), (2000, 8000), (8000, 24000)]


def load(path, rate_out=None):
    rate, x = wavfile.read(path)
    x = x.astype(np.float64) / 32768.0
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    if rate_out and rate != rate_out:
        g = math.gcd(rate, rate_out)
        x = resample_poly(x, rate_out // g, rate // g, axis=0)
        rate = rate_out
    return rate, x


def align(ref, sig, max_lag):
    """Shifts `sig` so it lines up with `ref` (mono cross-correlation)."""
    a = ref.mean(axis=1)[: max_lag * 4]
    b = sig.mean(axis=1)[: max_lag * 4]
    c = correlate(a, b, mode="full", method="fft")
    mid = len(b) - 1   # c[mid] is zero lag
    lo, hi = max(0, mid - max_lag), min(len(c), mid + max_lag + 1)
    lag = int(np.argmax(np.abs(c[lo:hi]))) + lo - mid
    if lag > 0:
        sig = np.concatenate([np.zeros((lag, 2)), sig])
    elif lag < 0:
        sig = sig[-lag:]
    n = min(len(ref), len(sig))
    return ref[:n], sig[:n], lag


def db(x):
    return 10 * math.log10(x) if x > 1e-20 else -200.0


def band_energy(x, rate):
    spec = np.abs(np.fft.rfft(x.mean(axis=1))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1.0 / rate)
    return [spec[(freqs >= lo) & (freqs < hi)].sum() for lo, hi in BANDS]


def compare(port_path, godot_path):
    rate, g = load(godot_path)
    _, p = load(port_path, rate)
    if len(g) < 256 or len(p) < 256:
        return None
    g, p, lag = align(g, p, rate // 2)
    if len(g) < 256:
        return None
    eg, ep = (g ** 2).sum(), (p ** 2).sum()
    level = db(ep / eg) if eg > 0 else 0.0
    scale = math.sqrt(eg / ep) if ep > 0 else 1.0
    err = ((g - p * scale) ** 2).sum()
    # The same, against Godot's mid channel: what a mono bank could reach at best,
    # so snr - mono separates the stereo loss from the codec and resampler.
    gm = (g[:, 0] + g[:, 1]) / 2
    pm = (p[:, 0] + p[:, 1]) / 2
    em, epm = (gm ** 2).sum(), (pm ** 2).sum()
    mscale = math.sqrt(em / epm) if epm > 0 else 1.0
    mono = db(em / ((gm - pm * mscale) ** 2).sum()) if em > 0 else 99.0
    side = (((g[:, 0] - g[:, 1]) / 2) ** 2).sum()
    mid = (((g[:, 0] + g[:, 1]) / 2) ** 2).sum()
    bg, bp = band_energy(g, rate), band_energy(p * scale, rate)
    bands = [db(b / a) if a > 0 and b > 0 else 0.0 for a, b in zip(bg, bp)]
    peak_g, peak_p = np.abs(g).max(), np.abs(p).max()
    return {
        "level": level, "snr": db(eg / err) if err > 0 else 99.0, "mono": mono, "side": db(side / mid) if mid > 0 else -99.0,
        "bands": bands, "lag": lag, "peak_g": db(peak_g ** 2), "peak_p": db(peak_p ** 2),
    }


def main(port_dir, godot_dir):
    names = [n.strip() for n in open(os.path.join(port_dir, "cues.txt"), encoding="utf-8") if n.strip()]
    rows = []
    for n in names:
        gp = os.path.join(godot_dir, "plain__" + n + ".wav")
        if not os.path.exists(gp):
            print("missing godot recording:", n)
            continue
        r = compare(os.path.join(port_dir, n + ".wav"), gp)
        if r is None:
            print("too short to compare (empty recording?):", n)
            continue
        rows.append((n, r))
    rows.sort(key=lambda r: r[1]["snr"])
    print(f"{'cue':44s} {'level':>6s} {'snr':>6s} {'mono':>6s} {'side':>6s}  bands <200 /2k /8k />8k (dB)")
    for n, r in rows:
        b = " ".join(f"{x:+5.1f}" for x in r["bands"])
        print(f"{n[:44]:44s} {r['level']:+6.1f} {r['snr']:6.1f} {r['mono']:6.1f} {r['side']:6.1f}  {b}")
    snrs = [r["snr"] for _, r in rows]
    levels = [r["level"] for _, r in rows]
    sides = [r["side"] for _, r in rows]
    print(f"\n{len(rows)} cues: level mean {np.mean(levels):+.2f} dB (min {min(levels):+.1f}, max {max(levels):+.1f}); "
          f"SNR median {np.median(snrs):.1f} dB (worst {min(snrs):.1f}), against the mid "
          f"{np.median([r['mono'] for _, r in rows]):.1f} dB; "
          f"stereo side median {np.median(sides):.1f} dB below mid ({sum(s > -20 for s in sides)} cues above -20 dB)")
    for b in range(len(BANDS)):
        vals = [r["bands"][b] for _, r in rows]
        print(f"band {BANDS[b][0]}-{BANDS[b][1]} Hz: median {np.median(vals):+.2f} dB, worst {max(vals, key=abs):+.1f}")

    print("\nGodot's spatial gains (measured from its own recordings):")
    for kind, expect in (("2d", 0.362318), ("3d", 0.583571)):
        for cue in ("temp gunshot_live.wav", "round blinker wave.ogg"):
            sp = os.path.join(godot_dir, f"{kind}__{cue}.wav")
            pl = os.path.join(godot_dir, f"plain__{cue}.wav")
            if not (os.path.exists(sp) and os.path.exists(pl)):
                continue
            _, a = load(pl)
            _, b = load(sp)
            ga = math.sqrt((a[:, 0] ** 2).sum()) or 1.0
            gl = math.sqrt((b[:, 0] ** 2).sum()) / ga
            gr = math.sqrt((b[:, 1] ** 2).sum()) / math.sqrt((a[:, 1] ** 2).sum() or 1.0)
            print(f"  {kind} {cue:28s} L {gl:.4f} R {gr:.4f}   godot::OutputGain {expect:.4f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
