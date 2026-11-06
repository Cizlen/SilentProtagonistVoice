#include "PCH.h"

#include "WavFile.h"

namespace Wav
{
	namespace
	{
		template <class T>
		bool ReadPOD(std::istream& a_in, T& a_out)
		{
			return static_cast<bool>(a_in.read(reinterpret_cast<char*>(&a_out), sizeof(T)));
		}

		// one sample of channel 0..n mixed to mono, as -1..1
		float DecodeFrame(const std::uint8_t* a_p, std::uint16_t a_format, std::uint16_t a_bits, std::uint16_t a_channels)
		{
			float       sum = 0.0f;
			const auto  bytes = a_bits / 8;
			for (std::uint16_t c = 0; c < a_channels; ++c) {
				const auto* s = a_p + static_cast<std::size_t>(c) * bytes;
				float       v = 0.0f;
				if (a_format == 3 && a_bits == 32) {
					std::memcpy(&v, s, 4);
				} else if (a_bits == 8) {
					v = (static_cast<int>(s[0]) - 128) / 128.0f;
				} else if (a_bits == 16) {
					std::int16_t i;
					std::memcpy(&i, s, 2);
					v = i / 32768.0f;
				} else if (a_bits == 24) {
					const auto i = static_cast<std::int32_t>((std::uint32_t(s[0]) << 8) | (std::uint32_t(s[1]) << 16) | (std::uint32_t(s[2]) << 24));
					v = (i >> 8) / 8388608.0f;
				} else if (a_bits == 32) {
					std::int32_t i;
					std::memcpy(&i, s, 4);
					v = static_cast<float>(i / 2147483648.0);
				}
				sum += v;
			}
			return sum / std::max<std::uint16_t>(a_channels, 1);
		}
	}

	float Info::Sample(float a_t) const
	{
		if (envelope.empty() || a_t < 0.0f) {
			return 0.0f;
		}
		const float pos = a_t / kEnvStep;
		const auto  i = static_cast<std::size_t>(pos);
		if (i + 1 >= envelope.size()) {
			return i < envelope.size() ? envelope[i] : 0.0f;
		}
		const float f = pos - static_cast<float>(i);
		return envelope[i] + (envelope[i + 1] - envelope[i]) * f;
	}

	std::shared_ptr<Info> Load(const std::filesystem::path& a_diskPath, std::string a_gamePath)
	{
		std::ifstream in(a_diskPath, std::ios::binary);
		if (!in) {
			logger::warn("Cannot open {}", a_diskPath.string());
			return nullptr;
		}

		char riff[4], wave[4];
		std::uint32_t riffSize = 0;
		if (!in.read(riff, 4) || !ReadPOD(in, riffSize) || !in.read(wave, 4) ||
			std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(wave, "WAVE", 4) != 0) {
			logger::warn("{} is not a RIFF/WAVE file", a_diskPath.string());
			return nullptr;
		}

		auto info = std::make_shared<Info>();
		info->gamePath = std::move(a_gamePath);

		std::uint16_t blockAlign = 0;
		bool          haveFmt = false;
		std::vector<std::uint8_t> data;

		while (in) {
			char          id[4];
			std::uint32_t size = 0;
			if (!in.read(id, 4) || !ReadPOD(in, size)) {
				break;
			}
			if (std::memcmp(id, "fmt ", 4) == 0) {
				std::vector<std::uint8_t> fmt(size);
				if (!in.read(reinterpret_cast<char*>(fmt.data()), size) || size < 16) {
					break;
				}
				std::memcpy(&info->format, &fmt[0], 2);
				std::memcpy(&info->channels, &fmt[2], 2);
				std::memcpy(&info->sampleRate, &fmt[4], 4);
				std::memcpy(&blockAlign, &fmt[12], 2);
				std::memcpy(&info->bits, &fmt[14], 2);
				if (info->format == 0xFFFE && size >= 26) {  // WAVE_FORMAT_EXTENSIBLE: real tag in the sub-format GUID
					std::memcpy(&info->format, &fmt[24], 2);
				}
				haveFmt = true;
			} else if (std::memcmp(id, "data", 4) == 0) {
				data.resize(size);
				in.read(reinterpret_cast<char*>(data.data()), size);
				data.resize(static_cast<std::size_t>(in.gcount()));
			} else {
				in.seekg(size, std::ios::cur);
			}
			if (size & 1) {
				in.seekg(1, std::ios::cur);  // chunks are word aligned
			}
		}

		if (!haveFmt || info->sampleRate == 0 || blockAlign == 0 || data.empty()) {
			logger::warn("{}: missing fmt/data chunk", a_diskPath.string());
			return nullptr;
		}

		const std::size_t frames = data.size() / blockAlign;
		info->duration = static_cast<float>(frames) / static_cast<float>(info->sampleRate);

		const bool decodable = (info->format == 1 && (info->bits == 8 || info->bits == 16 || info->bits == 24 || info->bits == 32)) ||
		                       (info->format == 3 && info->bits == 32);
		if (!decodable) {
			logger::info("{}: format {} / {} bit has no envelope, mouth will use a procedural flap",
				a_diskPath.string(), info->format, info->bits);
			return info;
		}
		if (info->format != 1 || info->bits != 16) {
			logger::info("{}: {} bit format {}; 16-bit PCM is the safest format for the game's sound engine",
				a_diskPath.string(), info->bits, info->format);
		}

		info->pcm.resize(frames);
		for (std::size_t f = 0; f < frames; ++f) {
			info->pcm[f] = DecodeFrame(data.data() + f * blockAlign, info->format, info->bits, info->channels);
		}

		// RMS per 10 ms window, normalised against the loudest window of the clip
		const auto window = std::max<std::size_t>(1, static_cast<std::size_t>(info->sampleRate * Info::kEnvStep));
		info->envelope.reserve(frames / window + 1);
		float peak = 0.0f;
		for (std::size_t start = 0; start < frames; start += window) {
			const auto end = std::min(frames, start + window);
			double     acc = 0.0;
			for (std::size_t f = start; f < end; ++f) {
				const float v = info->pcm[f];
				acc += static_cast<double>(v) * v;
			}
			const float rms = static_cast<float>(std::sqrt(acc / static_cast<double>(end - start)));
			info->envelope.push_back(rms);
			peak = std::max(peak, rms);
		}
		if (peak > 1e-5f) {
			for (auto& e : info->envelope) {
				// a soft knee so quiet consonants still crack the lips open
				e = std::pow(e / peak, 0.7f);
			}
		} else {
			info->envelope.clear();
		}
		return info;
	}
}
