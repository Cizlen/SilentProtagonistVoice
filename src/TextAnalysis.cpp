#include "PCH.h"

#include "TextAnalysis.h"

namespace Text
{
	namespace
	{
		// UTF-8 -> code points, invalid bytes become U+FFFD
		std::u32string Decode(std::string_view a_s)
		{
			std::u32string out;
			out.reserve(a_s.size());
			for (std::size_t i = 0; i < a_s.size();) {
				const auto c = static_cast<unsigned char>(a_s[i]);
				char32_t   cp = 0xFFFD;
				std::size_t len = 1;
				if (c < 0x80) {
					cp = c;
				} else if ((c >> 5) == 0x6 && i + 1 < a_s.size()) {
					cp = ((c & 0x1F) << 6) | (a_s[i + 1] & 0x3F);
					len = 2;
				} else if ((c >> 4) == 0xE && i + 2 < a_s.size()) {
					cp = ((c & 0x0F) << 12) | ((a_s[i + 1] & 0x3F) << 6) | (a_s[i + 2] & 0x3F);
					len = 3;
				} else if ((c >> 3) == 0x1E && i + 3 < a_s.size()) {
					cp = ((c & 0x07) << 18) | ((a_s[i + 1] & 0x3F) << 12) | ((a_s[i + 2] & 0x3F) << 6) | (a_s[i + 3] & 0x3F);
					len = 4;
				}
				out.push_back(cp);
				i += len;
			}
			return out;
		}

		std::string Encode(const std::u32string& a_s)
		{
			std::string out;
			for (char32_t cp : a_s) {
				if (cp < 0x80) {
					out.push_back(static_cast<char>(cp));
				} else if (cp < 0x800) {
					out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				} else if (cp < 0x10000) {
					out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				} else {
					out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
					out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
				}
			}
			return out;
		}

		char32_t Fold(char32_t c)
		{
			if (c >= U'A' && c <= U'Z') {
				return c + 32;
			}
			if (c >= 0xC0 && c <= 0xDE && c != 0xD7) {  // Latin-1 capitals
				return c + 32;
			}
			if (c >= 0x410 && c <= 0x42F) {  // Cyrillic capitals
				return c + 32;
			}
			return c;
		}

		bool IsVowel(char32_t c)
		{
			c = Fold(c);
			static constexpr std::u32string_view vowels =
				U"aeiouy"
				U"àáâãäåæèéêëìíîï"
				U"òóôõöøùúûüýÿœ"
				U"аеёиоуыэюя";  // а е ё и о у ы э ю я
			return vowels.find(c) != std::u32string_view::npos;
		}

		// scripts where one character is roughly one syllable
		bool IsSyllabic(char32_t c)
		{
			return (c >= 0x3040 && c <= 0x30FF) ||  // kana
			       (c >= 0x3400 && c <= 0x9FFF) ||  // CJK
			       (c >= 0xAC00 && c <= 0xD7AF) ||  // Hangul
			       (c >= 0xF900 && c <= 0xFAFF);
		}

		bool IsLetter(char32_t c)
		{
			const auto f = Fold(c);
			return (f >= U'a' && f <= U'z') || (c >= 0xC0 && c <= 0x24F && c != 0xD7 && c != 0xF7) ||
			       (c >= 0x400 && c <= 0x4FF) || c == U'\'' || c == 0x2019;
		}

		int WordSyllables(const std::u32string& a_word)
		{
			std::u32string w;
			for (auto c : a_word) {
				if (c != U'\'' && c != 0x2019) {
					w.push_back(Fold(c));
				}
			}
			if (w.empty()) {
				return 0;
			}

			int  count = 0;
			bool prevVowel = false;
			for (std::size_t i = 0; i < w.size(); ++i) {
				const bool v = IsVowel(w[i]) && !(w[i] == U'y' && i == 0);
				if (v && !prevVowel) {
					++count;
				}
				prevVowel = v;
			}

			// English silent 'e' ("make", "there") but not "-le" ("table") or "the"/"be"
			const auto n = w.size();
			if (count > 1 && n > 2 && w[n - 1] == U'e' && !IsVowel(w[n - 2]) && !(w[n - 2] == U'l' && !IsVowel(w[n - 3]))) {
				--count;
			}
			// "-ed" after a consonant other than t/d is usually silent ("walked")
			if (count > 1 && n > 3 && w[n - 1] == U'd' && w[n - 2] == U'e' && !IsVowel(w[n - 3]) && w[n - 3] != U't' && w[n - 3] != U'd') {
				--count;
			}
			return std::max(count, 1);
		}

		int NumberSyllables(const std::u32string& a_digits)
		{
			// "1000" said aloud is ~2-4 syllables; keep it simple
			return std::clamp(static_cast<int>(a_digits.size()) + 1, 1, 6);
		}
	}

	const char* ToneName(Tone a_tone)
	{
		switch (a_tone) {
		case Tone::kQuestion:
			return "question";
		case Tone::kExclaim:
			return "exclaim";
		case Tone::kTrailing:
			return "trailing";
		default:
			return "statement";
		}
	}

	Line Analyze(std::string_view a_text)
	{
		Line line;
		auto cps = Decode(a_text);

		// drop (stage directions), [tags], <markup>, {braces} and *actions*
		std::u32string spoken;
		int            depth = 0;
		bool           inStars = false;
		for (auto c : cps) {
			if (c == U'(' || c == U'[' || c == U'<' || c == U'{') {
				++depth;
				continue;
			}
			if (c == U')' || c == U']' || c == U'>' || c == U'}') {
				depth = std::max(0, depth - 1);
				continue;
			}
			if (c == U'*') {
				inStars = !inStars;
				continue;
			}
			if (depth == 0 && !inStars) {
				spoken.push_back(c);
			}
		}

		// trim
		auto isSpace = [](char32_t c) { return c == U' ' || c == U'\t' || c == U'\n' || c == U'\r' || c == 0xA0 || c == 0x3000; };
		while (!spoken.empty() && isSpace(spoken.back())) {
			spoken.pop_back();
		}
		std::size_t lead = 0;
		while (lead < spoken.size() && isSpace(spoken[lead])) {
			++lead;
		}
		spoken.erase(0, lead);
		line.spoken = Encode(spoken);

		// tone from the tail
		{
			std::u32string tail;
			for (auto it = spoken.rbegin(); it != spoken.rend() && tail.size() < 4; ++it) {
				if (*it == U'"' || *it == U'\'' || *it == 0x201D || *it == 0x2019 || *it == 0xBB) {
					continue;  // closing quotes
				}
				if (*it == U'?' || *it == U'!' || *it == U'.' || *it == 0x2026 || *it == 0xFF1F || *it == 0xFF01) {
					tail.push_back(*it);
				} else {
					break;
				}
			}
			const bool q = tail.find(U'?') != std::u32string::npos || tail.find(0xFF1F) != std::u32string::npos;
			const bool e = tail.find(U'!') != std::u32string::npos || tail.find(0xFF01) != std::u32string::npos;
			const bool dots = tail.find(0x2026) != std::u32string::npos || std::ranges::count(tail, U'.') >= 2;
			line.tone = q ? Tone::kQuestion : e ? Tone::kExclaim : dots ? Tone::kTrailing : Tone::kStatement;
		}

		// words, syllables, segments
		Segment        seg;
		std::u32string word, digits;
		auto           flushWord = [&]() {
            if (!word.empty()) {
                const int s = WordSyllables(word);
                seg.syllables += s;
                line.syllables += s;
                ++line.words;
                word.clear();
            }
            if (!digits.empty()) {
                const int s = NumberSyllables(digits);
                seg.syllables += s;
                line.syllables += s;
                ++line.words;
                digits.clear();
            }
		};
		auto flushSegment = [&](bool a_hard) {
			flushWord();
			if (seg.syllables > 0) {
				seg.hardStop = a_hard;
				line.segments.push_back(seg);
			}
			seg = {};
		};

		for (auto c : spoken) {
			if (IsSyllabic(c)) {
				flushWord();
				seg.syllables += 1;
				line.syllables += 1;
				++line.words;
			} else if (c >= U'0' && c <= U'9') {
				if (!word.empty()) {
					flushWord();
				}
				digits.push_back(c);
			} else if (IsLetter(c)) {
				if (!digits.empty()) {
					flushWord();
				}
				word.push_back(c);
			} else if (c == U',' || c == U';' || c == U':' || c == 0x2014 || c == 0x2013 || c == 0x3001 || c == 0xFF0C) {
				flushSegment(false);
			} else if (c == U'.' || c == U'!' || c == U'?' || c == 0x2026 || c == 0x3002 || c == 0xFF01 || c == 0xFF1F) {
				flushSegment(true);
			} else {
				flushWord();
			}
		}
		flushSegment(true);

		return line;
	}
}
