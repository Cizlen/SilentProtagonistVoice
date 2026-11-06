# Silent Protagonist Voice

An SKSE plugin for Skyrim SE/AE. When you pick a dialogue line, your character
hums, murmurs or "babbles" it and their mouth moves, the way silent heroes did
in older RPGs. Then the NPC answers. The sound depends on the line: short
replies get a quick "hm" or "mm-hm", questions rise at the end, exclamations
push harder, and long sentences become a longer string of syllables with
pauses at the commas. Nothing is pronounced, so it fits any line without the
uncanny-valley feel of AI text-to-speech.

* **Emotion-aware.** Every scripted NPC reply in Skyrim has an emotion (anger, disgust, fear, sad, happy, surprise, puzzled) and a
  strength. Your character takes on the mood of the exchange: its voice bends, it opens with a scoff or a sigh, and its face shows it.
  Speech-check tags like (Intimidate) or (Persuade) set the mood directly.
* **Shape the voice** with Throat, Breath, Brightness and Expression sliders. Each has a description in the menu.
* No .esp, no .swf edits, no Papyrus.
* Voice packs are folders of loose `.wav` files under `/voices/`.
* There's a settings page in SKSE Menu Framework 3 (optional; the INI also works).
* Optional AudioUtil integration.

---

## 1. Requirements

| | |
|---|---|
| Skyrim SE 1.5.97 or AE 1.6.x (1.6.1170 is fine) | one DLL covers both |
| [SKSE64](https://skse.silverlock.org/) | |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | |
| SKSE Menu Framework 3 *(optional, on Nexus)* | settings page |
| [AudioUtil](https://github.com/crajjjj/AudioUtil) *(optional)* | see section 6 |

## 2. Install

After building (section 8) you get a ready-to-install folder: `build\mod\`
with xmake (`xmake install -o build\mod`), or `build/release/mod/` with CMake:

```
SKSE/Plugins/SilentProtagonistVoice.dll  (+ .pdb)
SKSE/Plugins/SilentProtagonistVoice.ini
Sound/FX/SilentProtagonistVoice/voices/SuddenDiceFemale_AS000834/...   (default female voice)
Sound/FX/SilentProtagonistVoice/voices/SuddenDiceFemale_AS015537/...
Sound/FX/SilentProtagonistVoice/voices/SuddenDiceMale_AS049690/...     (default male voice)
Sound/FX/SilentProtagonistVoice/voices/SuddenDiceMale_AS054715/...
```

The four bundled voices are real people: wordless recordings by
[SuddenDice](https://freesound.org/people/SuddenDice/) on Freesound, released CC0 (public domain). Each pack carries the
original licence file. They were built with `tools/make_voicepack.py --preset suddendice` and neutralized (see *Making
packs*). AS054715 comes from a small source recording, so it has fewer clips and repeats sooner than the other three.

Zip it and install it with MO2 like any other mod. Load order doesn't matter
because there's no plugin file.

## 3. Use

Talk to someone and pick a line. To hear and see it outside dialogue, open
the SKSE Menu Framework page, go to *Silent Protagonist Voice → Settings →
Test*, type a line, press **Speak**, then close the menu. Use third person if
you want to watch the mouth.

The settings, all of them also in `SilentProtagonistVoice.ini`:

| Setting | What it does |
|---|---|
| NPC waits for my character | On (default): your line plays first and the NPC answers after it. Off: the hum plays over the NPC's reply. |
| Click to skip | Clicking while your character hums skips straight to the NPC. |
| Male / Female character | The pack to use for each sex. |
| Mode | **Auto**: short lines play one whole clip, longer lines babble. **Babble**: always chain syllables. **Phrase**: always play whole clips. |
| Pitch, Pitch variation, Intonation | Pitch shaping. Intonation makes questions rise, statements fall and "..." trail off. |
| Babble: syllables per blip, speed, max blips, comma pause, overlap | The rhythm of babble mode. |
| Phrase: short / medium limits | The syllable counts that pick the short, medium or long folder. |
| Lip sync | Mouth gain, jaw opening, vowel shapes (each syllable gets a different mouth shape), and brows that lift on questions and exclamations. |
| **Voice character** | **Throat**: the size of the vocal tract; it moves the resonances without moving the pitch (bigger/deeper ↔ smaller/younger). **Breath**: clear → husky → whisper. **Brightness**: muffled ↔ crisp. **Expression**: monotone ↔ theatrical intonation and pitch glides. Throat, Breath and Brightness need *Processed* playback. |
| **Emotion** | Source (tags only, or tags plus the NPC's reply mood), intensity, the threshold below which a reply counts as neutral, emotional reactions (short lines become one reaction sound, longer ones open with one), and facial expression. |

The **Try it** box at the top plays a line *right now*, with the menu open. **Preview mood** forces a mood, so you can hear the
sliders and moods without leaving the menu. **Speak in game** plays the line with the mouth moving once you close the menu.

Text in `(parentheses)`, `[brackets]`, `<angle brackets>` and `*asterisks*` is
ignored, so "(Persuade) Surely you can…" only voices the sentence, and lines
like "(Remain silent)" or "[Leave]" make no sound.

## 4. Voice packs

**Step-by-step guide for pack makers: [VOICE_PACKS.md](VOICE_PACKS.md)** (recording tips, the build script, testing,
sharing). The reference:

```
Data/Sound/FX/SilentProtagonistVoice/voices/<PackName>/
    voice.ini        optional
    syllables/       babble blips, one syllable each, ~0.1-0.25 s
    short/           whole reactions for short lines   ("Hm.", "Mm-hm.")
    medium/          whole phrases, ~0.6-1.2 s
    long/            whole phrases, ~1.2-3 s
    question/        used instead of short/medium when the line ends in ?
    exclaim/         used instead of short/medium when the line ends in !
    anger/ disgust/ fear/ sad/ happy/ surprise/ puzzled/
                     emotional reactions (a scoff, a sigh, a pleased hum), used when the line
                     carries that mood. Questions fall back to puzzled/ and exclamations to
                     surprise/ when a pack has no question/ or exclaim/.
```

A pack needs at least one of these folders. Anything it lacks falls back to
the nearest folder it has. A pack with only `syllables/` always babbles, and a
pack with no `syllables/` always plays whole clips.

**Format:** loose `.wav`, 16-bit PCM, mono, 44.1 or 48 kHz. The plugin reads
the files from disk to get their length and loudness curve, so packed BSAs
don't work for packs.

**voice.ini** (all keys optional):

```ini
[Pack]
Description=Gravelly old soldier
Sex=male              ; any / male / female, only used if the chosen pack is missing
Mode=babble           ; force a mode for this pack (auto / babble / phrase)
BasePitch=1.0         ; multiplies the user's Pitch slider
PitchVariation=0.05   ; overrides the user's slider for this pack
Speed=1.1
Gain=1.0
SyllablesPerBlip=1.6
Throat=0.2            ; voice character offsets, added to the menu sliders (-1..1)
Breath=0
Brightness=-0.1
Expression=0
```

Press **Rescan voice packs** in the menu after adding or changing files. You
don't need to restart the game.

### Making packs

`tools/` has two Python scripts (`pip install numpy scipy`).

**`synth_voicepack.py`** synthesises wordless hums, for when you want a voice that isn't a recording.

```
python tools/synth_voicepack.py --out "<Data>/Sound/FX/SilentProtagonistVoice/voices" ^
    --name Gruff --f0 95 --breath 0.09 --sex male
```

**Real, wordless voices: SuddenDice (CC0, Freesound).** The best free source I found. Volunteers recorded wordless reactions
to a list of emotions, about 35 sounds per person, each as its own pack. Download a few of their packs from
[freesound.org/people/SuddenDice/packs](https://freesound.org/people/SuddenDice/packs/) (a free Freesound account is needed
to download) into one folder, then:

```
python tools/make_voicepack.py "C:\Downloads\SuddenDice" --preset suddendice ^
    --out "<Data>/Sound/FX/SilentProtagonistVoice/voices"
```

You get one voice pack per speaker (`SuddenDice_AS000834`, ...). Each SuddenDice file holds several takes of one emotion;
the script converts the FLAC files, splits every file into its takes, sorts them into the emotion folders by the file name
(hatred → anger, relief → happy, grief → sad, confusion → puzzled, ...), fills the ordinary short/medium/long folders and the
babble syllables from the everyday sounds (yeah, affirmation, reflection, calmness), skips pain and effort sounds, and
guesses each speaker's sex from their pitch (emotional sounds run high, so check it and fix `Sex=` in `voice.ini` if it's
wrong). Pick the packs in the menu and press Rescan.

The preset also **neutralizes** the pack (`tools/neutralize.py`; `--neutralize` / `--no-neutralize` for any source).
Emotion recordings mix calm takes with screams, squeals, laughs and breaths, and strung together as babble they sound like
several people taking turns. Neutralizing measures every clip's pitch, loudness, brightness, breathiness and how much of it
is actually voiced, then:

- babble syllables and the everyday short/medium/long/question folders are held to the speaker's calm voice (measured from
  their everyday takes): clips far off are dropped and the rest are nudged to the same pitch;
- each emotion folder is judged against the rest of that folder, so anger keeps its range and only the takes that stand out
  from the other angry ones go;
- screams, squeals and mostly-breath clips are dropped everywhere;
- everything is levelled to the same loudness.

It prints what it kept and why it dropped the rest.

Babble blending (syllables flowing into each other) is done by the mod itself at playback, see *Blend* in the Babble
section of the menu, so packs stay plain hard-cut clips.

**`make_voicepack.py`** builds a pack from any real recordings: a folder of `.wav`,
`.fuz`, `.xwm`, `.mp3` and so on. Every format except `.wav` needs ffmpeg on
your PATH. It cuts the recordings into syllables and utterances, sorts them by
length, and puts rising-pitch lines in `question/`. If the file names say what the sound is ("angry_grunt.wav",
"happy sigh.wav"), it files them into the emotion folders. `--no-keywords` turns that off. By default it also
"murmurs" everything with a low-pass filter. The voice's pitch, rhythm and
character come through, but the words don't, so the result stays generic. Use
`--cutoff 0` if your source is already wordless (hums, grunts, efforts).

```
python tools/make_voicepack.py "C:\path\to\voice files" --name MyVoice --sex female ^
    --out "<Data>/Sound/FX/SilentProtagonistVoice/voices"
```

For hand-made packs, record 10-20 neutral "mm", "hm", "ah", "eh" syllables
for `syllables/` plus a few acknowledgements, questions and exclamations.
Vary the takes, because the plugin avoids playing the same clip twice in a
row.

## 5. How it works (useful when debugging)

1. **The click.** The plugin hooks `DialogueMenu::Accept`, the function where
   the menu hands its Scaleform callbacks to the game. It swaps in its own
   handlers for `TopicClicked`, `SkipText` and `CloseMenu`. When you click a
   topic, it reads the text you clicked from the menu's list entry, or from
   `MenuTopicManager` if that fails. It then holds the click back.
2. **The plan.** The text is analysed for syllables, pauses and how it ends
   (question, exclamation, trailing off, statement). The plugin then picks
   clips from the pack and gives each one a start time, pitch and volume.
3. **Mood.** If the option text has a speech-check tag, that sets the mood.
   Otherwise the mood comes from the emotion type and strength on the NPC's
   scripted reply (`DialogueResponse`, the same data the NPC's own face
   animation uses). The mood bends pitch, speed, loudness, breath, brightness
   and the direction of each syllable's glide; fear adds a tremble. It can pick
   a reaction clip from the pack's emotion folder, and it sets the player's
   facial expression through `BSFaceGenAnimationData::SetExpressionOverride`.
4. **Playback.** *Processed* (the default): the plugin shapes each clip itself
   (cepstral formant shift for Throat, a spectral tilt for Brightness,
   envelope-shaped aspiration noise for Breath, then a resampler for pitch,
   glides and tremble) on a worker thread, caches it, and plays it through its
   own XAudio2 output, which is part of Windows 10 and 11. The sound is centred
   on you rather than placed in 3D. *Game sound engine*: `BSAudioManager`
   plays the clip, 3D on your character, pitch only. Either way the volume can
   follow the game's Voice slider.
5. **The mouth.** Each frame (from a `PlayerCharacter::Update` hook), the
   loudness curve of whatever is sounding drives the facegen phonemes: Aah
   and BigAah for the jaw plus one vowel shape per syllable. The shapes are
   smoothed so the mouth doesn't snap, and they're released when the line
   ends.
6. **The reply.** When the line finishes, or you click to skip, the held
   click is replayed and the NPC answers. If something goes wrong, a watchdog
   releases the NPC after *Longest line* + 2 seconds, so a conversation can
   never get stuck.

## 6. AudioUtil

[AudioUtil](https://github.com/crajjjj/AudioUtil) is detected at runtime, so
it's not required. If it's installed:

* **Native playback (default)** claims the player's mouth through AudioUtil's
  mouth-claim API for the length of each line, so mods that drive the mouth
  through AudioUtil don't fight it.
* **Advanced → Playback = AudioUtil** sends every clip through
  `AudioUtil_PlayFileWithLipSync` instead. AudioUtil then plays the clips and
  animates the mouth, including its pseudo-phoneme lip sync, and its group
  volume and ducking apply (group `SilentProtagonistVoice`). AudioUtil can't
  change playback rate, so pitch variation and intonation are lost in this
  mode.

## 7. Compatibility

* **Dragonborn Voice Over (DBVO) / DBReV / other player-voice mods:** don't
  run them at the same time as this mod. Each one plays a player line and
  delays the NPC, so both would play. Disable this mod in the menu, or remove
  it, when you use one of them.
* **Open Your Mouth** (the in-dialogue lip movement feature) also animates the
  player's mouth during dialogue. Turn that feature off, or turn off *Animate
  mouth* here.
* **Dialogue menu replacers:** these work as long as they still call the
  vanilla `TopicClicked` game delegate, which nearly all do.
* **Fuz Ro D'oh, Alternate Conversation Camera and similar mods:** these work.
  A dialogue camera that shows your character's face makes the mouth
  animation worth having.
* **VR:** not supported.

## 8. Building

### xmake (recommended)

CommonLibSSE-NG is a git submodule, so clone with `--recursive`:

```powershell
git clone --recursive https://github.com/<you>/SilentProtagonistVoice.git
```

(Already cloned without it? Run `git submodule update --init --recursive`.)

Then:

```powershell
xmake f -m releasedbg -y
xmake -y
xmake install -o build\mod     # DLL + PDB + INI + voice packs, ready to zip for MO2
```

If `XSE_TES5_MODS_PATH` is set, a plain `xmake install` puts everything in
`<mods>\SilentProtagonistVoice` instead.

### GitHub Actions (no local setup)

Every push and pull request is built on GitHub's Windows machines with xmake
(`.github/workflows/build.yml`). Open the run on the **Actions** tab and download
the **SilentProtagonistVoice** artifact: that's the ready-to-install mod folder.
On a fork, enable Actions once on its Actions tab. The first run compiles
CommonLib and takes a while; later runs use the cache.

### CMake (alternative)

The CMake files describe the same build, with vcpkg for the dependencies.

**One-time setup**

1. **Visual Studio 2022** (Community is fine), with the
   *Desktop development with C++* workload. That workload includes MSVC, the
   Windows SDK, CMake and Ninja.
2. **Git for Windows**.
3. **vcpkg**. Use a full clone, not a shallow one, because the manifest pins a
   baseline commit:
   ```
   git clone https://github.com/microsoft/vcpkg C:\dev\vcpkg
   C:\dev\vcpkg\bootstrap-vcpkg.bat -disableMetrics
   ```
4. Set these environment variables (Start → "Edit environment variables for
   your account"):

   | Variable | Value | |
   |---|---|---|
   | `VCPKG_ROOT` | `C:\dev\vcpkg` | required |
   | `SKYRIM_MODS_FOLDER` | your MO2 `mods` folder | optional: every build gets copied to `mods\Silent Protagonist Voice` |
   | `COMMONLIB_SSE_FOLDER` | a local clone of CommonLibSSE-NG | optional: skips the download if you already have one (`git clone -b ng https://github.com/alandtse/CommonLibVR`) |

   Open a **new** terminal afterwards so it sees them.

**Build**

1. Start **x64 Native Tools Command Prompt for VS 2022** from the Start menu.
   An ordinary cmd or PowerShell window won't find `cl.exe`.
2. Run:
   ```
   cd path\to\SilentProtagonistVoice
   cmake --preset release
   cmake --build --preset release
   ```
   The first configure downloads CommonLibSSE-NG and builds the vcpkg
   dependencies (spdlog, fmt, DirectXTK, …), which takes a while. Later builds
   take seconds.
3. The result is in `build\release\mod\`. It's also copied to
   `SKYRIM_MODS_FOLDER` if you set that variable.

To use Visual Studio as an editor, choose *File → Open → Folder*. VS reads
`CMakePresets.json`, so you can pick **Release** in the toolbar and build.

**Common build errors**

| Error | Fix |
|---|---|
| `cl.exe` not found / "No CMAKE_CXX_COMPILER could be found" | You're not in the *x64 Native Tools* prompt. |
| "Could not find toolchain file …/vcpkg.cmake" | `VCPKG_ROOT` isn't set, or the terminal was opened before you set it. |
| vcpkg: "failed to … baseline" / "builtin-baseline" | vcpkg was cloned with `--depth`. Re-clone it without that flag, or run `git -C %VCPKG_ROOT% fetch --unshallow`. |
| Errors inside CommonLib headers after updating CommonLib | Delete `build\` and configure again. |
| SKSE says the plugin is "not compatible" / doesn't load | Address Library is missing, or you're on VR. |

## 9. Troubleshooting in game

The log is `SilentProtagonistVoice.log`, in the same SKSE log folder as your
other SKSE logs (`Documents\My Games\Skyrim Special Edition\SKSE`; if
Documents is synced by OneDrive, look under `OneDrive\Documents`). It lists
every pack it found and each hook it installed. Turn on *Advanced → Debug
log* to also record each line you speak: its text, syllable count, tone, the
clips chosen, and the three places the text was read from.

| Symptom | Check |
|---|---|
| No sound, NPC answers instantly | The log should show `Voice pack '…'` lines. If it says "no voice packs", the `Sound\FX\SilentProtagonistVoice\voices` folder isn't in Data. |
| Sound but no mouth movement | Check third person, *Animate mouth*, and whether another mod (Open Your Mouth, a player-voice mod) is driving the face. |
| Pitch variation has no effect | Only with *Game sound engine* playback: it relies on the engine's `SetFrequency` as a rate ratio. Processed playback does its own resampling. |
| Throat/Breath/Brightness do nothing | Advanced → Playback must be *Processed*. The menu line "In use:" shows what is actually playing. |
| No mood ever | Turn on the debug log. It prints `Reply emotion: <type> <value>` for every click. If the value is below the threshold, lower *Reply emotion threshold*. |
| Debug log shows `list=''` and only `manager='…'` | Your dialogue menu replacer restructured the movie. The fallback is in use and should still be right. |

## 10. First in-game test checklist

These are the parts that can only be confirmed in the game:

- [ ] The log shows `Hooked DialogueMenu::Accept` and `Hooked PlayerCharacter::Update`.
- [ ] With the debug log on, picking a topic logs `TopicClicked: list='…'` with the text you clicked.
- [ ] The hum plays and follows your character, and the NPC answers right after it.
- [ ] Clicking during the hum skips to the NPC.
- [ ] Pressing Tab during the hum closes the conversation cleanly.
- [ ] The mouth moves in third person and closes when the line ends.
- [ ] Pitch variation is audible (babble mode makes it easiest to hear).
- [ ] The log shows `XAudio2 output ready`, and the menu's Preview button plays at once.
- [ ] With the debug log on, an angry or sad NPC reply logs `Reply emotion: anger 50`, and your character's face and voice follow.

---

## License and credits

Silent Protagonist Voice is free software under the **GNU General Public License v3.0** ([LICENSE](LICENSE)): use it,
fork it, change it and share it, as long as what you share stays under the GPL with its source available.

- **Bundled voices:** wordless recordings by [SuddenDice](https://freesound.org/people/SuddenDice/) on Freesound,
  released **CC0** (public domain). Each pack folder carries its original licence file. Voice packs you make yourself are
  yours to license however you like.
- **[CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng)** (MIT), the library the plugin is built on.
- **`include/SKSEMenuFramework.h`** from the SKSE Menu Framework 3 example (MIT, Thiago099).
- **[SimpleIni](https://github.com/brofield/simpleini)** (MIT) for the settings file.
- Optional integration with [AudioUtil](https://github.com/crajjjj/AudioUtil), loaded at runtime if installed.
