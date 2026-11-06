#pragma once

// All user-facing options. Read from / written to
// Data\SKSE\Plugins\SilentProtagonistVoice.ini, edited live from the
// SKSE Menu Framework page.

namespace Settings
{
	enum class Mode : int
	{
		kAuto = 0,    // short lines use a phrase clip if the pack has one, the rest babble
		kBabble = 1,  // chain syllable blips, count follows sentence length
		kPhrase = 2   // one (or two) whole pre-recorded clips picked by length
	};

	enum class Playback : int
	{
		kProcessed = 0,  // own audio output: full voice shaping (throat, breath, brightness, glides)
		kEngine = 1,     // the game's sound engine: 3D positioned, pitch only
		kAudioUtil = 2   // AudioUtil plays the clips and drives the mouth
	};

	enum class EmotionSource : int
	{
		kOff = 0,
		kTags = 1,         // only speech-check tags like (Intimidate) / (Persuade)
		kTagsAndReply = 2  // tags, else the mood of the NPC's scripted reply
	};

	enum class Reactions : int
	{
		kOff = 0,
		kShortLines = 1,  // a short emotional line is one reaction clip ("Hmph.")
		kLeadIn = 2       // ...and longer emotional lines open with one
	};

	struct Config
	{
		// General
		bool  enabled = true;
		bool  waitForPlayer = true;   // NPC answers after the player finishes
		bool  allowSkip = true;       // clicking during the hum skips it
		float maxLineSeconds = 6.0f;  // hard cap on one player line
		float responseDelay = 0.12f;  // breath between the player finishing and the NPC starting

		// Voice
		std::string packMale = "SuddenDiceMale_AS049690";
		std::string packFemale = "SuddenDiceFemale_AS000834";
		Mode        mode = Mode::kAuto;
		float       volume = 0.85f;
		bool        followVoiceSlider = true;
		float       pitch = 1.0f;            // multiplies the pack's base pitch
		float       pitchVariation = 0.06f;  // +/- random fraction per clip
		bool        intonation = true;       // questions rise, statements fall

		// Voice character (Processed playback only, except Expression)
		float throat = 0.0f;      // -1 small/young .. +1 large/deep vocal tract (formant shift)
		float breath = 0.0f;      // 0 clear .. 1 whisper
		float brightness = 0.0f;  // -1 dark/muffled .. +1 bright/crisp
		float expression = 1.0f;  // 0 monotone .. 2 very animated (pitch movement)

		// Emotion
		EmotionSource emotionSource = EmotionSource::kTagsAndReply;
		float         emotionIntensity = 1.0f;
		int           emotionThreshold = 25;  // NPC reply emotion below this (0-100) counts as neutral
		Reactions     reactions = Reactions::kLeadIn;
		bool          faceExpression = true;
		float         faceIntensity = 0.6f;

		// Babble
		float syllablesPerBlip = 1.6f;
		float speed = 1.0f;
		int   maxBlips = 22;
		float commaPause = 0.14f;
		float overlap = 0.12f;
		float blend = 0.7f;  // processed playback: babble syllables ring softly into each other

		// Phrase
		int  shortMaxSyllables = 4;
		int  mediumMaxSyllables = 12;
		bool chainLongLines = true;

		// Lip sync
		bool  lipSync = true;
		float lipGain = 1.0f;
		float jawOpen = 0.7f;
		bool  vowelShapes = true;
		bool  expressiveBrows = true;
		float mouthSmoothing = 0.035f;  // seconds

		// Advanced
		Playback playback = Playback::kProcessed;
		bool     debugLog = false;
	};

	Config& Get();
	std::mutex& Lock();

	// a copy under the lock
	Config Snapshot();

	void Load();
	void Save();

	std::filesystem::path IniPath();
}
