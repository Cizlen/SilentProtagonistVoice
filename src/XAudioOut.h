#pragma once

#include "Render.h"

// A tiny XAudio2 player for processed playback. The plugin renders each clip
// itself (voice shaping, glides) and hands the samples here. xaudio2_9.dll
// ships with Windows 10/11; it's loaded at runtime, so there's nothing to link.
// All functions are thread-safe.

namespace XAudioOut
{
	using Id = std::uint32_t;

	[[nodiscard]] bool Available();  // initialises on first call

	Id   Play(Render::Buffer a_samples, std::uint32_t a_sampleRate, float a_volume);
	void Stop(Id a_id, float a_fadeSeconds);
	void StopAll();
	[[nodiscard]] bool IsPlaying(Id a_id);

	// fades and frees finished voices; call regularly (Speech::Tick does)
	void Update(float a_dt);
}
