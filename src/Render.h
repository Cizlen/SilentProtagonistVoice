#pragma once

#include "Planner.h"

// Turns planned clips into final sample buffers for processed playback, with a
// cache so each pack clip is timbre-shaped once per voice setting. Thread-safe.

namespace Render
{
	using Buffer = std::shared_ptr<const std::vector<float>>;

	// the clip's samples with throat / breath / brightness applied (same pitch and length)
	Buffer Shaped(const std::shared_ptr<Wav::Info>& a_wav, const Dsp::Character& a_character);

	// shaped + repitched (glide, tremble); still at the clip's own sample rate
	Buffer Clip(const Planner::PlannedClip& a_clip, const Dsp::Character& a_character);

	// the whole line mixed to mono at a_sampleRate, clip volumes applied (preview and tests)
	std::vector<float> Mix(const Planner::Plan& a_plan, std::uint32_t a_sampleRate);

	// shape every clip of a pack on a background thread so the first line doesn't wait
	void Warm(std::shared_ptr<const VoicePacks::Pack> a_pack, Dsp::Character a_character);

	void ClearCache();
}
