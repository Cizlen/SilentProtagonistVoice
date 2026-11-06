#include "PCH.h"

#include "Planner.h"

#include "Settings.h"

namespace Planner
{
	namespace
	{
		using Cat = VoicePacks::Category;

		// phoneme indices (BSFaceGenKeyframeMultiple::Phoneme) a clip may shape the
		// mouth with while it sounds: Eh, Oh, I, OohQ, Eee, Aah
		constexpr std::array<int, 6> kVowels{ 6, 11, 8, 12, 5, 0 };

		struct Ctx
		{
			const Text::Line&        line;
			const VoicePacks::Pack&  pack;
			const Settings::Config&  cfg;
			std::mt19937&            rng;
			Emotion::Prosody         prosody;
			float                    basePitch;
			float                    variation;
			float                    speed;
			float                    gain;
			float                    expression;  // 0..2.5, 1 = normal
			float                    tremble;

			float Uni(float a_lo, float a_hi) { return std::uniform_real_distribution<float>(a_lo, a_hi)(rng); }
			int   Vowel() { return kVowels[std::uniform_int_distribution<std::size_t>(0, kVowels.size() - 1)(rng)]; }
		};

		float Clamp(float a_p) { return std::clamp(a_p, 0.5f, 2.0f); }

		// one whole clip from a folder, with a glide
		std::optional<PlannedClip> Whole(Ctx& a_c, Cat a_cat, float a_start, float a_glide)
		{
			auto wav = VoicePacks::Pick(a_c.pack, a_cat, a_c.rng);
			if (!wav) {
				return std::nullopt;
			}
			const float p = Clamp(a_c.basePitch * (1.0f + a_c.Uni(-a_c.variation, a_c.variation) * 0.5f));
			return PlannedClip{ .wav = std::move(wav), .start = a_start, .pitch = p, .pitchEnd = Clamp(p * (1.0f + a_glide)),
				.tremble = a_c.tremble, .volume = a_c.gain, .vowel = a_c.Vowel() };
		}

		// syllable blips, timed by the text, starting at a_t
		float Babble(Ctx& a_c, Plan& a_plan, float a_t)
		{
			const auto& cfg = a_c.cfg;
			const float perBlip = a_c.pack.syllablesPerBlip > 0.0f ? a_c.pack.syllablesPerBlip : cfg.syllablesPerBlip;
			const float ex = a_c.expression;

			std::vector<int> perSeg;
			int              total = 0;
			for (const auto& s : a_c.line.segments) {
				const int n = std::max(1, static_cast<int>(std::lround(s.syllables / perBlip)));
				perSeg.push_back(n);
				total += n;
			}
			if (total > cfg.maxBlips) {  // squeeze long lines
				const float k = static_cast<float>(cfg.maxBlips) / static_cast<float>(total);
				total = 0;
				for (auto& n : perSeg) {
					n = std::max(1, static_cast<int>(std::lround(n * k)));
					total += n;
				}
			}

			float t = a_t;
			int   k = 0;
			for (std::size_t si = 0; si < perSeg.size(); ++si) {
				for (int b = 0; b < perSeg[si]; ++b, ++k) {
					auto wav = VoicePacks::Pick(a_c.pack, Cat::kSyllables, a_c.rng);
					if (!wav) {
						return t;
					}
					const float x = total > 1 ? static_cast<float>(k) / static_cast<float>(total - 1) : 1.0f;
					float       p = a_c.basePitch * (1.0f + a_c.Uni(-a_c.variation, a_c.variation));
					float       v = a_c.gain * a_c.Uni(0.82f, 1.0f);
					// every syllable slides a little; emotion sets the direction
					float glide = (a_c.prosody.glide + a_c.Uni(-0.03f, 0.03f)) * ex;

					if (cfg.intonation) {
						switch (a_c.line.tone) {
						case Text::Tone::kQuestion:
							if (k == total - 1) {
								p *= 1.0f + 0.2f * ex;
								glide += 0.12f * ex;  // the last syllable lifts as it goes
							} else if (k == total - 2) {
								p *= 1.0f + 0.08f * ex;
							}
							break;
						case Text::Tone::kExclaim:
							p *= 1.0f + 0.07f * ex;
							v *= 1.1f;
							if (k == 0) {
								glide += 0.05f * ex;
							}
							break;
						case Text::Tone::kTrailing:
							p *= 1.0f - 0.08f * ex * x;
							v *= 1.0f - 0.45f * x;
							glide -= 0.03f * ex;
							break;
						default:  // gentle declination, the last syllable drops
							p *= 1.0f + (0.03f - 0.05f * x) * ex;
							if (k == total - 1 && total > 2) {
								p *= 1.0f - 0.07f * ex;
								glide -= 0.06f * ex;
							}
							break;
						}
						if (k == 0) {
							v *= 1.08f;  // a touch of attack on the first syllable
						}
					}

					p = Clamp(p);
					PlannedClip clip{ .wav = std::move(wav), .start = t, .pitch = p, .pitchEnd = Clamp(p * (1.0f + glide)),
						.tremble = a_c.tremble, .volume = v, .vowel = a_c.Vowel() };
					const float len = clip.Length();  // spacing follows the clip as recorded...
					if (cfg.playback == Settings::Playback::kProcessed && cfg.blend > 0.0f) {
						// ...but it sounds a little longer and softer, so it rings into the next one
						clip.stretch = 1.0f + 0.45f * cfg.blend;
						clip.soften = cfg.blend;
					}
					a_plan.clips.push_back(std::move(clip));
					t += std::max(0.045f, len * (1.0f - cfg.overlap)) / a_c.speed;
				}
				if (si + 1 < perSeg.size()) {
					const bool hard = si < a_c.line.segments.size() && a_c.line.segments[si].hardStop;
					t += cfg.commaPause * (hard ? 1.7f : 1.0f) / a_c.speed;
				}
			}
			return t;
		}

		// one or two whole phrases picked by length and tone
		void Phrase(Ctx& a_c, Plan& a_plan, float a_t, std::string& a_used)
		{
			const auto& cfg = a_c.cfg;
			const auto& line = a_c.line;
			const Cat   want = line.syllables <= cfg.shortMaxSyllables  ? Cat::kShort :
			                   line.syllables <= cfg.mediumMaxSyllables ? Cat::kMedium :
			                                                              Cat::kLong;

			std::vector<Cat> order;
			if (want != Cat::kLong) {
				if (line.tone == Text::Tone::kQuestion) {
					order.insert(order.end(), { Cat::kQuestion, Cat::kPuzzled });
				} else if (line.tone == Text::Tone::kExclaim) {
					order.insert(order.end(), { Cat::kExclaim, Cat::kSurprise });
				}
			}
			switch (want) {
			case Cat::kShort:
				order.insert(order.end(), { Cat::kShort, Cat::kMedium, Cat::kLong });
				break;
			case Cat::kMedium:
				order.insert(order.end(), { Cat::kMedium, Cat::kShort, Cat::kLong });
				break;
			default:
				order.insert(order.end(), { Cat::kLong, Cat::kMedium, Cat::kShort });
				break;
			}
			// a pack with only question/exclaim clips still says something
			order.insert(order.end(), { Cat::kQuestion, Cat::kExclaim });

			Cat used = want;
			for (auto c : order) {
				if (a_c.pack.Has(c)) {
					used = c;
					break;
				}
			}

			float glide = a_c.prosody.glide * a_c.expression;
			if (cfg.intonation && line.tone == Text::Tone::kQuestion && used != Cat::kQuestion && used != Cat::kPuzzled) {
				glide += 0.07f * a_c.expression;  // make a plain clip ask
			}
			auto first = Whole(a_c, used, a_t, glide);
			if (!first) {
				return;
			}
			const float len = first->Length();
			a_plan.clips.push_back(std::move(*first));
			a_used = VoicePacks::kFolderNames[static_cast<int>(used)];

			// very long lines: follow up with a second phrase
			if (cfg.chainLongLines && line.syllables > cfg.mediumMaxSyllables * 2) {
				const Cat second = a_c.pack.Has(Cat::kLong) ? Cat::kLong : a_c.pack.Has(Cat::kMedium) ? Cat::kMedium : used;
				if (auto c2 = Whole(a_c, second, a_t + len + 0.12f, glide - 0.03f * a_c.expression)) {
					c2->volume *= 0.95f;
					a_plan.clips.push_back(std::move(*c2));
					a_used += " + follow-up";
				}
			}
		}
	}

	Plan Build(const Text::Line& a_line, const Emotion::Mood& a_mood, const VoicePacks::Pack& a_pack, std::mt19937& a_rng)
	{
		const auto cfg = Settings::Snapshot();
		Plan       plan;
		plan.tone = a_line.tone;
		plan.mood = cfg.emotionSource == Settings::EmotionSource::kOff ? Emotion::Mood{} : a_mood;

		const auto  prosody = Emotion::Blend(plan.mood, cfg.emotionIntensity);
		const float moodAmount = plan.mood.Neutral() ? 0.0f : std::clamp(plan.mood.strength * cfg.emotionIntensity, 0.0f, 1.5f);
		const float expression = std::clamp(cfg.expression + a_pack.expression + prosody.expression, 0.0f, 2.5f);
		const float variation = (a_pack.pitchVariation >= 0.0f ? a_pack.pitchVariation : cfg.pitchVariation) *
		                        prosody.variation * (0.5f + 0.5f * expression);

		Ctx c{ a_line, a_pack, cfg, a_rng, prosody,
			cfg.pitch * a_pack.basePitch * prosody.pitch,
			variation,
			std::max(0.2f, cfg.speed * a_pack.speed * prosody.speed),
			cfg.volume * a_pack.gain * prosody.volume,
			expression,
			plan.mood.type == Emotion::Type::kFear ? 0.025f * moodAmount : 0.0f };

		plan.character = {
			std::clamp(cfg.throat + a_pack.throat, -1.0f, 1.0f),
			std::clamp(cfg.breath + a_pack.breath + prosody.breath, 0.0f, 1.0f),
			std::clamp(cfg.brightness + a_pack.brightness + prosody.brightness, -1.0f, 1.0f),
		};

		// ------------------------------------------------------------ emotional reaction
		const Cat  emoCat = VoicePacks::ForEmotion(static_cast<int>(plan.mood.type));
		const bool canReact = cfg.reactions != Settings::Reactions::kOff && emoCat != Cat::kTotal &&
		                      a_pack.Has(emoCat) && moodAmount >= 0.15f;
		const bool isShort = a_line.syllables <= cfg.shortMaxSyllables;

		float       t = 0.0f;
		std::string what;
		if (canReact && isShort) {
			if (auto r = Whole(c, emoCat, 0.0f, prosody.glide * expression)) {
				r->reaction = true;
				plan.clips.push_back(std::move(*r));
				plan.summary = std::format("{} reaction", Emotion::Name(plan.mood.type));
			}
		}

		if (plan.clips.empty()) {
			if (canReact && cfg.reactions == Settings::Reactions::kLeadIn && moodAmount >= 0.35f) {
				if (auto r = Whole(c, emoCat, 0.0f, prosody.glide * expression)) {
					r->reaction = true;
					t = r->Length() + 0.1f / c.speed;
					plan.clips.push_back(std::move(*r));
					what = std::format("{} lead-in + ", Emotion::Name(plan.mood.type));
				}
			}

			auto mode = static_cast<Settings::Mode>(a_pack.modeOverride >= 0 ? a_pack.modeOverride : static_cast<int>(cfg.mode));
			const bool hasSyl = a_pack.Has(Cat::kSyllables);
			const bool hasPhr = a_pack.HasPhrases() || a_pack.Has(Cat::kQuestion) || a_pack.Has(Cat::kExclaim);
			bool       useBabble = false;
			switch (mode) {
			case Settings::Mode::kBabble:
				useBabble = hasSyl;
				break;
			case Settings::Mode::kPhrase:
				useBabble = !hasPhr;
				break;
			default:
				// short lines sound better as one natural reaction ("Hm?"), longer ones as babble
				useBabble = hasSyl && !(hasPhr && isShort);
				break;
			}

			if (useBabble) {
				const auto before = plan.clips.size();
				Babble(c, plan, t);
				what += std::format("babble, {} blips", plan.clips.size() - before);
			} else {
				std::string used;
				Phrase(c, plan, t, used);
				what += "phrase, " + (used.empty() ? "nothing"s : used);
			}
			plan.summary = what;
		}

		// length and the hard cap
		float length = 0.0f;
		std::erase_if(plan.clips, [&](const PlannedClip& a_clip) { return a_clip.start >= cfg.maxLineSeconds; });
		for (const auto& clip : plan.clips) {
			length = std::max(length, clip.start + clip.Length());
		}
		plan.length = std::min(length, cfg.maxLineSeconds);
		if (!plan.mood.Neutral()) {
			plan.summary += std::format(", {} {:.0f}% ({})", Emotion::Name(plan.mood.type), plan.mood.strength * 100.0f, plan.mood.source);
		}
		plan.summary += std::format(", {:.2f}s", plan.length);
		return plan;
	}
}
