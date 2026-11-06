#pragma once

#include "WavFile.h"

// Voice packs live in
//   Data\Sound\FX\SilentProtagonistVoice\voices\<PackName>  (one folder per pack)
// with any of these sub-folders (loose 16-bit PCM .wav files):
//   syllables\   short blips/hums chained together in babble mode
//   short\       whole reactions for short lines    ("Hm.", "Mm-hm.")
//   medium\      whole phrases for medium lines
//   long\        whole phrases for long lines
//   question\    used instead of short/medium when the line ends in '?'
//   exclaim\     used instead of short/medium when the line ends in '!'
//   anger\ disgust\ fear\ sad\ happy\ surprise\ puzzled\
//                emotional reactions, picked when the line carries that
//                mood (Skyrim's own dialogue emotions)
// plus an optional voice.ini (see README).

namespace VoicePacks
{
	enum class Category : int
	{
		kSyllables = 0,
		kShort,
		kMedium,
		kLong,
		kQuestion,
		kExclaim,
		// emotion folders, in Emotion::Type order starting at kAnger
		kAnger,
		kDisgust,
		kFear,
		kSad,
		kHappy,
		kSurprise,
		kPuzzled,
		kTotal
	};

	inline constexpr std::array<const char*, static_cast<int>(Category::kTotal)> kFolderNames{
		"syllables", "short", "medium", "long", "question", "exclaim",
		"anger", "disgust", "fear", "sad", "happy", "surprise", "puzzled"
	};

	// the emotion folder for a mood, or kTotal for neutral
	[[nodiscard]] Category ForEmotion(int a_emotionType);

	struct Pack
	{
		std::string name;
		std::string description;
		std::string sex = "any";  // any / male / female, used only to pick defaults
		int         modeOverride = -1;  // -1 = use the user's setting
		float       basePitch = 1.0f;
		float       pitchVariation = -1.0f;  // -1 = use the user's setting
		float       speed = 1.0f;
		float       gain = 1.0f;
		float       syllablesPerBlip = -1.0f;  // -1 = use the user's setting
		// voice character offsets, added to the user's sliders
		float throat = 0.0f;
		float breath = 0.0f;
		float brightness = 0.0f;
		float expression = 0.0f;

		std::array<std::vector<std::shared_ptr<Wav::Info>>, static_cast<int>(Category::kTotal)> clips;

		[[nodiscard]] bool Has(Category a_cat) const { return !clips[static_cast<int>(a_cat)].empty(); }
		[[nodiscard]] bool HasPhrases() const { return Has(Category::kShort) || Has(Category::kMedium) || Has(Category::kLong); }
		[[nodiscard]] bool Usable() const
		{
			for (const auto& c : clips) {
				if (!c.empty()) {
					return true;
				}
			}
			return false;
		}
		[[nodiscard]] std::size_t EmotionClipCount() const
		{
			std::size_t n = 0;
			for (int c = static_cast<int>(Category::kAnger); c < static_cast<int>(Category::kTotal); ++c) {
				n += clips[c].size();
			}
			return n;
		}
		[[nodiscard]] std::size_t Count(Category a_cat) const { return clips[static_cast<int>(a_cat)].size(); }
	};

	// Scans the voices folder. Safe to call again (Rescan button).
	void Scan();

	[[nodiscard]] std::vector<std::string> Names();
	[[nodiscard]] std::shared_ptr<const Pack> Find(const std::string& a_name);

	// The pack for the player's sex according to the settings, falling back
	// to any usable pack.
	[[nodiscard]] std::shared_ptr<const Pack> ForPlayer();

	// Random clip from a category, avoiding the clip played last time from
	// that pack + category. nullptr if the category is empty.
	[[nodiscard]] std::shared_ptr<Wav::Info> Pick(const Pack& a_pack, Category a_cat, std::mt19937& a_rng);

	[[nodiscard]] std::filesystem::path RootOnDisk();
	inline constexpr const char* kGameRoot = "Sound\\FX\\SilentProtagonistVoice\\voices";
}
