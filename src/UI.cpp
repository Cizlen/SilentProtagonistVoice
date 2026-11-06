#include "PCH.h"

#include "UI.h"

#include "AudioUtilBridge.h"
#include "Emotion.h"
#include "Settings.h"
#include "Speech.h"
#include "VoicePacks.h"
#include "XAudioOut.h"

// must come after CommonLib (PCH): it includes <windows.h>
#include "SKSEMenuFramework.h"

namespace UI
{
	namespace
	{
		namespace ImGui = ImGuiMCP;

		bool        g_dirty = false;
		char        g_previewText[256] = "Have you heard any news from the war? I hear the Stormcloaks are winning.";
		int         g_previewEmotion = 0;  // 0 = from the text's tags, else Emotion::Type
		float       g_previewStrength = 0.7f;
		std::string g_status;

		const ImGui::ImVec4 kGrey{ 0.62f, 0.62f, 0.62f, 1.0f };
		const ImGui::ImVec4 kWarn{ 1.0f, 0.65f, 0.3f, 1.0f };

		// a wrapped, dimmed explanation under the control above it
		void Desc(const char* a_text)
		{
			ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kGrey);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(a_text);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
			ImGui::Spacing();
		}

		void Warn(const char* a_text)
		{
			ImGui::PushStyleColor(ImGui::ImGuiCol_Text, kWarn);
			ImGui::PushTextWrapPos(0.0f);
			ImGui::TextUnformatted(a_text);
			ImGui::PopTextWrapPos();
			ImGui::PopStyleColor();
		}

		bool PackCombo(const char* a_label, std::string& a_value, const std::vector<std::string>& a_names)
		{
			bool changed = false;
			if (ImGui::BeginCombo(a_label, a_value.empty() ? "(none)" : a_value.c_str())) {
				for (const auto& n : a_names) {
					const bool selected = n == a_value;
					if (ImGui::Selectable(n.c_str(), selected)) {
						a_value = n;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			return changed;
		}

		void PackDetails(const std::string& a_name)
		{
			const auto pack = VoicePacks::Find(a_name);
			if (!pack) {
				ImGui::TextDisabled("  '%s' not found", a_name.c_str());
				return;
			}
			using C = VoicePacks::Category;
			ImGui::TextDisabled("  %zu syllables, %zu short, %zu medium, %zu long, %zu question, %zu exclaim, %zu emotion",
				pack->Count(C::kSyllables), pack->Count(C::kShort), pack->Count(C::kMedium), pack->Count(C::kLong),
				pack->Count(C::kQuestion), pack->Count(C::kExclaim), pack->EmotionClipCount());
			if (!pack->description.empty()) {
				ImGui::TextDisabled("  %s", pack->description.c_str());
			}
		}

		Emotion::Mood PreviewMood()
		{
			if (g_previewEmotion <= 0) {
				return Emotion::FromTags(g_previewText).value_or(Emotion::Mood{});
			}
			return Emotion::Mood{ static_cast<Emotion::Type>(g_previewEmotion), g_previewStrength, "preview" };
		}

		void PreviewButtons(const char* a_id)
		{
			ImGui::PushID(a_id);
			if (ImGui::Button("Preview")) {
				if (!Speech::PreviewNow(g_previewText, PreviewMood())) {
					g_status = XAudioOut::Available() ? "Nothing to say (no voice pack, or the line is silent)" :
					                                    "Preview needs XAudio2 (Windows 10/11)";
				}
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("Plays right now, with the menu open. Sound only, no mouth.");
			}
			ImGui::SameLine();
			if (ImGui::Button("Speak in game")) {
				Speech::Say(g_previewText, PreviewMood(), nullptr);
				g_status = "Queued: close the menu to hear it with the mouth moving (third person)";
			}
			ImGui::PopID();
		}

		void __stdcall Render()
		{
			auto       c = Settings::Snapshot();
			const auto names = VoicePacks::Names();
			const bool processed = c.playback == Settings::Playback::kProcessed;
			bool       changed = false;

			// ------------------------------------------------------------ test line
			ImGui::SeparatorText("Try it");
			ImGui::InputText("Line", g_previewText, sizeof(g_previewText));
			const char* emotions[] = { "From the text", "Anger", "Disgust", "Fear", "Sad", "Happy", "Surprise", "Puzzled" };
			ImGui::Combo("Preview mood", &g_previewEmotion, emotions, 8);
			if (g_previewEmotion > 0) {
				ImGui::SliderFloat("Preview mood strength", &g_previewStrength, 0.1f, 1.0f, "%.2f");
			}
			PreviewButtons("top");
			if (const auto last = Speech::LastSummary(); !last.empty()) {
				ImGui::TextWrapped("Last line: %s", last.c_str());
			}
			if (!g_status.empty()) {
				ImGui::TextDisabled("%s", g_status.c_str());
			}

			// ------------------------------------------------------------ general
			if (ImGui::CollapsingHeader("General", ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
				changed |= ImGui::Checkbox("Enabled", &c.enabled);
				changed |= ImGui::Checkbox("NPC waits for my character to finish", &c.waitForPlayer);
				Desc("On: your character's line plays first and the NPC answers after it, like older silent-protagonist RPGs. "
					 "Off: the NPC answers at once and the hum plays over them.");
				changed |= ImGui::Checkbox("Click to skip", &c.allowSkip);
				Desc("Clicking while your character is talking jumps straight to the NPC's reply.");
				changed |= ImGui::SliderFloat("Longest line (s)", &c.maxLineSeconds, 1.0f, 15.0f, "%.1f");
				Desc("Hard limit for one line. Very long dialogue options are squeezed to fit.");
				changed |= ImGui::SliderFloat("Pause before reply (s)", &c.responseDelay, 0.0f, 1.0f, "%.2f");
				Desc("A breath of silence between your character finishing and the NPC starting.");
			}

			// ------------------------------------------------------------ voice
			if (ImGui::CollapsingHeader("Voice", ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
				if (names.empty()) {
					Warn("No voice packs found in Data\\Sound\\FX\\SilentProtagonistVoice\\voices");
				}
				changed |= PackCombo("Male character", c.packMale, names);
				PackDetails(c.packMale);
				changed |= PackCombo("Female character", c.packFemale, names);
				PackDetails(c.packFemale);
				Desc("The voice pack used for each sex of player character. Packs are folders under Data\\Sound\\FX\\SilentProtagonistVoice\\voices.");

				const char* modes[] = { "Auto", "Babble", "Phrase" };
				int         mode = static_cast<int>(c.mode);
				if (ImGui::Combo("Mode", &mode, modes, 3)) {
					c.mode = static_cast<Settings::Mode>(mode);
					changed = true;
				}
				Desc("Babble: a string of short syllables, as many as the sentence is long. "
					 "Phrase: one whole recorded clip picked by length (short / medium / long). "
					 "Auto: phrase for short lines, babble for longer ones. A pack's voice.ini can force one.");
				changed |= ImGui::SliderFloat("Volume", &c.volume, 0.0f, 1.0f, "%.2f");
				changed |= ImGui::Checkbox("Scale with the game's Voice volume slider", &c.followVoiceSlider);
				changed |= ImGui::SliderFloat("Pitch", &c.pitch, 0.6f, 1.6f, "%.2f");
				Desc("Raises or lowers the whole voice. In processed and engine playback this also speeds it up or slows it down slightly, like a tape.");
				changed |= ImGui::SliderFloat("Pitch variation", &c.pitchVariation, 0.0f, 0.3f, "%.2f");
				Desc("How much each syllable's pitch wanders at random. 0 sounds robotic, around 0.06 sounds natural, high values sound sing-song.");
				changed |= ImGui::Checkbox("Intonation", &c.intonation);
				Desc("Questions rise at the end, statements settle down, exclamations push up, \"...\" trails off.");
			}

			// ------------------------------------------------------------ character
			if (ImGui::CollapsingHeader("Voice character", ImGui::ImGuiTreeNodeFlags_DefaultOpen)) {
				if (!processed) {
					Warn("Throat, Breath and Brightness only work with Processed playback (Advanced). Expression works everywhere.");
				}
				ImGui::BeginDisabled(!processed);
				changed |= ImGui::SliderFloat("Throat", &c.throat, -1.0f, 1.0f, "%.2f");
				Desc("The size of the voice. Right: a bigger, deeper throat (broad, older, orc-like). Left: a smaller one (younger, lighter, "
					 "elf- or child-like). It moves the resonances of the voice without changing its pitch, so it changes who is "
					 "speaking rather than how high they sound.");
				changed |= ImGui::SliderFloat("Breath", &c.breath, 0.0f, 1.0f, "%.2f");
				Desc("Air in the voice. 0 is a clear, ringing tone. Around 0.3 is soft and intimate, 0.6 is husky, 1 is a whisper. "
					 "Fear and sadness add some on top.");
				changed |= ImGui::SliderFloat("Brightness", &c.brightness, -1.0f, 1.0f, "%.2f");
				Desc("Tone colour. Left: darker and more muffled, as if behind a hood or a helmet. Right: crisper and more present. "
					 "Anger and happiness brighten it, sadness darkens it.");
				ImGui::EndDisabled();
				changed |= ImGui::SliderFloat("Expression", &c.expression, 0.0f, 2.0f, "%.2f");
				Desc("How animated the delivery is. 0 is a flat monotone. 1 is normal: questions rise, sentences settle, every syllable slides a "
					 "little. 2 is theatrical, with big rises and falls. Emotions add or remove expression.");
				if (ImGui::Button("Reset voice character")) {
					c.throat = 0.0f;
					c.breath = 0.0f;
					c.brightness = 0.0f;
					c.expression = 1.0f;
					changed = true;
				}
				ImGui::SameLine();
				PreviewButtons("character");
			}

			// ------------------------------------------------------------ emotion
			if (ImGui::CollapsingHeader("Emotion")) {
				const char* sources[] = { "Off", "Speech-check tags only", "Tags + mood of the NPC's reply" };
				int         source = static_cast<int>(c.emotionSource);
				if (ImGui::Combo("Emotion source", &source, sources, 3)) {
					c.emotionSource = static_cast<Settings::EmotionSource>(source);
					changed = true;
				}
				Desc("Where the mood of your line comes from. Tags: options like (Intimidate), (Persuade) or [Brawl] set anger, confidence and "
					 "so on. Reply mood: every scripted NPC reply in Skyrim has an emotion (anger, fear, sad, happy, surprise, puzzled, "
					 "disgust) and a strength; your character matches the mood of the exchange.");
				changed |= ImGui::SliderFloat("Emotion intensity", &c.emotionIntensity, 0.0f, 2.0f, "%.2f");
				Desc("How strongly a mood bends the voice: pitch, speed, loudness, breath, brightness, glides. 0 ignores moods, 1 is natural, 2 is exaggerated.");
				changed |= ImGui::SliderInt("Reply emotion threshold", &c.emotionThreshold, 0, 100);
				Desc("NPC reply emotions weaker than this (0-100 in the game's data) count as neutral, so mild lines don't colour your voice.");
				const char* reacts[] = { "Off", "Short lines", "Short lines + lead-in" };
				int         react = static_cast<int>(c.reactions);
				if (ImGui::Combo("Emotional reactions", &react, reacts, 3)) {
					c.reactions = static_cast<Settings::Reactions>(react);
					changed = true;
				}
				Desc("Needs a pack with emotion folders (anger, sad, happy...). Short lines: a short emotional line becomes one reaction sound "
					 "(a scoff, a sigh, a pleased hum). Lead-in: longer emotional lines also open with one before the babble.");
				changed |= ImGui::Checkbox("Facial expression", &c.faceExpression);
				ImGui::BeginDisabled(!c.faceExpression);
				changed |= ImGui::SliderFloat("Expression strength", &c.faceIntensity, 0.0f, 1.0f, "%.2f");
				ImGui::EndDisabled();
				Desc("Your character's face takes on the mood while talking and for a moment after. It leaves the face alone if another mod has "
					 "set an expression.");
			}

			// ------------------------------------------------------------ babble
			if (ImGui::CollapsingHeader("Babble")) {
				changed |= ImGui::SliderFloat("Syllables per blip", &c.syllablesPerBlip, 0.5f, 5.0f, "%.1f");
				Desc("How many written syllables each sound stands for. Higher means fewer, longer-spaced sounds per line.");
				changed |= ImGui::SliderFloat("Speed", &c.speed, 0.5f, 2.0f, "%.2f");
				Desc("Talking speed. Only the gaps change, not the pitch.");
				changed |= ImGui::SliderInt("Max blips per line", &c.maxBlips, 2, 60);
				changed |= ImGui::SliderFloat("Comma pause (s)", &c.commaPause, 0.0f, 0.6f, "%.2f");
				Desc("Silence at commas. Full stops inside a line get a bit more.");
				changed |= ImGui::SliderFloat("Overlap", &c.overlap, 0.0f, 0.5f, "%.2f");
				Desc("How much each syllable runs into the next. 0 is choppy and distinct, 0.3 or more is slurred and flowing.");
				ImGui::BeginDisabled(!processed);
				changed |= ImGui::SliderFloat("Blend", &c.blend, 0.0f, 1.0f, "%.2f");
				ImGui::EndDisabled();
				Desc(processed ?
				         "Lets each syllable ring softly into the next while keeping its own peak, so babble flows like speech "
				         "instead of ticking like keys. 0 plays the clips as recorded; the pace stays the same either way." :
				         "Needs Processed playback (Advanced): the engine and AudioUtil play the clips as they are.");
			}

			// ------------------------------------------------------------ phrase
			if (ImGui::CollapsingHeader("Phrase")) {
				changed |= ImGui::SliderInt("Short line: up to N syllables", &c.shortMaxSyllables, 1, 20);
				changed |= ImGui::SliderInt("Medium line: up to N syllables", &c.mediumMaxSyllables, 2, 40);
				Desc("Which folder a line draws from by its length: short, medium, or long beyond that.");
				changed |= ImGui::Checkbox("Add a second phrase to very long lines", &c.chainLongLines);
			}

			// ------------------------------------------------------------ lips
			if (ImGui::CollapsingHeader("Lip sync")) {
				changed |= ImGui::Checkbox("Animate mouth", &c.lipSync);
				changed |= ImGui::SliderFloat("Mouth gain", &c.lipGain, 0.0f, 2.5f, "%.2f");
				Desc("How much loudness opens the mouth. Raise it for quiet packs.");
				changed |= ImGui::SliderFloat("Jaw open", &c.jawOpen, 0.0f, 1.0f, "%.2f");
				Desc("The widest the jaw opens on the loudest sounds.");
				changed |= ImGui::Checkbox("Vowel shapes", &c.vowelShapes);
				Desc("Each syllable picks a different mouth shape, so it reads as speech rather than a flapping jaw.");
				changed |= ImGui::Checkbox("Expressive brows", &c.expressiveBrows);
				Desc("Brows lift on questions, puzzlement, surprise and exclamations.");
				changed |= ImGui::SliderFloat("Smoothing (s)", &c.mouthSmoothing, 0.005f, 0.15f, "%.3f");
				Desc("Low values are snappy, high values are soft and lazy.");
			}

			// ------------------------------------------------------------ advanced
			if (ImGui::CollapsingHeader("Advanced")) {
				const char* playbacks[] = { "Processed (full voice shaping)", "Game sound engine", "AudioUtil" };
				int         pb = static_cast<int>(c.playback);
				if (ImGui::Combo("Playback", &pb, playbacks, 3)) {
					c.playback = static_cast<Settings::Playback>(pb);
					changed = true;
				}
				Desc("Processed: the plugin shapes every clip itself (throat, breath, brightness, pitch glides, tremble) and plays it through its "
					 "own audio output. The sound is centred on you, not 3D. Game sound engine: 3D-positioned on your character, "
					 "pitch only. AudioUtil: AudioUtil plays and lip-syncs the clips, pitch is ignored.");
				ImGui::TextDisabled("In use: %s.  XAudio2: %s.  AudioUtil: %s", Speech::ActivePlaybackName(),
					XAudioOut::Available() ? "ready" : "unavailable", AudioUtilBridge::VersionString().c_str());
				changed |= ImGui::Checkbox("Debug log", &c.debugLog);
				Desc("Writes every line, its syllables, mood, reply emotion and chosen clips to SilentProtagonistVoice.log.");
				if (ImGui::Button("Rescan voice packs")) {
					VoicePacks::Scan();
					g_status = std::format("Found {} voice pack(s)", VoicePacks::Names().size());
				}
				ImGui::SameLine();
				if (ImGui::Button("Save now")) {
					Settings::Save();
					g_dirty = false;
					g_status = "Saved";
				}
				Desc("Settings also save automatically when the menu closes.");
			}

			if (changed) {
				if (c.mediumMaxSyllables <= c.shortMaxSyllables) {
					c.mediumMaxSyllables = c.shortMaxSyllables + 1;
				}
				std::scoped_lock lock{ Settings::Lock() };
				Settings::Get() = c;
				g_dirty = true;
			}
		}

		void __stdcall OnEvent(SKSEMenuFramework::Model::EventType a_event)
		{
			if (a_event == SKSEMenuFramework::Model::EventType::kCloseMenu && g_dirty) {
				Settings::Save();
				g_dirty = false;
			}
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled()) {
			logger::info("SKSE Menu Framework not found; edit SilentProtagonistVoice.ini instead");
			return;
		}
		SKSEMenuFramework::SetSection(MOD_NAME);
		SKSEMenuFramework::AddSectionItem("Settings", Render);
		SKSEMenuFramework::AddEvent(OnEvent, 0.0f);
		logger::info("Registered SKSE Menu Framework page");
	}
}
