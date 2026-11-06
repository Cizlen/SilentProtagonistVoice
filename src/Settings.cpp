#include "PCH.h"

#include "Settings.h"

#include <SimpleIni.h>

namespace Settings
{
	namespace
	{
		Config     g_config;
		std::mutex g_lock;

		float ReadF(CSimpleIniA& a_ini, const char* a_sec, const char* a_key, float a_def, float a_min, float a_max)
		{
			const auto v = static_cast<float>(a_ini.GetDoubleValue(a_sec, a_key, a_def));
			return std::clamp(v, a_min, a_max);
		}

		int ReadI(CSimpleIniA& a_ini, const char* a_sec, const char* a_key, int a_def, int a_min, int a_max)
		{
			const auto v = static_cast<int>(a_ini.GetLongValue(a_sec, a_key, a_def));
			return std::clamp(v, a_min, a_max);
		}

		// written via std::format so the file says 0.85, not 0.84999999999999998
		void SetF(CSimpleIniA& a_ini, const char* a_sec, const char* a_key, float a_v, const char* a_comment = nullptr)
		{
			const double v = std::round(static_cast<double>(a_v) * 10000.0) / 10000.0;
			a_ini.SetValue(a_sec, a_key, std::format("{}", v).c_str(), a_comment);
		}

		bool ReadB(CSimpleIniA& a_ini, const char* a_sec, const char* a_key, bool a_def)
		{
			return a_ini.GetBoolValue(a_sec, a_key, a_def);
		}
	}

	Config& Get() { return g_config; }
	std::mutex& Lock() { return g_lock; }

	Config Snapshot()
	{
		std::scoped_lock lock{ g_lock };
		return g_config;
	}

	std::filesystem::path IniPath()
	{
		return std::filesystem::path("Data") / "SKSE" / "Plugins" / "SilentProtagonistVoice.ini";
	}

	void Load()
	{
		CSimpleIniA ini;
		ini.SetUnicode();
		const auto  path = IniPath();
		if (ini.LoadFile(path.string().c_str()) < 0) {
			logger::info("No INI at {}, using defaults", path.string());
			Save();
			return;
		}

		std::scoped_lock lock{ g_lock };
		auto&            c = g_config;
		const Config     d{};

		c.enabled = ReadB(ini, "General", "bEnabled", d.enabled);
		c.waitForPlayer = ReadB(ini, "General", "bNpcWaitsForPlayer", d.waitForPlayer);
		c.allowSkip = ReadB(ini, "General", "bClickToSkip", d.allowSkip);
		c.maxLineSeconds = ReadF(ini, "General", "fMaxLineSeconds", d.maxLineSeconds, 1.0f, 20.0f);
		c.responseDelay = ReadF(ini, "General", "fResponseDelay", d.responseDelay, 0.0f, 1.0f);

		c.packMale = ini.GetValue("Voice", "sPackMale", d.packMale.c_str());
		c.packFemale = ini.GetValue("Voice", "sPackFemale", d.packFemale.c_str());
		c.mode = static_cast<Mode>(ReadI(ini, "Voice", "iMode", static_cast<int>(d.mode), 0, 2));
		c.volume = ReadF(ini, "Voice", "fVolume", d.volume, 0.0f, 1.0f);
		c.followVoiceSlider = ReadB(ini, "Voice", "bFollowVoiceSlider", d.followVoiceSlider);
		c.pitch = ReadF(ini, "Voice", "fPitch", d.pitch, 0.5f, 2.0f);
		c.pitchVariation = ReadF(ini, "Voice", "fPitchVariation", d.pitchVariation, 0.0f, 0.4f);
		c.intonation = ReadB(ini, "Voice", "bIntonation", d.intonation);

		c.throat = ReadF(ini, "Character", "fThroat", d.throat, -1.0f, 1.0f);
		c.breath = ReadF(ini, "Character", "fBreath", d.breath, 0.0f, 1.0f);
		c.brightness = ReadF(ini, "Character", "fBrightness", d.brightness, -1.0f, 1.0f);
		c.expression = ReadF(ini, "Character", "fExpression", d.expression, 0.0f, 2.0f);

		c.emotionSource = static_cast<EmotionSource>(ReadI(ini, "Emotion", "iSource", static_cast<int>(d.emotionSource), 0, 2));
		c.emotionIntensity = ReadF(ini, "Emotion", "fIntensity", d.emotionIntensity, 0.0f, 2.0f);
		c.emotionThreshold = ReadI(ini, "Emotion", "iThreshold", d.emotionThreshold, 0, 100);
		c.reactions = static_cast<Reactions>(ReadI(ini, "Emotion", "iReactions", static_cast<int>(d.reactions), 0, 2));
		c.faceExpression = ReadB(ini, "Emotion", "bFaceExpression", d.faceExpression);
		c.faceIntensity = ReadF(ini, "Emotion", "fFaceIntensity", d.faceIntensity, 0.0f, 1.0f);

		c.syllablesPerBlip = ReadF(ini, "Babble", "fSyllablesPerBlip", d.syllablesPerBlip, 0.5f, 6.0f);
		c.speed = ReadF(ini, "Babble", "fSpeed", d.speed, 0.4f, 2.5f);
		c.maxBlips = ReadI(ini, "Babble", "iMaxBlips", d.maxBlips, 1, 80);
		c.commaPause = ReadF(ini, "Babble", "fCommaPause", d.commaPause, 0.0f, 0.8f);
		c.overlap = ReadF(ini, "Babble", "fOverlap", d.overlap, 0.0f, 0.6f);
		c.blend = ReadF(ini, "Babble", "fBlend", d.blend, 0.0f, 1.0f);

		c.shortMaxSyllables = ReadI(ini, "Phrase", "iShortMaxSyllables", d.shortMaxSyllables, 1, 40);
		c.mediumMaxSyllables = ReadI(ini, "Phrase", "iMediumMaxSyllables", d.mediumMaxSyllables, 2, 80);
		c.chainLongLines = ReadB(ini, "Phrase", "bChainLongLines", d.chainLongLines);

		c.lipSync = ReadB(ini, "LipSync", "bEnabled", d.lipSync);
		c.lipGain = ReadF(ini, "LipSync", "fGain", d.lipGain, 0.0f, 3.0f);
		c.jawOpen = ReadF(ini, "LipSync", "fJawOpen", d.jawOpen, 0.0f, 1.0f);
		c.vowelShapes = ReadB(ini, "LipSync", "bVowelShapes", d.vowelShapes);
		c.expressiveBrows = ReadB(ini, "LipSync", "bExpressiveBrows", d.expressiveBrows);
		c.mouthSmoothing = ReadF(ini, "LipSync", "fSmoothing", d.mouthSmoothing, 0.005f, 0.2f);

		c.playback = static_cast<Playback>(ReadI(ini, "Advanced", "iPlayback", static_cast<int>(d.playback), 0, 2));
		c.debugLog = ReadB(ini, "Advanced", "bDebugLog", d.debugLog);

		if (c.mediumMaxSyllables <= c.shortMaxSyllables) {
			c.mediumMaxSyllables = c.shortMaxSyllables + 1;
		}
	}

	void Save()
	{
		Config c;
		{
			std::scoped_lock lock{ g_lock };
			c = g_config;
		}

		CSimpleIniA ini;
		ini.SetUnicode();

		ini.SetBoolValue("General", "bEnabled", c.enabled);
		ini.SetBoolValue("General", "bNpcWaitsForPlayer", c.waitForPlayer, "; NPC replies only after your character finishes");
		ini.SetBoolValue("General", "bClickToSkip", c.allowSkip, "; clicking while your character hums skips to the NPC");
		SetF(ini, "General", "fMaxLineSeconds", c.maxLineSeconds, "; longest a single player line may run");
		SetF(ini, "General", "fResponseDelay", c.responseDelay);

		ini.SetValue("Voice", "sPackMale", c.packMale.c_str(), "; folder names under Data\\Sound\\FX\\SilentProtagonistVoice\\voices");
		ini.SetValue("Voice", "sPackFemale", c.packFemale.c_str());
		ini.SetLongValue("Voice", "iMode", static_cast<long>(c.mode), "; 0 = auto, 1 = babble, 2 = phrase");
		SetF(ini, "Voice", "fVolume", c.volume);
		ini.SetBoolValue("Voice", "bFollowVoiceSlider", c.followVoiceSlider);
		SetF(ini, "Voice", "fPitch", c.pitch);
		SetF(ini, "Voice", "fPitchVariation", c.pitchVariation);
		ini.SetBoolValue("Voice", "bIntonation", c.intonation);

		SetF(ini, "Character", "fThroat", c.throat, "; -1 small/young .. +1 large/deep (Processed playback)");
		SetF(ini, "Character", "fBreath", c.breath, "; 0 clear .. 1 whisper (Processed playback)");
		SetF(ini, "Character", "fBrightness", c.brightness, "; -1 dark .. +1 bright (Processed playback)");
		SetF(ini, "Character", "fExpression", c.expression, "; 0 monotone .. 2 very animated");

		ini.SetLongValue("Emotion", "iSource", static_cast<long>(c.emotionSource), "; 0 = off, 1 = speech-check tags only, 2 = tags + mood of the NPC's reply");
		SetF(ini, "Emotion", "fIntensity", c.emotionIntensity);
		ini.SetLongValue("Emotion", "iThreshold", c.emotionThreshold, "; reply emotion (0-100) below this counts as neutral");
		ini.SetLongValue("Emotion", "iReactions", static_cast<long>(c.reactions), "; 0 = off, 1 = short lines, 2 = short lines + lead-in for long lines");
		ini.SetBoolValue("Emotion", "bFaceExpression", c.faceExpression);
		SetF(ini, "Emotion", "fFaceIntensity", c.faceIntensity);

		SetF(ini, "Babble", "fSyllablesPerBlip", c.syllablesPerBlip);
		SetF(ini, "Babble", "fSpeed", c.speed);
		ini.SetLongValue("Babble", "iMaxBlips", c.maxBlips);
		SetF(ini, "Babble", "fCommaPause", c.commaPause);
		SetF(ini, "Babble", "fOverlap", c.overlap);
		SetF(ini, "Babble", "fBlend", c.blend);

		ini.SetLongValue("Phrase", "iShortMaxSyllables", c.shortMaxSyllables);
		ini.SetLongValue("Phrase", "iMediumMaxSyllables", c.mediumMaxSyllables);
		ini.SetBoolValue("Phrase", "bChainLongLines", c.chainLongLines);

		ini.SetBoolValue("LipSync", "bEnabled", c.lipSync);
		SetF(ini, "LipSync", "fGain", c.lipGain);
		SetF(ini, "LipSync", "fJawOpen", c.jawOpen);
		ini.SetBoolValue("LipSync", "bVowelShapes", c.vowelShapes);
		ini.SetBoolValue("LipSync", "bExpressiveBrows", c.expressiveBrows);
		SetF(ini, "LipSync", "fSmoothing", c.mouthSmoothing);

		ini.SetLongValue("Advanced", "iPlayback", static_cast<long>(c.playback), "; 0 = processed (full voice shaping), 1 = game sound engine, 2 = AudioUtil");
		ini.SetBoolValue("Advanced", "bDebugLog", c.debugLog);

		const auto path = IniPath();
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
		if (ini.SaveFile(path.string().c_str()) < 0) {
			logger::warn("Could not write {}", path.string());
		}
	}
}
