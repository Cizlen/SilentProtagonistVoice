#pragma once

// Turns a dialogue option's text into the numbers the voice needs:
// how many syllables, where the pauses fall, and how it ends.

namespace Text
{
	enum class Tone
	{
		kStatement,
		kQuestion,
		kExclaim,
		kTrailing  // "..." - fades out
	};

	struct Segment
	{
		int  syllables = 0;
		bool hardStop = false;  // ended with . ! ? (longer pause) rather than , ; :
	};

	struct Line
	{
		std::string          spoken;  // text with (stage directions) and [tags] removed
		int                  words = 0;
		int                  syllables = 0;
		Tone                 tone = Tone::kStatement;
		std::vector<Segment> segments;

		[[nodiscard]] bool Silent() const { return syllables == 0; }
	};

	Line Analyze(std::string_view a_text);

	const char* ToneName(Tone a_tone);
}
