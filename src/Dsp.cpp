#include "PCH.h"

#include "Dsp.h"

#include <complex>
#include <numbers>

namespace Dsp
{
	std::vector<float> Stretch(const std::vector<float>& a_in, std::uint32_t a_sampleRate, float a_factor)
	{
		// WSOLA: overlap-add windowed grains read at 1/factor speed, each grain
		// nudged (within a few ms) to where it best continues the previous one,
		// so pitch periods line up and the voice doesn't phase or warble.
		const std::size_t n = a_in.size();
		const auto        frame = static_cast<std::size_t>(a_sampleRate * 0.024);
		if (a_factor < 1.01f || n < frame * 2) {
			return a_in;
		}
		const std::size_t hop = frame / 2;
		const auto        tol = static_cast<std::ptrdiff_t>(a_sampleRate * 0.008);
		const std::size_t outLen = static_cast<std::size_t>(static_cast<double>(n) * a_factor);
		std::vector<float> out(outLen + frame, 0.0f), norm(outLen + frame, 0.0f), win(frame);
		for (std::size_t i = 0; i < frame; ++i) {
			win[i] = 0.5f - 0.5f * std::cos(6.2831853f * static_cast<float>(i) / static_cast<float>(frame));
		}
		std::ptrdiff_t prev = 0;
		for (std::size_t ps = 0; ps + frame <= out.size(); ps += hop) {
			const auto nominal = static_cast<std::ptrdiff_t>(static_cast<double>(ps) / a_factor);
			std::ptrdiff_t pos = nominal;
			if (ps > 0) {
				// the natural continuation of the last grain is the template
				const std::ptrdiff_t natural = prev + static_cast<std::ptrdiff_t>(hop);
				if (natural + static_cast<std::ptrdiff_t>(frame) <= static_cast<std::ptrdiff_t>(n)) {
					double best = -1e30;
					for (std::ptrdiff_t d = -tol; d <= tol; d += 2) {
						const std::ptrdiff_t cand = nominal + d;
						if (cand < 0 || cand + static_cast<std::ptrdiff_t>(frame) > static_cast<std::ptrdiff_t>(n)) {
							continue;
						}
						double acc = 0.0;
						for (std::size_t i = 0; i < frame; i += 2) {
							acc += static_cast<double>(a_in[natural + i]) * a_in[cand + i];
						}
						if (acc > best) {
							best = acc;
							pos = cand;
						}
					}
				}
			}
			if (pos < 0 || pos + static_cast<std::ptrdiff_t>(frame) > static_cast<std::ptrdiff_t>(n)) {
				pos = std::clamp<std::ptrdiff_t>(pos, 0, static_cast<std::ptrdiff_t>(n - frame));
			}
			for (std::size_t i = 0; i < frame; ++i) {
				out[ps + i] += a_in[pos + i] * win[i];
				norm[ps + i] += win[i];
			}
			prev = pos;
		}
		for (std::size_t i = 0; i < out.size(); ++i) {
			out[i] /= std::max(norm[i], 0.5f);  // the very ends just fade, never blow up
		}
		out.resize(outLen);
		return out;
	}

	void Soften(std::vector<float>& a_buf, float a_amount)
	{
		// raised-cosine in over the first ~30 %, out over the last ~45 %: the
		// middle (where the syllable peaks) is untouched
		const std::size_t n = a_buf.size();
		a_amount = std::clamp(a_amount, 0.0f, 1.0f);
		if (n < 8 || a_amount <= 0.0f) {
			return;
		}
		const auto in = static_cast<std::size_t>(static_cast<float>(n) * 0.30f * a_amount);
		const auto rel = static_cast<std::size_t>(static_cast<float>(n) * 0.45f * a_amount);
		for (std::size_t i = 0; i < in; ++i) {
			a_buf[i] *= 0.5f - 0.5f * std::cos(3.14159265f * static_cast<float>(i) / static_cast<float>(in));
		}
		for (std::size_t i = 0; i < rel; ++i) {
			a_buf[n - 1 - i] *= 0.5f - 0.5f * std::cos(3.14159265f * static_cast<float>(i) / static_cast<float>(rel));
		}
	}

	namespace
	{
		using cpx = std::complex<float>;

		constexpr int kN = 1024;  // ~23 ms at 44.1 kHz
		constexpr int kHop = kN / 4;

		void FFT(std::vector<cpx>& a_data, bool a_inverse)
		{
			const auto n = a_data.size();
			for (std::size_t i = 1, j = 0; i < n; ++i) {  // bit reversal
				std::size_t bit = n >> 1;
				for (; j & bit; bit >>= 1) {
					j ^= bit;
				}
				j ^= bit;
				if (i < j) {
					std::swap(a_data[i], a_data[j]);
				}
			}
			for (std::size_t len = 2; len <= n; len <<= 1) {
				const float ang = 2.0f * std::numbers::pi_v<float> / static_cast<float>(len) * (a_inverse ? 1.0f : -1.0f);
				const cpx   wlen(std::cos(ang), std::sin(ang));
				for (std::size_t i = 0; i < n; i += len) {
					cpx w(1.0f, 0.0f);
					for (std::size_t k = 0; k < len / 2; ++k) {
						const cpx u = a_data[i + k];
						const cpx v = a_data[i + k + len / 2] * w;
						a_data[i + k] = u + v;
						a_data[i + k + len / 2] = u - v;
						w *= wlen;
					}
				}
			}
			if (a_inverse) {
				const float inv = 1.0f / static_cast<float>(n);
				for (auto& x : a_data) {
					x *= inv;
				}
			}
		}

		const std::array<float, kN>& Hann()
		{
			static const auto w = []() {
				std::array<float, kN> out{};
				for (int i = 0; i < kN; ++i) {
					out[i] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * static_cast<float>(i) / static_cast<float>(kN));
				}
				return out;
			}();
			return w;
		}

		float Rms(const std::vector<float>& a_x)
		{
			double acc = 0.0;
			for (float v : a_x) {
				acc += static_cast<double>(v) * v;
			}
			return a_x.empty() ? 0.0f : static_cast<float>(std::sqrt(acc / static_cast<double>(a_x.size())));
		}

		// cubic Hermite sample at fractional position
		float SampleAt(const std::vector<float>& a_x, double a_pos)
		{
			const auto  i = static_cast<std::int64_t>(std::floor(a_pos));
			const float t = static_cast<float>(a_pos - static_cast<double>(i));
			auto        at = [&](std::int64_t k) {
                if (k < 0 || k >= static_cast<std::int64_t>(a_x.size())) {
                    return 0.0f;
                }
                return a_x[static_cast<std::size_t>(k)];
			};
			const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
			const float c0 = y1;
			const float c1 = 0.5f * (y2 - y0);
			const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
			const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
			return ((c3 * t + c2) * t + c1) * t + c0;
		}
	}

	std::vector<float> Shape(const std::vector<float>& a_in, std::uint32_t a_sampleRate, const Character& a_c)
	{
		if (a_in.empty() || a_c.Neutral()) {
			return a_in;
		}

		const auto&  w = Hann();
		const float  formant = FormantFactor(a_c.throat);
		const float  breath = std::clamp(a_c.breath, 0.0f, 1.0f);
		// perceptually even slider: noise comes in early, the tone fades late
		const float  noiseAmt = std::pow(breath, 0.5f) * 0.9f;
		const float  toneAmt = 1.0f - 0.85f * std::pow(breath, 1.4f);
		const float  bright = std::clamp(a_c.brightness, -1.0f, 1.0f);
		const float  sr = static_cast<float>(a_sampleRate);
		const int    lifter = std::clamp(static_cast<int>(sr * 0.0012f), 12, kN / 4);  // ~1.2 ms: envelope, not harmonics
		const int    half = kN / 2;

		// padded copy so the first and last frames are complete
		std::vector<float> x(a_in.size() + 2 * kN, 0.0f);
		std::copy(a_in.begin(), a_in.end(), x.begin() + kN);
		std::vector<float> y(x.size(), 0.0f), norm(x.size(), 0.0f);

		// per-bin brightness tilt (natural-log gain) and breath colour, fixed for the clip
		std::vector<float> tilt(half + 1), hiss(half + 1);
		for (int k = 0; k <= half; ++k) {
			const float f = static_cast<float>(k) * sr / static_cast<float>(kN);
			tilt[k] = bright * 0.9f * std::log((f + 300.0f) / 1300.0f);
			// aspiration falls off ~6 dB/oct less than the voiced tone: tilt the noise up
			hiss[k] = std::clamp((f + 150.0f) / 900.0f, 0.2f, 6.0f);
		}

		std::mt19937                          rng(12345u + static_cast<std::uint32_t>(a_in.size()));
		std::uniform_real_distribution<float> phaseDist(0.0f, 2.0f * std::numbers::pi_v<float>);

		std::vector<cpx>   spec(kN), ceps(kN);
		std::vector<float> logMag(half + 1), env(half + 1), phase(half + 1), lin(half + 1), smooth(half + 1);
		const int          smoothW = std::max(2, static_cast<int>(std::lround(250.0f / (sr / static_cast<float>(kN)))));

		for (std::size_t pos = 0; pos + kN <= x.size(); pos += kHop) {
			for (int i = 0; i < kN; ++i) {
				spec[i] = cpx(x[pos + i] * w[i], 0.0f);
			}
			FFT(spec, false);

			for (int k = 0; k <= half; ++k) {
				lin[k] = std::abs(spec[k]);
				logMag[k] = std::log(lin[k] + 1e-7f);
				phase[k] = std::arg(spec[k]);
			}
			// average energy around each bin (~250 Hz either side): the level breath noise has to reach
			if (breath > 0.0f) {
				double acc = 0.0;
				int    count = 0;
				for (int k = 0; k <= std::min(half, smoothW); ++k) {
					acc += static_cast<double>(lin[k]) * lin[k];
					++count;
				}
				for (int k = 0; k <= half; ++k) {
					smooth[k] = static_cast<float>(std::sqrt(std::max(0.0, acc) / std::max(1, count)));
					if (const int add = k + smoothW + 1; add <= half) {
						acc += static_cast<double>(lin[add]) * lin[add];
						++count;
					}
					if (const int drop = k - smoothW; drop >= 0) {
						acc -= static_cast<double>(lin[drop]) * lin[drop];
						--count;
					}
				}
			}

			// spectral envelope by cepstral smoothing
			for (int k = 0; k < kN; ++k) {
				const int m = k <= half ? k : kN - k;
				ceps[k] = cpx(logMag[m], 0.0f);
			}
			FFT(ceps, true);
			for (int q = lifter; q <= kN - lifter; ++q) {
				ceps[q] = 0.0f;
			}
			FFT(ceps, false);
			for (int k = 0; k <= half; ++k) {
				env[k] = ceps[k].real();
			}

			for (int k = 0; k <= half; ++k) {
				// formants move, harmonics (the fine structure) stay where they are
				const float src = static_cast<float>(k) / formant;
				float       newEnv, noiseLevel = 0.0f;
				if (src >= static_cast<float>(half)) {
					newEnv = env[half] - 4.0f;  // nothing above the old top: fade it out
				} else {
					const int   i0 = static_cast<int>(src);
					const int   i1 = std::min(i0 + 1, half);
					const float t = src - static_cast<float>(i0);
					newEnv = env[i0] + (env[i1] - env[i0]) * t;
					noiseLevel = smooth[i0] + (smooth[i1] - smooth[i0]) * t;
				}
				const float fine = logMag[k] - env[k];
				const float harmonic = std::exp(fine + newEnv + tilt[k]) * toneAmt;
				const float noise = breath > 0.0f ? noiseLevel * std::exp(tilt[k]) * noiseAmt * hiss[k] : 0.0f;
				spec[k] = std::polar(harmonic, phase[k]) + std::polar(noise, phaseDist(rng));
			}
			for (int k = 1; k < half; ++k) {
				spec[kN - k] = std::conj(spec[k]);
			}
			spec[0] = cpx(spec[0].real(), 0.0f);
			spec[half] = cpx(spec[half].real(), 0.0f);
			FFT(spec, true);

			for (int i = 0; i < kN; ++i) {
				y[pos + i] += spec[i].real() * w[i];
				norm[pos + i] += w[i] * w[i];
			}
		}

		std::vector<float> out(a_in.size());
		for (std::size_t i = 0; i < out.size(); ++i) {
			const float n = norm[i + kN];
			const float v = n > 1e-4f ? y[i + kN] / n : 0.0f;
			out[i] = std::isfinite(v) ? v : 0.0f;
		}

		// keep the loudness of the original, never clip
		const float inRms = Rms(a_in), outRms = Rms(out);
		float       gain = outRms > 1e-6f ? inRms / outRms : 1.0f;
		float       peak = 0.0f;
		for (float v : out) {
			peak = std::max(peak, std::fabs(v));
		}
		if (peak * gain > 0.98f) {
			gain = 0.98f / peak;
		}
		for (auto& v : out) {
			v *= gain;
		}
		return out;
	}

	std::vector<float> Repitch(const std::vector<float>& a_in, std::uint32_t a_sampleRate, float a_p0, float a_p1,
		float a_trembleDepth, float a_trembleHz)
	{
		if (a_in.empty()) {
			return {};
		}
		a_p0 = std::clamp(a_p0, 0.25f, 4.0f);
		a_p1 = std::clamp(a_p1, 0.25f, 4.0f);
		if (std::fabs(a_p0 - 1.0f) < 1e-4f && std::fabs(a_p1 - 1.0f) < 1e-4f && a_trembleDepth <= 0.0f) {
			return a_in;
		}

		const double avg = 0.5 * (a_p0 + a_p1);
		const auto   outLen = static_cast<std::size_t>(static_cast<double>(a_in.size()) / avg);
		std::vector<float> out;
		out.reserve(outLen + 16);

		const double sr = static_cast<double>(a_sampleRate);
		double       pos = 0.0;
		for (std::size_t j = 0; pos < static_cast<double>(a_in.size() - 1); ++j) {
			const double u = outLen > 0 ? std::min(1.0, static_cast<double>(j) / static_cast<double>(outLen)) : 1.0;
			double       r = a_p0 + (a_p1 - a_p0) * u;
			if (a_trembleDepth > 0.0f) {
				r *= 1.0 + a_trembleDepth * std::sin(2.0 * std::numbers::pi * a_trembleHz * static_cast<double>(j) / sr);
			}
			out.push_back(SampleAt(a_in, pos));
			pos += r;
		}

		// 3 ms fade at the tail so a cut never clicks
		const auto fade = std::min<std::size_t>(out.size(), static_cast<std::size_t>(sr * 0.003));
		for (std::size_t i = 0; i < fade; ++i) {
			out[out.size() - 1 - i] *= static_cast<float>(i) / static_cast<float>(fade);
		}
		return out;
	}
}
