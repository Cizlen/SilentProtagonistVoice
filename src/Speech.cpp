#include "PCH.h"

#include "Speech.h"

#include "AudioUtilBridge.h"
#include "Render.h"
#include "Settings.h"
#include "XAudioOut.h"

#ifdef GetObject  // <windows.h> renames it to GetObjectA/W
#	undef GetObject
#endif

namespace Speech
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		using Phoneme = RE::BSFaceGenKeyframeMultiple::Phoneme;
		using Modifier = RE::BSFaceGenKeyframeMultiple::Modifier;
		using Playback = Settings::Playback;

		constexpr std::uint32_t kSoundFlags = 0x1A;  // same as AudioUtil / engine default for loose files
		constexpr std::uint32_t kSoundPriority = 128;
		constexpr int           kPhonemeCount = 16;

		struct Request
		{
			std::string   text;
			Emotion::Mood mood;
			DoneCallback  onDone;
		};

		// buffers for processed playback, filled on a worker thread
		struct RenderJob
		{
			std::vector<Render::Buffer> buffers;
			std::atomic<bool>           done{ false };
		};

		struct Voice  // one playing clip
		{
			PlannedClip       clip;
			bool              started = false;
			bool              stopped = false;
			RE::BSSoundHandle handle;
			std::int32_t      auHandle = 0;
			XAudioOut::Id     xaId = 0;
		};

		struct Active
		{
			Plan                       plan;
			std::vector<Voice>         voices;
			float                      elapsed = 0.0f;
			float                      waited = 0.0f;  // waiting for the render job
			DoneCallback               onDone;
			bool                       doneSent = false;
			Playback                   playback = Playback::kEngine;
			bool                       claimedMouth = false;
			std::shared_ptr<RenderJob> job;
		};

		// --- shared with other threads ---
		std::mutex             g_queueLock;
		std::optional<Request> g_request;
		bool                   g_skip = false;
		bool                   g_cancel = false;
		std::atomic<bool>      g_busy{ false };
		std::string            g_lastSummary;
		std::atomic<int>       g_activePlayback{ -1 };
		std::mutex             g_rngLock;
		std::mt19937           g_rng{ std::random_device{}() };

		// --- main thread only ---
		std::optional<Active>              g_active;
		Clock::time_point                  g_lastTick{};
		std::array<float, kPhonemeCount>   g_phon{};  // smoothed values we have written
		std::array<float, 2>               g_brow{};
		bool                               g_mouthDirty = false;
		bool                               g_faceSet = false;
		float                              g_faceClearIn = 0.0f;
		std::array<int, 3>                 g_warmedKey{ 9999, 9999, 9999 };
		std::string                        g_warmedPack;

		float VoiceSliderFactor()
		{
			auto* dobj = RE::BGSDefaultObjectManager::GetSingleton();
			auto* cat = dobj ? dobj->GetObject<RE::BGSSoundCategory>(RE::DEFAULT_OBJECT::kDialogueVoiceCategory) : nullptr;
			return cat ? std::clamp(cat->GetCategoryVolume(), 0.0f, 1.0f) : 1.0f;
		}

		Playback ResolvePlayback(Playback a_wanted)
		{
			if (a_wanted == Playback::kAudioUtil && AudioUtilBridge::Get().CanPlay()) {
				return Playback::kAudioUtil;
			}
			if (a_wanted == Playback::kProcessed && XAudioOut::Available()) {
				return Playback::kProcessed;
			}
			return Playback::kEngine;
		}

		Plan MakePlan(const std::string& a_text, const Emotion::Mood& a_mood, Text::Line& a_line,
			std::shared_ptr<const VoicePacks::Pack>& a_pack)
		{
			a_line = Text::Analyze(a_text);
			a_pack = VoicePacks::ForPlayer();
			if (a_line.Silent() || !a_pack) {
				return {};
			}
			std::scoped_lock lock{ g_rngLock };
			return Planner::Build(a_line, a_mood, *a_pack, g_rng);
		}

		// ------------------------------------------------------------------ playback

		bool StartVoice(Voice& a_voice, Playback a_playback, float a_volumeScale, const Render::Buffer& a_buffer)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !a_voice.clip.wav) {
				return false;
			}
			const float vol = std::clamp(a_voice.clip.volume * a_volumeScale, 0.0f, 1.0f);
			const auto& path = a_voice.clip.wav->gamePath;

			switch (a_playback) {
			case Playback::kProcessed:
				if (a_buffer) {
					a_voice.xaId = XAudioOut::Play(a_buffer, a_voice.clip.wav->sampleRate, vol);
					return a_voice.xaId != 0;
				}
				return false;
			case Playback::kAudioUtil:
				// no channel: babble blips overlap and must not cut each other off
				a_voice.auHandle = AudioUtilBridge::Get().playFileWithLipSync(path.c_str(), player, vol, AudioUtilBridge::kGroup, "");
				return a_voice.auHandle > 0;
			default:
				break;
			}

			auto* manager = RE::BSAudioManager::GetSingleton();
			if (!manager) {
				return false;
			}
			RE::BSResource::ID id;
			id.GenerateFromPath(path.c_str());
			manager->GetSoundHandleByFile(a_voice.handle, id, kSoundFlags, kSoundPriority);
			if (!a_voice.handle.IsValid()) {
				logger::warn("Could not create a sound for {}", path);
				return false;
			}
			a_voice.handle.SetVolume(vol);
			if (std::fabs(a_voice.clip.AvgPitch() - 1.0f) > 0.001f) {
				a_voice.handle.SetFrequency(a_voice.clip.AvgPitch());
			}
			if (auto* node = player->Get3D(false)) {
				a_voice.handle.SetObjectToFollow(node);
			}
			if (!a_voice.handle.Play()) {
				logger::warn("Play failed for {}", path);
				return false;
			}
			return true;
		}

		void StopVoice(Voice& a_voice, Playback a_playback)
		{
			if (!a_voice.started || a_voice.stopped) {
				return;
			}
			a_voice.stopped = true;
			switch (a_playback) {
			case Playback::kProcessed:
				XAudioOut::Stop(a_voice.xaId, 0.06f);
				break;
			case Playback::kAudioUtil:
				if (const auto& au = AudioUtilBridge::Get(); au.stopHandle && a_voice.auHandle > 0) {
					au.stopHandle(a_voice.auHandle);
				}
				break;
			default:
				if (a_voice.handle.IsValid()) {
					a_voice.handle.FadeOutAndRelease(60);
				}
				break;
			}
		}

		// ------------------------------------------------------------------ face

		RE::BSFaceGenAnimationData* PlayerFace()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			return player ? player->GetFaceGenAnimationData() : nullptr;
		}

		void WriteFace(bool a_writePhonemes)
		{
			auto* face = PlayerFace();
			if (!face) {
				return;
			}
			RE::BSSpinLockGuard guard{ face->lock };
			if (a_writePhonemes) {
				auto& ph = face->phenomeKeyFrame;
				if (ph.values && ph.count >= kPhonemeCount) {
					for (int i = 0; i < kPhonemeCount; ++i) {
						ph.SetValue(i, g_phon[i]);
					}
				}
			}
			auto& mod = face->modifierKeyFrame;
			if (mod.values && mod.count > Modifier::BrowUpRight) {
				mod.SetValue(Modifier::BrowUpLeft, g_brow[0]);
				mod.SetValue(Modifier::BrowUpRight, g_brow[1]);
			}
		}

		// a_targets: where the face wants to be this frame (all zero = rest)
		void UpdateMouth(const std::array<float, kPhonemeCount>& a_targets, float a_browTarget, float a_dt,
			const Settings::Config& a_cfg, bool a_writePhonemes)
		{
			const float tauOpen = std::max(0.005f, a_cfg.mouthSmoothing * 0.6f);
			const float tauClose = std::max(0.005f, a_cfg.mouthSmoothing * 1.4f);
			bool        any = false;
			for (int i = 0; i < kPhonemeCount; ++i) {
				const float tau = a_targets[i] > g_phon[i] ? tauOpen : tauClose;
				g_phon[i] += (a_targets[i] - g_phon[i]) * (1.0f - std::exp(-a_dt / tau));
				if (g_phon[i] < 0.003f && a_targets[i] == 0.0f) {
					g_phon[i] = 0.0f;
				}
				any = any || g_phon[i] > 0.0f;
			}
			for (auto& b : g_brow) {
				b += (a_browTarget - b) * (1.0f - std::exp(-a_dt / 0.12f));
				if (b < 0.003f && a_browTarget == 0.0f) {
					b = 0.0f;
				}
				any = any || b > 0.0f;
			}

			if (any || g_mouthDirty) {
				WriteFace(a_writePhonemes);
			}
			g_mouthDirty = any;
		}

		// facial expression for the mood; never touches an override someone else set
		void SetExpression(const Emotion::Mood& a_mood, const Settings::Config& a_cfg)
		{
			const int idx = Emotion::FaceExpression(a_mood.type);
			if (!a_cfg.faceExpression || idx < 0 || a_mood.Neutral()) {
				return;
			}
			auto* face = PlayerFace();
			if (!face || (face->exprOverride && !g_faceSet)) {
				return;
			}
			const float value = std::clamp(a_mood.strength * a_cfg.emotionIntensity * a_cfg.faceIntensity, 0.0f, 1.0f);
			if (value < 0.05f) {
				return;
			}
			face->SetExpressionOverride(static_cast<std::uint32_t>(idx), value);
			g_faceSet = true;
			g_faceClearIn = 0.0f;
		}

		void ClearExpression()
		{
			if (!g_faceSet) {
				return;
			}
			if (auto* face = PlayerFace()) {
				face->ClearExpressionOverride();
			}
			g_faceSet = false;
		}

		// ------------------------------------------------------------------ lifecycle

		void SendDone(Active& a_active, bool a_cancelled)
		{
			if (a_active.doneSent) {
				return;
			}
			a_active.doneSent = true;
			if (a_active.onDone) {
				auto cb = std::move(a_active.onDone);
				cb(a_cancelled);
			}
		}

		void Finish(bool a_cancelled)
		{
			if (!g_active) {
				return;
			}
			for (auto& v : g_active->voices) {
				StopVoice(v, g_active->playback);
			}
			if (g_active->claimedMouth) {
				if (const auto& au = AudioUtilBridge::Get(); au.CanClaim()) {
					au.releaseMouth(RE::PlayerCharacter::GetSingleton(), AudioUtilBridge::kOwner);
				}
			}
			// keep the expression a moment while the NPC answers, drop it at once if cut off
			if (a_cancelled) {
				ClearExpression();
			} else if (g_faceSet) {
				g_faceClearIn = 1.2f;
			}
			SendDone(*g_active, a_cancelled);
			g_active.reset();
			g_busy = false;
		}

		void Begin(Request&& a_req, const Settings::Config& a_cfg)
		{
			Active active;
			active.onDone = std::move(a_req.onDone);
			active.playback = ResolvePlayback(a_cfg.playback);
			g_activePlayback = static_cast<int>(active.playback);

			Text::Line                              line;
			std::shared_ptr<const VoicePacks::Pack> pack;
			if (a_cfg.enabled) {
				active.plan = MakePlan(a_req.text, a_req.mood, line, pack);
			}

			if (active.plan.Empty()) {
				if (a_cfg.debugLog) {
					logger::info("Silent: '{}' ({})", a_req.text, !a_cfg.enabled ? "disabled" : !pack ? "no voice pack" : "nothing spoken");
				}
				SendDone(active, false);
				g_busy = false;
				return;
			}

			if (active.playback == Playback::kAudioUtil) {
				// AudioUtil can't change playback rate: re-time the plan at pitch 1
				float length = 0.0f;
				for (auto& c : active.plan.clips) {
					c.pitch = c.pitchEnd = 1.0f;
					length = std::max(length, c.start + c.Length());
				}
				active.plan.length = std::min(length, a_cfg.maxLineSeconds);
			}
			if (active.playback != Playback::kProcessed) {
				// the engine and AudioUtil play the files as they are: no babble blend
				float length = 0.0f;
				for (auto& c : active.plan.clips) {
					c.stretch = 1.0f;
					c.soften = 0.0f;
					length = std::max(length, c.start + c.Length());
				}
				active.plan.length = std::min(length, a_cfg.maxLineSeconds);
			}

			{
				std::scoped_lock lock{ g_queueLock };
				g_lastSummary = std::format("\"{}\" -> {}", line.spoken, active.plan.summary);
			}
			if (a_cfg.debugLog) {
				logger::info("Say \"{}\": {} words, {} syllables, {}, pack '{}', {} playback -> {}", line.spoken, line.words,
					line.syllables, Text::ToneName(line.tone), pack->name, ActivePlaybackName(), active.plan.summary);
			}

			for (const auto& c : active.plan.clips) {
				active.voices.push_back(Voice{ .clip = c });
			}

			if (active.playback == Playback::kProcessed) {
				// render on a worker; the line starts as soon as it's ready (a few ms)
				auto job = std::make_shared<RenderJob>();
				job->buffers.resize(active.plan.clips.size());
				active.job = job;
				std::thread([job, clips = active.plan.clips, character = active.plan.character]() {
					for (std::size_t i = 0; i < clips.size(); ++i) {
						job->buffers[i] = Render::Clip(clips[i], character);
					}
					job->done = true;
				}).detach();
			}

			// keep AudioUtil-driven content off the mouth while we use it
			if (active.playback != Playback::kAudioUtil && a_cfg.lipSync) {
				if (const auto& au = AudioUtilBridge::Get(); au.CanClaim()) {
					au.claimMouth(RE::PlayerCharacter::GetSingleton(), active.plan.length + 0.6f, AudioUtilBridge::kOwner);
					active.claimedMouth = true;
				}
			}

			SetExpression(active.plan.mood, a_cfg);

			g_active = std::move(active);
			g_busy = true;
		}

		// re-shape the pack in the background when the voice sliders move
		void MaybeWarm(const Settings::Config& a_cfg)
		{
			if (a_cfg.playback != Playback::kProcessed) {
				return;
			}
			auto pack = VoicePacks::ForPlayer();
			if (!pack) {
				return;
			}
			const Dsp::Character ch{ std::clamp(a_cfg.throat + pack->throat, -1.0f, 1.0f),
				std::clamp(a_cfg.breath + pack->breath, 0.0f, 1.0f),
				std::clamp(a_cfg.brightness + pack->brightness, -1.0f, 1.0f) };
			const std::array<int, 3> key{ static_cast<int>(std::lround(ch.throat * 100)), static_cast<int>(std::lround(ch.breath * 100)),
				static_cast<int>(std::lround(ch.brightness * 100)) };
			if (key == g_warmedKey && pack->name == g_warmedPack) {
				return;
			}
			g_warmedKey = key;
			g_warmedPack = pack->name;
			Render::Warm(pack, ch);
		}
	}

	// ====================================================================== API

	void Say(std::string a_text, Emotion::Mood a_mood, DoneCallback a_onDone)
	{
		std::scoped_lock lock{ g_queueLock };
		if (g_request && g_request->onDone) {
			// superseded before it even started; let its owner carry on
			auto old = std::move(g_request->onDone);
			g_request.reset();
			SKSE::GetTaskInterface()->AddTask([old = std::move(old)]() { old(false); });
		}
		g_request = Request{ std::move(a_text), a_mood, std::move(a_onDone) };
		g_busy = true;
	}

	bool PreviewNow(const std::string& a_text, Emotion::Mood a_mood)
	{
		if (!XAudioOut::Available()) {
			return false;
		}
		Text::Line                              line;
		std::shared_ptr<const VoicePacks::Pack> pack;
		auto                                    plan = MakePlan(a_text, a_mood, line, pack);
		if (plan.Empty()) {
			return false;
		}
		{
			std::scoped_lock lock{ g_queueLock };
			g_lastSummary = std::format("\"{}\" -> {} (preview)", line.spoken, plan.summary);
		}
		// mixed on a worker so the menu doesn't stutter; plays at the set volume
		std::thread([plan = std::move(plan)]() {
			constexpr std::uint32_t rate = 44100;
			auto                    mix = std::make_shared<const std::vector<float>>(Render::Mix(plan, rate));
			XAudioOut::Play(mix, rate, 1.0f);
		}).detach();
		return true;
	}

	void Skip()
	{
		std::scoped_lock lock{ g_queueLock };
		g_skip = true;
	}

	void Cancel()
	{
		std::scoped_lock lock{ g_queueLock };
		g_cancel = true;
	}

	bool Busy() { return g_busy.load(); }

	std::string LastSummary()
	{
		std::scoped_lock lock{ g_queueLock };
		return g_lastSummary;
	}

	const char* ActivePlaybackName()
	{
		switch (g_activePlayback.load()) {
		case 0:
			return "processed";
		case 1:
			return "game engine";
		case 2:
			return "AudioUtil";
		default:
			return "not used yet";
		}
	}

	void Tick()
	{
		const auto now = Clock::now();
		float      dt = g_lastTick == Clock::time_point{} ? 0.016f : std::chrono::duration<float>(now - g_lastTick).count();
		g_lastTick = now;
		dt = std::clamp(dt, 0.0f, 0.1f);

		std::optional<Request> request;
		bool                   skip = false, cancel = false;
		{
			std::scoped_lock lock{ g_queueLock };
			request.swap(g_request);
			std::swap(skip, g_skip);
			std::swap(cancel, g_cancel);
		}

		const auto cfg = Settings::Snapshot();
		XAudioOut::Update(dt);
		MaybeWarm(cfg);

		if (cancel) {
			if (request) {
				if (request->onDone) {
					request->onDone(true);
				}
				request.reset();
			}
			Finish(true);
		}
		if (request) {
			Finish(false);  // a new line replaces the old one
			Begin(std::move(*request), cfg);
			skip = false;
		}
		if (skip && g_active) {
			Finish(false);
		}

		if (g_faceSet && !g_active && g_faceClearIn > 0.0f) {
			g_faceClearIn -= dt;
			if (g_faceClearIn <= 0.0f) {
				ClearExpression();
			}
		}

		std::array<float, kPhonemeCount> targets{};
		float                            browTarget = 0.0f;

		if (g_active) {
			auto& a = *g_active;

			// processed playback waits for its buffers (normally ready the next frame)
			bool ready = true;
			if (a.job && !a.job->done.load()) {
				a.waited += dt;
				ready = false;
				if (a.waited > 0.5f) {
					logger::warn("Voice rendering is slow; using the game engine for this line");
					a.playback = Playback::kEngine;
					a.job.reset();
					ready = true;
				}
			}

			if (ready) {
				a.elapsed += dt;
				const float volumeScale = (cfg.followVoiceSlider && a.playback != Playback::kAudioUtil) ? VoiceSliderFactor() : 1.0f;

				float level = 0.0f;
				int   vowel = -1;
				float vowelLevel = 0.0f;

				for (std::size_t i = 0; i < a.voices.size(); ++i) {
					auto& v = a.voices[i];
					if (!v.started && a.elapsed >= v.clip.start) {
						v.started = true;
						const Render::Buffer buffer = a.job ? a.job->buffers[i] : nullptr;
						if (!StartVoice(v, a.playback, volumeScale, buffer)) {
							v.stopped = true;
						}
					}
					if (!v.started || v.stopped) {
						continue;
					}
					const float local = v.clip.FileTime(a.elapsed - v.clip.start);  // clip time
					if (local >= v.clip.wav->duration) {
						continue;
					}
					float l;
					if (v.clip.wav->HasEnvelope()) {
						l = v.clip.wav->Sample(local);
					} else {
						// procedural flap for clips we can't read: ~6 Hz open/close under a fade
						const float prog = local / v.clip.wav->duration;
						l = (0.55f + 0.45f * std::sin(local * 6.0f * 6.2831853f)) * std::sin(prog * 3.14159265f);
					}
					l *= std::min(1.0f, v.clip.volume / std::max(0.01f, cfg.volume));
					if (l > level) {
						level = l;
					}
					if (l > vowelLevel) {
						vowelLevel = l;
						vowel = v.clip.vowel;
					}
				}

				if (a.elapsed >= a.plan.length) {
					for (auto& v : a.voices) {
						if (a.elapsed < v.clip.start + v.clip.Length()) {
							StopVoice(v, a.playback);  // past the cap
						}
					}
				}

				if (cfg.lipSync && a.playback != Playback::kAudioUtil) {
					level = std::clamp(level * cfg.lipGain, 0.0f, 1.0f);
					targets[Phoneme::Aah] = level * cfg.jawOpen * 0.85f;
					targets[Phoneme::BigAah] = std::max(0.0f, level - 0.6f) / 0.4f * cfg.jawOpen * 0.4f;
					if (cfg.vowelShapes && vowel >= 0) {
						targets[vowel] = std::max(targets[vowel], level * 0.45f);
					}
				}
				if (cfg.lipSync && cfg.expressiveBrows) {
					const float prog = a.plan.length > 0.0f ? a.elapsed / a.plan.length : 1.0f;
					if (a.plan.tone == Text::Tone::kQuestion || a.plan.mood.type == Emotion::Type::kPuzzled) {
						browTarget = prog > 0.5f ? 0.45f : 0.12f;
					} else if (a.plan.tone == Text::Tone::kExclaim || a.plan.mood.type == Emotion::Type::kSurprise) {
						browTarget = 0.3f;
					}
				}

				// the line is over: let the NPC in, keep the mouth relaxing on its own
				if (a.elapsed >= a.plan.length + cfg.responseDelay) {
					Finish(false);
				}
			}
		}

		UpdateMouth(targets, browTarget, dt, cfg, !(g_active && g_active->playback == Playback::kAudioUtil));
	}
}
