# Making voice packs for Silent Protagonist Voice

A voice pack is just a folder of short `.wav` clips of one person humming, murmuring and reacting, with no words. The mod
strings them together while your character talks, picks emotional ones when the line calls for it, and moves the mouth to
the sound. This guide covers three ways to make one, from easiest to most hands-on:

1. **Record yourself (or a friend) and let the script build the pack.** Best results, and the pack is yours to share.
2. **Convert an existing free recording set** (for example SuddenDice's CC0 packs on Freesound).
3. **Build the folders by hand** in an audio editor.

---

## What a pack looks like

```
Data/Sound/FX/SilentProtagonistVoice/voices/<PackName>/
    voice.ini        optional settings (see below)
    syllables/       babble: one syllable each, about 0.1-0.25 s     ← the most important folder
    short/           whole replies for short lines: "Mm-hm." "Hm."    (about 0.3-0.7 s)
    medium/          whole murmured phrases                            (about 0.6-1.8 s)
    long/            longer murmured phrases                           (about 1.2-3 s)
    question/        rising "Hm?" sounds, used when a line ends in ?
    exclaim/         lively sounds, used when a line ends in !
    anger/ disgust/ fear/ sad/ happy/ surprise/ puzzled/
                     emotional reactions: a scoff, a sigh, a pleased hum, a gasp...
```

- **Every folder is optional.** Anything a pack lacks falls back to the nearest folder it has. A pack with only
  `syllables/` always babbles; a pack with no `syllables/` always plays whole clips.
- **Files go directly in each folder** (no subfolders inside them). File names don't matter to the mod.
- **Format:** loose `.wav`, 16-bit PCM, mono, 44.1 or 48 kHz. Packs can't go in a BSA: the mod reads the files from disk.
- **The pack name is the folder name.** Avoid spaces; something like `GruffNord` or `SoftSpokenElf` works well.

How the mod uses them: short lines play one clip from `short/` (or an emotion folder if the line has a mood); everything
else babbles from `syllables/`, and a strongly emotional line opens with a clip from its emotion folder. With the default
playback, the mod also softens and blends babble syllables into each other (*Blend* in the menu), so your syllables can be
plain hard-cut clips.

---

## 1. Record your own voice (recommended)

### What you need

- Any decent microphone (a headset is fine) and a quiet room.
- [Audacity](https://www.audacityteam.org/) or any recorder that saves WAV.
- Python 3 with two packages: open a terminal and run `py -m pip install numpy scipy`.
- [ffmpeg](https://ffmpeg.org/) on your PATH if your recordings aren't WAV (on Windows: `winget install ffmpeg`).
- The `tools` folder that comes with the mod (`make_voicepack.py` and `neutralize.py` must sit next to each other).

### What to record

The secret of a good pack is **consistency**: babble sounds like one person only if every syllable is the same person, in
the same mood, at roughly the same pitch and loudness. Record in one sitting, at the same distance from the mic, in a
relaxed speaking voice.

Make one file per kind of sound, a minute or so each, with short pauses between takes. **Name each file after what's in
it.** The script sorts by file name.

| File name (examples) | What to say | Where it ends up |
|---|---|---|
| `calm.wav`, `hmm.wav`, `yes.wav` | Relaxed "mm-hm", "hmm", "uh", "ah", "eh", "mm", murmured nonsense at your normal pitch. **The most important file: record at least two minutes.** | babble syllables + short/medium/long |
| `no.wav`, `nope.wav` | "Mm-mm", "nuh-uh" | babble syllables only |
| `angry.wav` | Growls, "hmph", irritated grunts | anger |
| `disgust.wav` | "Ugh", "eugh", scoffs | disgust |
| `scared.wav` | Nervous gasps, shaky "uh-uh" | fear |
| `sad.wav`, `sigh.wav` | Sighs, low "mm..." | sad |
| `happy.wav`, `laugh.wav` | Pleased hums, a short chuckle | happy |
| `surprise.wav`, `gasp.wav` | "Oh!", sharp breath in | surprise |
| `confused.wav`, `huh.wav` | "Hm?", "huh?" | puzzled |
| `excited.wav` | Eager "ooh!", "ha!" | exclaim |

Other words the script recognises include *rage, annoyed, contempt, nervous, panic, grief, sorrow, relief, amused, wow,
shock, doubt, curious, sceptical, cheer, eager*. Files named for pain, effort, coughs, yawns, screams and the like are
skipped. Files with no recognised name are sorted by length into short/medium/long.

Tips:

- **Keep the calm takes calm.** Babble is cut from them, so one excited "mm!" in there will stick out.
- **Leave a little silence between takes** (half a second or more): the script splits files at pauses.
- **Don't speak real words.** If you do, see the "murmur" filter below.
- Emotional takes can be as expressive as you like. The script judges them against other takes of the same emotion, not
  against your calm voice.

### Build the pack

```
py tools\make_voicepack.py "C:\Recordings\MyVoice" --name MyVoice --sex male --cutoff 0 --neutralize ^
    --out "C:\MO2\mods\My Voice Pack\Sound\FX\SilentProtagonistVoice\voices"
```

- `--name` is the pack (folder) name.
- `--sex male|female` tags the pack. Leave it out and the script guesses from pitch (emotional sounds run high, so check).
- `--cutoff 0` keeps the audio clean, which is right for wordless recordings. Leave it out if your recordings contain real
  speech: the default 850 Hz "murmur" filter blurs the words so only the voice's pitch and rhythm come through.
- `--neutralize` drops clips that don't fit (shouts, squeals, breaths, a take at a very different pitch), nudges babble
  syllables to the same pitch and levels everything to the same loudness. It prints what it kept and why it dropped the rest.
- Point `--out` at a new, empty MO2 mod folder as shown, so the pack is a mod you can enable, disable and share.

Read the printed summary. As a rough guide, a good pack has **20-40 syllables** and **at least 3 clips** in each folder
it uses. If a folder ends up nearly empty, record more of that sound and run the script again (it replaces the pack).

---

## 2. Convert a free recording set

**SuddenDice (CC0, public domain)**: volunteers recorded wordless reactions to a list of emotions, one Freesound pack per
person. Download a few from [freesound.org/people/SuddenDice/packs](https://freesound.org/people/SuddenDice/packs/) (a free
Freesound account is needed), unzip them into one folder, then:

```
py tools\make_voicepack.py "C:\Downloads\SuddenDice" --preset suddendice ^
    --out "C:\MO2\mods\SuddenDice Voices\Sound\FX\SilentProtagonistVoice\voices"
```

You get one pack per speaker (`SuddenDice_AS015537`, ...), with clean audio, emotions sorted and neutralizing on. Then
check each pack's `voice.ini`: the male/female guess is often wrong for these recordings, so fix `Sex=` by ear. Some
speakers only recorded one or two takes per emotion and make thin packs that repeat themselves; try a few and keep the best.

**Any other recordings** (a folder of `.wav`, `.mp3`, `.ogg`, `.flac`, even game `.fuz`/`.xwm` files) work the same way
as section 1: name or sort the files, run the script with `--name`, and add `--neutralize` if the source mixes moods. Only
share packs made from recordings you're allowed to redistribute (see *Sharing*).

Useful options for odd sources:

| Option | What it does |
|---|---|
| `--no-keywords` | Ignore file names; sort everything by length. |
| `--group "regex"` | Split one folder into several packs: each distinct first regex group becomes a pack. |
| `--max-syllables 40`, `--max-per-folder 24` | How many clips to keep. |
| `--no-questions` | Don't sort rising-pitch takes into `question/`. |

---

## 3. Build a pack by hand

Make the folders from *What a pack looks like* and cut clips in an audio editor:

- **Syllables:** single syllables ("ma", "hm", "eh", "uh"), 0.1-0.25 s, from calm speech at one steady pitch. Cut at
  the quiet point between syllables, with a few milliseconds of fade at each end so they don't click.
- **Whole clips** (short, medium, long, question, exclaim, emotions): one complete sound each, trimmed of leading and
  trailing silence, with a short fade-out so the decay isn't chopped off.
- **Even loudness:** normalise every clip to the same loudness (Audacity: *Effect → Volume and Compression → Loudness
  Normalization*), not just the same peak.
- **Export** as WAV, 16-bit PCM, mono.

Tip: run your hand-cut pack folder through `make_voicepack.py ... --no-keywords --cutoff 0 --neutralize` to find clips
that don't fit. Its summary tells you what it would drop.

---

## voice.ini

Optional, in the pack folder. Every key is optional.

```ini
[Pack]
Description=Gravelly old soldier   ; shown in the menu
Sex=male              ; any / male / female; used to pick a pack when the chosen one is missing
Mode=babble           ; force a mode for this pack: auto / babble / phrase (default: the user's choice)
BasePitch=1.0         ; multiplies the user's Pitch slider
PitchVariation=0.05   ; overrides the user's pitch variation for this pack
Speed=1.0             ; babble pace (0.4-2.5)
Gain=1.0              ; loudness (0-2)
SyllablesPerBlip=1.6  ; written syllables per babble sound; higher = fewer sounds per line
Throat=0              ; voice character offsets added to the menu sliders (-1..1):
Breath=0              ;   bigger/deeper ↔ smaller, husky, muffled ↔ crisp, monotone ↔ theatrical
Brightness=0          ;   (Throat, Breath and Brightness need Processed playback)
Expression=0
```

---

## Testing your pack

1. **Install it** as an MO2 mod (or copy it into `Data\Sound\FX\SilentProtagonistVoice\voices\`).
2. **Launch the game through MO2.** MO2 only shows the game files that existed when it launched, so a pack added while the
   game is running won't appear until you restart. After that, changes to existing files show up after **Rescan voice
   packs** (Advanced section).
3. **Pick it** under *Voice* in the mod's SKSE Menu Framework page. The line under the picker shows how many syllables, short,
   medium, long, question, exclaim and emotion clips it found.
4. **Use the *Try it* box** at the top of the menu: type a line, choose a mood, and press *Preview* to hear it instantly.
   Try a one-word line, a long line, a question, and each mood.
5. Turn on **Debug log** (Advanced) if something is off. `Documents\My Games\Skyrim Special Edition\SKSE\
   SilentProtagonistVoice.log` lists every pack found and what each line played.

### Troubleshooting

| Problem | Fix |
|---|---|
| Pack isn't in the list | Check the path (`...\voices\<PackName>\syllables\*.wav`), that files are loose WAVs (not in a BSA), and restart through MO2. |
| Silent, or some clips never play | The file must be a real WAV: PCM (8/16/24/32-bit) or 32-bit float, not MP3/ADPCM renamed to `.wav`. Re-export as 16-bit PCM mono. |
| Babble sounds like several people taking turns | Run the script with `--neutralize`, or record more calm takes in one sitting. |
| Babble is choppy | Raise *Blend* or *Overlap* in the Babble section (Blend needs Processed playback). |
| Babble is smeared | Lower *Blend*. |
| Same few sounds repeat | Add more syllables (aim for 20-40) and more clips per folder. |
| Real words are audible | Use the default murmur filter (leave out `--cutoff 0`) or lower it, e.g. `--cutoff 600`. |
| One clip is much louder | Re-run with `--neutralize`, or loudness-normalise by hand. |

---

## Sharing

- **Only share recordings you have the right to share:** your own voice, a friend's with their permission, or audio under
  a licence that allows redistribution (CC0 is easiest). Don't share packs made from game voice files or other
  copyrighted audio; those are fine for your own game only.
- If the source licence asks for credit, credit it in your mod page and include its licence file in the pack folder.
- **Package it as an MO2-installable zip** whose root contains `Sound\FX\SilentProtagonistVoice\voices\<PackName>\...`.
  Users install it, launch the game and pick it in the menu. No plugin file, no load order.
