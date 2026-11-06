#!/usr/bin/env python3
"""
Builds a Silent Protagonist Voice pack from real recordings of a voice
(any folder of .wav / .fuz / .xwm / .mp3 / .ogg / .flac files).

Real speech has words in it, which is exactly what a silent protagonist must
not have. So by default every clip goes through a "murmur" low-pass: the
voice's pitch, rhythm and character survive, the words don't - it sounds like
the character talking behind a door, or humming along. Set --cutoff 0 to keep
clean audio (useful if your recordings are already wordless hums/grunts).

    python make_voicepack.py "C:\\voices\\MyCharacter" --name MyCharacter ^
        --out "<Skyrim>\\Data\\Sound\\FX\\SilentProtagonistVoice\\voices" --sex female

What it makes:
  syllables\\  one-syllable slices for babble mode
  short\\ medium\\ long\\  whole utterances sorted by length
  question\\   utterances whose pitch rises at the end (--no-questions to skip)
  anger\\ disgust\\ fear\\ sad\\ happy\\ surprise\\ puzzled\\  emotional reactions,
               sorted by keywords in the file names ("AS054715_scepticism.flac"
               -> puzzled, "..._relief.flac" -> happy). Pain/effort sounds are skipped.
  voice.ini   (Sex= guessed from the voice's pitch)

Presets:
  --preset suddendice   SuddenDice's CC0 "Audio Donation" packs from Freesound
                        (freesound.org/people/SuddenDice/packs/). Put any number
                        of their downloaded packs in one folder: you get one voice
                        pack per speaker (AS054715 -> SuddenDice_AS054715), clean
                        audio (they're already wordless), emotions sorted.

    python make_voicepack.py "C:\\Downloads\\SuddenDice" --preset suddendice ^
        --out "<Skyrim>\\Data\\Sound\\FX\\SilentProtagonistVoice\\voices"

Needs numpy and scipy. Non-WAV input needs ffmpeg on PATH (.fuz is unpacked
to its xWMA stream first).
"""
import argparse
import os
import random
import re
import shutil
import struct
import subprocess
import tempfile
import wave

import sys

import numpy as np
from scipy.signal import butter, resample_poly, sosfiltfilt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import neutralize  # noqa: E402  (sits next to this script)

SR = 44100
FRAME = 0.01  # analysis hop, seconds


# --------------------------------------------------------------------- input

def read_wav(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768
    elif width == 1:
        x = (np.frombuffer(raw, np.uint8).astype(np.float64) - 128) / 128
    elif width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        i = (b[:, 0].astype(np.int32) << 8 | b[:, 1].astype(np.int32) << 16 | b[:, 2].astype(np.int32) << 24) >> 8
        x = i / 8388608.0
    elif width == 4:
        x = np.frombuffer(raw, "<i4").astype(np.float64) / 2147483648.0
    else:
        raise ValueError(f"{width * 8}-bit wav")
    x = x.reshape(-1, ch).mean(axis=1)
    if rate != SR:
        g = np.gcd(rate, SR)
        x = resample_poly(x, SR // g, rate // g)
    return x


def decode_with_ffmpeg(path, tmpdir):
    if not shutil.which("ffmpeg"):
        raise RuntimeError("ffmpeg not found on PATH (needed for non-WAV input)")
    src = path
    if path.lower().endswith(".fuz"):
        with open(path, "rb") as f:
            data = f.read()
        if data[:4] != b"FUZE":
            raise ValueError("not a FUZE file")
        lip_size = struct.unpack_from("<I", data, 8)[0]
        src = os.path.join(tmpdir, "fuz_audio.xwm")
        with open(src, "wb") as f:
            f.write(data[12 + lip_size:])
    out = os.path.join(tmpdir, "decoded.wav")
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-ac", "1", "-ar", str(SR), "-c:a", "pcm_s16le", out], check=True)
    return read_wav(out)


def load(path, tmpdir):
    if path.lower().endswith(".wav"):
        try:
            return read_wav(path)
        except Exception:
            pass  # odd wav flavours (ADPCM, float): let ffmpeg try
    return decode_with_ffmpeg(path, tmpdir)


# ------------------------------------------------------------------ analysis

def rms_frames(x):
    hop = int(SR * FRAME)
    n = len(x) // hop
    if n == 0:
        return np.zeros(0)
    return np.sqrt(np.mean(x[: n * hop].reshape(n, hop) ** 2, axis=1) + 1e-12)


def voiced_regions(env, thresh, min_gap, min_len):
    on = env > thresh
    regions, start, gap = [], None, 0
    for i, v in enumerate(on):
        if v:
            if start is None:
                start = i
            gap = 0
        elif start is not None:
            gap += 1
            if gap >= min_gap:
                end = i - gap + 1
                if end - start >= min_len:
                    regions.append((start, end))
                start, gap = None, 0
    if start is not None and len(on) - start >= min_len:
        regions.append((start, len(on)))
    return regions


def f0_track(x):
    """Very small autocorrelation pitch tracker: one f0 (or nan) per 10 ms."""
    hop, win = int(SR * FRAME), int(SR * 0.04)
    lo, hi = int(SR / 400), int(SR / 70)
    out = []
    for s in range(0, len(x) - win, hop):
        seg = x[s:s + win] - np.mean(x[s:s + win])
        if np.sqrt(np.mean(seg ** 2)) < 0.02:
            out.append(np.nan)
            continue
        ac = np.correlate(seg, seg, "full")[win - 1:]
        ac /= ac[0] + 1e-12
        lag = lo + int(np.argmax(ac[lo:hi]))
        out.append(SR / lag if ac[lag] > 0.45 else np.nan)
    return np.array(out)


def rises(x):
    f = f0_track(x)
    f = f[~np.isnan(f)]
    if len(f) < 12:
        return False
    tail = np.median(f[-max(4, len(f) // 5):])
    body = np.median(f[: max(4, len(f) * 3 // 5)])
    return tail > body * 1.12


def murmur(x, cutoff):
    if cutoff <= 0:
        return x
    sos = butter(4, cutoff / (SR / 2), output="sos")
    y = sosfiltfilt(sos, x)
    # a little of the 1-2 kHz band back so it stays a voice, not a rumble
    sos_b = butter(2, [cutoff / (SR / 2), min(0.99, 2.2 * cutoff / (SR / 2))], btype="band", output="sos")
    return y + 0.12 * sosfiltfilt(sos_b, x)


def fade(x, ms=10):
    n = min(len(x) // 2, int(SR * ms / 1000))
    if n > 0:
        r = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
        x = x.copy()
        x[:n] *= r
        x[-n:] *= r[::-1]
    return x


MAX_TAKE = 4.0     # seconds; longer takes are cut with a slow fade-out
TAIL_MAX = 0.9     # how far past the loud part a take may ring out (decay, trailing breath)
HEAD_MAX = 0.35    # how far before it a take may start (the breath in)
ABOVE_FLOOR = 8.0  # dB over the recording's noise floor that still counts as sound


def tail_fade(x, ms=250):
    n = min(len(x) // 2, int(SR * ms / 1000))
    if n > 0:
        x = x.copy()
        x[-n:] *= 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, n))
    return x


def blend_window(x, pre, post, amount):
    """Raised-cosine in and out: over the added margins plus a share of the
    syllable itself, so the peak stays where it was and the edges melt."""
    n = len(x)
    body = max(1, n - pre - post)
    a = min(n // 2, pre + int(0.30 * amount * body))
    r = min(n - a, post + int(0.40 * amount * body))
    w = np.ones(n)
    if a > 0:
        w[:a] = 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, a))
    if r > 0:
        w[n - r:] = 0.5 + 0.5 * np.cos(np.linspace(0, np.pi, r))
    return x * w


def head_fade(x, ms=10):
    n = min(len(x) // 2, int(SR * ms / 1000))
    if n > 0:
        x = x.copy()
        x[:n] *= 0.5 - 0.5 * np.cos(np.linspace(0, np.pi, n))
    return x


def take(x, env, s0, e0, prev_end, next_start, floor):
    """One take, from its breath in to the end of its decay. The loud part
    (s0..e0, in frames) is found against the file's peak; the edges are
    followed out until the sound really reaches the noise floor, without
    running into the neighbouring takes."""
    hop = int(SR * FRAME)
    quiet = max(floor * 10 ** (ABOVE_FLOOR / 20), env.max() * 10 ** (-60 / 20))
    lo = max(prev_end + 5, s0 - int(HEAD_MAX / FRAME))
    a = s0
    while a > lo and env[a - 1] > quiet:
        a -= 1
    hi = min(next_start - 10, e0 + int(TAIL_MAX / FRAME), len(env))
    b = e0
    while b < hi and env[b] > quiet:
        b += 1
    a = max(0, a - 3)                 # 30 ms of room before the first sound
    b = min(len(env), b + 6)          # 60 ms of room after the last
    seg = x[a * hop: b * hop]
    if len(seg) > MAX_TAKE * SR:
        return tail_fade(seg[: int(MAX_TAKE * SR)], 400)
    # fade over the quiet ending we just kept, so the decay itself stays intact
    tail_ms = max(40, min(150, (b - e0) * FRAME * 1000 * 0.5))
    return seg if tail_ms <= 0 else tail_fade(seg, tail_ms)


def normalise(x, peak=0.7):
    x = x - np.mean(x)
    m = np.abs(x).max()
    return x / m * peak if m > 0 else x


def write(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(np.clip(x * 32767, -32768, 32767).astype("<i2").tobytes())


# ---------------------------------------------------------------- keywords

# folder -> keywords found in file names. Long stems match anywhere in the name,
# short words (<= 3 letters) only as whole words.
EMOTION_KEYWORDS = {
    "anger": ["anger", "angry", "rage", "fury", "furious", "annoy", "irritat", "frustrat", "hate", "hatred", "wrath",
              "aggress", "threat", "grumpy", "outrage", "indignat"],
    "disgust": ["disgust", "disdain", "contempt", "revuls", "gross", "ugh", "envy", "envious", "scorn", "sneer", "jealous",
                "loath", "boo"],
    "fear": ["fear", "afraid", "scared", "terror", "panic", "anxi", "nervous", "dread", "horror", "fright", "worry", "worried"],
    "sad": ["sad", "sadness", "grief", "low spirits", "low-spirits", "nostalg", "boredom", "bored", "empath", "griev", "despair", "sorrow", "shame", "pity", "regret", "disappoint", "melanchol", "sob", "sigh",
            "lonel", "guilt", "mourn", "hopeless"],
    "happy": ["happy", "joy", "satisf", "relief", "relieved", "friendship", "love", "amuse", "laugh", "pride", "proud",
              "content", "delight", "glad", "pleased", "triumph", "giggle", "chuckle", "affection",
              "adoration", "admiration", "admire", "kindness", "calm", "appreciat", "warm"],
    "surprise": ["surprise", "realization", "realisation", "astonish", "amaz", "shock", "wow", "awe", "gasp", "startle",
                 "entranc", "fascinat"],
    "puzzled": ["confus", "sceptic", "skeptic", "doubt", "puzzl", "curious", "curiosity", "wonder", "uncertain", "hesitat",
                "ponder", "suspic", "huh",
                "reflect", "awkward", "thinking", "pensive"],
    "exclaim": ["excite", "enthusias", "cheer", "hooray", "eager", "anticipat"],
}
# Everyday, low-emotion sounds. Their takes fill the ordinary short/medium/long
# folders (and the babble syllables), so a plain line never borrows a sob or a growl.
NEUTRAL_KEYWORDS = ["yeah", "yes", "agree", "affirm", "okay", "ok", "nod", "hmm", "mhm", "acknowledg", "uh huh", "uh-huh",
                    "sure", "reflect", "calm", "kind", "friend", "satisf", "content", "ponder", "thinking", "neutral",
                    "interest"]
# Fine as raw material for syllables, wrong as a whole reply to an arbitrary line.
SYLLABLE_ONLY_KEYWORDS = ["negat", "no no", "nope"]
# checked before the skip list: names that contain a skip word but are still dialogue
# ("empathetic pain" is a sympathetic "aww", not someone getting hurt)
KEEP_KEYWORDS = ["empath"]
# not dialogue: never used, not even for babble syllables
SKIP_KEYWORDS = ["pain", "hurt", "exert", "exortion", "exertion", "endurance", "physical", "strength", "feats of", "feat of", "effort", "cough", "sneeze",
                 "burp", "death", "dying", "damage", "scream", "craving", "hungry", "eating", "yawn", "snore"]

PRESETS = {
    "suddendice": {"cutoff": 0.0, "neutralize": True, "group": r"(?:^|[_\s-])(as\d{4,})(?=[_\s.-]|$)", "prefix": "SuddenDice_"},
}


def keyword_hit(name, words):
    for w in words:
        if len(w) <= 3:
            if re.search(r"(?<![a-z])" + re.escape(w) + r"(?![a-z])", name):
                return True
        elif w in name:
            return True
    return False


def _name(path):
    return os.path.splitext(os.path.basename(path))[0].lower().replace("_", " ")


def classify(path):
    """'skip', 'syllables', an emotion folder name, or None (sort by length)."""
    name = _name(path)
    if keyword_hit(name, SKIP_KEYWORDS) and not keyword_hit(name, KEEP_KEYWORDS):
        return "skip"
    if keyword_hit(name, SYLLABLE_ONLY_KEYWORDS):
        return "syllables"
    if keyword_hit(name, NEUTRAL_KEYWORDS):
        return None
    for folder, words in EMOTION_KEYWORDS.items():
        if keyword_hit(name, words):
            return folder
    return None


def is_neutral(path):
    return keyword_hit(_name(path), NEUTRAL_KEYWORDS)


def median_f0(clips):
    vals = []
    for c in clips[:12]:
        f = f0_track(c)
        vals.extend(f[~np.isnan(f)].tolist())
    return float(np.median(vals)) if vals else 0.0


# ---------------------------------------------------------------------- main

def build_pack(files, root, a, tmp):
    """Everything from one speaker -> one voice pack folder."""
    buckets = {k: [] for k in ("short", "medium", "long", "question", "exclaim",
                               "anger", "disgust", "fear", "sad", "happy", "surprise", "puzzled")}
    syllables, calm_syllables, voiced, tagged = [], [], [], []
    hop = int(SR * FRAME)

    for i, path in enumerate(files):
        kind = classify(path) if a.keywords else None
        if kind == "skip":
            print(f"  skip (not dialogue) {os.path.basename(path)}")
            continue
        try:
            x = load(path, tmp)
        except Exception as e:
            print(f"  skip {os.path.basename(path)}: {e}")
            continue
        env = rms_frames(x)
        if len(env) == 0 or env.max() < 1e-4:
            continue
        thresh = env.max() * 10 ** (-30 / 20)
        regions = voiced_regions(env, thresh, min_gap=25, min_len=15)
        if not regions:
            continue

        # One file often holds several takes separated by pauses: every take is
        # its own clip. A named reaction goes to its emotion folder; anything
        # else (or an everyday sound) is sorted by length.
        floor = np.percentile(env, 10)
        for j, (s0, e0) in enumerate(regions):
            if (e0 - s0) * FRAME < 0.2:
                continue  # a click or a breath, not a take
            prev_end = regions[j - 1][1] if j > 0 else 0
            next_start = regions[j + 1][0] if j + 1 < len(regions) else len(env)
            seg = take(x, env, s0, e0, prev_end, next_start, floor)
            dur = len(seg) / SR
            if kind == "syllables":
                pass
            elif kind is not None:
                buckets[kind].append(seg)
            else:
                k = "short" if dur < 0.7 else "medium" if dur < 1.8 else "long"
                if not a.no_questions and k != "long" and rises(seg):
                    k = "question"
                buckets[k].append(seg)
            voiced.append(seg)

        # syllables: split voiced stretches at envelope dips
        sm = np.convolve(env, np.ones(3) / 3, "same")
        for s0, e0 in voiced_regions(env, thresh * 2, min_gap=4, min_len=8):
            cuts = [s0]
            for k in range(s0 + 9, e0 - 9):
                if sm[k] < sm[k - 1] and sm[k] <= sm[k + 1] and sm[k] < 0.6 * sm[cuts[-1]:k + 1].max() and k - cuts[-1] >= 9:
                    cuts.append(k)
            cuts.append(e0)
            for c0, c1 in zip(cuts, cuts[1:]):
                if 9 <= c1 - c0 <= 26:  # 90..260 ms
                    end = c1
                    if c1 == e0:
                        # the last syllable before a pause ends on its own: keep its release
                        quiet = max(floor * 10 ** (ABOVE_FLOOR / 20), env.max() * 10 ** (-60 / 20))
                        lim = min(len(env), c1 + 6)  # up to 60 ms, so babble keeps its pace
                        while end < lim and env[end] > quiet:
                            end += 1
                    if a.blend > 0:
                        # keep a little of the real lead-in and trail-off around the
                        # syllable and taper into it, so overlapped blips run into each
                        # other instead of starting and stopping like keys on a keyboard
                        pre = int(round(a.blend * 2))   # up to 20 ms before
                        post = int(round(a.blend * 4))  # up to 40 ms after
                        s0_, e1_ = max(0, c0 - pre), min(len(env), end + post)
                        seg = blend_window(x[s0_ * hop: e1_ * hop], (c0 - s0_) * hop, (e1_ - end) * hop, a.blend)
                    else:
                        seg = x[c0 * hop: end * hop]
                    (calm_syllables if kind in (None, "syllables") else syllables).append(seg)
                    tagged.append((seg, kind))
        print(f"  [{i + 1}/{len(files)}] {os.path.basename(path)}" + (f" -> {kind}" if kind else ""))

    if not voiced:
        print("  nothing usable")
        return

    if os.path.isdir(root):
        for folder in ["syllables"] + list(buckets):
            shutil.rmtree(os.path.join(root, folder), ignore_errors=True)

    # babble from everyday sounds when there's enough of it; emotional takes
    # (sobs, growls) only top it up
    if a.neutralize:
        # one person, one mood: outliers out, babble held to the calm baseline,
        # everything levelled to the same loudness (see neutralize.py)
        for kind in buckets:
            random.shuffle(buckets[kind])
        random.shuffle(tagged)
        buckets, syllables = neutralize.run(buckets, tagged, SR, f0_track, FRAME, a.max_syllables)
        finish_syl = lambda c: neutralize.level(tail_fade(head_fade(murmur(c, a.cutoff), 6), 30), neutralize.TARGET_RMS_DB - 1)
        finish = lambda c: neutralize.level(tail_fade(head_fade(murmur(c, a.cutoff), 8), 20))
    else:
        random.shuffle(calm_syllables)
        random.shuffle(syllables)
        syllables = calm_syllables + (syllables if len(calm_syllables) < 24 else [])
        for kind in buckets:
            random.shuffle(buckets[kind])
        finish_syl = lambda c: normalise(tail_fade(head_fade(murmur(c, a.cutoff), 6), 30), 0.65)
        finish = lambda c: normalise(tail_fade(head_fade(murmur(c, a.cutoff), 8), 20))
    for i, s_ in enumerate(syllables[: a.max_syllables]):
        write(os.path.join(root, "syllables", f"syl_{i + 1:02d}.wav"), finish_syl(s_))
    for kind, clips in buckets.items():
        for i, c in enumerate(clips[: a.max_per_folder]):
            write(os.path.join(root, kind, f"{kind}_{i + 1:02d}.wav"), finish(c))

    sex = a.sex
    if sex == "any":
        f0 = median_f0(voiced)
        sex = "any" if f0 <= 0 else ("male" if f0 < 165 else "female")
        print(f"  median pitch {f0:.0f} Hz -> Sex={sex}")
    with open(os.path.join(root, "voice.ini"), "w", encoding="utf-8", newline="\r\n") as f:
        f.write("[Pack]\n")
        f.write(f"Description=Made from {len(files)} recordings with make_voicepack.py\n")
        f.write(f"Sex={sex}\n")
        if a.blend > 0:
            # soft-edged syllables only flow if they overlap; a slower pace keeps the
            # line about as long as hard-cut babble
            f.write("; Mode=auto\nBasePitch=1.0\n; PitchVariation=0.06\n")
            f.write(f"Speed={1.0 - 0.1 * a.blend:.2f}\n; Gain=1.0\n; SyllablesPerBlip=1.6\n")
        else:
            f.write("; Mode=auto\nBasePitch=1.0\n; PitchVariation=0.06\n; Speed=1.0\n; Gain=1.0\n; SyllablesPerBlip=1.6\n")
        f.write("; voice character offsets, added to the menu sliders:\n; Throat=0\n; Breath=0\n; Brightness=0\n; Expression=0\n")

    print(f"  -> {root}")
    print("     syllables " + str(min(len(syllables), a.max_syllables)) + ", " +
          ", ".join(f"{k} {min(len(v), a.max_per_folder)}" for k, v in buckets.items() if v))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("input", help="folder with recordings (searched recursively)")
    ap.add_argument("--out", required=True, help="the voices folder")
    ap.add_argument("--name", help="pack (folder) name, no spaces recommended (required unless the preset groups by speaker)")
    ap.add_argument("--preset", choices=sorted(PRESETS), help="settings for a known source pack")
    ap.add_argument("--group", help="regex on file names; each distinct first group becomes its own pack")
    ap.add_argument("--sex", default="any", choices=["any", "male", "female"], help="default: guessed from pitch")
    ap.add_argument("--cutoff", type=float, default=None, help="murmur low-pass in Hz; 0 keeps clean audio (default 850)")
    ap.add_argument("--no-keywords", dest="keywords", action="store_false", help="ignore file names, sort by length only")
    ap.add_argument("--max-per-folder", type=int, default=24)
    ap.add_argument("--max-syllables", type=int, default=40)
    ap.add_argument("--no-questions", action="store_true")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--neutralize", action="store_true", default=None,
                    help="drop outlier clips and hold babble to the speaker's calm voice (on in the suddendice preset)")
    ap.add_argument("--no-neutralize", dest="neutralize", action="store_false")
    ap.add_argument("--blend", type=float, default=None,
                    help="0..1: bake soft edges into the babble syllables (default 0). Not needed for the mod's own Blend "
                         "setting, which does this at playback; writes a slower Speed into voice.ini")
    a = ap.parse_args()
    random.seed(a.seed)

    preset = PRESETS.get(a.preset, {})
    if a.cutoff is None:
        a.cutoff = preset.get("cutoff", 850.0)
    group = a.group or preset.get("group")
    if a.neutralize is None:
        a.neutralize = preset.get("neutralize", False)
    if a.blend is None:
        a.blend = preset.get("blend", 0.0)
    a.blend = min(1.0, max(0.0, a.blend))
    prefix = preset.get("prefix", "")

    files = []
    for root, _, names in os.walk(a.input):
        for n in names:
            if os.path.splitext(n)[1].lower() in (".wav", ".fuz", ".xwm", ".mp3", ".ogg", ".flac", ".m4a"):
                files.append(os.path.join(root, n))
    if not files:
        raise SystemExit("no audio files found")
    files.sort()

    packs = {}
    if group:
        rx = re.compile(group, re.IGNORECASE)
        for f in files:
            m = rx.search(os.path.basename(f))
            key = (prefix + m.group(1).upper()) if m else (a.name or "Ungrouped")
            packs.setdefault(key, []).append(f)
    else:
        if not a.name:
            raise SystemExit("--name is required")
        packs[a.name] = files

    print(f"{len(files)} recordings -> {len(packs)} pack(s)")
    with tempfile.TemporaryDirectory() as tmp:
        for name, group_files in packs.items():
            print(f"\n{name}: {len(group_files)} recordings")
            build_pack(group_files, os.path.join(a.out, name), a, tmp)


if __name__ == "__main__":
    main()
