#include "PCH.h"

#include "Render.h"

namespace Render
{
	namespace
	{
		using Key = std::array<int, 3>;

		struct Entry
		{
			std::weak_ptr<Wav::Info> owner;  // guards against a rescan reusing the address
			Key                      key{};
			Buffer                   data;
		};

		std::mutex                                       g_lock;
		std::unordered_map<const Wav::Info*, Entry>      g_cache;
		std::atomic<std::uint64_t>                       g_warmGeneration{ 0 };

		Key MakeKey(const Dsp::Character& a_c)
		{
			auto q = [](float a_v) { return static_cast<int>(std::lround(a_v * 100.0f)); };
			return { q(a_c.throat), q(a_c.breath), q(a_c.brightness) };
		}

		Dsp::Character FromKey(const Key& a_k)
		{
			return { a_k[0] / 100.0f, a_k[1] / 100.0f, a_k[2] / 100.0f };
		}
	}

	Buffer Shaped(const std::shared_ptr<Wav::Info>& a_wav, const Dsp::Character& a_character)
	{
		if (!a_wav || !a_wav->HasPcm()) {
			return nullptr;
		}
		const auto key = MakeKey(a_character);
		{
			std::scoped_lock lock{ g_lock };
			if (auto it = g_cache.find(a_wav.get());
				it != g_cache.end() && it->second.key == key && it->second.owner.lock() == a_wav) {
				return it->second.data;
			}
		}
		// quantised parameters, so a cached and a fresh render are identical
		auto out = std::make_shared<const std::vector<float>>(Dsp::Shape(a_wav->pcm, a_wav->sampleRate, FromKey(key)));
		std::scoped_lock lock{ g_lock };
		g_cache[a_wav.get()] = Entry{ a_wav, key, out };
		return out;
	}

	Buffer Clip(const Planner::PlannedClip& a_clip, const Dsp::Character& a_character)
	{
		auto shaped = Shaped(a_clip.wav, a_character);
		if (!shaped) {
			return nullptr;
		}
		const bool blend = a_clip.stretch > 1.001f || a_clip.soften > 0.0f;
		const bool repitch = std::fabs(a_clip.pitch - 1.0f) >= 1e-4f || std::fabs(a_clip.pitchEnd - 1.0f) >= 1e-4f || a_clip.tremble > 0.0f;
		if (!blend && !repitch) {
			return shaped;
		}
		std::vector<float> out = a_clip.stretch > 1.001f ? Dsp::Stretch(*shaped, a_clip.wav->sampleRate, a_clip.stretch) : *shaped;
		if (repitch) {
			out = Dsp::Repitch(out, a_clip.wav->sampleRate, a_clip.pitch, a_clip.pitchEnd, a_clip.tremble);
		}
		Dsp::Soften(out, a_clip.soften);
		return std::make_shared<const std::vector<float>>(std::move(out));
	}

	std::vector<float> Mix(const Planner::Plan& a_plan, std::uint32_t a_sampleRate)
	{
		std::vector<float> out(static_cast<std::size_t>((a_plan.length + 0.05f) * static_cast<float>(a_sampleRate)), 0.0f);
		for (const auto& c : a_plan.clips) {
			auto buf = Clip(c, a_plan.character);
			if (!buf) {
				continue;
			}
			const auto* samples = buf.get();
			std::vector<float> converted;
			if (c.wav->sampleRate != a_sampleRate) {
				const float ratio = static_cast<float>(c.wav->sampleRate) / static_cast<float>(a_sampleRate);
				converted = Dsp::Repitch(*buf, c.wav->sampleRate, ratio, ratio);
				samples = &converted;
			}
			const auto start = static_cast<std::size_t>(c.start * static_cast<float>(a_sampleRate));
			for (std::size_t i = 0; i < samples->size() && start + i < out.size(); ++i) {
				out[start + i] += (*samples)[i] * c.volume;
			}
		}
		float peak = 0.0f;
		for (float v : out) {
			peak = std::max(peak, std::fabs(v));
		}
		if (peak > 0.98f) {
			for (auto& v : out) {
				v *= 0.98f / peak;
			}
		}
		return out;
	}

	void Warm(std::shared_ptr<const VoicePacks::Pack> a_pack, Dsp::Character a_character)
	{
		if (!a_pack || a_character.Neutral()) {
			return;
		}
		const auto generation = ++g_warmGeneration;
		std::thread([a_pack = std::move(a_pack), a_character, generation]() {
			for (const auto& list : a_pack->clips) {
				for (const auto& wav : list) {
					if (g_warmGeneration.load() != generation) {
						return;  // the sliders moved again; a newer warm-up is running
					}
					(void)Shaped(wav, a_character);
				}
			}
		}).detach();
	}

	void ClearCache()
	{
		++g_warmGeneration;
		std::scoped_lock lock{ g_lock };
		g_cache.clear();
	}
}
