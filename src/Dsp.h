#pragma once

// Offline voice shaping for short clips. Pure math, no engine calls, so it can
// run on any thread and be tested outside the game.
//
//   Shape()   changes the timbre but keeps pitch and length:
//             throat     - moves the formants (vocal-tract size) without moving pitch
//             brightness - tilts the spectrum (dark/muffled .. bright/crisp)
//             breath     - blends in aspiration noise shaped like the voice
//   Repitch() changes pitch (and so length), with a glide from start to end
//             pitch and optional tremble.
//   Stretch() changes length but keeps pitch (WSOLA), for babble that rings
//             into the next syllable.
//   Soften()  rounds a clip's onset and release so overlapping clips melt
//             together while each keeps its peak.

namespace Dsp
{
	struct Character
	{
		float throat = 0.0f;      // -1 .. +1
		float breath = 0.0f;      //  0 .. 1
		float brightness = 0.0f;  // -1 .. +1

		[[nodiscard]] bool Neutral() const
		{
			return std::fabs(throat) < 0.01f && breath < 0.01f && std::fabs(brightness) < 0.01f;
		}
	};

	std::vector<float> Shape(const std::vector<float>& a_in, std::uint32_t a_sampleRate, const Character& a_character);

	std::vector<float> Repitch(const std::vector<float>& a_in, std::uint32_t a_sampleRate, float a_pitchStart, float a_pitchEnd,
		float a_trembleDepth = 0.0f, float a_trembleHz = 6.0f);

	std::vector<float> Stretch(const std::vector<float>& a_in, std::uint32_t a_sampleRate, float a_factor);

	void Soften(std::vector<float>& a_buf, float a_amount);

	// formant scale for a throat value: +1 -> 0.73 (deeper), -1 -> 1.37 (smaller)
	[[nodiscard]] inline float FormantFactor(float a_throat) { return std::exp2(-0.45f * std::clamp(a_throat, -1.0f, 1.0f)); }
}
