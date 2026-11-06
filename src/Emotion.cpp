#include "PCH.h"

#include "Emotion.h"

namespace Emotion
{
	namespace
	{
		std::string Lower(std::string_view a_s)
		{
			std::string out(a_s);
			std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return out;
		}

		float Lerp(float a_from, float a_to, float a_t) { return a_from + (a_to - a_from) * a_t; }
	}

	const char* Name(Type a_type)
	{
		static constexpr std::array names{ "neutral", "anger", "disgust", "fear", "sad", "happy", "surprise", "puzzled" };
		const auto                  i = static_cast<std::size_t>(a_type);
		return i < names.size() ? names[i] : "neutral";
	}

	const char* DisplayName(Type a_type)
	{
		static constexpr std::array names{ "Neutral", "Anger", "Disgust", "Fear", "Sad", "Happy", "Surprise", "Puzzled" };
		const auto                  i = static_cast<std::size_t>(a_type);
		return i < names.size() ? names[i] : "Neutral";
	}

	Prosody ProsodyFor(Type a_type)
	{
		//                pitch speed  vol  vari  expr   bright breath glide
		switch (a_type) {
		case Type::kAnger:
			return { 0.94f, 1.12f, 1.15f, 0.8f, 0.25f, 0.35f, -0.05f, -0.04f };  // low, clipped, hard
		case Type::kDisgust:
			return { 0.93f, 0.92f, 0.95f, 0.7f, -0.1f, -0.15f, 0.10f, -0.06f };  // low, slow, sneering fall
		case Type::kFear:
			return { 1.10f, 1.18f, 0.85f, 1.5f, 0.15f, 0.05f, 0.25f, 0.03f };   // high, quick, shaky, breathy
		case Type::kSad:
			return { 0.91f, 0.82f, 0.80f, 0.6f, -0.35f, -0.30f, 0.20f, -0.05f };  // low, slow, flat, soft
		case Type::kHappy:
			return { 1.07f, 1.06f, 1.05f, 1.3f, 0.30f, 0.15f, 0.0f, 0.04f };     // up, lively, bouncy
		case Type::kSurprise:
			return { 1.12f, 1.05f, 1.10f, 1.4f, 0.45f, 0.20f, 0.05f, 0.08f };   // high and rising
		case Type::kPuzzled:
			return { 1.02f, 0.92f, 0.95f, 1.1f, 0.20f, 0.0f, 0.05f, 0.06f };    // hesitant, lifting
		default:
			return {};
		}
	}

	Prosody Blend(const Mood& a_mood, float a_intensity)
	{
		const float t = std::clamp(a_mood.strength * a_intensity, 0.0f, 1.5f);
		if (a_mood.Neutral() || t <= 0.0f) {
			return {};
		}
		const Prosody n{};
		const auto    e = ProsodyFor(a_mood.type);
		return {
			Lerp(n.pitch, e.pitch, t),
			Lerp(n.speed, e.speed, t),
			Lerp(n.volume, e.volume, t),
			Lerp(n.variation, e.variation, t),
			Lerp(n.expression, e.expression, t),
			Lerp(n.brightness, e.brightness, t),
			Lerp(n.breath, e.breath, t),
			Lerp(n.glide, e.glide, t),
		};
	}

	std::optional<Mood> FromTags(std::string_view a_rawText)
	{
		const auto text = Lower(a_rawText);
		struct Tag
		{
			const char* word;
			Type        type;
			float       strength;
		};
		// vanilla speech checks plus the common ones mods add
		static constexpr std::array tags{
			Tag{ "intimidat", Type::kAnger, 0.75f },
			Tag{ "threaten", Type::kAnger, 0.7f },
			Tag{ "brawl", Type::kAnger, 0.6f },
			Tag{ "attack", Type::kAnger, 0.6f },
			Tag{ "insult", Type::kDisgust, 0.6f },
			Tag{ "mock", Type::kDisgust, 0.5f },
			Tag{ "persuade", Type::kHappy, 0.4f },
			Tag{ "charm", Type::kHappy, 0.5f },
			Tag{ "flirt", Type::kHappy, 0.6f },
			Tag{ "seduce", Type::kHappy, 0.6f },
			Tag{ "joke", Type::kHappy, 0.6f },
			Tag{ "laugh", Type::kHappy, 0.7f },
			Tag{ "comfort", Type::kSad, 0.4f },
			Tag{ "console", Type::kSad, 0.4f },
			Tag{ "apolog", Type::kSad, 0.4f },
			Tag{ "beg", Type::kFear, 0.5f },
			Tag{ "plead", Type::kFear, 0.5f },
			Tag{ "lie", Type::kPuzzled, 0.25f },
		};

		// only look inside (...) [...] <...> - "persuade" in a sentence is just a word
		std::size_t pos = 0;
		while (pos < text.size()) {
			const auto open = text.find_first_of("([<", pos);
			if (open == std::string::npos) {
				break;
			}
			const char closeCh = text[open] == '(' ? ')' : text[open] == '[' ? ']' : '>';
			const auto close = text.find(closeCh, open + 1);
			if (close == std::string::npos) {
				break;
			}
			const std::string_view inner(text.data() + open + 1, close - open - 1);
			for (const auto& t : tags) {
				if (inner.find(t.word) != std::string_view::npos) {
					return Mood{ t.type, t.strength, "tag" };
				}
			}
			pos = close + 1;
		}
		return std::nullopt;
	}

	int FaceExpression(Type a_type)
	{
		// BSFaceGenKeyframeMultiple::Expression: DialogueAnger 0, Fear 1, Happy 2,
		// Sad 3, Surprise 4, Puzzled 5, Disgusted 6
		switch (a_type) {
		case Type::kAnger:
			return 0;
		case Type::kFear:
			return 1;
		case Type::kHappy:
			return 2;
		case Type::kSad:
			return 3;
		case Type::kSurprise:
			return 4;
		case Type::kPuzzled:
			return 5;
		case Type::kDisgust:
			return 6;
		default:
			return -1;
		}
	}
}
