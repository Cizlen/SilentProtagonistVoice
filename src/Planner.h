#pragma once

#include "Dsp.h"
#include "Emotion.h"
#include "TextAnalysis.h"
#include "VoicePacks.h"

// Text + mood + voice pack + settings -> a timed list of clips. Pure logic: no
// engine calls, so it can be tested outside the game.

namespace Planner
{
	struct PlannedClip
	{
		std::shared_ptr<Wav::Info> wav;
		float                      start = 0.0f;     // seconds from the start of the line
		float                      pitch = 1.0f;     // playback rate at the start of the clip
		float                      pitchEnd = 1.0f;  // ...and at its end (a glide; engine playback uses `pitch` only)
		float                      tremble = 0.0f;   // pitch wobble depth (fear)
		float                      volume = 1.0f;    // 0..1 before the voice slider
		int                        vowel = 0;        // phoneme shape used while this clip sounds
		bool                       reaction = false; // an emotion clip rather than speech
		float                      stretch = 1.0f;   // longer without changing pitch (processed playback, babble blend)
		float                      soften = 0.0f;    // 0..1 rounded onset/release (processed playback, babble blend)

		[[nodiscard]] float AvgPitch() const { return 0.5f * (pitch + pitchEnd); }
		[[nodiscard]] float Length() const { return wav ? wav->duration * stretch / AvgPitch() : 0.0f; }
		// clip-file time at a moment since the clip started (for the mouth)
		[[nodiscard]] float FileTime(float a_sinceStart) const { return a_sinceStart * AvgPitch() / stretch; }
	};

	struct Plan
	{
		std::vector<PlannedClip> clips;
		float                    length = 0.0f;
		Text::Tone               tone = Text::Tone::kStatement;
		Emotion::Mood            mood;
		Dsp::Character           character;  // timbre for processed playback
		std::string              summary;    // for the log / menu

		[[nodiscard]] bool Empty() const { return clips.empty(); }
	};

	Plan Build(const Text::Line& a_line, const Emotion::Mood& a_mood, const VoicePacks::Pack& a_pack, std::mt19937& a_rng);
}
