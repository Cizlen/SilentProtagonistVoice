#pragma once

// Minimal RIFF/WAVE reader. We only need the clip length (to schedule clips
// and hold the NPC) and a loudness envelope (to move the mouth). The engine
// does the actual decoding and playback.

namespace Wav
{
	struct Info
	{
		std::string        gamePath;        // Data-relative, backslashes: Sound\FX\...
		float              duration = 0.0f;  // seconds at pitch 1.0
		std::uint16_t      format = 0;       // 1 = PCM, 3 = float
		std::uint16_t      channels = 0;
		std::uint32_t      sampleRate = 0;
		std::uint16_t      bits = 0;
		std::vector<float> envelope;  // normalised 0..1, one value per kEnvStep seconds
		std::vector<float> pcm;       // mono samples -1..1 for the voice processor (empty if undecodable)

		static constexpr float kEnvStep = 0.01f;

		[[nodiscard]] bool  HasEnvelope() const { return !envelope.empty(); }
		[[nodiscard]] bool  HasPcm() const { return !pcm.empty(); }
		[[nodiscard]] float Sample(float a_t) const;  // a_t in clip seconds
	};

	// a_diskPath: path relative to the game folder (Data\...).
	std::shared_ptr<Info> Load(const std::filesystem::path& a_diskPath, std::string a_gamePath);
}
