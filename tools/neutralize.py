"""Make a voice pack sound like one person in one mood.

Recordings made to show off emotions (SuddenDice and the like) mix calm takes
with screams, squeals, laughs and breaths. Strung together as babble they sound
like several people taking turns. This module measures every clip and:

* babble syllables and the everyday short/medium/long/question folders are held
  to the speaker's calm baseline (measured from their everyday takes): clips
  far off in pitch, loudness, brightness, breathiness or voicing are dropped,
  and what's left is nudged toward the baseline pitch;
* each emotion folder is judged against itself, so anger keeps its range and
  only the takes that stand out from the other angry ones go;
* a few hard limits (screams, squeals, mostly-unvoiced noise) apply everywhere;
* everything is levelled to the same loudness, not the same peak.

Used by make_voicepack.py (--neutralize, on in the suddendice preset).
"""

import numpy as np

NEUTRAL_FOLDERS = ("short", "medium", "long", "question")
EMOTION_FOLDERS = ("exclaim", "anger", "disgust", "fear", "sad", "happy", "surprise", "puzzled")
# emotional sources that may top up babble when a speaker has too few calm syllables
MILD_SOURCES = ("happy", "puzzled", "surprise")

# robust spread floors, so a very consistent speaker doesn't get every clip rejected
FLOOR = {"f0": 1.2, "level": 2.5, "bright": 0.22, "flat": 0.035, "voiced": 0.08}

TARGET_RMS_DB = -20.0  # loudness every clip is levelled to (active part)
PEAK_CAP = 0.89


def _semitones(hz):
    return 12.0 * np.log2(hz / 100.0)


def yin(x, sr, hop, fmin=65.0, fmax=500.0, thresh=0.2):
    """YIN pitch tracker: (f0 per hop or nan, aperiodicity per hop). Far less
    prone to octave errors than plain autocorrelation."""
    tau_max = int(sr / fmin)
    tau_min = max(2, int(sr / fmax))
    w = int(sr * 0.025)
    f0s, aps = [], []
    for s in range(0, max(0, len(x) - w - tau_max), hop):
        seg = x[s:s + w + tau_max]
        # difference function via FFT
        n = 1 << int(np.ceil(np.log2(2 * len(seg))))
        X = np.fft.rfft(seg, n)
        ac = np.fft.irfft(X * np.conj(X), n)[: tau_max + 1]
        cs = np.concatenate(([0.0], np.cumsum(seg ** 2)))
        e0 = cs[w] - cs[0]
        etau = cs[w + np.arange(tau_max + 1)] - cs[np.arange(tau_max + 1)]
        # ac here is over the full segment; good enough with w << segment
        d = e0 + etau - 2 * ac
        d[0] = 0
        cmnd = np.ones_like(d)
        run = np.cumsum(d[1:])
        cmnd[1:] = d[1:] * np.arange(1, len(d)) / np.maximum(run, 1e-12)
        band = cmnd[tau_min:tau_max]
        below = np.where(band < thresh)[0]
        if len(below):
            t = below[0]
            while t + 1 < len(band) and band[t + 1] < band[t]:
                t += 1
        else:
            t = int(np.argmin(band))
        ap = float(band[t])
        tau = t + tau_min
        if 0 < t < len(band) - 1:  # parabolic refinement
            a, b, c = band[t - 1], band[t], band[t + 1]
            den = a - 2 * b + c
            tau = tau + (0.5 * (a - c) / den if abs(den) > 1e-12 else 0)
        f0s.append(sr / tau if ap < thresh * 1.5 else np.nan)
        aps.append(ap)
    return np.array(f0s), np.array(aps)


def measure(x, sr, f0_track, frame):
    """Features of one clip. f0_track and frame come from make_voicepack."""
    peak = np.abs(x).max()
    if peak <= 0 or len(x) < int(sr * 0.05):
        return None
    y = x / peak * 0.7  # the pitch tracker gates on absolute level
    hop = int(sr * frame)
    n = len(y) // hop
    if n < 3:
        return None
    env = np.sqrt(np.mean(y[: n * hop].reshape(n, hop) ** 2, axis=1) + 1e-12)
    active = env > env.max() * 10 ** (-30 / 20)

    f0, _ = yin(y, sr, hop)
    loud = env > env.max() * 10 ** (-20 / 20)  # voicing judged on the clearly audible part
    m = min(len(f0), len(loud))
    f0a = f0[:m][loud[:m]] if m else np.array([])
    voiced_f0 = f0a[~np.isnan(f0a)]
    voiced = len(voiced_f0) / max(1, len(f0a))
    f0_st = float(np.median(_semitones(voiced_f0))) if len(voiced_f0) >= 3 else np.nan
    f0_spread = float(np.subtract(*np.percentile(_semitones(voiced_f0), [90, 10]))) if len(voiced_f0) >= 5 else 0.0

    # loudness of the active part, at the recording's own gain (screams are loud)
    xa = x[: n * hop].reshape(n, hop)[active]
    level = 20 * np.log10(np.sqrt(np.mean(xa ** 2)) + 1e-9)

    # spectrum of the active part
    win = 1024
    spec = np.zeros(win // 2 + 1)
    frames = 0
    for s in range(0, len(y) - win, win // 2):
        if env[min(n - 1, s // hop)] < env.max() * 10 ** (-30 / 20):
            continue
        spec += np.abs(np.fft.rfft(y[s:s + win] * np.hanning(win))) ** 2
        frames += 1
    if frames == 0:
        spec = np.abs(np.fft.rfft(y[:win] * np.hanning(min(win, len(y))), n=win)) ** 2
    freqs = np.fft.rfftfreq(win, 1 / sr)
    band = (freqs >= 100) & (freqs <= 5000)
    p = spec[band] + 1e-12
    bright = float(np.log2(np.sum(freqs[band] * p) / np.sum(p)))       # octaves
    flat = float(np.exp(np.mean(np.log(p))) / np.mean(p))               # 0 tonal .. 1 noise

    return {"f0": f0_st, "spread": f0_spread, "voiced": voiced, "level": level, "bright": bright, "flat": flat,
            "dur": len(x) / sr}


def _stats(feats, keys=("f0", "level", "bright", "flat", "voiced")):
    centre, scale = {}, {}
    for k in keys:
        v = np.array([f[k] for f in feats if f is not None and not np.isnan(f[k])])
        if len(v) == 0:
            centre[k], scale[k] = np.nan, FLOOR[k]
            continue
        med = float(np.median(v))
        mad = float(np.median(np.abs(v - med))) * 1.4826
        centre[k], scale[k] = med, max(mad, FLOOR[k])
    return centre, scale


def _score(f, centre, scale, keys):
    """Largest robust z over the given features (nan pitch = unvoiced = far)."""
    worst, why = 0.0, ""
    for k in keys:
        v, c = f[k], centre[k]
        if np.isnan(c):
            continue
        z = 9.0 if np.isnan(v) else abs(v - c) / scale[k]
        if z > worst:
            worst, why = z, k
    return worst, why


def _hard_reject(f, base, neutral):
    """Things no folder should contain."""
    if f is None:
        return "unmeasurable"
    if f["voiced"] < (0.55 if neutral else 0.3):
        return "mostly unvoiced (breath, hiss, click)"
    if not np.isnan(base["f0"]) and not np.isnan(f["f0"]):
        if f["f0"] - base["f0"] > 12:
            return "squeal/scream pitch"
        if f["f0"] - base["f0"] < -9:
            return "far below the voice"
    if f["level"] - base["level"] > 14:
        return "shouted"
    return None


def _filter(items, centre, scale, base, keys, limit, keep_min, neutral, spread_max=None):
    kept, dropped = [], []
    for clip, f in items:
        reason = _hard_reject(f, base, neutral)
        if reason is None and spread_max is not None and f["spread"] > spread_max:
            reason = "pitch wobbles (laugh, sob, yodel)"
        if reason is None:
            z, why = _score(f, centre, scale, keys)
            if z > limit:
                reason = f"outlier in {why} ({z:.1f})"
            else:
                kept.append((z, clip, f))
                continue
        dropped.append((reason, clip, f))
    kept.sort(key=lambda t: t[0])
    # never empty a folder: take back the least extreme of the soft rejects
    if len(kept) < keep_min:
        soft = [i for i, d in enumerate(dropped) if d[0].startswith("outlier") or d[0].startswith("pitch wobbles")]
        soft.sort(key=lambda i: _score(dropped[i][2], centre, scale, keys)[0])
        back = set(soft[: keep_min - len(kept)])
        kept += [(0.0, dropped[i][1], dropped[i][2]) for i in sorted(back)]
        dropped = [d for i, d in enumerate(dropped) if i not in back]
    return [(c, f) for _, c, f in kept], dropped


def _repitch(x, semitones):
    """Resample by a small interval (formants move with it; fine within +-2 st)."""
    if abs(semitones) < 0.05:
        return x
    ratio = 2 ** (semitones / 12.0)
    n = max(8, int(round(len(x) / ratio)))
    t = np.linspace(0, len(x) - 1, n)
    return np.interp(t, np.arange(len(x)), x)


def level(x, target_db=TARGET_RMS_DB, frame_len=441):
    n = len(x) // frame_len
    if n == 0:
        return x
    env = np.sqrt(np.mean(x[: n * frame_len].reshape(n, frame_len) ** 2, axis=1) + 1e-12)
    act = env > env.max() * 10 ** (-30 / 20)
    rms = np.sqrt(np.mean(env[act] ** 2)) if act.any() else env.max()
    y = x * (10 ** (target_db / 20) / (rms + 1e-12))
    pk = np.abs(y).max()
    return y * (PEAK_CAP / pk) if pk > PEAK_CAP else y


def run(buckets, syllables, sr, f0_track, frame, max_syllables=40, log=print):
    """buckets: folder -> [clip]; syllables: [(clip, source_kind)] with source
    None for everyday sources. Returns (buckets, syllables) cleaned, pitch
    nudged where it should be, still un-levelled (call level() when writing)."""
    feats = {k: [(c, measure(c, sr, f0_track, frame)) for c in v] for k, v in buckets.items()}
    syl = [(c, src, measure(c, sr, f0_track, frame)) for c, src in syllables]

    # the speaker's calm baseline: everyday takes, else calm syllables, else everything
    calm = [f for k in NEUTRAL_FOLDERS for _, f in feats.get(k, []) if f is not None]
    if len(calm) < 4:
        calm += [f for _, s, f in syl if s is None and f is not None]
    if len(calm) < 4:
        calm = [f for v in feats.values() for _, f in v if f is not None]
    base, base_scale = _stats(calm)
    log(f"  baseline: pitch {100 * 2 ** (base['f0'] / 12):.0f} Hz, spread {base_scale['f0']:.1f} st, "
        f"from {len(calm)} everyday takes")
    keys = ("f0", "level", "bright", "flat", "voiced")

    out = {}
    for k, items in feats.items():
        if not items:
            out[k] = []
            continue
        if k in NEUTRAL_FOLDERS:
            # held to the calm baseline; questions may rise, so no wobble check there
            kept, dropped = _filter(items, base, base_scale, base, keys, 2.6, min(3, len(items)), True,
                                    None if k == "question" else 7.0)
            kept = [(_repitch(c, -0.7 * np.clip(f["f0"] - base["f0"], -2.8, 2.8)) if not np.isnan(f["f0"]) else c, f)
                    for c, f in kept]
        else:
            # judged against the rest of its own folder; small folders borrow the
            # speaker's spread (widened) so 3 clips can't vote each other out
            centre, scale = _stats([f for _, f in items])
            if len(items) < 6:
                scale = {kk: max(scale[kk], base_scale[kk] * 1.6) for kk in scale}
            kept, dropped = _filter(items, centre, scale, base, keys, 2.6, min(3, len(items)), False)
        out[k] = [c for c, _ in kept]
        if dropped:
            log(f"  {k:9s} kept {len(kept):2d}/{len(items):2d}; dropped: " +
                "; ".join(sorted({d[0].split(' (')[0] for d in dropped})))
        else:
            log(f"  {k:9s} kept {len(kept):2d}/{len(items):2d}")

    # babble: calm sources only, tight; mild emotions top up a short supply
    def pick(source_ok, limit, keep_min=0):
        items = [(c, f) for c, s, f in syl if source_ok(s)]
        kept, _ = _filter(items, base, base_scale, base, keys, limit, keep_min, True, 6.0)
        return kept
    chosen = pick(lambda s: s is None or s == "syllables", 2.2)
    if len(chosen) < 16:  # a thin speaker: loosen a step before borrowing from emotions
        chosen = pick(lambda s: s is None or s == "syllables", 2.8)
    if len(chosen) < 16:
        extra = pick(lambda s: s in MILD_SOURCES, 1.8)
        chosen += extra[: 16 - len(chosen)]
    if len(chosen) < 16:  # still short: the least extreme calm syllables, closest first
        chosen = pick(lambda s: s is None or s == "syllables", 2.8, keep_min=16)
    chosen = chosen[:max_syllables]
    syl_out = [_repitch(c, -0.8 * np.clip(f["f0"] - base["f0"], -3.0, 3.0)) if not np.isnan(f["f0"]) else c
               for c, f in chosen]
    log(f"  syllables kept {len(syl_out):2d}/{len(syl):2d} (calm sources first, held to the baseline)")
    return out, syl_out
