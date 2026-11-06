#pragma once

// The mood a line is said in. Uses Skyrim's own eight dialogue emotions (the
// same values as RE::EmotionType / the TRDT emotion of every INFO response),
// so it can come straight from the game's dialogue data.

namespace Emotion
{
	enum class Type : int
	{
		kNeutral = 0,
		kAnger,
		kDisgust,
		kFear,
		kSad,
		kHappy,
		kSurprise,
		kPuzzled,
		kTotal
	};

	struct Mood
	{
		Type        type = Type::kNeutral;
		float       strength = 0.0f;   // 0..1 (the game stores 0..100)
		const char* source = "none";  // for the log: "tag", "reply", "none"

		[[nodiscard]] bool Neutral() const { return type == Type::kNeutral || strength <= 0.0f; }
	};

	// How a mood bends the voice. Neutral is all zero / one; the planner
	// scales the difference by mood strength and the user's intensity.
	struct Prosody
	{
		float pitch = 1.0f;       // multiplier
		float speed = 1.0f;       // multiplier
		float volume = 1.0f;      // multiplier
		float variation = 1.0f;   // multiplier on random pitch spread
		float expression = 0.0f;  // added to the Expression setting
		float brightness = 0.0f;  // added to the Brightness setting
		float breath = 0.0f;      // added to the Breath setting
		float glide = 0.0f;       // per-syllable pitch slide (+ rises, - falls)
	};

	[[nodiscard]] const char*  Name(Type a_type);           // "anger"
	[[nodiscard]] const char*  DisplayName(Type a_type);    // "Anger"
	[[nodiscard]] Prosody      ProsodyFor(Type a_type);
	[[nodiscard]] Prosody      Blend(const Mood& a_mood, float a_intensity);

	// Speech-check and action tags in the option text: "(Intimidate)",
	// "(Persuade)", "[Brawl]"... An explicit tag beats everything else.
	[[nodiscard]] std::optional<Mood> FromTags(std::string_view a_rawText);

	// Expression index for BSFaceGenAnimationData::SetExpressionOverride
	// (DialogueAnger..DialogueDisgusted), -1 for neutral.
	[[nodiscard]] int FaceExpression(Type a_type);
}
