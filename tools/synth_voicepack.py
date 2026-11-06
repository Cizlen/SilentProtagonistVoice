#!/usr/bin/env python3
"""
Synthesises a generic "silent protagonist" voice pack: wordless hums, murmurs
and grunt-like syllables, in the spirit of older RPGs that never voiced the
hero. Output is 16-bit PCM mono WAV laid out the way the plugin expects:

    <out>/<PackName>/voice.ini
    <out>/<PackName>/syllables/*.wav   babble blips
    <out>/<PackName>/short|medium|long/*.wav   whole phrases by length
    <out>/<PackName>/question|exclaim/*.wav

Source-filter model: band-limited glottal pulse train (with jitter, shimmer,
a little vibrato and breath noise) through Klatt-style formant resonators that
glide between vowel targets.

    python synth_voicepack.py --out "<Data>/Sound/FX/SilentProtagonistVoice/voices"
    python synth_voicepack.py --out voices --name MyVoice --f0 150 --breath 0.08

Needs numpy and scipy.
"""
import argparse
import math
import os
import wave

import numpy as np
from scipy.signal import butter, lfilter

SR = 44100

# (F1, F2, F3, F4) in Hz; 'm'/'n' are nasal murmurs (lips/tongue closed)
VOWELS = {
    "a": (750, 1220, 2600, 3500),
    "e": (530, 1840, 2480, 3500),
    "i": (390, 2000, 2550, 3500),
    "o": (500, 860, 2450, 3400),
    "u": (380, 950, 2300, 3300),
    "@": (580, 1300, 2500, 3500),  # schwa, "uh"
    "m": (260, 1100, 2300, 3200),
    "n": (280, 1600, 2500, 3300),
}
BANDWIDTHS = (80, 100, 140, 200)
NASAL_BW = (60, 300, 400, 500)


def resonator_coeffs(f, bw):
    # Klatt (1980) digital resonator, unity gain at DC
    T = 1.0 / SR
    C = -math.exp(-2 * math.pi * bw * T)
    B = 2 * math.exp(-math.pi * bw * T) * math.cos(2 * math.pi * f * T)
    A = 1 - B - C
    return [A], [1, -B, -C]


def glottal_source(f0_curve, rng, jitter=0.006, shimmer=0.04, tilt=1.0):
    """Additive band-limited pulse train following f0_curve (Hz per sample)."""
    n = len(f0_curve)
    # jitter: slow random f0 wobble
    wob = rng.standard_normal(n // 200 + 2)
    wob = np.interp(np.arange(n), np.linspace(0, n, len(wob)), wob)
    f0 = f0_curve * (1 + jitter * wob)
    phase = 2 * np.pi * np.cumsum(f0) / SR
    out = np.zeros(n)
    max_h = int(5500 / max(60.0, float(f0.min())))
    for h in range(1, max_h + 1):
        amp = 1.0 / (h ** (1.0 + 0.35 * tilt))
        # drop harmonics above Nyquist-ish smoothly
        mask = np.clip((5500 - h * f0) / 800.0, 0, 1)
        out += amp * mask * np.sin(h * phase)
    shim = rng.standard_normal(n // 300 + 2)
    shim = np.interp(np.arange(n), np.linspace(0, n, len(shim)), shim)
    out *= 1 + shimmer * shim
    return out


def formant_filter(src, targets, nasal_flags, block=220):
    """Time-varying cascade of 4 resonators; targets is an (n,4) array of formants."""
    n = len(src)
    out = np.zeros(n)
    states = [np.zeros(2) for _ in range(4)]
    for start in range(0, n, block):
        end = min(n, start + block)
        seg = src[start:end]
        mid = (start + end) // 2
        fset = targets[mid]
        bws = NASAL_BW if nasal_flags[mid] else BANDWIDTHS
        for k in range(4):
            b, a = resonator_coeffs(fset[k], bws[k])
            seg, states[k] = lfilter(b, a, seg, zi=states[k])
        out[start:end] = seg
    return out


def envelope(n, attack, release, shape=1.0):
    t = np.arange(n) / SR
    dur = n / SR
    env = np.ones(n)
    a = np.clip(t / max(attack, 1e-3), 0, 1)
    r = np.clip((dur - t) / max(release, 1e-3), 0, 1)
    env = np.minimum(a, r) ** shape
    return 0.5 - 0.5 * np.cos(np.pi * env)  # raised-cosine edges


def syllable(rng, f0_start, f0_end, dur, onset, vowel, coda=None, breath=0.05, loud=1.0, vib_depth=0.008, vib_hz=5.2):
    """One syllable: optional consonant-ish onset (m/n/h), vowel, optional nasal coda."""
    n = int(dur * SR)
    t = np.linspace(0, 1, n)
    # f0: start->end with a slight arch
    f0 = f0_start + (f0_end - f0_start) * t + 0.04 * f0_start * np.sin(np.pi * t)
    vib = 1 + vib_depth * np.sin(2 * np.pi * vib_hz * np.arange(n) / SR + rng.uniform(0, 6.28))
    src = glottal_source(f0 * vib, rng)

    v = np.array(VOWELS[vowel], dtype=float)
    targets = np.tile(v, (n, 1))
    nasal = np.zeros(n, dtype=bool)
    on_len = 0
    if onset in ("m", "n"):
        on_len = int(n * rng.uniform(0.18, 0.3))
        m = np.array(VOWELS[onset], dtype=float)
        ramp = np.clip(np.arange(n) / max(1, on_len), 0, 1)[:, None]
        targets = m * (1 - ramp) + v * ramp
        nasal[: int(on_len * 0.8)] = True
    if coda in ("m", "n"):
        c_len = int(n * rng.uniform(0.2, 0.35))
        c = np.array(VOWELS[coda], dtype=float)
        ramp = np.clip((np.arange(n) - (n - c_len)) / max(1, c_len), 0, 1)[:, None]
        targets = targets * (1 - ramp) + c * ramp
        nasal[n - int(c_len * 0.7):] = True

    voiced = formant_filter(src, targets, nasal)

    # breath noise, shaped by the same envelope, a bit brighter
    noise = rng.standard_normal(n)
    noise = lfilter([1, -0.85], [1], noise)
    noise = formant_filter(noise * 0.6, targets, nasal)
    sig = voiced + breath * 6.0 * noise

    if onset == "h":
        h_len = int(n * rng.uniform(0.12, 0.2))
        h = rng.standard_normal(n) * np.clip(1 - np.arange(n) / max(1, h_len), 0, 1) ** 2
        h = lfilter(*resonator_coeffs(1600, 900), h)
        sig = sig * np.clip(np.arange(n) / max(1, h_len), 0.2, 1) + 0.3 * h / (np.abs(h).max() + 1e-9) * np.abs(voiced).max()

    # nasal parts are quieter
    sig *= np.where(nasal, 0.55, 1.0)
    sig *= envelope(n, attack=0.012 if onset != "m" else 0.03, release=0.05) * loud
    return sig


def phrase(rng, base_f0, n_syl, contour, breath, speed=1.0):
    """Several syllables with a prosodic contour: 'fall', 'rise', 'exclaim', 'flat'."""
    parts = []
    for i in range(n_syl):
        x = i / max(1, n_syl - 1)
        if contour == "rise":
            f = base_f0 * (0.97 + 0.05 * x + (0.25 if i == n_syl - 1 else 0))
            fe = f * (1.18 if i == n_syl - 1 else 1.02)
        elif contour == "exclaim":
            f = base_f0 * (1.18 - 0.12 * x)
            fe = f * 0.9
        elif contour == "flat":
            f = base_f0 * rng.uniform(0.97, 1.03)
            fe = f * 0.99
        else:  # fall
            f = base_f0 * (1.06 - 0.12 * x) * rng.uniform(0.96, 1.05)
            fe = f * (0.82 if i == n_syl - 1 else 0.97)
        accent = 1.15 if (i == 0 or rng.random() < 0.25) else 1.0
        dur = rng.uniform(0.11, 0.2) / speed
        if i == n_syl - 1:
            dur *= 1.5
        onset = rng.choice(["m", "h", "n", None, None], p=[0.3, 0.2, 0.15, 0.2, 0.15])
        vowel = rng.choice(list("aeiou@"), p=[0.2, 0.2, 0.12, 0.16, 0.1, 0.22])
        coda = "m" if (i == n_syl - 1 and rng.random() < 0.4) else None
        loud = accent * (1.2 if contour == "exclaim" else 1.0)
        parts.append(syllable(rng, f, fe, dur, onset, vowel, coda, breath, loud))
        gap = rng.uniform(0.0, 0.035) / speed
        if rng.random() < 0.15 and i < n_syl - 1:
            gap += 0.08 / speed  # an occasional little breath
        parts.append(np.zeros(int(gap * SR)))
    return np.concatenate(parts)


def finish(sig, peak=0.7):
    # warm it up: hums live below ~3.5 kHz, keep a little air above
    b, a = butter(2, 3400 / (SR / 2))
    sig = 0.85 * lfilter(b, a, sig) + 0.15 * sig
    sig = sig - np.mean(sig)
    m = np.abs(sig).max()
    if m > 0:
        sig = sig / m * peak
    pad = np.zeros(int(0.01 * SR))
    return np.concatenate([pad, sig, pad])


def write_wav(path, sig):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    data = np.clip(sig * 32767, -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(data.tobytes())


def make_pack(out_dir, name, f0, breath, sex, seed, description):
    rng = np.random.default_rng(seed)
    root = os.path.join(out_dir, name)

    # babble blips: short one-syllable murmurs
    combos = [("m", "a"), ("m", "e"), ("m", "o"), ("h", "a"), ("h", "e"), ("h", "@"), ("n", "a"), ("n", "e"),
              (None, "@"), (None, "a"), (None, "o"), ("m", "@"), ("h", "o"), ("n", "o"), ("m", "i"), (None, "e")]
    for i, (onset, v) in enumerate(combos):
        f = f0 * rng.uniform(0.95, 1.06)
        sig = syllable(rng, f, f * rng.uniform(0.93, 1.0), rng.uniform(0.11, 0.16), onset, v, None, breath)
        write_wav(os.path.join(root, "syllables", f"syl_{i + 1:02d}.wav"), finish(sig, 0.65))

    # whole phrases
    def batch(folder, count, syl_range, contour, peak=0.7, speed=1.0):
        for i in range(count):
            n = int(rng.integers(syl_range[0], syl_range[1] + 1))
            sig = phrase(rng, f0, n, contour, breath, speed)
            write_wav(os.path.join(root, folder, f"{folder}_{i + 1:02d}.wav"), finish(sig, peak))

    # short: classic acknowledgement hums ("hm.", "mm-hm", "uh-huh")
    for i in range(6):
        kind = i % 3
        if kind == 0:  # "hm."
            sig = syllable(rng, f0 * 1.05, f0 * 0.85, rng.uniform(0.28, 0.36), "h", "@", "m", breath)
        elif kind == 1:  # "mm-hm"
            a = syllable(rng, f0 * 0.98, f0 * 1.0, 0.2, "m", "@", "m", breath)
            b = syllable(rng, f0 * 1.12, f0 * 0.9, 0.26, "h", "@", "m", breath)
            sig = np.concatenate([a, np.zeros(int(0.03 * SR)), b])
        else:  # "uh-huh"
            a = syllable(rng, f0 * 1.0, f0 * 0.98, 0.16, None, "@", None, breath)
            b = syllable(rng, f0 * 1.14, f0 * 0.92, 0.24, "h", "@", None, breath)
            sig = np.concatenate([a, np.zeros(int(0.04 * SR)), b])
        write_wav(os.path.join(root, "short", f"short_{i + 1:02d}.wav"), finish(sig))

    batch("medium", 6, (3, 5), "fall")
    batch("long", 5, (6, 9), "fall", speed=1.05)
    batch("question", 5, (1, 4), "rise")
    batch("exclaim", 4, (1, 3), "exclaim", peak=0.78)

    # emotional reactions, one folder per Skyrim dialogue emotion
    gap = lambda sec: np.zeros(int(sec * SR))
    for i in range(3):
        j = rng.uniform(0.96, 1.04)
        # anger: a clipped, low "hmph" / "hrm!"
        a = syllable(rng, f0 * 0.9 * j, f0 * 0.78 * j, rng.uniform(0.16, 0.24), "h", "@", "m", breath * 0.5, 1.25)
        write_wav(os.path.join(root, "anger", f"anger_{i + 1:02d}.wav"), finish(a, 0.78))
        # disgust: a slow, falling "eugh"
        d = syllable(rng, f0 * 0.95 * j, f0 * 0.72 * j, rng.uniform(0.32, 0.42), None, "@", "n", breath * 1.5, 1.0)
        write_wav(os.path.join(root, "disgust", f"disgust_{i + 1:02d}.wav"), finish(d))
        # fear: a shaky, breathy "uh-uh"
        fa = syllable(rng, f0 * 1.2 * j, f0 * 1.15 * j, 0.14, None, "@", None, breath * 3, 0.9, 0.035, 7.5)
        fb = syllable(rng, f0 * 1.28 * j, f0 * 1.18 * j, 0.2, "h", "@", None, breath * 3, 0.85, 0.04, 7.5)
        write_wav(os.path.join(root, "fear", f"fear_{i + 1:02d}.wav"), finish(np.concatenate([fa, gap(0.05), fb]), 0.62))
        # sad: a long, falling sigh-hum
        sd = syllable(rng, f0 * 1.0 * j, f0 * 0.74 * j, rng.uniform(0.55, 0.7), "h", "o", "m", breath * 3.5, 0.8)
        write_wav(os.path.join(root, "sad", f"sad_{i + 1:02d}.wav"), finish(sd, 0.6))
        # happy: a bright, bouncing "mm-hm!"
        ha = syllable(rng, f0 * 1.08 * j, f0 * 1.18 * j, 0.15, "m", "e", None, breath, 1.0)
        hb = syllable(rng, f0 * 1.25 * j, f0 * 1.05 * j, 0.22, "h", "a", "m", breath, 1.1)
        write_wav(os.path.join(root, "happy", f"happy_{i + 1:02d}.wav"), finish(np.concatenate([ha, gap(0.02), hb])))
        # surprise: a rising "oh!"
        su = syllable(rng, f0 * 1.15 * j, f0 * 1.4 * j, rng.uniform(0.24, 0.3), None, "o", None, breath * 1.5, 1.15)
        write_wav(os.path.join(root, "surprise", f"surprise_{i + 1:02d}.wav"), finish(su, 0.75))
        # puzzled: a lifting "hm?"
        pz = syllable(rng, f0 * 0.95 * j, f0 * 1.28 * j, rng.uniform(0.28, 0.36), "h", "@", "m", breath, 1.0)
        write_wav(os.path.join(root, "puzzled", f"puzzled_{i + 1:02d}.wav"), finish(pz))

    # leave Mode commented out so the player's own setting applies
    mode_line = "; Mode=auto"
    with open(os.path.join(root, "voice.ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("[Pack]\n")
        f.write(f"Description={description}\n")
        f.write(f"Sex={sex}\n")
        f.write(f"{mode_line}\n")
        f.write("BasePitch=1.0\n")
        f.write("; PitchVariation=0.06\n; Speed=1.0\n; Gain=1.0\n; SyllablesPerBlip=1.6\n")
        f.write("; voice character offsets, added to the menu sliders:\n; Throat=0\n; Breath=0\n; Brightness=0\n; Expression=0\n")
    print("wrote", root)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="the voices folder")
    ap.add_argument("--name", help="make one custom pack with this name instead of the two defaults")
    ap.add_argument("--f0", type=float, default=140.0, help="base pitch in Hz for --name")
    ap.add_argument("--breath", type=float, default=0.05, help="breathiness for --name (0..0.2)")
    ap.add_argument("--sex", default="any", choices=["any", "male", "female"])
    ap.add_argument("--seed", type=int, default=7)
    a = ap.parse_args()

    if a.name:
        make_pack(a.out, a.name, a.f0, a.breath, a.sex, a.seed, "Synthesised hums")
    else:
        make_pack(a.out, "DefaultMale", 112.0, 0.045, "male", 11, "Synthesised low hums and murmurs")
        make_pack(a.out, "DefaultFemale", 205.0, 0.06, "female", 23, "Synthesised hums and murmurs")


if __name__ == "__main__":
    main()
